#pragma warning(disable: 6031)
#pragma warning(disable: 6387)
#pragma warning(disable: 6029)

#include "PluginWindow.h"
#include <io.h>
#include <fcntl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#define READ(var) std::fread(&var, sizeof(var), 1, stdin)

void setIOMode() {
    freopen(nullptr, "rb", stdin);
    freopen(nullptr, "wb", stdout);
#ifdef JUCE_WINDOWS
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

juce::ArgumentList* args;
juce::AudioPluginFormatManager manager;
juce::AudioDeviceManager::AudioDeviceSetup setup;
bool done = false;

template <typename T> inline void write(T var) { std::fwrite(&var, sizeof(var), 1, stdout); }
void writeError(juce::String str) {
    write((char)127);
    write(str.length());
    std::fwrite(str.toRawUTF8(), sizeof(char), str.length(), stdout);
}

class EIMPluginHost : public juce::JUCEApplication, public juce::AudioPlayHead, private juce::Thread {
public:
    EIMPluginHost(): juce::AudioPlayHead(), juce::Thread("IO Thread") { }

    const juce::String getApplicationName() override { return "EIMPluginHost"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        write((short)0x0102);
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
            writeError(error);
            quit();
            return;
        }
        processor->setPlayHead(this);
		window.reset(new PluginWindow(*processor, args->containsOption("-H|--handle")
            ? args->getValueForOption("-H|--handle").getLargeIntValue() : 0));
        setIOMode();
        /*
        processor->prepareToPlay(sampleRate, bufferSize);
        buffer.setSize(juce::jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels()), bufferSize);
        double bpm = 140;
        long long timeInSamples = 0;
        double timeInSeconds = (double)timeInSamples / sampleRate;
        positionInfo.setBpm(bpm);
        positionInfo.setTimeInSamples(timeInSamples);
        positionInfo.setTimeInSeconds(timeInSeconds);
        positionInfo.setPpqPosition(timeInSeconds / 60.0 * bpm);
        juce::MidiBuffer buf;
        processor->enableAllBuses();
        processor->processBlock(buffer, buf);
        */

        write((char) 0);
        write(processor->getTotalNumInputChannels());
        write(processor->getTotalNumOutputChannels());
        fflush(stdout);
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
                    char numInputChannels, numOutputChannels;
                    char isPlaying;
                    READ(isPlaying);
                    READ(bpm);
                    READ(timeInSamples);
                    READ(numInputChannels);
                    READ(numOutputChannels);
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
                    processor->processBlock(buffer, buf);
                    write((char) 1);
                    for (int i = 0; i < numOutputChannels; i++) std::fwrite(buffer.getReadPointer(i), sizeof(float), bufferSize, stdout);
                    fflush(stdout);
                    break;
                }
            }
        }
        stopThread(-1);
    }

    void shutdown() override {
        window = nullptr;
        processor = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String &) override {
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return positionInfo; }

    static juce::JUCEApplicationBase* createInstance() { return new EIMPluginHost(); }

private:
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<PluginWindow> window;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    juce::AudioPlayHead::PositionInfo positionInfo;
    int sampleRate = 44800, bufferSize = 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EIMPluginHost)
};

class AudioCallback : public juce::AudioIODeviceCallback {
public:
    AudioCallback() {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int, int, const juce::AudioIODeviceCallbackContext&) override {
        write((char)0);
        fflush(stdout);
        char id;
        if (std::fread(&id, 1, 1, stdin) != 1) return;
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
        std::cerr << "Device started: " << device->getName() << std::endl;
    }

    void audioDeviceStopped() override { }

    void audioDeviceError(const juce::String& errorMessage) override { std::cerr << errorMessage << std::endl; }
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
        juce::AudioDeviceManager deviceManager;
        AudioCallback audioCallback;

        setup.bufferSize = args->containsOption("-B|--bufferSize") ? args->getValueForOption("-B|--bufferSize").getIntValue() : 1024;
        setup.sampleRate = args->containsOption("-R|--sampleRate") ? args->getValueForOption("-R|--sampleRate").getIntValue() : 44800;
        deviceManager.initialise(0, 2, nullptr, false, "", &setup);
        deviceManager.addAudioCallback(&audioCallback);

        write((short)0x0102);
        fflush(stdout);
        setIOMode();
        while (!done) juce::Thread::sleep(50);
    }
    return 0;
}
