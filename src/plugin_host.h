#include <juce_audio_utils/juce_audio_utils.h>
#include <jshm.h>
#include "utils.h"
#include "plugin_window.h"

constexpr auto FLAGS_IS_PLAYING   = 0b0001;
constexpr auto FLAGS_IS_LOOPING   = 0b0010;
constexpr auto FLAGS_IS_RECORDING = 0b0100;
constexpr auto FLAGS_IS_REALTIME  = 0b1000;

constexpr auto PARAMETER_IS_AUTOMATABLE = 0b00001;
constexpr auto PARAMETER_IS_DISCRETE = 0b00010;
constexpr auto PARAMETER_IS_BOOLEAN = 0b00100;
constexpr auto PARAMETER_IS_META = 0b01000;
constexpr auto PARAMETER_IS_ORIENTATION_INVERTED = 0b10000;

namespace eim {
class plugin_host : public juce::JUCEApplication, public juce::AudioPlayHead, public juce::AudioProcessorListener,
    private juce::Thread, private juce::AsyncUpdater, private juce::Value::Listener{
    public:
        plugin_host() : juce::AudioPlayHead(), juce::Thread("IO Thread") { }
        ~plugin_host() override {
            delete[] prevParameterChanges;
            shm.reset();
        }

        static juce::JUCEApplicationBase* createInstance() { return new plugin_host(); }

        const juce::String getApplicationName() override { return "EIMPluginHost"; }
        const juce::String getApplicationVersion() override { return "0.0.0"; }
        bool moreThanOneInstanceAllowed() override { return true; }

        void initialise(const juce::String&) override {
            streams::out.writeByteOrderMessage();
            triggerAsyncUpdate();
        }

        void shutdown() override {
            window = nullptr;
            processor = nullptr;
        }

        void anotherInstanceStarted(const juce::String&) override { }

        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return positionInfo; }

        bool canControlTransport() override { return true; }

        void transportPlay(bool shouldStartPlaying) override {
            if (!mtx.try_lock()) return;
            writeToHostBuffer((unsigned char)2);
            writeToHostBuffer((unsigned char)shouldStartPlaying);
            mtx.unlock();
        }

        void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex, float newValue) override {
            if (!mtx.try_lock()) return;
            auto time = juce::Time::getApproximateMillisecondCounter() + 500;
            if (!parameterChanges.try_emplace(parameterIndex, newValue, time).second) {
                auto& p = parameterChanges[parameterIndex];
                p.first = newValue;
                p.second = time;
            }
            mtx.unlock();
        }

        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
            if (details.parameterInfoChanged) shouldWriteInformation = true;
            else if (details.latencyChanged) shouldWriteLatency = true;
        }

    private:
        juce::MidiBuffer midiBuffer;
        juce::AudioBuffer<float> buffer;
        std::unique_ptr<jshm::shared_memory> shm;
        std::unique_ptr<plugin_window> window;
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::AudioPlayHead::PositionInfo positionInfo;
        juce::Array<juce::AudioProcessorParameter*> parameters;
        std::unordered_map<int, std::pair<float, juce::uint32>> parameterChanges;
        float* prevParameterChanges{};
        int prevParameterChangesCnt = 0;
        bool isRealtime = true, bypass = false,
            shouldWriteInformation = false, shouldWriteLatency = false, shouldWriteBypass = false;
        int sampleRate = 48000, bufferSize = 1024;
        int hostBufferPos = 0;
        juce::int8 hostBuffer[8192] = {0};
        std::mutex mtx;

        void handleAsyncUpdate() override {
            juce::PluginDescription desc;
            auto jsonStr = args->getValueForOption("-L|--load");
            auto json = juce::JSON::fromString(jsonStr == "#" ? streams::in.readString() : jsonStr);
            desc.name = json.getProperty("name", "").toString();
            desc.pluginFormatName = json.getProperty("pluginFormatName", "").toString();
            desc.fileOrIdentifier = json.getProperty("fileOrIdentifier", "").toString();
            desc.uniqueId = (int)json.getProperty("uniqueId", 0);
            desc.deprecatedUid = (int)json.getProperty("deprecatedUid", 0);

            juce::String error;

            juce::AudioPluginFormatManager manager;
            manager.addDefaultFormats();
            processor = manager.createPluginInstance(desc, sampleRate, bufferSize, error);
            if (error.isNotEmpty()) {
                streams::out.writeError(error);
                quit();
                return;
            }
            prevParameterChangesCnt = processor->getParameters().size();
            prevParameterChanges = new float[(size_t) prevParameterChangesCnt];
            processor->enableAllBuses();
            processor->setPlayHead(this);
            processor->addListener(this);

            bool isWindowOpen = true;
            if (args->containsOption("-P|--preset")) {
                auto preset = args->getValueForOption("-P|--preset");
                isWindowOpen = loadState(preset == "#" ? streams::in.readString() : preset);
				std::cerr << "openwindow: " << (int)isWindowOpen << std::endl;
            }

            if (isWindowOpen) createEditorWindow();
            writeInitInformation();

            startThread();
        }

        void run() override {
            juce::int8 id;
            while (!threadShouldExit() && streams::in.read(id) == 1) {
                switch (id) {
                    case 0: { // init
                        bool enabledSharedMemory;
                        streams::in >> sampleRate >> bufferSize >> enabledSharedMemory;
                        juce::MessageManagerLock mml(Thread::getCurrentThread());
                        if (!mml.lockWasGained()) return;
                        auto channels = juce::jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
                        bool setInnerBuffer = true;
                        if (enabledSharedMemory) {
                            int shmSize;
                            juce::String shmName = streams::in.readString();
                            streams::in >> shmSize;
                            if (shmSize && shmName.isNotEmpty()) {
                                shm.reset(jshm::shared_memory::open(shmName.toRawUTF8(), shmSize));
                                if (shm) {
                                    auto buffers = new float* [(unsigned long)channels];
                                    for (int i = 0; i < channels; i++) buffers[i] = reinterpret_cast<float*>(shm->address()) + i * bufferSize;
                                    buffer = juce::AudioBuffer<float>(buffers, channels, bufferSize);
                                    delete[] buffers;
                                    setInnerBuffer = false;
                                }
                            } else setInnerBuffer = false;
                        } else shm.reset();
                        if (setInnerBuffer) buffer = juce::AudioBuffer<float>(channels, bufferSize);
                        processor->prepareToPlay(sampleRate, bufferSize);
                        break;
                    }
                    case 1: { // process block
                        double bpm;
                        juce::int8 numInputChannels, numOutputChannels = 0, flags;
                        juce::int64 timeInSamples;
                        juce::int16 numMidiEvents;
                        streams::in >> flags >> bpm >> numMidiEvents;
                        streams::in.readVarLong(timeInSamples);

                        double timeInSeconds = (double)timeInSamples / sampleRate;
                        auto _isRealtime = (flags & FLAGS_IS_REALTIME) != 0;
                        if (isRealtime != _isRealtime) {
                            processor->setNonRealtime(!_isRealtime);
                            isRealtime = _isRealtime;
                        }
                        positionInfo.setIsPlaying((flags & FLAGS_IS_PLAYING) != 0);
                        positionInfo.setIsLooping((flags & FLAGS_IS_LOOPING) != 0);
                        positionInfo.setIsRecording((flags & FLAGS_IS_RECORDING) != 0);
                        positionInfo.setBpm(bpm);
                        positionInfo.setTimeInSamples(timeInSamples);
                        positionInfo.setTimeInSeconds(timeInSeconds);
                        positionInfo.setPpqPosition(timeInSeconds / 60.0 * bpm);

                        if (!shm) {
                            streams::in >> numInputChannels >> numOutputChannels;
                            for (int i = 0; i < numInputChannels; i++)
                                streams::in.readArray(buffer.getWritePointer(i), bufferSize);
                        }
                        juce::MidiBuffer buf;
                        for (int i = 0; i < numMidiEvents; i++) {
                            int data;
                            short time;
                            streams::in.readVarInt(data);
                            streams::in >> time;
                            buf.addEvent(juce::MidiMessage(data & 0xFF, (data >> 8) & 0xFF, (data >> 16) & 0xFF), time);
                        }

                        int numParameters;
                        streams::in.readVarInt(numParameters);
                        
                        for (int i = 0; i < numParameters; i++) {
                            int pid;
                            float value;
                            streams::in.readVarInt(pid);
                            streams::in >> value;
                            if (pid != 9999999) if (auto* param = parameters[pid]) param->setValue(value);
                        }
                        
                        processor->processBlock(buffer, buf);

                        writeNotify(false);
                        
                        if (!shm) for (int i = 0; i < numOutputChannels; i++) streams::out.writeArray(buffer.getReadPointer(i), bufferSize);
                        streams::out.flush();
                        break;
                    }
                    case 2: { // open control panel
                        juce::MessageManager::callAsync([this] {
                            if (window == nullptr) createEditorWindow();
                            else window.reset(nullptr);
                        });
                        break;
                    }
                    case 3: { // save state
                        streams::out.write(saveState(streams::in.readString()));
                        streams::out.flush();
                        break;
                    }
                    case 4: { // load state
                        juce::MessageManagerLock mml(Thread::getCurrentThread());
                        if (!mml.lockWasGained()) return;
                        loadState(streams::in.readString());
                        break;
                    }
                    case 5: { // bypass state change
                        writeNotify(true);
                        break;
                    }
                    default:; // unknown command
                }
            }
            quit();
        }

        void createEditorWindow() {
            if (!processor->hasEditor()) return;
            auto component = processor->createEditorIfNeeded();
            if (!component) return;
            long long parentHandle = 0;
#ifdef JUCE_WINDOWS
            parentHandle = args->containsOption("-H|--handle") ? args->getValueForOption("-H|--handle").getLargeIntValue() : 0;
#endif
            window = std::make_unique<plugin_window>("[EIMHost] " + processor->getName() + " (" +
                processor->getPluginDescription().pluginFormatName + ")", component, window,
                processor->wrapperType != juce::AudioProcessor::wrapperType_VST, parentHandle);
            window->getBypassState().addListener(this);
        }

        void writeNotify(bool bypassNewValue) {
            auto hasParameterChanges = !parameterChanges.empty();
            if ((hostBufferPos > 0 || hasParameterChanges) && mtx.try_lock()) {
                if (hostBufferPos > 0) {
                    streams::out.writeArray(hostBuffer, hostBufferPos);
                    hostBufferPos = 0;
                }
                if (hasParameterChanges) writeAllParameterChanges();
                mtx.unlock();
            }
            
            if (shouldWriteInformation) writeInitInformation();
            if (shouldWriteLatency) {
                streams::out.writeAction(4);
                streams::out << (juce::int32)processor->getLatencySamples();
                shouldWriteLatency = false;
            }
            if (shouldWriteBypass) {
                streams::out.writeAction(5);
                streams::out << bypass;
                shouldWriteBypass = false;
            } else if (bypassNewValue != bypass) {
                bypass = bypassNewValue;
                if (window != nullptr) {
                    juce::MessageManagerLock mml(Thread::getCurrentThread());
                    if (!mml.lockWasGained()) return;
                    window->setBypass(bypassNewValue);
                }
            }
            streams::out.writeAction(1);
            streams::out.flush();
        }

        void valueChanged(juce::Value& value) override {
            bool v = !value.getValue();
            if (bypass != v) {
                bypass = v;
                shouldWriteBypass = true;
            }
        }

        bool loadState(const juce::String& file) {
            juce::FileInputStream stream(file);
            if (!stream.openedOk()) {
                std::cerr << "Failed to open file: " << file << '\n';
                fflush(stderr);
                return true;
            }
            bool isWindowOpen = false;
            juce::MemoryBlock memory;
            switch (stream.readByte()) {
                case 0:
                    isWindowOpen = stream.readBool();
                    plugin_window::x_ = stream.readInt();
                    plugin_window::y_ = stream.readInt();
                    plugin_window::width_ = stream.readInt();
                    plugin_window::height_ = stream.readInt();
            }
            stream.readIntoMemoryBlock(memory);
            processor->setStateInformation(memory.getData(), (int)memory.getSize());
            return isWindowOpen;
        }

        bool saveState(const juce::String& file) {
            juce::MessageManagerLock mml(Thread::getCurrentThread());
            if (!mml.lockWasGained()) return false;
            juce::MemoryBlock memory;
            processor->getStateInformation(memory);
            if (auto stream = juce::File(file).createOutputStream()) {
                stream->setPosition(0);
                stream->truncate();
                stream->writeByte(0); // version
                stream->writeBool(window != nullptr);
                stream->writeInt(plugin_window::x_);
                stream->writeInt(plugin_window::y_);
                stream->writeInt(plugin_window::width_);
                stream->writeInt(plugin_window::height_);
                stream->write(memory.getData(), memory.getSize());
                return true;
            }
            return false;
        }

        void writeInitInformation() {
            shouldWriteInformation = false;
            parameters = processor->getParameters();
            streams::out.writeAction(0);
            streams::out << (juce::int8)processor->getTotalNumInputChannels()
                << (juce::int8)processor->getTotalNumOutputChannels() << (juce::int32)processor->getLatencySamples();
            auto size = parameters.size();
            streams::out.writeVarInt(size);
            for (int i = 0; i < size; i++) {
				auto p = parameters[i];
                auto value = p->getValue();
                prevParameterChanges[p->getParameterIndex()] = value;
                juce::int8 flags = 0;
                if (p->isAutomatable()) flags |= PARAMETER_IS_AUTOMATABLE;
                if (p->isDiscrete()) flags |= PARAMETER_IS_DISCRETE;
                if (p->isBoolean()) flags |= PARAMETER_IS_BOOLEAN;
                if (p->isMetaParameter()) flags |= PARAMETER_IS_META;
                if (p->isOrientationInverted()) flags |= PARAMETER_IS_ORIENTATION_INVERTED;
                
                streams::out << flags << value << p->getDefaultValue() << (int)p->getCategory()
                    << p->getNumSteps() << p->getName(64) << p->getLabel();

                auto valueStrings = p->getAllValueStrings();
                auto size = valueStrings.size();
                if (size > 64 || size == 0 || (size == 1 && valueStrings[0].isEmpty())) streams::out.writeVarInt(0);
                else streams::out << valueStrings;
				if (i > 30) streams::out.flush(); // avoid buffer overflow
            }
            streams::out.flush();
        }

        void writeAllParameterChanges() {
            auto time = juce::Time::getApproximateMillisecondCounter();
            std::vector<int>* toRemove = nullptr;
            for (auto it = parameterChanges.begin(); it != parameterChanges.end();) {
                if (it->second.second > time) {
                    it++;
                    continue;
                }
                auto id = it->first;
                auto value = it->second.first;
                if (prevParameterChangesCnt > id && !juce::approximatelyEqual(prevParameterChanges[id], value)) {
                    prevParameterChanges[id] = value;
                    if (!toRemove) toRemove = new std::vector<int>();
                    toRemove->push_back(id);
                }
                parameterChanges.erase(it++);
            }

            if (toRemove) {
                streams::out.writeAction(3);
                streams::out.writeVarInt((int) toRemove->size());
                for (auto id : *toRemove) {
                    streams::out.writeVarInt(id);
                    streams::out << prevParameterChanges[id];
                }
                delete toRemove;
            }
        }

        template <typename T> inline void writeToHostBuffer(T var) {
            T* p = reinterpret_cast<T*>(&var);
            for (size_t i = 0; i < sizeof(T); i++) hostBuffer[hostBufferPos++] = ((char*)p)[i];
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_host)
    };
}
