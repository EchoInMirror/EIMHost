#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[]) {
    juce::initialiseJuce_GUI();
    juce::AudioPluginFormatManager manager;
    manager.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> results;
    for (auto it : manager.getFormats()) it->findAllTypesForFile(results, juce::StringArray(argv, argc).joinIntoString(" "));
    if (results.isEmpty()) return 1;
    juce::XmlElement::TextFormat format;
    format.newLineChars = nullptr;
    format.addDefaultHeader = false;
    for (auto it : results) puts(it->createXml()->toString(format).toRawUTF8());
    juce::shutdownJuce_GUI();
    return 0;
}
