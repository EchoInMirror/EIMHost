#include <juce_audio_processors/juce_audio_processors.h>
#include "components.h"

#ifdef JUCE_WINDOWS
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

constexpr int TITLE_BAR_TOP_PADDING = 0;
constexpr int TITLE_BAR_HEIGHT = 40;
#else
constexpr int TITLE_BAR_TOP_PADDING = 30;
constexpr int TITLE_BAR_HEIGHT = 70;
#endif

const char* SAVE_ICON = R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path d="M840-680v480q0 33-23.5 56.5T760-120H200q-33 0-56.5-23.5T120-200v-560q0-33 23.5-56.5T200-840h480l160 160Zm-80 34L646-760H200v560h560v-446ZM480-240q50 0 85-35t35-85q0-50-35-85t-85-35q-50 0-85 35t-35 85q0 50 35 85t85 35ZM240-560h360v-160H240v160Zm-40-86v446-560 114Z"/></svg>)";

class plugin_state_switchers : public juce::Component {
public:
    explicit plugin_state_switchers(const juce::String &name, juce::AudioProcessor* instance) : juce::Component(name) {
        setSize(140, 32);
        for (int i = 0; i < 3; i++) {
            buttons[i].setTopLeftPosition((i + 1) * 36, 0);
            buttons[i].setRadioGroupId(2);
            int temp = i;
            buttons[i].onClick = [this, temp, instance] {
                currentButton = temp;
                if (buttons[temp].getUnderline())
                    instance->setStateInformation(states[temp].getData(), (int) states[temp].getSize());
            };
            addAndMakeVisible(buttons[i]);
        }
        saveButton.onClick = [this, instance] {
            instance->getStateInformation(states[currentButton]);
            buttons[currentButton].setUnderline(true);
            buttons[currentButton].setToggleState(true, juce::sendNotification);
        };
        addAndMakeVisible(saveButton);
    }

private:
    int currentButton = 0;
    eim::component_icon_button saveButton{SAVE_ICON};
    eim::component_toggle_button buttons[3]{
        eim::component_toggle_button("A"),
        eim::component_toggle_button("B"),
        eim::component_toggle_button("C")
    };

    juce::MemoryBlock states[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_state_switchers)
};

class plugin_decorate_component : public juce::Component, private juce::ComponentListener {
public:
    explicit plugin_decorate_component(juce::AudioProcessorEditor* _component, const juce::String& name) :
        juce::Component(name), component(_component), stateSwitchers("State Switchers", component->getAudioProcessor()) {
        setBounds(0, 0, component->getWidth(), TITLE_BAR_HEIGHT + component->getHeight());
        component->addComponentListener(this);
        addAndMakeVisible(component);

        setBypass(false);
        addAndMakeVisible(bypassButton);

        presetName.setSize(160, presetName.getHeight());
        presetName.setEnabled(false);
        presetName.setText("Init");
        addAndMakeVisible(presetName);

        addAndMakeVisible(stateSwitchers);

        setInterceptsMouseClicks(false, true);
        plugin_decorate_component::resized();
    }

    void setFocused(bool focus) {
        isFocused = focus;
        repaint();
    }

    void setBypass(bool bypass) { bypassButton.setToggleState(!bypass, juce::dontSendNotification); }
    [[nodiscard]] juce::Value& getBypassState() { return bypassButton.getToggleStateValue(); }

    ~plugin_decorate_component() override {
        delete component;
    }
private:
    juce::AudioProcessorEditor* component;
    eim::component_switcher bypassButton{"Bypass"};
    eim::component_text_field presetName{"Preset Name"};
    plugin_state_switchers stateSwitchers;
    bool isFocused = false;

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff28262e));

#ifndef JUCE_WINDOWS
        g.setColour(juce::Colour(isFocused ? 0xafffffff : 0x3fffffff));
        g.setFont(g.getCurrentFont().withStyle(juce::Font::bold).withHorizontalScale(1.04F));
        g.drawText(getName(), 0, 0, getWidth(), TITLE_BAR_TOP_PADDING, juce::Justification::centred, true);
#endif
    }

    void resized() override {
        component->setBounds(0, TITLE_BAR_HEIGHT, getWidth(), getHeight() - TITLE_BAR_HEIGHT);

        bypassButton.setTopLeftPosition(16, TITLE_BAR_TOP_PADDING);
        stateSwitchers.setTopLeftPosition(getWidth() - stateSwitchers.getWidth() - 8, TITLE_BAR_TOP_PADDING);
        presetName.setSize(juce::jmin(
            (int)(getWidth() / 3),
			getWidth() - bypassButton.getWidth() - stateSwitchers.getWidth() - 46
        ), 32);
        presetName.setTopLeftPosition(
			getWidth() / 2 + presetName.getWidth() / 2 > getWidth() - stateSwitchers.getWidth() - 16 ?
			32 + bypassButton.getWidth() : getWidth() / 2 - presetName.getWidth() / 2,
            TITLE_BAR_TOP_PADDING
        );
    }

    void componentMovedOrResized(juce::Component& comp, bool, bool wasResized) override {
        if (wasResized) {
            auto width = comp.getWidth();
            auto height = comp.getHeight() + TITLE_BAR_HEIGHT;
            if (width != getWidth() || height != getHeight()) setBounds(0, TITLE_BAR_HEIGHT, width, height);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_decorate_component)
};

class plugin_window : public juce::ResizableWindow {
public:
    static int width_, height_, x_, y_;

    plugin_window(const juce::String& title, juce::AudioProcessorEditor* component, std::unique_ptr<plugin_window>& ptr,
        bool resizable, long long parentHandle)
        : ResizableWindow(title, !parentHandle), thisWindow(ptr), decorate_component(component, title) {
        setUsingNativeTitleBar(true);
        setResizable(resizable && component->isResizable(), false);
        
        if (width_ == 0) width_ = decorate_component.getWidth();
        if (height_ == 0) height_ = decorate_component.getHeight();

        auto screenBounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true).toFloat();
        auto scaleFactor = juce::jmin((screenBounds.getWidth() - 50) / (float)getWidth(), (screenBounds.getHeight() - 50) / (float)getHeight());
        auto trueWidth = scaleFactor < 1.0f ? (int)((float)width_ * scaleFactor) : width_;
        auto trueHeight = scaleFactor < 1.0f ? (int)((float)height_ * scaleFactor) : height_;

        if (x_ > 20 && y_ > 20 && x_ <= screenBounds.getWidth() - 20.0 && y_ <= screenBounds.getHeight() - 20.0) {
            setBounds(x_, y_, trueWidth, trueHeight);
        } else centreWithSize(trueWidth, trueHeight);

        setContentOwned(&decorate_component, true);
        plugin_window::setVisible(true);
        
#ifdef JUCE_WINDOWS
        if (parentHandle) addToDesktop(getDesktopWindowStyleFlags(), (HWND) parentHandle);
        auto handle = (HWND)this->getWindowHandle();
        long color = 0x002e2628;
        DwmSetWindowAttribute(handle, 35, &color, 4);
        color = 0x004f4549;
        DwmSetWindowAttribute(handle, 34, &color, 4);
#else
        setAlwaysOnTop(true);
#endif
    }
    
    [[nodiscard]] int getDesktopWindowStyleFlags() const override {
        return ResizableWindow::getDesktopWindowStyleFlags()
            | juce::ComponentPeer::windowHasMinimiseButton
            | (isResizable() ? juce::ComponentPeer::windowHasMaximiseButton : 0)
            | juce::ComponentPeer::windowHasCloseButton
            | 1 << 28;
    }

    void setBypass(bool bypass) { decorate_component.setBypass(bypass); }
    juce::Value& getBypassState() { return decorate_component.getBypassState(); }

private:
    std::unique_ptr<plugin_window>& thisWindow;
    plugin_decorate_component decorate_component;

    void moved() override {
        juce::ResizableWindow::moved();
        x_ = getX();
        y_ = getY();
    }

    void resized() override {
        juce::ResizableWindow::resized();
        width_ = getWidth();
        height_ = getHeight();
    }

    void activeWindowStatusChanged() override {
        juce::ResizableWindow::activeWindowStatusChanged();
        decorate_component.setFocused(isActiveWindow());
    }

    void userTriedToCloseWindow() override { thisWindow.reset(nullptr); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_window)
};

int plugin_window::height_ = 0;
int plugin_window::width_ = 0;
int plugin_window::x_ = 0;
int plugin_window::y_ = 0;
