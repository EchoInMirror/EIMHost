#pragma warning(disable: 6031)
#pragma warning(disable: 6387)
#pragma warning(disable: 6029)

#include "PluginWindow.h"
#include <io.h>
#include <fcntl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#define READ(var) std::fread(&var, sizeof(var), 1, stdin)

juce::ArgumentList* args;
juce::AudioPluginFormatManager manager;
juce::AudioDeviceManager::AudioDeviceSetup setup;
bool done = false;

template <typename T> inline void write(T var) { std::fwrite(&var, sizeof(var), 1, stdout); }
template <typename T> inline void writeCerr(T var) { std::fwrite(&var, sizeof(var), 1, stderr); }
void writeError(juce::String str, FILE* file) {
    write((char)127);
    write(str.length());
    std::fwrite(str.toRawUTF8(), sizeof(char), str.length(), file);
    fflush(file);
    (file == stderr ? std::cout : std::cerr) << str << '\n';
}

class EIMPluginHost : public juce::JUCEApplication, public juce::AudioPlayHead, private juce::Thread {
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
        createEditorWindow();

        freopen(nullptr, "rb", stdin);
        freopen(nullptr, "wb", stderr);
#ifdef JUCE_WINDOWS
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
#endif

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
                    char isPlaying;
                    READ(isPlaying);
                    READ(bpm);
                    READ(timeInSamples);
                    READ(numInputChannels);
                    READ(numOutputChannels);
                    READ(midiEvents);
                    double timeInSeconds = (double)timeInSamples / sampleRate;
                    juce::MessageManagerLock mml(Thread::getCurrentThread());
                    if (!mml.lockWasGained()) return;
                    positionInfo.setIsPlaying(isPlaying);
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
                    processor->processBlock(buffer, buf);
                    writeCerr((char) 1);
                    for (int i = 0; i < numOutputChannels; i++) std::fwrite(buffer.getReadPointer(i), sizeof(float), bufferSize, stderr);
                    fflush(stderr);
                    break;
                }
                case 2: {
                    if (window == nullptr) createEditorWindow();
                    else window.reset(nullptr);
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

private:
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<PluginWindow> window;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    juce::AudioPlayHead::PositionInfo positionInfo;
    int sampleRate = 44800, bufferSize = 1024, ppq = 96;

    void createEditorWindow() {
		if (!processor->hasEditor()) return;
        auto component = processor->createEditorIfNeeded();
        if (!component) return;
        window.reset(new PluginWindow("[EIMHost] " + processor->getName() + " (" +
            processor->getPluginDescription().pluginFormatName + ")", component, window,
            processor->wrapperType != juce::AudioProcessor::wrapperType_VST,
            args->containsOption("-H|--handle") ? args->getValueForOption("-H|--handle").getLargeIntValue() : 0));
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
            done = true;
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
        default: done = true;
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        std::cerr << "Device started: " << device->getName() << ", " << device->getOutputLatencyInSamples() << '\n';
    }

    void audioDeviceStopped() override { }

    void audioDeviceError(const juce::String& errorMessage) override { std::cerr << errorMessage << '\n'; }
private:
    juce::AudioDeviceManager& deviceManager;
};

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(0);
    std::cout.tie(0);
    std::cerr.tie(0);
    args = new juce::ArgumentList(argc, argv);

    if (args->containsOption("-S|--scan")) {
        juce::initialiseJuce_GUI();
        manager.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> results;
        for (auto it : manager.getFormats()) it->findAllTypesForFile(results, args->getValueForOption("-S|--scan"));

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
        auto deviceType = args->getValueForOption("-T|--type");
        auto deviceName = args->getValueForOption("-O|--output");
        setup.bufferSize = args->containsOption("-B|--bufferSize") ? args->getValueForOption("-B|--bufferSize").getIntValue() : 1024;
        setup.sampleRate = args->containsOption("-R|--sampleRate") ? args->getValueForOption("-R|--sampleRate").getIntValue() : 44800;

#ifdef JUCE_WINDOWS
        CoInitialize(nullptr);
#endif

        juce::AudioDeviceManager deviceManager;
        AudioCallback audioCallback(deviceManager);
        for (auto& it : deviceManager.getAvailableDeviceTypes()) {
            if (deviceType == it->getTypeName()) it->scanForDevices();
            /*std::cerr << it->getTypeName() << "\n";
            for (auto& j : it->getDeviceNames()) std::cerr << "    " << j << "\n";*/
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
        while (!done) juce::Thread::sleep(100);
        deviceManager.closeAudioDevice();
    }
    return 0;
}
