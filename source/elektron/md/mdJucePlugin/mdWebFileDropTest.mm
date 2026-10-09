// The web view's drag routing (mdWebFileDrop.mm), on plain views: no window, no WebKit, no pasteboard server. A view
// that takes file drags stands for JUCE's; a view with dragging methods of its own stands for WKWebView; a drag is an
// object with the two things the routing reads (its pasteboard: a file URL or not; its source). A drag of files from
// outside the page goes to the window's view, every method of it; a drag that starts in the page, or carries no file
// URL, or has no window view above it stays the web view's own; only the one view's class changes; a view key-value
// observing made a class for is left alone, and one observed after it was extended still routes.
#include "mdWebFileDrop.h"

#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstring>
#include <objc/runtime.h>
#include <string>

namespace
{
	int g_failures = 0;
	std::string g_calls;	// what each view was told, in order

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	void called(const char* _what)
	{
		g_calls += (g_calls.empty() ? "" : " ") + std::string(_what);
	}
}

// JUCE's view: takes file drags, says what it was told
@interface MdmmTestWindowView : NSView
@end
@implementation MdmmTestWindowView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)_info { called("window:entered"); return NSDragOperationGeneric; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)_info { called("window:updated"); return NSDragOperationGeneric; }
- (void)draggingExited:(id<NSDraggingInfo>)_info { called("window:exited"); }
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)_info { called("window:prepare"); return YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)_info { called("window:perform"); return YES; }
- (void)concludeDragOperation:(id<NSDraggingInfo>)_info { called("window:conclude"); }
- (void)draggingEnded:(id<NSDraggingInfo>)_info { called("window:ended"); }
@end

// WKWebView's part: dragging methods of its own (no draggingEnded:, as a class may lack one)
@interface MdmmTestWebView : NSView
@end
@implementation MdmmTestWebView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)_info { called("web:entered"); return NSDragOperationCopy; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)_info { called("web:updated"); return NSDragOperationCopy; }
- (void)draggingExited:(id<NSDraggingInfo>)_info { called("web:exited"); }
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)_info { called("web:prepare"); return YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)_info { called("web:perform"); return YES; }
- (void)concludeDragOperation:(id<NSDraggingInfo>)_info { called("web:conclude"); }
@end

// a pasteboard that holds a file URL or not
@interface MdmmTestPasteboard : NSObject
@property BOOL files;
@end
@implementation MdmmTestPasteboard
- (BOOL)canReadObjectForClasses:(NSArray*)_classes options:(NSDictionary*)_options
{
	return self.files && [_classes containsObject:[NSURL class]] && [_options[NSPasteboardURLReadingFileURLsOnlyKey] boolValue];
}
@end

// a drag: its pasteboard and its source (nil: another application, the Finder)
@interface MdmmTestDrag : NSObject
@property(retain) MdmmTestPasteboard* pasteboard;
@property(assign) id source;
@end
@implementation MdmmTestDrag
- (id)draggingPasteboard { return self.pasteboard; }
- (id)draggingSource { return self.source; }
@end

@interface MdmmTestObserver : NSObject
@end
@implementation MdmmTestObserver
- (void)observeValueForKeyPath:(NSString*)_path ofObject:(id)_object change:(NSDictionary*)_change context:(void*)_context {}
@end

namespace
{
	MdmmTestDrag* drag(const bool _files, const id _source)
	{
		MdmmTestDrag* d = [[MdmmTestDrag alloc] init];
		d.pasteboard = [[[MdmmTestPasteboard alloc] init] autorelease];
		d.pasteboard.files = _files;
		d.source = _source;
		return [d autorelease];
	}

	// every dragging method, in AppKit's order for a drop, then draggingEnded: when the view has it (AppKit asks first);
	// what the views were told
	std::string dropOn(NSView* _web, MdmmTestDrag* _drag)
	{
		g_calls.clear();
		id<NSDraggingInfo> info = (id<NSDraggingInfo>)_drag;
		id web = _web;
		[web draggingEntered:info];
		[web draggingUpdated:info];
		[web prepareForDragOperation:info];
		[web performDragOperation:info];
		[web concludeDragOperation:info];
		if([web respondsToSelector:@selector(draggingEnded:)])
			[web draggingEnded:info];
		return g_calls;
	}

	const char* g_toWindow = "window:entered window:updated window:prepare window:perform window:conclude window:ended";
	const char* g_toWeb = "web:entered web:updated web:prepare web:perform web:conclude";
}

int main()
{
	@autoreleasepool
	{
		std::printf("mdWebFileDropTest\n");
		MdmmTestWindowView* window = [[MdmmTestWindowView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
		[window registerForDraggedTypes:@[@"public.file-url"]];
		NSView* middle = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];	// a view between them takes nothing
		[window addSubview:middle];
		MdmmTestWebView* web = [[MdmmTestWebView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
		[middle addSubview:web];
		NSView* inPage = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
		[web addSubview:inPage];

		check(dropOn(web, drag(true, nil)) == g_toWeb, "before: a file from the Finder is the web view's");
		check(mdJucePlugin::webFileDrop::install(web) == -1 && object_getClass(web) == [MdmmTestWebView class], "install: a view that is not a WKWebView is left alone");
		check(mdJucePlugin::webFileDrop::extend(web) == 1, "extended");
		const char* name = class_getName(object_getClass(web));
		check(std::strncmp(name, "MdmmFileDrop_", 13) == 0 && [web isKindOfClass:[MdmmTestWebView class]] && class_getSuperclass(object_getClass(web)) == [MdmmTestWebView class],
			std::string("its class is a subclass of its own made for it: ") + name);
		check(mdJucePlugin::webFileDrop::extend(web) == 1 && std::strcmp(class_getName(object_getClass(web)), name) == 0, "extended again: nothing changes");

		check(dropOn(web, drag(true, nil)) == g_toWindow, "files from the Finder: every method goes to the window's view (" + dropOn(web, drag(true, nil)) + ")");
		g_calls.clear();
		[(id)web draggingExited:(id<NSDraggingInfo>)drag(true, nil)];
		check(g_calls == "window:exited", "and their leaving");
		check(dropOn(web, drag(true, window)) == g_toWindow, "files from another view of the app (a DAW's browser): the window's");
		check(dropOn(web, drag(false, nil)) == g_toWeb, "a drag without a file URL (text from another app): the web view's");
		check(dropOn(web, drag(false, web)) == g_toWeb, "the page's own drag (a kit to a slot, its source the web view): the web view's, draggingEnded: it lacks not called");
		check(dropOn(web, drag(true, inPage)) == g_toWeb, "a drag that starts in a view inside the page, with a file URL: the web view's");
		const NSDragOperation op = [(id)web draggingEntered:(id<NSDraggingInfo>)drag(true, nil)];
		const NSDragOperation own = [(id)web draggingEntered:(id<NSDraggingInfo>)drag(false, nil)];
		check(op == NSDragOperationGeneric && own == NSDragOperationCopy, "each answers with its own operation");

		[window unregisterDraggedTypes];
		check(dropOn(web, drag(true, nil)) == g_toWeb, "no view above takes file drags: the web view's, as before");
		[window registerForDraggedTypes:@[@"public.file-url"]];

		MdmmTestWebView* other = [[MdmmTestWebView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
		[window addSubview:other];
		check(object_getClass(other) == [MdmmTestWebView class] && dropOn(other, drag(true, nil)) == g_toWeb, "another view of the same class is not touched");
		check(mdJucePlugin::webFileDrop::extend(other) == 1 && object_getClass(other) == object_getClass(web), "extended, it shares the subclass");

		// key-value observing: a view observed before is left alone; one observed after still routes
		MdmmTestObserver* observer = [[MdmmTestObserver alloc] init];
		MdmmTestWebView* observed = [[MdmmTestWebView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
		[window addSubview:observed];
		[observed addObserver:observer forKeyPath:@"frame" options:0 context:nullptr];
		const char* kvo = class_getName(object_getClass(observed));
		check(mdJucePlugin::webFileDrop::extend(observed) == -1 && std::strcmp(class_getName(object_getClass(observed)), kvo) == 0,
			std::string("a view observed before (") + kvo + "): left alone");
		[observed removeObserver:observer forKeyPath:@"frame"];
		[web addObserver:observer forKeyPath:@"frame" options:0 context:nullptr];
		check(dropOn(web, drag(true, nil)) == g_toWindow && dropOn(web, drag(false, nil)) == g_toWeb,
			std::string("observed after it was extended (") + class_getName(object_getClass(web)) + "): still routes both ways");
		[web removeObserver:observer forKeyPath:@"frame"];
		check(std::strcmp(class_getName(object_getClass(web)), name) == 0, "the observer gone: its class is ours again");

		[observer release];
		[observed release];
		[other release];
		[inPage release];
		[web release];
		[middle release];
		[window release];
	}
	std::printf(g_failures ? "%d failure(s)\n" : "mdWebFileDropTest: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
