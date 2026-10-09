// A minimal VST3 host for the CI start test (CMakeLists.txt) and the user journeys in the plug-in
// (scripts/mdmm-journeys.sh --host vst3): loads one VST3, feeds it silent audio blocks from a thread paced like an
// audio device, opens its editor in a window and quits after the given seconds. Exit code 0 when the editor opened.
//
//   mdmmVst3EditorHost <plug-in.vst3> [seconds] [--background] [--state <file>] [--reopen <seconds>]
//
// --state: the plug-in's state to start from, a file holding it as JUCE's MemoryBlock base64 text (what a JUCE
// standalone keeps as "filterState" in its settings), given to the plug-in before its editor opens.
//
// --reopen: after that many seconds the editor's window is closed (its editor deleted: the plug-in's view is removed,
// as a DAW closing the plug-in's window does) and a second later opened again with a new editor (B-029: on Windows the
// page was blank after that; the start test checks it comes back, scripts/windows/smoke_mdmm.ps1).
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

#if JUCE_LINUX || JUCE_BSD
#include <dlfcn.h>
#endif

namespace
{
#if JUCE_LINUX || JUCE_BSD
	// Xlib before 1.8 (Ubuntu 22.04 ships 1.7.5) is thread-safe only when XInitThreads is the process's first Xlib
	// call; 1.8 makes it on its own. JUCE makes it when the host opens its first window, but the plug-in's own copy
	// of JUCE opens its display connection while it loads, before that: the editor's threads then lost the reply
	// sequence ("_XReply: Assertion `!xcb_xlib_threads_sequence_lost' failed", the start test's VST3 abort on 22.04).
	// So the host makes the call before main. libX11 is loaded as JUCE loads it (dlopen, no link-time dependency).
	const bool g_xlibThreads = []
	{
		if(auto* x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_GLOBAL))
			if(auto* initThreads = reinterpret_cast<int (*)()>(dlsym(x11, "XInitThreads")))
				return initThreads() != 0;
		return false;
	}();
#endif

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
			int reopenSeconds = 0;
			if(const auto i = args.indexOf("--reopen"); i >= 0 && i + 1 < args.size())
			{
				reopenSeconds = args[i + 1].getIntValue();
				args.removeRange(i, 2);
			}
			if(args.size() < 1)
				return fail("usage: mdmmVst3EditorHost <plug-in.vst3> [seconds] [--background] [--state <file>] [--reopen <seconds>]");
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

			m_background = background;
			if(!openEditor())
				return fail("the plug-in has no editor");

			m_audio = std::make_unique<AudioThread>(*m_plugin);
			m_audio->startThread(juce::Thread::Priority::highest);

			const auto now = juce::Time::getMillisecondCounter();
			m_end = now + static_cast<juce::uint32>(seconds) * 1000;
			if(reopenSeconds > 0)
				m_closeAt = now + static_cast<juce::uint32>(reopenSeconds) * 1000;
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

		// The plug-in's editor in a window of its own (the window owns the editor).
		bool openEditor()
		{
			auto* editor = m_plugin->createEditorIfNeeded();
			if(editor == nullptr)
				return false;
			m_window = std::make_unique<HostWindow>();
			m_window->setUsingNativeTitleBar(true);
			m_window->setContentOwned(editor, true);
			m_window->setResizable(true, false);
			if(m_background)
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
			if(m_background)
				if(auto* peer = m_window->getPeer())
					hostSendWindowBack(peer->getNativeHandle());
#endif
			std::cout << "editor open: " << editor->getWidth() << " x " << editor->getHeight() << std::endl;
			return true;
		}

		void timerCallback() override
		{
			const auto now = juce::Time::getMillisecondCounter();
			// --reopen: the window and its editor go (the plug-in's view is removed), and a second later a new one comes
			if(m_closeAt != 0 && now >= m_closeAt)
			{
				m_closeAt = 0;
				m_window.reset();
				std::cout << "editor closed" << std::endl;
				m_openAt = now + 1000;
			}
			if(m_openAt != 0 && now >= m_openAt)
			{
				m_openAt = 0;
				if(!openEditor())
				{
					stopTimer();
					return fail("the plug-in has no editor the second time");
				}
			}
			if(now >= m_end)
			{
				stopTimer();
				quit();
			}
		}

		std::unique_ptr<juce::AudioPluginInstance> m_plugin;
		std::unique_ptr<HostWindow> m_window;
		std::unique_ptr<AudioThread> m_audio;
		bool m_background = false;
		juce::uint32 m_end = 0;
		juce::uint32 m_closeAt = 0;	// --reopen: when the editor's window goes, and when a new one comes
		juce::uint32 m_openAt = 0;
	};
}

START_JUCE_APPLICATION(App)
