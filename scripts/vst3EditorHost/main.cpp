// A minimal VST3 host for the CI start test (CMakeLists.txt) and the user journeys in the plug-in
// (scripts/mdmm-journeys.sh --host vst3): loads one VST3, feeds it silent audio blocks from a thread paced like an
// audio device, opens its editor in a window and quits after the given seconds. Exit code 0 when the editor opened.
//
//   mdmmVst3EditorHost <plug-in.vst3> [seconds] [--background] [--state <file>]
//
// --state: the plug-in's state to start from, a file holding it as JUCE's MemoryBlock base64 text (what a JUCE
// standalone keeps as "filterState" in its settings), given to the plug-in before its editor opens.
//
// --background (macOS): the host never takes the focus nor comes to the front: an accessory app (no Dock icon,
// never activated), App Nap off, its window small (720 x 462: the editors' page zoomed to half), in the
// bottom-left corner of the main screen, behind every other window (hostMac.mm).

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include <atomic>
#include <iostream>

#if JUCE_MAC
void hostBecomeBackgroundApp();			// hostMac.mm
void hostSendWindowBack(void* _nsView);
#endif

namespace
{
	constexpr double g_sampleRate = 48000.0;
	constexpr int g_blockSize = 480;	// 10 ms

	class HostWindow final : public juce::DocumentWindow
	{
	public:
		HostWindow() : juce::DocumentWindow("mdmmVst3EditorHost", juce::Colours::black, juce::DocumentWindow::allButtons) {}
		void closeButtonPressed() override { juce::JUCEApplication::quit(); }
	};

	// The audio side as a host that is playing nothing runs it: a block of silence every 10 ms on a thread of its
	// own, on time (a late block is caught up, as an audio device would ask for it), whatever the message thread does.
	class AudioThread final : public juce::Thread
	{
	public:
		explicit AudioThread(juce::AudioPluginInstance& _plugin) : juce::Thread("mdmmVst3EditorHost audio"), m_plugin(_plugin)
		{
			m_buffer.setSize(std::max(1, std::max(m_plugin.getTotalNumInputChannels(), m_plugin.getTotalNumOutputChannels())), g_blockSize);
		}

		void run() override
		{
			const double blockMs = 1000.0 * g_blockSize / g_sampleRate;
			auto next = juce::Time::getMillisecondCounterHiRes();
			while(!threadShouldExit())
			{
				m_buffer.clear();
				juce::MidiBuffer midi;
				m_plugin.processBlock(m_buffer, midi);
				next += blockMs;
				const auto now = juce::Time::getMillisecondCounterHiRes();
				if(next > now)
					juce::Thread::sleep(static_cast<int>(next - now));
				else if(now - next > 1000.0)
					next = now;	// more than a second behind (the machine was asleep): start again from now
			}
		}

	private:
		juce::AudioPluginInstance& m_plugin;
		juce::AudioBuffer<float> m_buffer;
	};

	class App final : public juce::JUCEApplication, private juce::Timer
	{
	public:
		const juce::String getApplicationName() override { return "mdmmVst3EditorHost"; }
		const juce::String getApplicationVersion() override { return "1.0"; }

		void initialise(const juce::String& _commandLine) override
		{
			auto args = juce::StringArray::fromTokens(_commandLine, true);
			const bool background = args.contains("--background");
			args.removeString("--background");
			juce::File stateFile;
			if(const auto i = args.indexOf("--state"); i >= 0 && i + 1 < args.size())
			{
				stateFile = juce::File(args[i + 1].unquoted());
				args.removeRange(i, 2);
			}
			if(args.size() < 1)
				return fail("usage: mdmmVst3EditorHost <plug-in.vst3> [seconds] [--background] [--state <file>]");
			const auto path = args[0].unquoted();
			const auto seconds = args.size() > 1 ? args[1].getIntValue() : 60;
#if JUCE_MAC
			if(background)
				hostBecomeBackgroundApp();
#endif

			juce::VST3PluginFormat format;
			juce::OwnedArray<juce::PluginDescription> types;
			format.findAllTypesForFile(types, path);
			if(types.isEmpty())
				return fail("no VST3 plug-in in " + path);

			juce::String error;
			m_plugin = format.createInstanceFromDescription(*types[0], g_sampleRate, g_blockSize, error);
			if(!m_plugin)
				return fail("the plug-in did not load: " + error);
			m_plugin->enableAllBuses();
			m_plugin->prepareToPlay(g_sampleRate, g_blockSize);
			if(stateFile != juce::File())
			{
				juce::MemoryBlock state;
				if(!state.fromBase64Encoding(stateFile.loadFileAsString().trim()) || state.isEmpty())
					return fail("no state in " + stateFile.getFullPathName());
				m_plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
				std::cout << "state: " << state.getSize() << " bytes" << std::endl;
			}

			auto* editor = m_plugin->createEditorIfNeeded();
			if(editor == nullptr)
				return fail("the plug-in has no editor");
			m_window = std::make_unique<HostWindow>();
			m_window->setUsingNativeTitleBar(true);
			m_window->setContentOwned(editor, true);
			m_window->setResizable(true, false);
			if(background)
			{
				const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
				const auto area = display ? display->userArea : juce::Rectangle<int>(0, 0, 1440, 900);
				m_window->setBounds(area.getX(), area.getBottom() - 462, 720, 462);
			}
			else
			{
				m_window->setTopLeftPosition(20, 20);
			}
			m_window->setVisible(true);
#if JUCE_MAC
			if(background)
				if(auto* peer = m_window->getPeer())
					hostSendWindowBack(peer->getNativeHandle());
#endif
			std::cout << "editor open: " << editor->getWidth() << " x " << editor->getHeight() << std::endl;

			m_audio = std::make_unique<AudioThread>(*m_plugin);
			m_audio->startThread(juce::Thread::Priority::highest);

			m_end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(seconds) * 1000;
			startTimer(100);
		}

		void shutdown() override
		{
			stopTimer();
			if(m_audio)
				m_audio->stopThread(2000);
			m_audio.reset();
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
			if(juce::Time::getMillisecondCounter() >= m_end)
			{
				stopTimer();
				quit();
			}
		}

		std::unique_ptr<juce::AudioPluginInstance> m_plugin;
		std::unique_ptr<HostWindow> m_window;
		std::unique_ptr<AudioThread> m_audio;
		juce::uint32 m_end = 0;
	};
}

START_JUCE_APPLICATION(App)
