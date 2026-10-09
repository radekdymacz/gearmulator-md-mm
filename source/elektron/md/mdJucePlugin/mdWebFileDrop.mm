// Files dragged from the Finder (or from a DAW's own browser: not tried) onto the editor's page go to JUCE's window, not
// to the web view (macOS; mdWebFileDrop.h, FOUNDATION.md "Files dropped on the window"). The web view would take them
// itself: WKWebView answers AppKit's drag hit test with itself for any drag over it, whatever types it registered, and
// then opens a dropped file as a page (the page host refuses that navigation). So taking its drag types away, as tried in
// September 2026 (0b0762618), left a dropped file with nobody: the web view stayed the destination and no longer knew
// the types.
//
// Here the web view stays the destination, and this one web view's class is given a subclass (made at run time, as
// key-value observing does) whose dragging methods hand a drag of files from outside the page to the view JUCE draws
// the window in (its peer's view: the first one above the web view that takes file drags). JUCE then finds the
// FileDragAndDropTarget under the pointer: the page host's web view component (mdWebPageHost.cpp), which tells the
// editor. Every other drag stays WebKit's: the page's own HTML drags (the library's kits and patterns, the song's rows)
// start in the web view and carry no file URL. No JUCE and no WebKit here (WKWebView is looked up by name), so
// mdWebFileDropTest runs it on plain views.

#include "mdWebFileDrop.h"

#import <AppKit/AppKit.h>

#include <objc/message.h>
#include <objc/runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>

namespace mdJucePlugin::webFileDrop
{
	namespace
	{
		constexpr const char* g_prefix = "MdmmFileDrop_";

		// The subclasses made, by the class they extend: disposed at exit before JUCE disposes its own web view class
		// (a static made later goes first), as JUCE's ObjCClass disposes its classes: not one key-value observing made
		// a class of its own for.
		struct Made
		{
			std::map<Class, Class> classes;

			~Made()
			{
				for(const auto& [base, made] : classes)
					if(objc_getClass((std::string("NSKVONotifying_") + class_getName(made)).c_str()) == nil)
						objc_disposeClassPair(made);
			}
		};

		Made& made()
		{
			static Made m;
			return m;
		}

		bool ours(Class _c)
		{
			return std::strncmp(class_getName(_c), g_prefix, std::strlen(g_prefix)) == 0;
		}

		// The class our subclass extends, for the instance _self (one of ours, perhaps observed since: walked up).
		Class baseOf(id _self)
		{
			for(Class c = object_getClass(_self); c != nil; c = class_getSuperclass(c))
				if(ours(c))
					return class_getSuperclass(c);
			return nil;
		}

		// The view JUCE draws the window in, when this drag is the window's: files (a file URL on the pasteboard) from
		// anywhere but the page itself. nil: the base class's (WebKit's), as before.
		NSView* windowFor(NSView* _web, id<NSDraggingInfo> _info)
		{
			const id source = [_info draggingSource];
			if(source != nil && [source isKindOfClass:[NSView class]] && ((NSView*)source == _web || [(NSView*)source isDescendantOf:_web]))
				return nil;
			NSPasteboard* pasteboard = [_info draggingPasteboard];
			if(![pasteboard canReadObjectForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}])
				return nil;
			// NSPasteboardTypeFileURL, which JUCE's view registers (the name is macOS 10.13's; the build targets older)
			NSString* fileUrl = @"public.file-url";
			for(NSView* v = [_web superview]; v != nil; v = [v superview])
				if([[v registeredDraggedTypes] containsObject:fileUrl])
					return v;
			return nil;
		}

		// One dragging method: the window's view or the base class's own (WebKit's), whichever this drag is for. A
		// method the receiver does not have answers nothing (NO, no operation).
		template<typename R>
		R route(id _self, SEL _cmd, id<NSDraggingInfo> _info)
		{
			if(NSView* window = windowFor((NSView*)_self, _info))
			{
				if constexpr(std::is_void_v<R>)
				{
					if([window respondsToSelector:_cmd])
						reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(window, _cmd, _info);
					return;
				}
				else
					return [window respondsToSelector:_cmd] ? reinterpret_cast<R (*)(id, SEL, id)>(objc_msgSend)(window, _cmd, _info) : R{};
			}
			Class base = baseOf(_self);
			if(base == nil || !class_respondsToSelector(base, _cmd))
			{
				if constexpr(std::is_void_v<R>)
					return;
				else
					return R{};
			}
			struct objc_super up = {_self, base};
			return reinterpret_cast<R (*)(struct objc_super*, SEL, id)>(objc_msgSendSuper)(&up, _cmd, _info);
		}

		NSDragOperation entered(id _self, SEL _cmd, id<NSDraggingInfo> _info) { return route<NSDragOperation>(_self, _cmd, _info); }
		NSDragOperation updated(id _self, SEL _cmd, id<NSDraggingInfo> _info) { return route<NSDragOperation>(_self, _cmd, _info); }
		void exited(id _self, SEL _cmd, id<NSDraggingInfo> _info) { route<void>(_self, _cmd, _info); }
		BOOL prepare(id _self, SEL _cmd, id<NSDraggingInfo> _info) { return route<BOOL>(_self, _cmd, _info); }
		BOOL perform(id _self, SEL _cmd, id<NSDraggingInfo> _info) { return route<BOOL>(_self, _cmd, _info); }
		void conclude(id _self, SEL _cmd, id<NSDraggingInfo> _info) { route<void>(_self, _cmd, _info); }
		void ended(id _self, SEL _cmd, id<NSDraggingInfo> _info) { route<void>(_self, _cmd, _info); }

		// Our subclass of _base, made once. Its name carries the address of this code: a Machinedrum and a Monomachine
		// Editor in one host are two binaries, and each must call its own methods.
		Class subclassOf(Class _base)
		{
			auto& m = made();
			if(const auto it = m.classes.find(_base); it != m.classes.end())
				return it->second;
			char code[24];
			std::snprintf(code, sizeof(code), "%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&entered)));
			const std::string name = std::string(g_prefix) + code + "_" + class_getName(_base);
			Class c = objc_allocateClassPair(_base, name.c_str(), 0);
			if(c == nil)
				return nil;
			// the methods' types: the result, then self, _cmd and the dragging info
			const std::string operation = std::string(@encode(NSDragOperation)) + "@:@", flag = std::string(@encode(BOOL)) + "@:@", none = "v@:@";
			class_addMethod(c, @selector(draggingEntered:), reinterpret_cast<IMP>(entered), operation.c_str());
			class_addMethod(c, @selector(draggingUpdated:), reinterpret_cast<IMP>(updated), operation.c_str());
			class_addMethod(c, @selector(draggingExited:), reinterpret_cast<IMP>(exited), none.c_str());
			class_addMethod(c, @selector(prepareForDragOperation:), reinterpret_cast<IMP>(prepare), flag.c_str());
			class_addMethod(c, @selector(performDragOperation:), reinterpret_cast<IMP>(perform), flag.c_str());
			class_addMethod(c, @selector(concludeDragOperation:), reinterpret_cast<IMP>(conclude), none.c_str());
			class_addMethod(c, @selector(draggingEnded:), reinterpret_cast<IMP>(ended), none.c_str());
			objc_registerClassPair(c);
			m.classes[_base] = c;
			return c;
		}
	}

	int extend(void* _view)
	{
		id view = (__bridge id)_view;
		if(view == nil)
			return -1;
		if(baseOf(view) != nil)
			return 1;
		Class cls = object_getClass(view);
		// a class key-value observing made: extending it would break the observer's own bookkeeping
		if(std::strncmp(class_getName(cls), "NSKVONotifying_", 15) == 0)
			return -1;
		Class sub = subclassOf(cls);
		if(sub == nil)
			return -1;
		object_setClass(view, sub);
		return 1;
	}

	int install(void* _webView)
	{
		Class web = NSClassFromString(@"WKWebView");
		id view = (__bridge id)_webView;
		if(web == nil || view == nil || ![view isKindOfClass:web])
			return -1;
		return extend(_webView);
	}
}
