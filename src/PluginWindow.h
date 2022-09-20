#include <juce_audio_processors/juce_audio_processors.h>

inline juce::String getFormatSuffix(const juce::AudioProcessor* plugin) {
	if (auto* instance = dynamic_cast<const juce::AudioPluginInstance*> (plugin))
		return " (" + instance->getPluginDescription().pluginFormatName + ")";

	return "";
}

class PluginWindow : public juce::DocumentWindow {
public:
	PluginWindow(juce::AudioProcessor& p)
		: DocumentWindow(p.getName() + getFormatSuffix(&p),
			juce::LookAndFeel::getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
			DocumentWindow::minimiseButton | DocumentWindow::closeButton), processor(p) {
		setSize(400, 300);

		if (auto* ui = createProcessorEditor(processor)) {
			setContentOwned(ui, true);
			setResizable(ui->isResizable(), false);
		}

		auto screenBounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true).toFloat();
		auto scaleFactor = juce::jmin((screenBounds.getWidth() - 50) / getWidth(), (screenBounds.getHeight() - 50) / getHeight());

		if (scaleFactor < 1.0f)
			setSize((int)(getWidth() * scaleFactor), (int)(getHeight() * scaleFactor));

		setTopLeftPosition(20, 20);

		setVisible(true);
	}

	~PluginWindow() override {
		clearContentComponent();
	}

	void moved() override {
	}

	void closeButtonPressed() override {
		juce::JUCEApplication::quit();
	}

	juce::AudioProcessor& processor;

	juce::BorderSize<int> getBorderThickness() override {
#if JUCE_IOS || JUCE_ANDROID
		const int border = 10;
		return { border, border, border, border };
#else
		return DocumentWindow::getBorderThickness();
#endif
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

		/*jassertfalse;
		return {};*/
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
