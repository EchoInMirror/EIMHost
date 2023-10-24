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
        private juce::Thread, private juce::AsyncUpdater {
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
            writeToHostBuffer((char)2);
            writeToHostBuffer((char)shouldStartPlaying);
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

        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { }

    private:
        juce::MidiBuffer midiBuffer;
        juce::AudioBuffer<float> buffer;
        std::unique_ptr<jshm::shared_memory> shm;
        std::unique_ptr<PluginWindow> window;
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::AudioPlayHead::PositionInfo positionInfo;
        juce::Array<juce::AudioProcessorParameter*> parameters;
        std::unordered_map<int, std::pair<float, juce::uint32>> parameterChanges;
        float* prevParameterChanges{};
        int prevParameterChangesCnt = 0;
        bool isRealtime = true;
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
            if (args->containsOption("-P|--preset")) {
                auto preset = args->getValueForOption("-P|--preset");
                loadState(preset == "#" ? streams::in.readString() : preset);
            }

            createEditorWindow();
            writeInitInformation();

            startThread();
        }

        void run() override {
            juce::int8 id;
            while (!threadShouldExit() && streams::in.read(id) == 1) {
                switch (id) {
                    case 0: {
                        bool enabledSharedMemory;
                        streams::in >> sampleRate >> bufferSize >> enabledSharedMemory;
                        juce::MessageManagerLock mml(Thread::getCurrentThread());
                        if (!mml.lockWasGained()) return;
                        auto channels = juce::jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
                        bool setInnerBuffer = true;
                        if (enabledSharedMemory) {
                            int shmSize;
                            streams::in >> shmSize;
                            if (!shm || shmSize) {
                                shm.reset();
                                auto shmName = args->getValueForOption("-M|--memory");
                                if (shmName.isNotEmpty()) {
                                    shm.reset(jshm::shared_memory::open(shmName.toRawUTF8(), shmSize));
                                    if (shm) {
                                        auto buffers = new float* [(unsigned long)channels];
                                        for (int i = 0; i < channels; i++) buffers[i] = reinterpret_cast<float*>(shm->address()) + i * bufferSize;
                                        buffer = juce::AudioBuffer<float>(buffers, channels, bufferSize);
                                        delete[] buffers;
                                        setInnerBuffer = false;
                                    }
                                }
                            } else setInnerBuffer = false;
                        }
                        if (setInnerBuffer) buffer = juce::AudioBuffer<float>(channels, bufferSize);
                        processor->prepareToPlay(sampleRate, bufferSize);
                        break;
                    }
                    case 1: {
                        double bpm;
                        juce::int8 numInputChannels, numOutputChannels = 0, flags;
                        juce::int64 timeInSamples;
                        juce::int16 numMidiEvents, numParameters;
                        streams::in >> flags >> bpm >> numMidiEvents >> numParameters;
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
                            streams::in >> data >> time;
                            buf.addEvent(juce::MidiMessage(data & 0xFF, (data >> 8) & 0xFF, (data >> 16) & 0xFF), time);
                        }
                        for (int i = 0; i < numParameters; i++) {
                            int pid;
                            float value;
                            streams::in.readVarInt(pid);
                            streams::in >> value;
                            if (pid != 9999999) if (auto* param = parameters[pid]) param->setValue(value);
                        }
                        auto hasParameterChanges = !parameterChanges.empty();
                        if ((hostBufferPos > 0 || hasParameterChanges) && mtx.try_lock()) {
                            if (hostBufferPos > 0) {
                                streams::out.writeArray(hostBuffer, hostBufferPos);
                                hostBufferPos = 0;
                            }
                            if (hasParameterChanges) writeAllParameterChanges();
                            mtx.unlock();
                        }
                        processor->processBlock(buffer, buf);

                        streams::out.writeAction(1);
                        if (!shm) for (int i = 0; i < numOutputChannels; i++) streams::out.writeArray(buffer.getReadPointer(i), bufferSize);
                        eim::streams::out.flush();
                        break;
                    }
                    case 2: {
                        juce::MessageManager::callAsync([this] {
                            if (window == nullptr) createEditorWindow();
                            else window.reset(nullptr);
                        });
                        break;
                    }
                    case 3: {
                        juce::MessageManagerLock mml(Thread::getCurrentThread());
                        if (!mml.lockWasGained()) return;
                        juce::MemoryBlock memory;
                        processor->getStateInformation(memory);
                        juce::File(streams::in.readString()).replaceWithData(memory.getData(), memory.getSize());
                        break;
                    }
                    case 4: {
                        juce::MessageManagerLock mml(Thread::getCurrentThread());
                        if (!mml.lockWasGained()) return;
                        loadState(streams::in.readString());
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
            window = std::make_unique<PluginWindow>("[EIMHost] " + processor->getName() + " (" +
                processor->getPluginDescription().pluginFormatName + ")", component, window,
                processor->wrapperType != juce::AudioProcessor::wrapperType_VST, parentHandle);
        }

        void loadState(const juce::String& file) {
            juce::FileInputStream stream(file);
            if (!stream.openedOk()) {
                std::cerr << "Failed to open file: " << file << '\n';
                fflush(stderr);
                return;
            }
            juce::MemoryBlock memory;
            stream.readIntoMemoryBlock(memory);
            processor->setStateInformation(memory.getData(), (int)memory.getSize());
        }

        void writeInitInformation() {
            parameters = processor->getParameters();
            streams::out.writeAction(0);
            streams::out << (juce::int8)processor->getTotalNumInputChannels()
                << (juce::int8)processor->getTotalNumOutputChannels();
            streams::out.writeVarInt(parameters.size());
            for (auto p : parameters) {
                juce::int8 flags = 0;
                if (p->isAutomatable()) flags |= PARAMETER_IS_AUTOMATABLE;
                if (p->isDiscrete()) flags |= PARAMETER_IS_DISCRETE;
                if (p->isBoolean()) flags |= PARAMETER_IS_BOOLEAN;
                if (p->isMetaParameter()) flags |= PARAMETER_IS_META;
                if (p->isOrientationInverted()) flags |= PARAMETER_IS_ORIENTATION_INVERTED;
                streams::out << flags << p->getDefaultValue() << (int)p->getCategory()
                    << p->getName(1024) << p->getLabel() << p->getAllValueStrings();
            }
            eim::streams::out.flush();
        }

        void writeAllParameterChanges() {
            auto time = juce::Time::getApproximateMillisecondCounter();
            for (auto it = parameterChanges.begin(); it != parameterChanges.end();) {
                if (it->second.second > time) {
                    it++;
                    continue;
                }
                auto id = it->first;
                auto value = it->second.first;
                if (prevParameterChangesCnt > id && !juce::approximatelyEqual(prevParameterChanges[id], value)) {
                    prevParameterChanges[id] = value;
                    streams::out.writeAction(3);
                    streams::out.writeVarInt(id);
                    streams::out << value;
                }
                parameterChanges.erase(it++);
            }
        }

        template <typename T> inline void writeToHostBuffer(T var) {
            T* p = reinterpret_cast<T*>(&var);
            for (size_t i = 0; i < sizeof(T); i++) hostBuffer[hostBufferPos++] = ((char*)p)[i];
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_host)
    };
}
