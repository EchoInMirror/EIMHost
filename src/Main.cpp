#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[]) {
    juce::initialiseJuce_GUI();
    juce::AudioPluginFormatManager manager;
    manager.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> results;
    for (auto it : manager.getFormats()) it->findAllTypesForFile(results, juce::StringArray(argv + 1, argc - 1).joinIntoString(" "));
    if (results.isEmpty()) return 0;
    juce::XmlElement::TextFormat format;
    format.newLineChars = nullptr;
    format.addDefaultHeader = false;
    for (auto it : results) {
        puts(it->createXml().release()->toString(format).toRawUTF8());
        putchar('\n');
    }
    juce::shutdownJuce_GUI();
    return 0;
}
