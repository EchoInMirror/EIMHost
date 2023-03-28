#include <juce_audio_processors/juce_audio_processors.h>
#ifdef JUCE_WINDOWS
#include <Windows.h>
#endif

int _width = 0, _height = 0, _x = 0, _y = 0;

class PluginWindow : public juce::ResizableWindow {
public:
	PluginWindow(juce::String title, juce::Component* component, std::unique_ptr<PluginWindow>& ptr,
		bool resizable, long long parentHandle)
		: ResizableWindow(title, !parentHandle), thisWindow(ptr) {
		setUsingNativeTitleBar(true);
		if (resizable) setResizable(true, false);
		
		if (_width == 0) _width = component->getWidth();
		if (_height == 0) _height = component->getHeight();

		auto screenBounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true).toFloat();
		auto scaleFactor = juce::jmin((screenBounds.getWidth() - 50) / getWidth(), (screenBounds.getHeight() - 50) / getHeight());
		auto trueWidth = scaleFactor < 1.0f ? (int)(_width * scaleFactor) : _width;
		auto trueHeight = scaleFactor < 1.0f ? (int)(_height * scaleFactor) : _height;
		if (_x > 20 && _y > 20) setBounds(_x, _y, trueWidth, trueHeight); else centreWithSize(trueWidth, trueHeight);

		setContentOwned(component, true);
		setVisible(true);
		
#ifdef JUCE_WINDOWS
		if (parentHandle) addToDesktop(getDesktopWindowStyleFlags(), (HWND)(LONG_PTR)parentHandle);
#endif
	}

	~PluginWindow() override {
		clearContentComponent();
	}

	void moved() override {
		juce::ResizableWindow::moved();
		_x = getX();
		_y = getY();
	}

	void resized() override {
		juce::ResizableWindow::resized();
		_width = getWidth();
		_height = getHeight();
	}

	void userTriedToCloseWindow() override { thisWindow.reset(nullptr); }
	
	int getDesktopWindowStyleFlags() const override {
		return ResizableWindow::getDesktopWindowStyleFlags()
			| juce::ComponentPeer::windowHasMinimiseButton
			| (isResizable() ? juce::ComponentPeer::windowHasMaximiseButton : 0)
			| juce::ComponentPeer::windowHasCloseButton
			| 1 << 28;
	}

private:
	std::unique_ptr<PluginWindow>& thisWindow;
	
	float getDesktopScaleFactor() const override { return 1.0f; }

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};
