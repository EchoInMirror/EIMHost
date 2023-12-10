#include "plugin_host.h"
#include "audio_output.h"

#if JUCE_MAC
namespace juce { extern void initialiseNSApplication(); }
#endif

class simple_msgbox : private juce::ModalComponentManager::Callback {
public:
    void modalStateFinished(int) override { juce::MessageManager::getInstance()->stopDispatchLoop(); }
    static void show(const juce::String& text) {
        juce::initialiseJuce_GUI();
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "EIMHost", text, nullptr, new simple_msgbox);
        juce::MessageManager::getInstance()->runDispatchLoop();
        juce::shutdownJuce_GUI();
    }
};

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);
    std::cerr.tie(nullptr);
    auto args = new juce::ArgumentList(argc, argv);
    eim::args = args;

    if (eim::args->containsOption("-S|--scan")) {
        juce::AudioPluginFormatManager manager;
        manager.addDefaultFormats();
        auto id = args->getValueForOption("-S|--scan");
        if (id.isEmpty()) {
            juce::StringArray paths;
            for (auto it : manager.getFormats()) {
                paths.addArray(it->searchPathsForPlugins(juce::FileSearchPath(), true, true));
            }
            puts(juce::JSON::toString(paths, true).toRawUTF8());
            return 0;
        } else if (id == "#") {
            char path[512];
            std::cin.getline(path, 512, 0);
            id = path;
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
        eim::streams::output_stream::preventStdout();
        juce::JUCEApplicationBase::createInstance = eim::plugin_host::createInstance;
        juce::JUCEApplicationBase::main(argc, (const char**)argv);
    } else if (args->containsOption("-O|--output")) {
#ifdef JUCE_WINDOWS
        juce::ignoreUnused(CoInitialize(nullptr));
#endif

        juce::initialiseJuce_GUI();
        juce::AudioDeviceManager deviceManager;
        if (args->containsOption("-A|--all")) {
            for (auto& it : deviceManager.getAvailableDeviceTypes()) {
                it->scanForDevices();
                for (auto& j : it->getDeviceNames()) {
                    std::cout << "[" << it->getTypeName() << "] " << j << "$EIM$";
                }
            }
            fflush(stdout);
            juce::shutdownJuce_GUI();
            return 0;
        }
        auto deviceType = args->getValueForOption("-T|--type");
        auto deviceName = args->getValueForOption("-O|--output");

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.bufferSize = args->containsOption("-B|--bufferSize") ? args->getValueForOption("-B|--bufferSize").getIntValue() : 1024;
        setup.sampleRate = args->containsOption("-R|--sampleRate") ? args->getValueForOption("-R|--sampleRate").getIntValue() : 48000;

        eim::streams::output_stream::preventStdout();
        eim::streams::out.writeByteOrderMessage();
        
        if (deviceName == "#") deviceName = eim::streams::in.readString();
        
        auto memorySize = args->getValueForOption("-MS|--memory-size");
        eim::audio_output audioCallback(deviceManager, setup, args->getValueForOption("-M|--memory"),
            memorySize.isEmpty() ? 0 : memorySize.getIntValue());
        for (auto& it : deviceManager.getAvailableDeviceTypes()) {
            if (deviceType == it->getTypeName()) it->scanForDevices();
        }
        if (deviceType.isNotEmpty()) deviceManager.setCurrentAudioDeviceType(deviceType, true);
        if (deviceName.isNotEmpty() && deviceName != "#") setup.outputDeviceName = deviceName;
        auto error = deviceManager.initialise(0, 2, nullptr, true, "", &setup);
        if (error.isNotEmpty()) {
            eim::streams::out.writeError(error);
            juce::shutdownJuce_GUI();
            return 1;
        }

        deviceManager.addAudioCallback(&audioCallback);

#if JUCE_MAC
        juce::initialiseNSApplication();
#endif
        juce::MessageManager::getInstance()->runDispatchLoop();
        deviceManager.closeAudioDevice();
        juce::shutdownJuce_GUI();
        return audioCallback.getExitCode();
    } else {
#ifdef JUCE_WINDOWS
        auto javaFile = "java.exe";
#else
        auto javaFile = "java";
#endif
        auto currentExec = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentExecutableFile);
        auto file = currentExec.getSiblingFile("jre/bin").getChildFile(javaFile);
        if (file.exists()) {
            auto vmoptions = currentExec.getSiblingFile(".vmoptions");
            juce::StringArray arr;
            if (vmoptions.exists()) vmoptions.readLines(arr);
            arr.insert(0, file.getFullPathName());
            arr.add("-jar");
            arr.add("EchoInMirror.jar");
            juce::ChildProcess process;
            process.start(arr);
        } else {
            std::cerr << "Cannot find java!\n";
            fflush(stderr);
            simple_msgbox::show("Java Runtime Environment not found.\nPlease install Java Runtime Environment 21 or higher.");
        }
    }
    return 0;
}
