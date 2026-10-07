// mdScreenRecorder.h: one take of the window and the app's own sound with ScreenCaptureKit.
//
// The filter is the window's display with this app alone on it: no other app's window can cover the
// picture, and the sound is this app's output only (excludesCurrentProcessAudio off: the app is this
// process). The window's content is cut out with sourceRect. SCRecordingOutput writes the .mp4, so
// picture and sound share ScreenCaptureKit's one clock and nothing here touches a sample buffer.
// Compiled with ARC (mdmmPlugins.cmake); ScreenCaptureKit is weakly linked (macOS 10.13 deployment).

#include "mdScreenRecorder.h"

#include "juce_gui_basics/juce_gui_basics.h"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <unistd.h>

#include <AvailabilityMacros.h>

#if !__has_feature(objc_arc)
#error "mdScreenRecorder.mm is built with -fobjc-arc"
#endif

// SCRecordingOutput is in the macOS 15 SDK (Xcode 16). A build with an older SDK (CI on Xcode 15.4) gets the
// stand-in at the end of the file: the Record menu says recording is not available in this build.
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000

namespace
{
	constexpr int g_fps = mdJucePlugin::ScreenRecorder::fps;
	constexpr int g_sampleRate = mdJucePlugin::ScreenRecorder::sampleRate;
	constexpr int g_channels = mdJucePlugin::ScreenRecorder::channels;
	constexpr double g_finishTimeoutSeconds = 5.0;

	juce::String text(NSError* _error)
	{
		if(!_error)
			return {};
		return juce::String(juce::CharPointer_UTF8(_error.localizedDescription.UTF8String));
	}

	// Where the window's content is, in the global top-left coordinates ScreenCaptureKit's displays
	// use, and the display it is on.
	struct Placement
	{
		CGRect rect = CGRectZero;
		CGDirectDisplayID display = 0;
		bool valid = false;
	};

	Placement placementOf(juce::TopLevelWindow& _window)
	{
		Placement p;
		auto* peer = _window.getPeer();
		if(!peer)
			return p;
		NSView* view = (__bridge NSView*)peer->getNativeHandle();
		NSWindow* window = view.window;
		NSScreen* primary = NSScreen.screens.firstObject;
		if(!view || !window || !primary)
			return p;
		const NSRect onScreen = [window convertRectToScreen:[view convertRect:view.bounds toView:nil]];
		p.rect = CGRectMake(onScreen.origin.x, primary.frame.size.height - NSMaxY(onScreen), onScreen.size.width, onScreen.size.height);
		NSNumber* number = window.screen.deviceDescription[@"NSScreenNumber"];
		p.display = number ? number.unsignedIntValue : 0;
		p.valid = p.rect.size.width >= 16 && p.rect.size.height >= 16;
		return p;
	}

	int evenPixels(const CGFloat _points, const CGFloat _scale)
	{
		return static_cast<int>(_points * _scale) & ~1;	// h264 wants even sizes
	}
}

namespace mdJucePlugin
{
	struct ScreenRecorder::Impl
	{
		State state = State::Idle;
		std::function<void(const Started&)> onStarted;
		std::function<void(const Ended&)> onEnded;
		juce::File file;
		juce::String problem;
		bool stopAsked = false;
		bool wroteFrames = false;
		double firstFrameMs = 0;
		int take = 0;		// which take a delayed check belongs to
		id stream = nil;	// SCStream (typed id: the class is macOS 12.3+)
		id output = nil;	// SCRecordingOutput
		id delegate = nil;	// MdmmRecordingDelegate

		~Impl();

		void started(const Started& _s)
		{
			if(!_s.recording)
			{
				state = State::Idle;
				stream = nil;
				output = nil;
			}
			else
			{
				state = State::Recording;
			}
			if(onStarted)
				onStarted(_s);
			if(_s.recording && stopAsked)
				stopCapture();
		}

		void stopCapture();
		void finishAfterTimeout();

		void firstFrame()
		{
			wroteFrames = true;
			firstFrameMs = juce::Time::getMillisecondCounterHiRes();
		}

		void streamStopped(const juce::String& _problem)
		{
			if(state == State::Idle)
				return;
			if(problem.isEmpty())
				problem = _problem;
			if(state != State::Stopping)
			{
				state = State::Stopping;
				finishAfterTimeout();
			}
		}

		void finish()
		{
			if(state == State::Idle)
				return;
			Ended e;
			e.file = file;
			e.problem = problem;
			e.early = !stopAsked;
			if(e.early && e.problem.isEmpty())
				e.problem = "macOS ended the recording by itself.";
			e.seconds = wroteFrames ? (juce::Time::getMillisecondCounterHiRes() - firstFrameMs) / 1000.0 : 0.0;
			state = State::Idle;
			stream = nil;
			output = nil;
			++take;
			const auto ended = onEnded;
			if(ended)
				ended(e);
		}
	};
}

API_AVAILABLE(macos(15.0))
@interface MdmmRecordingDelegate : NSObject <SCStreamDelegate, SCRecordingOutputDelegate>
// The recorder while it lives; read and cleared on the main thread only.
@property(nonatomic, assign) mdJucePlugin::ScreenRecorder::Impl* owner;
@end

@implementation MdmmRecordingDelegate

- (void)onMain:(void (^)(mdJucePlugin::ScreenRecorder::Impl&))_block
{
	dispatch_async(dispatch_get_main_queue(), ^{
		if(self.owner)
			_block(*self.owner);
	});
}

- (void)recordingOutputDidStartRecording:(SCRecordingOutput*)_output
{
	[self onMain:^(mdJucePlugin::ScreenRecorder::Impl& _impl) { _impl.firstFrame(); }];
}

- (void)recordingOutput:(SCRecordingOutput*)_output didFailWithError:(NSError*)_error
{
	const auto problem = text(_error);
	[self onMain:^(mdJucePlugin::ScreenRecorder::Impl& _impl) { _impl.streamStopped("The recording failed: " + problem); }];
}

- (void)recordingOutputDidFinishRecording:(SCRecordingOutput*)_output
{
	[self onMain:^(mdJucePlugin::ScreenRecorder::Impl& _impl) { _impl.finish(); }];
}

- (void)stream:(SCStream*)_stream didStopWithError:(NSError*)_error
{
	// ScreenCaptureKit sometimes ends a take by itself ("Unknown stream error"); the recording
	// output still closes the file with what it has.
	const auto problem = text(_error);
	[self onMain:^(mdJucePlugin::ScreenRecorder::Impl& _impl) { _impl.streamStopped("macOS stopped the recording: " + problem); }];
}

@end

namespace mdJucePlugin
{
	ScreenRecorder::Impl::~Impl()
	{
		if(@available(macOS 15.0, *))
		{
			if(auto* d = (MdmmRecordingDelegate*)delegate)
				d.owner = nullptr;
			if(SCStream* s = stream)
				[s stopCaptureWithCompletionHandler:^(NSError*) {}];
		}
	}

	void ScreenRecorder::Impl::stopCapture()
	{
		if(@available(macOS 15.0, *))
		{
			state = State::Stopping;
			if(SCStream* s = stream)
				[s stopCaptureWithCompletionHandler:^(NSError*) {}];
			finishAfterTimeout();
		}
	}

	// recordingOutputDidFinishRecording ends the take; a take that never gets it ends here.
	void ScreenRecorder::Impl::finishAfterTimeout()
	{
		if(@available(macOS 15.0, *))
		{
			MdmmRecordingDelegate* d = delegate;
			const int t = take;
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(g_finishTimeoutSeconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
				auto* impl = d.owner;
				if(!impl || impl->take != t || impl->state != State::Stopping)
					return;
				if(impl->problem.isEmpty())
					impl->problem = "The recording did not finish within 5 seconds of stopping.";
				impl->finish();
			});
		}
	}

	API_AVAILABLE(macos(15.0))
	static void startWithContent(ScreenRecorder::Impl& _impl, SCShareableContent* _content, const Placement& _where)
	{
		auto fail = [&_impl](const juce::String& _problem)
		{
			ScreenRecorder::Started s;
			s.problem = _problem;
			_impl.started(s);
		};

		SCRunningApplication* app = nil;
		for(SCRunningApplication* a in _content.applications)
		{
			if(a.processID == getpid())
			{
				app = a;
				break;
			}
		}
		if(!app)
			return fail("The editor's window is not on screen.");

		const CGPoint centre = CGPointMake(CGRectGetMidX(_where.rect), CGRectGetMidY(_where.rect));
		SCDisplay* display = nil;
		for(SCDisplay* d in _content.displays)
		{
			if(d.displayID == _where.display)
				display = d;
		}
		for(SCDisplay* d in _content.displays)
		{
			if(!display && CGRectContainsPoint(d.frame, centre))
				display = d;
		}
		if(!display)
			return fail("The editor's window is on no display.");

		SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display includingApplications:@[app] exceptingWindows:@[]];
		const CGFloat scale = filter.pointPixelScale > 0 ? filter.pointPixelScale : 1;
		const CGRect bounds = CGRectMake(0, 0, display.frame.size.width, display.frame.size.height);
		const CGRect rect = CGRectIntersection(CGRectOffset(_where.rect, -display.frame.origin.x, -display.frame.origin.y), bounds);
		if(CGRectIsEmpty(rect))
			return fail("The editor's window is off screen.");

		SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
		config.sourceRect = rect;
		config.width = static_cast<size_t>(evenPixels(rect.size.width, scale));
		config.height = static_cast<size_t>(evenPixels(rect.size.height, scale));
		config.scalesToFit = NO;
		config.minimumFrameInterval = CMTimeMake(1, g_fps);
		config.queueDepth = 8;
		config.showsCursor = YES;	// the reels show the pointer (marketing/FOOTAGE-REQUESTS.md)
		config.capturesAudio = YES;
		config.excludesCurrentProcessAudio = NO;	// this process is the app whose sound we want
		config.sampleRate = g_sampleRate;
		config.channelCount = g_channels;
		config.colorSpaceName = kCGColorSpaceSRGB;

		SCRecordingOutputConfiguration* rc = [[SCRecordingOutputConfiguration alloc] init];
		rc.outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:_impl.file.getFullPathName().toRawUTF8()]];
		rc.outputFileType = AVFileTypeMPEG4;
		rc.videoCodecType = AVVideoCodecTypeH264;

		MdmmRecordingDelegate* delegate = _impl.delegate;
		SCRecordingOutput* output = [[SCRecordingOutput alloc] initWithConfiguration:rc delegate:delegate];
		SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:delegate];
		NSError* error = nil;
		if(![stream addRecordingOutput:output error:&error])
			return fail("Cannot record to " + _impl.file.getFullPathName() + ": " + text(error));
		_impl.stream = stream;
		_impl.output = output;

		ScreenRecorder::Picture picture{static_cast<int>(config.width), static_cast<int>(config.height), g_fps};
		[stream startCaptureWithCompletionHandler:^(NSError* _error) {
			const auto problem = text(_error);
			dispatch_async(dispatch_get_main_queue(), ^{
				auto* impl = delegate.owner;
				if(!impl)
					return;
				ScreenRecorder::Started s;
				s.recording = problem.isEmpty();
				s.notAllowed = !s.recording && !CGPreflightScreenCaptureAccess();
				s.problem = problem.isEmpty() ? juce::String() : "Cannot start the recording: " + problem;
				s.picture = picture;
				impl->started(s);
			});
		}];
	}

	juce::String ScreenRecorder::unavailableReason()
	{
		if(@available(macOS 15.0, *))
			return {};
		return "Recording needs macOS 15 or later.";
	}

	bool ScreenRecorder::screenRecordingAllowed()
	{
		if(@available(macOS 10.15, *))
			return CGPreflightScreenCaptureAccess();
		return true;
	}

	void ScreenRecorder::askForScreenRecording()
	{
		if(@available(macOS 10.15, *))
			CGRequestScreenCaptureAccess();
	}

	void ScreenRecorder::openScreenRecordingSettings()
	{
		NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"];
		[[NSWorkspace sharedWorkspace] openURL:url];
	}

	ScreenRecorder::ScreenRecorder() : m_impl(std::make_unique<Impl>())
	{
		if(@available(macOS 15.0, *))
		{
			MdmmRecordingDelegate* d = [[MdmmRecordingDelegate alloc] init];
			d.owner = m_impl.get();
			m_impl->delegate = d;
		}
	}

	ScreenRecorder::~ScreenRecorder() = default;

	ScreenRecorder::State ScreenRecorder::state() const
	{
		return m_impl->state;
	}

	void ScreenRecorder::start(juce::TopLevelWindow& _window, const juce::File& _file,
		std::function<void(const Started&)> _onStarted, std::function<void(const Ended&)> _onEnded)
	{
		auto& impl = *m_impl;
		if(impl.state != State::Idle)
			return;
		impl.onStarted = std::move(_onStarted);
		impl.onEnded = std::move(_onEnded);
		impl.file = _file;
		impl.problem = {};
		impl.stopAsked = false;
		impl.wroteFrames = false;

		auto refuse = [&impl](const juce::String& _problem, const bool _notAllowed)
		{
			Started s;
			s.problem = _problem;
			s.notAllowed = _notAllowed;
			impl.started(s);
		};

		if(@available(macOS 15.0, *))
		{
			if(!CGPreflightScreenCaptureAccess())
				return refuse("Screen Recording is not allowed for this app.", true);
			const auto where = placementOf(_window);
			if(!where.valid)
				return refuse("The editor's window is not on screen.", false);

			impl.state = State::Starting;
			MdmmRecordingDelegate* delegate = impl.delegate;
			[SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
				completionHandler:^(SCShareableContent* _content, NSError* _error) {
					const auto problem = text(_error);
					dispatch_async(dispatch_get_main_queue(), ^{
						auto* owner = delegate.owner;
						if(!owner)
							return;
						if(!_content)
						{
							Started s;
							s.notAllowed = !CGPreflightScreenCaptureAccess();
							s.problem = "Cannot see the screen: " + problem;
							owner->started(s);
							return;
						}
						startWithContent(*owner, _content, where);
					});
				}];
			return;
		}
		refuse(unavailableReason(), false);
	}

	void ScreenRecorder::stop()
	{
		auto& impl = *m_impl;
		if(impl.state == State::Idle || impl.state == State::Stopping)
			return;
		impl.stopAsked = true;
		if(impl.state == State::Recording)
			impl.stopCapture();
		// Starting: started() stops it as soon as it runs
	}

	void ScreenRecorder::stopAndWait(const double _seconds)
	{
		stop();
		NSDate* end = [NSDate dateWithTimeIntervalSinceNow:_seconds];
		while(m_impl->state != State::Idle && [end timeIntervalSinceNow] > 0)
			[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
	}
}

#else	// built with an SDK older than macOS 15: no recording

namespace mdJucePlugin
{
	struct ScreenRecorder::Impl
	{
	};

	juce::String ScreenRecorder::unavailableReason() { return "Recording is not in this build (it needs macOS 15 or later and a build made with Xcode 16)."; }
	bool ScreenRecorder::screenRecordingAllowed() { return false; }
	void ScreenRecorder::askForScreenRecording() {}
	void ScreenRecorder::openScreenRecordingSettings() {}
	ScreenRecorder::ScreenRecorder() : m_impl(std::make_unique<Impl>()) {}
	ScreenRecorder::~ScreenRecorder() = default;
	ScreenRecorder::State ScreenRecorder::state() const { return State::Idle; }

	void ScreenRecorder::start(juce::TopLevelWindow&, const juce::File&,
		std::function<void(const Started&)> _onStarted, std::function<void(const Ended&)>)
	{
		Started s;
		s.problem = unavailableReason();
		if(_onStarted)
			_onStarted(s);
	}

	void ScreenRecorder::stop() {}
	void ScreenRecorder::stopAndWait(double) {}
}

#endif
