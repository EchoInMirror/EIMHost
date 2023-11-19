#include <juce_audio_processors/juce_audio_processors.h>
#ifdef JUCE_WINDOWS
#include <Windows.h>
#endif

class plugin_window : public juce::ResizableWindow {
public:
    static int width_, height_, x_, y_;

    plugin_window(const juce::String& title, juce::AudioProcessorEditor* component, std::unique_ptr<plugin_window>& ptr,
        bool resizable, long long parentHandle)
        : ResizableWindow(title, !parentHandle), thisWindow(ptr) {
        setUsingNativeTitleBar(true);
        setResizable(resizable && component->isResizable(), false);
        
        if (width_ == 0) width_ = component->getWidth();
        if (height_ == 0) height_ = component->getHeight();

        auto screenBounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true).toFloat();
        auto scaleFactor = juce::jmin((screenBounds.getWidth() - 50) / (float)getWidth(), (screenBounds.getHeight() - 50) / (float)getHeight());
        auto trueWidth = scaleFactor < 1.0f ? (int)((float)width_ * scaleFactor) : width_;
        auto trueHeight = scaleFactor < 1.0f ? (int)((float)height_ * scaleFactor) : height_;

        if (x_ > 20 && y_ > 20 && x_ <= screenBounds.getWidth() - 20.0 && y_ <= screenBounds.getHeight() - 20.0) {
            setBounds(x_, y_, trueWidth, trueHeight);
        } else centreWithSize(trueWidth, trueHeight);

        setContentOwned(component, true);
        plugin_window::setVisible(true);
        
#ifdef JUCE_WINDOWS
        if (parentHandle) addToDesktop(getDesktopWindowStyleFlags(), (HWND)(LONG_PTR)parentHandle);
#else
        setAlwaysOnTop(true);
#endif
    }

    ~plugin_window() override {
        clearContentComponent();
    }

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

    void userTriedToCloseWindow() override { thisWindow.reset(nullptr); }
    
    [[nodiscard]] int getDesktopWindowStyleFlags() const override {
        return ResizableWindow::getDesktopWindowStyleFlags()
            | juce::ComponentPeer::windowHasMinimiseButton
            | (isResizable() ? juce::ComponentPeer::windowHasMaximiseButton : 0)
            | juce::ComponentPeer::windowHasCloseButton
            | 1 << 28;
    }

private:
    std::unique_ptr<plugin_window>& thisWindow;
    
    [[nodiscard]] float getDesktopScaleFactor() const override { return 1.0f; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(plugin_window)
};

int plugin_window::height_ = 0;
int plugin_window::width_ = 0;
int plugin_window::x_ = 0;
int plugin_window::y_ = 0;
