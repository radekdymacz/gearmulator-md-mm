#pragma once

namespace mdJucePlugin
{
	// B-029, B-022: what the editor's WebView2 controller does when the page's native window changes (Windows,
	// mdWebView2Page.cpp), as plain values: no JUCE, no Windows, tested by mdWindowsPolicyTest.
	//
	// WebView2 puts a child window of its own into the window it is made in (or moved to), and a child window is
	// destroyed with its parent. The page outlives its windows: the processor's editor state keeps the editor, and so
	// the page, while a host has the plug-in's window closed (jucePluginEditorLib::PluginEditorState), and the
	// standalone makes its window again while it starts (ResizableWindow::recreateDesktopWindow). In both cases JUCE
	// destroys the window the controller lives in (a VST3's removed() deletes the peer) and the page then gets a new
	// one. Moving the controller into it (put_ParentWindow) moves nothing, its window is gone: the page stays blank
	// until the plug-in is loaded again. Such a controller is closed and a new one made, which loads the page again.
	namespace webView2Window
	{
		enum class Step
		{
			Nothing,	// as it is
			Create,		// no controller yet: make one in the window
			Reparent,	// the controller's window still exists: move it into the new one (the page keeps running)
			Remake,		// its window was destroyed: close it, make a new one in this window, load the page again
			Close		// its window was destroyed and there is none yet: close it; the next window makes a new one
		};

		struct Facts
		{
			bool controller = false;			// a controller exists
			bool creating = false;				// one is being made: its completion decides (E_ABORT: made again)
			bool window = false;				// the page's component has a native window now
			bool sameWindow = false;			// ... and it is the one the controller is in
			bool controllerWindowAlive = false;	// the window the controller was made in, or moved to, still exists
		};

		constexpr Step next(const Facts& _f)
		{
			if(!_f.controller)
				return _f.creating || !_f.window ? Step::Nothing : Step::Create;
			if(!_f.controllerWindowAlive)
				return _f.window ? Step::Remake : Step::Close;
			if(!_f.window || _f.sameWindow)
				return Step::Nothing;	// no window: it stays where it is, hidden, until one comes
			return Step::Reparent;
		}
	}
}
