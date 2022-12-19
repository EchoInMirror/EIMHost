#include <juce_audio_processors/juce_audio_processors.h>
#ifdef JUCE_WINDOWS
#include <Windows.h>
#endif

int _width = 0, _height = 0;

class PluginWindow : public juce::ResizableWindow {
public:
	PluginWindow(juce::String title, juce::Component* component, std::unique_ptr<PluginWindow>& ptr,
		bool resizable, long long parentHandle)
		: ResizableWindow(title, !parentHandle), thisWindow(ptr) {
		setUsingNativeTitleBar(true);
		if (resizable) setResizable(true, false);
		
		if (_width == 0) _width = component->getWidth();
		if (_height == 0) _height = component->getHeight();
		setSize(_width, _height);
		setContentOwned(component, true);

		auto screenBounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true).toFloat();
		auto scaleFactor = juce::jmin((screenBounds.getWidth() - 50) / getWidth(), (screenBounds.getHeight() - 50) / getHeight());

		if (scaleFactor < 1.0f)
			setSize((int)(getWidth() * scaleFactor), (int)(getHeight() * scaleFactor));

		setTopLeftPosition(20, 20);
		setVisible(true);

#ifdef JUCE_WINDOWS
		if (parentHandle) addToDesktop(getDesktopWindowStyleFlags(), (HWND)(LONG_PTR)parentHandle);
#endif
	}

	~PluginWindow() override {
		clearContentComponent();
	}

	void moved() override { }

	void userTriedToCloseWindow() override { thisWindow.reset(nullptr); }
	
	int getDesktopWindowStyleFlags() const override {
		return ResizableWindow::getDesktopWindowStyleFlags()
			| juce::ComponentPeer::windowHasMaximiseButton
			| juce::ComponentPeer::windowHasCloseButton
			| 1 << 28;
	}

private:
	std::unique_ptr<PluginWindow>& thisWindow;
	
	float getDesktopScaleFactor() const override { return 1.0f; }

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};
