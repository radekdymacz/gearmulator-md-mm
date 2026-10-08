// The host's --background on macOS (main.cpp): it stays out of the way of the person at the Mac.

#import <AppKit/AppKit.h>

// Before any window: an app that cannot be activated, so finishing its launch does not bring it to the front (a
// process started from a shell is activated then), and that App Nap does not slow down.
void hostBecomeBackgroundApp()
{
	[NSApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
	static id activity = nil;
	if(activity == nil)
	{
		activity = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
			reason:@"editor journeys in the background"];
		[activity retain];
	}
}

// Once the launch is over: an accessory app (no Dock icon, no menu bar, not activated by itself; its windows show,
// unlike a prohibited app's) with the window behind every other window: on screen, so the plug-in's page keeps
// drawing (with WebKit's occlusion detection off, mdBackgroundRun.mm, even while it is covered).
void hostSendWindowBack(void* _nsView)
{
	NSWindow* window = [[(NSView*)_nsView window] retain];
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
		[window orderBack:nil];
		[window release];
	});
}
