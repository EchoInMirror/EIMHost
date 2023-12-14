#include <juce_gui_basics/juce_gui_basics.h>

namespace eim {
    class component_switcher : public juce::ToggleButton {
    public:
        explicit component_switcher(const juce::String& name) : juce::ToggleButton(name) {
            setSize(52, 32);
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
            auto bounds = getLocalBounds();
            auto cornerSize = juce::jmin(15.0f, static_cast<float>(juce::jmin(bounds.getWidth(), bounds.getHeight())) * 0.45f);
            auto outlineThickness = juce::jmin(2.0f, cornerSize * 0.3f);
            auto halfThickness = outlineThickness / 2.0f;

            if (getToggleState()) {
                g.setColour(juce::Colour(0xffcdbdfa));
                g.fillRoundedRectangle(bounds.toFloat(), cornerSize);

                g.setColour(juce::Colour(0xff341f6e));
                g.fillEllipse(static_cast<float>(bounds.getWidth() - 28), 4, 24, 24);

                g.setColour(juce::Colour(0xffcdbdfa));
                // draw a check: √
                g.drawLine(31, 15, 35, 20, 1.5f);
                g.drawLine(35, 20, 41, 12, 1.5f);
            } else {
                g.setColour(juce::Colour(0xff48454e));
                g.fillRoundedRectangle(bounds.toFloat(), cornerSize);

                g.setColour(juce::Colour(0xff928f98));
                g.drawRoundedRectangle(bounds.toFloat().reduced(halfThickness), cornerSize, outlineThickness);
                g.fillEllipse(8, 8, 16, 16);
            }

            if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted) {
                g.setColour(juce::Colours::black.withAlpha(0.1f));
                g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
            }
        }
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(component_switcher)
    };

    class component_text_field : public juce::TextEditor {
    public:
        explicit component_text_field(const juce::String& name) : juce::TextEditor(name) {
            setSize(100, 32);
            setIndents(8, 8);
            setColour(juce::TextEditor::ColourIds::textColourId, juce::Colour(0x8fffffff));
        }

        void paint(juce::Graphics& g) override {
            auto bounds = getLocalBounds();
            g.setColour(juce::Colour(0xff48454e));
            auto cornerSize = 6.0f;
            g.fillRoundedRectangle(bounds.toFloat(), cornerSize);

        }

        void paintOverChildren(juce::Graphics&) override {
//            auto bounds = getLocalBounds();
//            auto cornerSize = 6.0f;
//            g.setColour(juce::Colour(0xff78747d));
//            g.drawRoundedRectangle(bounds.toFloat().reduced(1), cornerSize, hasFocus ? 2.0f : 1.0f);

//            g.setColour(juce::Colour(0xff28262e));
//            g.fillRect(8, -8, 32, 10);
        }
    private:
        bool hasFocus = false;

        void focusGained(FocusChangeType cause) override {
            hasFocus = true;
            juce::TextEditor::focusGained(cause);
        }

        void focusLost(FocusChangeType cause) override {
            hasFocus = false;
            juce::TextEditor::focusLost(cause);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(component_text_field)
    };

    class component_toggle_button : public juce::ToggleButton {
    public:
        explicit component_toggle_button(const juce::String& name) : juce::ToggleButton(name) {
            setSize(32, 32);
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
            auto bounds = getLocalBounds();
            if (getToggleState()) {
                g.setColour(juce::Colour(0xffcdbdfa));
                g.fillEllipse(bounds.toFloat());

                g.setColour(juce::Colour(0xff341f6e));
            } else {
                g.setColour(juce::Colour(0x4fffffff));
            }
            if (hasUnderline) g.setFont(g.getCurrentFont().withStyle(juce::Font::underlined));
            g.drawFittedText(getName(), bounds, juce::Justification::centred, 1);

            if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted) {
                g.setColour(juce::Colours::black.withAlpha(0.1f));
                g.fillEllipse(bounds.toFloat());
            }
        }

        void setUnderline(bool underline) {
            hasUnderline = underline;
            repaint();
        }

        [[nodiscard]] bool getUnderline() const { return hasUnderline; }

    private:
        bool hasUnderline = false;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(component_toggle_button)
    };

    class component_icon_button : public juce::Button {
    public:
        explicit component_icon_button(const juce::String& name) : juce::Button("") {
            image = juce::DrawableImage::createFromImageData(name.toRawUTF8(), (const size_t) name.length());
            setSize(32, 32);
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
            auto bounds = getLocalBounds();
            image->replaceColour(juce::Colours::black, juce::Colour(0x4fffffff));
            image->drawWithin(g, bounds.toFloat().reduced(8.0f), juce::RectanglePlacement::centred, 1.0f);

            if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted) {
                g.setColour(juce::Colours::black.withAlpha(shouldDrawButtonAsDown ? 0.2f : 0.1f));
                g.fillEllipse(bounds.toFloat());
            }
        }

    private:
       std::unique_ptr<juce::Drawable> image;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(component_icon_button)
    };
}
