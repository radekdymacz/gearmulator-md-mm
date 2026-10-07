// A minimal VST3 host for the CI start test (CMakeLists.txt): loads one VST3, feeds it silent audio blocks from a
// timer, opens its editor in a window and quits after the given seconds. Exit code 0 when the editor opened.

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include <iostream>

namespace
{
	class HostWindow final : public juce::DocumentWindow
	{
	public:
		HostWindow() : juce::DocumentWindow("mdmmVst3EditorHost", juce::Colours::black, juce::DocumentWindow::allButtons) {}
		void closeButtonPressed() override { juce::JUCEApplication::quit(); }
	};

	class App final : public juce::JUCEApplication, private juce::Timer
	{
	public:
		const juce::String getApplicationName() override { return "mdmmVst3EditorHost"; }
		const juce::String getApplicationVersion() override { return "1.0"; }

		void initialise(const juce::String& _commandLine) override
		{
			const auto args = juce::StringArray::fromTokens(_commandLine, true);
			if(args.size() < 1)
				return fail("usage: mdmmVst3EditorHost <plug-in.vst3> [seconds]");
			const auto path = args[0].unquoted();
			const auto seconds = args.size() > 1 ? args[1].getIntValue() : 60;

			juce::VST3PluginFormat format;
			juce::OwnedArray<juce::PluginDescription> types;
			format.findAllTypesForFile(types, path);
			if(types.isEmpty())
				return fail("no VST3 plug-in in " + path);

			juce::String error;
			m_plugin = format.createInstanceFromDescription(*types[0], 48000.0, 512, error);
			if(!m_plugin)
				return fail("the plug-in did not load: " + error);
			m_plugin->enableAllBuses();
			m_plugin->prepareToPlay(48000.0, 512);
			m_buffer.setSize(std::max(m_plugin->getTotalNumInputChannels(), m_plugin->getTotalNumOutputChannels()), 512);

			auto* editor = m_plugin->createEditorIfNeeded();
			if(editor == nullptr)
				return fail("the plug-in has no editor");
			m_window = std::make_unique<HostWindow>();
			m_window->setUsingNativeTitleBar(true);
			m_window->setContentOwned(editor, true);
			m_window->setResizable(true, false);
			m_window->setTopLeftPosition(20, 20);
			m_window->setVisible(true);
			std::cout << "editor open: " << editor->getWidth() << " x " << editor->getHeight() << std::endl;

			m_end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(seconds) * 1000;
			startTimer(10);
		}

		void shutdown() override
		{
			stopTimer();
			m_window.reset();
			if(m_plugin)
				m_plugin->releaseResources();
			m_plugin.reset();
		}

	private:
		void fail(const juce::String& _why)
		{
			std::cerr << _why << std::endl;
			setApplicationReturnValue(1);
			quit();
		}

		void timerCallback() override
		{
			// about 10 ms of silence a tick: the device runs, as in a host that is playing nothing
			m_buffer.clear();
			juce::MidiBuffer midi;
			m_buffer.setSize(m_buffer.getNumChannels(), 480, false, false, true);
			m_plugin->processBlock(m_buffer, midi);
			if(juce::Time::getMillisecondCounter() >= m_end)
			{
				stopTimer();
				quit();
			}
		}

		std::unique_ptr<juce::AudioPluginInstance> m_plugin;
		std::unique_ptr<HostWindow> m_window;
		juce::AudioBuffer<float> m_buffer;
		juce::uint32 m_end = 0;
	};
}

START_JUCE_APPLICATION(App)
