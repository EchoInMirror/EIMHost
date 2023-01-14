#pragma warning(disable: 6031)
#pragma warning(disable: 6387)
#pragma warning(disable: 6029)

#include "PluginWindow.h"
#include <io.h>
#include <fcntl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#define FLAGS_IS_PLAYING   0b0001
#define FLAGS_IS_LOOPING   0b0010
#define FLAGS_IS_RECORDING 0b0100
#define FLAGS_IS_REALTIME  0b1000
#define READ(var) std::fread(&var, sizeof(var), 1, stdin)

juce::ArgumentList* args;
juce::AudioPluginFormatManager manager;
juce::AudioDeviceManager::AudioDeviceSetup setup;

template <typename T> inline void write(T var) { std::fwrite(&var, sizeof(var), 1, stdout); }
template <typename T> inline void writeCerr(T var) { std::fwrite(&var, sizeof(var), 1, stderr); }
void writeString(juce::String str, FILE* file = stdout) {
    write(str.length());
    std::fwrite(str.toRawUTF8(), sizeof(char), str.length(), file);
    fflush(file);
}
void writeError(juce::String str, FILE* file) {
    write((char)127);
    writeString(str, file);
    (file == stderr ? std::cout : std::cerr) << str << '\n';
}
std::string readString() {
    int len;
    READ(len);
    char* str = new char[len + 1];
    std::fread(str, sizeof(char), len, stdin);
    str[len] = '\0';
    return str;
}

class EIMPluginHost : public juce::JUCEApplication, public juce::AudioPlayHead, public juce::AudioProcessorListener, private juce::Thread {
public:
    EIMPluginHost(): juce::AudioPlayHead(), juce::Thread("IO Thread") { }

    const juce::String getApplicationName() override { return "EIMPluginHost"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        writeCerr((short)0x0102);
        juce::PluginDescription desc;
        auto json = juce::JSON::fromString(args->getValueForOption("-L|--load"));
        desc.name = json.getProperty("name", "").toString();
        desc.pluginFormatName = json.getProperty("pluginFormatName", "").toString();
        desc.fileOrIdentifier = json.getProperty("fileOrIdentifier", "").toString();
        desc.uniqueId = (int)json.getProperty("uniqueId", 0);
        desc.deprecatedUid = (int)json.getProperty("deprecatedUid", 0);
        juce::String error;
        processor = manager.createPluginInstance(desc, sampleRate, bufferSize, error);
        if (error.isNotEmpty()) {
            writeError(error, stderr);
            quit();
            return;
        }
        processor->setPlayHead(this);
        processor->addListener(this);
        if (args->containsOption("-P|--preset")) loadState(args->getValueForOption("-P|--preset"));

        freopen(nullptr, "rb", stdin);
        freopen(nullptr, "wb", stderr);
#ifdef JUCE_WINDOWS
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
#endif

        createEditorWindow();

        writeCerr((char) 0);
        writeCerr(processor->getTotalNumInputChannels());
        writeCerr(processor->getTotalNumOutputChannels());
        fflush(stderr);
        startThread();
    }

    void run() override {
        char id;
        while (!threadShouldExit() && std::fread(&id, 1, 1, stdin) == 1) {
            switch (id) {
                case 0: {
                    READ(sampleRate);
                    READ(bufferSize);
                    juce::MessageManagerLock mml(Thread::getCurrentThread());
                    if (!mml.lockWasGained()) return;
                    buffer.setSize(juce::jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels()), bufferSize);
                    processor->prepareToPlay(sampleRate, bufferSize);
                    break;
                }
                case 1: {
                    double bpm;
                    long long timeInSamples;
                    short midiEvents;
                    char numInputChannels, numOutputChannels;
                    char flags;
                    READ(flags);
                    READ(bpm);
                    READ(timeInSamples);
                    READ(numInputChannels);
                    READ(numOutputChannels);
                    READ(midiEvents);
                    double timeInSeconds = (double)timeInSamples / sampleRate;
                    juce::MessageManagerLock mml(Thread::getCurrentThread());
                    if (!mml.lockWasGained()) return;
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
                    for (int i = 0; i < numInputChannels; i++) std::fread(buffer.getWritePointer(i), sizeof(float), bufferSize, stdin);
                    juce::MidiBuffer buf;
                    for (int i = 0; i < midiEvents; i++) {
                        int data;
                        short time;
                        READ(data);
                        READ(time);
                        buf.addEvent(juce::MidiMessage(data & 0xFF, (data >> 8) & 0xFF, (data >> 16) & 0xFF), time);
                    }
                    if (hostBufferPos > 0 && mtx.try_lock()) {
                        std::fwrite(hostBuffer, sizeof(char), hostBufferPos, stderr);
                        hostBufferPos = 0;
                    }
                    processor->processBlock(buffer, buf);
                    writeCerr((char) 1);
                    for (int i = 0; i < numOutputChannels; i++) std::fwrite(buffer.getReadPointer(i), sizeof(float), bufferSize, stderr);
                    fflush(stderr);
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
                    juce::File(readString()).replaceWithData(memory.getData(), memory.getSize());
                    break;
                }
                case 4: {
                    juce::MessageManagerLock mml(Thread::getCurrentThread());
                    if (!mml.lockWasGained()) return;
                    loadState(readString());
                    break;
                }
            }
        }
        quit();
    }

    void shutdown() override {
        window = nullptr;
        processor = nullptr;
    }

    void anotherInstanceStarted(const juce::String &) override { }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return positionInfo; }

    static juce::JUCEApplicationBase* createInstance() { return new EIMPluginHost(); }

    bool canControlTransport() override { return true; }
    
    virtual void transportPlay(bool shouldStartPlaying) override {
        writeToHostBuffer((char)2);
        writeToHostBuffer((char)shouldStartPlaying);
    }

    void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex, float newValue) override {
        writeToHostBuffer((char)3);
        writeToHostBuffer(parameterIndex);
        writeToHostBuffer(newValue);
    }
    
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {
    }

private:
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<PluginWindow> window;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    juce::AudioPlayHead::PositionInfo positionInfo;
    bool isRealtime = true;
    int sampleRate = 48000, bufferSize = 1024, ppq = 96;
    volatile int hostBufferPos = 0;
    char hostBuffer[8192];
    std::mutex mtx;

    void createEditorWindow() {
        if (!processor->hasEditor()) return;
        auto component = processor->createEditorIfNeeded();
        if (!component) return;
        window.reset(new PluginWindow("[EIMHost] " + processor->getName() + " (" +
            processor->getPluginDescription().pluginFormatName + ")", component, window,
            processor->wrapperType != juce::AudioProcessor::wrapperType_VST,
            args->containsOption("-H|--handle") ? args->getValueForOption("-H|--handle").getLargeIntValue() : 0));
    }

    void loadState(juce::String file) {
        juce::FileInputStream stream(file);
        juce::MemoryBlock memory;
        stream.readIntoMemoryBlock(memory);
        processor->setStateInformation(memory.getData(), (int)memory.getSize());
    }

    template <typename T> inline void writeToHostBuffer(T var) {
        T* p = reinterpret_cast<T*>(&var);
        if (mtx.try_lock()) {
            for (int i = 0; i < sizeof(T); i++) hostBuffer[hostBufferPos++] = ((char*)p)[i];
            mtx.unlock();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EIMPluginHost)
};

class AudioCallback : public juce::AudioIODeviceCallback {
public:
    AudioCallback(juce::AudioDeviceManager& deviceManager): deviceManager(deviceManager) {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int, int, const juce::AudioIODeviceCallbackContext&) override {
        write((char)0);
        fflush(stdout);
        char id;
        if (std::fread(&id, 1, 1, stdin) != 1) {
            lock.signal();
            return;
        }
        switch (id) {
        case 0:
        {
            char numOutputChannels;
            READ(numOutputChannels);
            for (int i = 0; i < numOutputChannels; i++) std::fread(outputChannelData[i], sizeof(float), setup.bufferSize, stdin);
            break;
        }
        default: lock.signal();
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        write((char)1);
        writeString("[" + device->getTypeName() + "] " + device->getName());
        write(device->getInputLatencyInSamples());
        write(device->getOutputLatencyInSamples());
        write((float)device->getCurrentSampleRate());
        write(device->getCurrentBufferSizeSamples());
        std::cerr << "Device started: " << device->getName() << '\n';
    }

    void audioDeviceStopped() override { }

    void audioDeviceError(const juce::String& errorMessage) override { std::cerr << errorMessage << '\n'; }

    void waitDeviceClose() { lock.wait(); }
private:
    juce::WaitableEvent lock;
    juce::AudioDeviceManager& deviceManager;
};

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(0);
    std::cout.tie(0);
    std::cerr.tie(0);
    args = new juce::ArgumentList(argc, argv);

    if (args->containsOption("-S|--scan")) {
        manager.addDefaultFormats();
        auto id = args->getValueForOption("-S|--scan");
        if (id.isEmpty()) {
#ifdef JUCE_MAC
            auto paths = manager.getFormat(0)->searchPathsForPlugins(juce::FileSearchPath(), true, true);
            puts(juce::JSON::toString(paths).toRawUTF8());
            return 0;
#else
            std::cerr << "No any file specified.\n";
            return -1;
#endif
        }
        juce::initialiseJuce_GUI();
        juce::OwnedArray<juce::PluginDescription> results;
        for (auto it : manager.getFormats()) it->findAllTypesForFile(results, id);

        if (results.isEmpty()) {
            juce::shutdownJuce_GUI();
            return -1;
        }
        juce::Array<juce::var> arr;
        for (auto it : results) {
            auto obj = new juce::DynamicObject();
            obj->setProperty("name", it->name);
            obj->setProperty("descriptiveName", it->descriptiveName);
            obj->setProperty("pluginFormatName", it->pluginFormatName);
            obj->setProperty("category", it->category);
            obj->setProperty("manufacturerName", it->manufacturerName);
            obj->setProperty("version", it->version);
            obj->setProperty("identifier", it->createIdentifierString());
            obj->setProperty("fileOrIdentifier", it->fileOrIdentifier);
            obj->setProperty("lastFileModTime", it->lastFileModTime.toMilliseconds());
            obj->setProperty("lastInfoUpdateTime", it->lastInfoUpdateTime.toMilliseconds());
            obj->setProperty("deprecatedUid", it->deprecatedUid);
            obj->setProperty("uniqueId", it->uniqueId);
            obj->setProperty("isInstrument", it->isInstrument);
            obj->setProperty("hasSharedContainer", it->hasSharedContainer);
            obj->setProperty("hasARAExtension", it->hasARAExtension);
            arr.add(obj);
        }
        puts(("$EIMHostScanner{{" + juce::JSON::toString(arr, true) + "}}EIMHostScanner$").toRawUTF8());
        juce::shutdownJuce_GUI();
    } else if (args->containsOption("-L|--load")) {
        juce::initialiseJuce_GUI();
        manager.addDefaultFormats();
        juce::JUCEApplicationBase::createInstance = EIMPluginHost::createInstance;
        juce::JUCEApplicationBase::main(argc, (const char**)argv);
        juce::shutdownJuce_GUI();
    } else if (args->containsOption("-O|--output")) {
#ifdef JUCE_WINDOWS
        CoInitialize(nullptr);
#endif

        juce::AudioDeviceManager deviceManager;
        if (args->containsOption("-A|--all")) {
            for (auto& it : deviceManager.getAvailableDeviceTypes()) {
                it->scanForDevices();
                for (auto& j : it->getDeviceNames()) {
                    std::cout << "[" << it->getTypeName() << "] " << j << "$EIM$";
                }
            }
            fflush(stdout);
            return 0;
        }
        auto deviceType = args->getValueForOption("-T|--type");
        auto deviceName = args->getValueForOption("-O|--output");
        setup.bufferSize = args->containsOption("-B|--bufferSize") ? args->getValueForOption("-B|--bufferSize").getIntValue() : 1024;
        setup.sampleRate = args->containsOption("-R|--sampleRate") ? args->getValueForOption("-R|--sampleRate").getIntValue() : 48000;

        AudioCallback audioCallback(deviceManager);
        for (auto& it : deviceManager.getAvailableDeviceTypes()) {
            if (deviceType == it->getTypeName()) it->scanForDevices();
        }
        if (deviceType.isNotEmpty()) deviceManager.setCurrentAudioDeviceType(deviceType, true);
        if (deviceName.isNotEmpty()) setup.outputDeviceName = setup.inputDeviceName = deviceName;
        auto error = deviceManager.initialise(0, 2, nullptr, true, "", &setup);
        if (error.isNotEmpty()) {
            writeError(error, stdout);
            return 1;
        }
        deviceManager.addAudioCallback(&audioCallback);

        write((short)0x0102);
        fflush(stdout);

        freopen(nullptr, "rb", stdin);
        freopen(nullptr, "wb", stdout);
#ifdef JUCE_WINDOWS
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        audioCallback.waitDeviceClose();
        deviceManager.closeAudioDevice();
    }
    return 0;
}
