#include <libipc/shm.h>
#include "PluginWindow.h"

juce::ArgumentList* args;
juce::AudioPluginFormatManager manager;

class EIMPluginHost : public juce::JUCEApplication, public juce::AudioPlayHead {
public:
    EIMPluginHost(): juce::AudioPlayHead() { }

    const juce::String getApplicationName() override { return "EIMPluginHost"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String & commandLine) override {
        juce::PluginDescription desc;
        auto xml = juce::parseXML(args->getValueForOption("-l|--load"));
        desc.loadFromXml(*xml);
        xml = nullptr;
        juce::String error;
        processor = manager.createPluginInstance(desc, 96000, 1024, error);
        processor->setPlayHead(this);
        window.reset(new PluginWindow(*processor));
        // ipc::shm::acquire(args.getValueForOption("token").toRawUTF8(), 1024, ipc::shm::open);
    }

    void shutdown() override {
        window = nullptr;
        processor = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String & commandLine) override {
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return positionInfo; }

    static juce::JUCEApplicationBase* createInstance() { return new EIMPluginHost(); }

private:
    std::unique_ptr<PluginWindow> window;
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioPlayHead::PositionInfo positionInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EIMPluginHost)
};

int main(int argc, char* argv[]) {
    args = new juce::ArgumentList(argc, argv);
    juce::initialiseJuce_GUI();
    manager.addDefaultFormats();

    if (args->containsOption("-s|--scan")) {
        juce::OwnedArray<juce::PluginDescription> results;
        for (auto it : manager.getFormats()) it->findAllTypesForFile(results, args->getValueForOption("-s|--scan"));

        if (results.isEmpty()) {
            juce::shutdownJuce_GUI();
            return -1;
        }
        juce::XmlElement::TextFormat format;
        format.newLineChars = nullptr;
        format.addDefaultHeader = false;
        for (auto it : results) {
            puts(it->createXml().release()->toString(format).toRawUTF8());
            putchar('\n');
        }
        juce::shutdownJuce_GUI();
        return 0;
    } else if (args->containsOption("-l|--load")) {
        if (args->getValueForOption("-t|--token").isEmpty()) {
            juce::shutdownJuce_GUI();
            return -1;
        }
        juce::JUCEApplicationBase::createInstance = EIMPluginHost::createInstance;
        juce::JUCEApplicationBase::main(argc, (const char**)argv);
        juce::shutdownJuce_GUI();
    }

    return 0;
}
