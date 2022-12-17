#include <juce_audio_processors/juce_audio_processors.h>
#ifdef JUCE_WINDOWS
#include <Windows.h>
#endif

inline juce::String getFormatSuffix(const juce::AudioProcessor* plugin) {
	if (auto* instance = dynamic_cast<const juce::AudioPluginInstance*> (plugin))
		return " (" + instance->getPluginDescription().pluginFormatName + ")";

	return "";
}

class PluginWindow : public juce::ResizableWindow {
public:
	PluginWindow(juce::AudioProcessor& p, long long parentHandle)
		: ResizableWindow("[EIMHost] " + p.getName() + getFormatSuffix(&p), !parentHandle), processor(p) {
		setUsingNativeTitleBar(true);
		setSize(400, 300);
		if (p.wrapperType != p.wrapperType_VST) setResizable(true, false);

		auto* ui = createProcessorEditor(processor);
		if (ui) setContentOwned(ui, true);

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

	void moved() override {
	}

	void userTriedToCloseWindow() override {
		juce::JUCEApplication::quit();
	}

	juce::AudioProcessor& processor;
	
	int getDesktopWindowStyleFlags() const override {
		return ResizableWindow::getDesktopWindowStyleFlags()
			| juce::ComponentPeer::windowHasMaximiseButton
			| juce::ComponentPeer::windowHasCloseButton
			| 1 << 28;
	}

private:
	float getDesktopScaleFactor() const override { return 1.0f; }

	static juce::AudioProcessorEditor* createProcessorEditor(juce::AudioProcessor& processor) {
		if (processor.hasEditor())
			if (auto* ui = processor.createEditorIfNeeded())
				return ui;

		/*if (type == PluginWindow::Type::araHost) {
#if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS)
			if (auto* araPluginInstanceWrapper = dynamic_cast<ARAPluginInstanceWrapper*> (&processor))
				if (auto* ui = araPluginInstanceWrapper->createARAHostEditor())
					return ui;
#endif
			return {};
		}*/

		jassertfalse;
		return {};
	}

	//==============================================================================
	struct ProgramAudioProcessorEditor : public juce::AudioProcessorEditor {
		ProgramAudioProcessorEditor(juce::AudioProcessor& p) : juce::AudioProcessorEditor(p) {
			setOpaque(true);

			addAndMakeVisible(panel);

			juce::Array<juce::PropertyComponent*> programs;

			auto numPrograms = p.getNumPrograms();
			int totalHeight = 0;

			for (int i = 0; i < numPrograms; ++i) {
				auto name = p.getProgramName(i).trim();

				if (name.isEmpty())
					name = "Unnamed";

				auto pc = new PropertyComp(name, p);
				programs.add(pc);
				totalHeight += pc->getPreferredHeight();
			}

			panel.addProperties(programs);

			setSize(400, juce::jlimit(25, 400, totalHeight));
		}

		void paint(juce::Graphics& g) override {
			g.fillAll(juce::Colours::grey);
		}

		void resized() override {
			panel.setBounds(getLocalBounds());
		}

	private:
		struct PropertyComp : public juce::PropertyComponent,
			private juce::AudioProcessorListener {
			PropertyComp(const juce::String& name, juce::AudioProcessor& p) : PropertyComponent(name), owner(p) {
				owner.addListener(this);
			}

			~PropertyComp() override {
				owner.removeListener(this);
			}

			void refresh() override { }
			void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { }
			void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { }

			juce::AudioProcessor& owner;

			JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PropertyComp)
		};

		juce::PropertyPanel panel;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramAudioProcessorEditor)
	};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};
