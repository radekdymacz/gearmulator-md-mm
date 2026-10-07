#pragma once

// One take of a window's content and this process's own sound to an .mp4: h264 at 60 fps in the
// display's pixels (Retina: 2x), AAC 48 kHz stereo, through ScreenCaptureKit's SCRecordingOutput
// (macOS 15 and later). The standalone apps only: mdmmPlugins.cmake adds this file and
// ScreenCaptureKit to the _Standalone targets alone, the VST3 and AU never link it.
//
// Every callback comes on the message thread. Destroying the recorder stops a take and calls
// nothing any more.

#include <functional>
#include <memory>

#include "juce_gui_basics/juce_gui_basics.h"

namespace mdJucePlugin
{
	class ScreenRecorder
	{
	public:
		enum class State { Idle, Starting, Recording, Stopping };

		// What a take is (marketing/FOOTAGE-REQUESTS.md: 60 fps, full Retina pixels, 48 kHz stereo).
		static constexpr int fps = 60;
		static constexpr int sampleRate = 48000;
		static constexpr int channels = 2;

		struct Picture
		{
			int width = 0;	// pixels
			int height = 0;
			int fps = 0;
		};

		struct Started
		{
			bool recording = false;
			bool notAllowed = false;	// Screen Recording is not allowed for the app (System Settings)
			juce::String problem;		// when not recording
			Picture picture;
		};

		struct Ended
		{
			juce::File file;		// the movie; missing when the take never wrote a frame
			juce::String problem;	// empty: it stopped as asked
			bool early = false;		// macOS ended the take before Stop; what was written is kept
			double seconds = 0;		// from the first frame to the end
		};

		// Empty when this Mac can record, otherwise why not (the menu shows it).
		static juce::String unavailableReason();
		static bool screenRecordingAllowed();
		// The system's prompt (the first time only) and, when it was answered before, nothing.
		static void askForScreenRecording();
		// System Settings > Privacy & Security > Screen & System Audio Recording.
		static void openScreenRecordingSettings();

		ScreenRecorder();
		~ScreenRecorder();

		ScreenRecorder(const ScreenRecorder&) = delete;
		ScreenRecorder& operator=(const ScreenRecorder&) = delete;

		// Records _window's content (not its title bar) and the app's sound into _file (.mp4)
		// until stop(). _onStarted says whether it runs; _onEnded comes once for every take
		// that started.
		void start(juce::TopLevelWindow& _window, const juce::File& _file,
			std::function<void(const Started&)> _onStarted, std::function<void(const Ended&)> _onEnded);
		void stop();
		// Stops a take and waits (the main run loop runs) until its file is closed, at most
		// _seconds: for quitting, when no later callback would get to run.
		void stopAndWait(double _seconds);

		State state() const;

		struct Impl;	// mdScreenRecorder.mm's (its ScreenCaptureKit delegate names it)

	private:
		std::unique_ptr<Impl> m_impl;
	};
}
