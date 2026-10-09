// The editor page's web view on Windows: WebView2 without JUCE's backend (mdWebView2Page.h). Windows only
// (mdmmWindowsWebView.cmake adds this file and the WebView2 SDK).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wrl.h>

#include "WebView2.h"

#include "mdWebView2Page.h"
#include "mdWebView2Window.h"

#include <cmath>
#include <deque>

#ifndef MDMM_DIAGNOSTICS
#define MDMM_DIAGNOSTICS 0
#endif

namespace mdJucePlugin
{
	using Microsoft::WRL::Callback;
	using Microsoft::WRL::ComPtr;

	namespace
	{
		juce::String takeString(LPWSTR _s)
		{
			if(_s == nullptr)
				return {};
			juce::String result(_s);
			CoTaskMemFree(_s);
			return result;
		}

		juce::String hresultText(const HRESULT _hr)
		{
			return "0x" + juce::String::toHexString(static_cast<int>(_hr)).paddedLeft('0', 8);
		}
	}

	struct WebView2Page::Impl final : private juce::ComponentMovementWatcher
	{
		Impl(WebView2Page& _owner, juce::File _userDataFolder, Callbacks _callbacks)
			: juce::ComponentMovementWatcher(&_owner)
			, owner(_owner), callbacks(std::move(_callbacks))
			, scaleNotifier(&_owner, [this](float) { updateBounds(); })
		{
			// WebView2 runs on a single-threaded COM apartment: the message thread's. A host has one already;
			// the standalone app may not.
			const auto co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			comInitialised = co == S_OK || co == S_FALSE;
			userDataFolder = std::move(_userDataFolder);
		}

		// B-022: GEARMULATOR_MDMM_WEBVIEW2_TEST=old acts as an old runtime would (no ICoreWebView2Settings3), =fail as a
		// machine without one (the environment refused): the start tests check both ways (scripts/windows/smoke_mdmm.ps1).
		static juce::String testMode() { return juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_WEBVIEW2_TEST", {}); }

		// After the owner holds this Impl: the loader may call back before it returns.
		void start()
		{
			// B-022: which runtime this machine has, before anything else (a Windows 10 with an old or no runtime)
			LPWSTR version = nullptr;
			const auto vhr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
			const auto runtime = takeString(version);
			if(FAILED(vhr) || runtime.isEmpty())
				event("WebView2 runtime: none found (" + hresultText(vhr) + ")");
			else
			{
				int order = 0;
				const auto older = SUCCEEDED(CompareBrowserVersions(runtime.toWideCharPointer(), juce::String(g_minimumRuntime).toWideCharPointer(), &order)) && order < 0;
				event("WebView2 runtime " + runtime + " (the editors need " + g_minimumRuntime + " or newer" + (older ? ": this one is OLDER" : "") + ")");
			}
			if(testMode() == "fail")
			{
				fail("the WebView2 environment was not created (test: GEARMULATOR_MDMM_WEBVIEW2_TEST=fail)");
				return;
			}
			userDataFolder.createDirectory();
			event("WebView2 user data: " + userDataFolder.getFullPathName());
			const auto folder = userDataFolder.getFullPathName();
			juce::Component::SafePointer<WebView2Page> safe(&owner);
			const auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.toWideCharPointer(), nullptr,
				Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
					[safe](const HRESULT _result, ICoreWebView2Environment* _env) -> HRESULT
					{
						if(safe == nullptr)
							return S_OK;
						auto& self = *safe->m_impl;
						if(FAILED(_result) || _env == nullptr)
						{
							self.fail("the WebView2 environment was not created (" + hresultText(_result) + ")");
							return S_OK;
						}
						self.environment = _env;
						LPWSTR used = nullptr;
						_env->get_BrowserVersionString(&used);
						self.event("WebView2 environment ready (" + hresultText(_result) + "), runtime " + takeString(used));
						self.createController();
						return S_OK;
					}).Get());
			if(FAILED(hr))
				fail("no WebView2 runtime (" + hresultText(hr) + ")");
			else
				event("WebView2 environment asked for (" + hresultText(hr) + ")");
		}

		~Impl() override
		{
			if(controller)
				controller->Close();
			controller = nullptr;
			webView = nullptr;
			environment = nullptr;
			if(comInitialised)
				CoUninitialize();
		}

		void event(const juce::String& _line) const
		{
			if(callbacks.onEvent)
				callbacks.onEvent(_line);
		}

		void fail(const juce::String& _why)
		{
			failed = true;
			juce::Logger::writeToLog("Gearmulator editor page: " + _why);
			event(_why);
			if(callbacks.onFailed)
				callbacks.onFailed(_why);
			owner.repaint();
		}

		HWND parentWindow() const
		{
			auto* peer = owner.getPeer();
			return peer != nullptr ? static_cast<HWND>(peer->getNativeHandle()) : nullptr;
		}

		// The controller is a child window of the component's peer; it is made once there are both.
		void createController()
		{
			if(environment == nullptr || controller != nullptr || creating)
				return;
			const auto parent = parentWindow();
			if(parent == nullptr)
				return;
			creating = true;
			juce::Component::SafePointer<WebView2Page> safe(&owner);
			const auto hr = environment->CreateCoreWebView2Controller(parent,
				Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[safe, parent](const HRESULT _result, ICoreWebView2Controller* _controller) -> HRESULT
					{
						if(safe == nullptr)
						{
							if(_controller != nullptr)
								_controller->Close();
							return S_OK;
						}
						auto& self = *safe->m_impl;
						self.creating = false;
						if(FAILED(_result) || _controller == nullptr)
						{
							// B-022: E_ABORT when the parent window went while the controller was being made (the
							// standalone makes its window again while it starts): made again in the new one, a few times
							if(_result == E_ABORT && ++self.controllerRetries <= g_controllerRetries)
							{
								self.event("the WebView2 controller was not created (" + hresultText(_result) + ", its window was made again): trying again");
								juce::Timer::callAfterDelay(100, [safe] { if(safe != nullptr) safe->m_impl->createController(); });
								return S_OK;
							}
							self.fail("the WebView2 controller was not created (" + hresultText(_result) + ")");
							return S_OK;
						}
						self.event("WebView2 controller ready (" + hresultText(_result) + ")");
						self.controller = _controller;
						self.controllerWindow = parent;	// B-029: WebView2's own window is in this one, and goes with it
						self.controllerRetries = 0;
						self.controller->get_CoreWebView2(&self.webView);
						if(self.webView == nullptr)
						{
							self.fail("the WebView2 controller has no web view");
							return S_OK;
						}
						self.ready();
						return S_OK;
					}).Get());
			if(FAILED(hr))
			{
				creating = false;
				fail("CreateCoreWebView2Controller failed (" + hresultText(hr) + ")");
			}
		}

		void ready()
		{
			ComPtr<ICoreWebView2Settings> settings;
			if(SUCCEEDED(webView->get_Settings(&settings)) && settings)
			{
				settings->put_IsStatusBarEnabled(FALSE);
				settings->put_IsZoomControlEnabled(FALSE);	// the plug-in sets the zoom (the page's design width)
				settings->put_AreDevToolsEnabled(MDMM_DIAGNOSTICS ? TRUE : FALSE);
				ComPtr<ICoreWebView2Settings3> settings3;
				// F5, Ctrl+R, Ctrl+P, Ctrl+F...: the browser's, not the editor's (a reload would restart the page). B-022:
				// a runtime older than 1.0.864 has no ICoreWebView2Settings3: the page works, those keys stay the browser's.
				const auto qi = testMode() == "old" ? E_NOINTERFACE : settings.As(&settings3);
				if(SUCCEEDED(qi) && settings3)
					settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
				else
					event("ICoreWebView2Settings3 not available (" + hresultText(qi) + "): an older runtime; the browser keys stay on");
			}

			webView->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
				[this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* _args) -> HRESULT
				{
					LPWSTR uri = nullptr;
					_args->get_Uri(&uri);
					const auto url = takeString(uri);
					if(callbacks.onNavigation && !callbacks.onNavigation(url))
						_args->put_Cancel(TRUE);
					return S_OK;
				}).Get(), &navigationStartingToken);

			webView->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
				[this](ICoreWebView2* _sender, ICoreWebView2NavigationCompletedEventArgs* _args) -> HRESULT
				{
					LPWSTR uri = nullptr;
					_sender->get_Source(&uri);
					const auto url = takeString(uri);
					BOOL ok = FALSE;
					_args->get_IsSuccess(&ok);
					if(ok)
					{
						event("finished loading " + url.substring(0, 60));
					}
					else
					{
						COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
						_args->get_WebErrorStatus(&status);
						// a cancelled navigation (a dropped file, the bridge) is not an error
						if(status != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)
						{
							juce::Logger::writeToLog("Gearmulator editor page: load error " + juce::String(static_cast<int>(status)));
							event("load error " + juce::String(static_cast<int>(status)) + " " + url.substring(0, 60));
						}
					}
					return S_OK;
				}).Get(), &navigationCompletedToken);

			webView->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
				[this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* _args) -> HRESULT
				{
					LPWSTR text = nullptr;
					if(FAILED(_args->TryGetWebMessageAsString(&text)) || text == nullptr)
						return S_OK;
					const auto message = takeString(text);
					if(callbacks.onMessage)
						callbacks.onMessage(message.toStdString());
					return S_OK;
				}).Get(), &webMessageToken);

			// No new windows: a link that asks for one opens nothing (as on macOS).
			webView->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
				[this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* _args) -> HRESULT
				{
					LPWSTR uri = nullptr;
					_args->get_Uri(&uri);
					event("new window not opened: " + takeString(uri).substring(0, 60));
					_args->put_Handled(TRUE);
					return S_OK;
				}).Get(), &newWindowToken);

			webView->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
				[this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* _args) -> HRESULT
				{
					COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
					_args->get_ProcessFailedKind(&kind);
					juce::Logger::writeToLog("Gearmulator editor page: WebView2 process failed, kind " + juce::String(static_cast<int>(kind)));
					event("WebView2 process failed, kind " + juce::String(static_cast<int>(kind)));
					return S_OK;
				}).Get(), &processFailedToken);

			applyZoom();
			updateBounds();
			updateVisibility();
			event("WebView2 ready");
			if(focusWanted)
				focus();
			if(pageLost)
			{
				// B-029: this web view replaces one whose window was destroyed: the page starts again from nothing, and
				// the owner sends it everything once it speaks (WebPageHost::pageLoadsAgain)
				pageLost = false;
				scripts.clear();
				if(callbacks.onReload)
					callbacks.onReload();
			}
			if(url.isNotEmpty())
				webView->Navigate(url.toWideCharPointer());
			for(const auto& s : scripts)
				run(s);
			scripts.clear();
		}

		void goToURL(const juce::String& _url)
		{
			url = _url;
			if(webView)
				webView->Navigate(url.toWideCharPointer());
		}

		void executeScript(const juce::String& _script)
		{
			if(!webView)
			{
				// Before the first page: run once the web view is there. B-029: a page whose window was destroyed is gone,
				// and what was meant for it too (the next one is sent everything); nothing piles up while a host has the
				// editor closed.
				if(!pageLost)
					scripts.push_back(_script);
				return;
			}
			run(_script);
		}

		void run(const juce::String& _script) const
		{
			webView->ExecuteScript(_script.toWideCharPointer(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
				[](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
		}

		// B-018: the keyboard into the page (WebView2's own window), now or once the controller is there.
		void focus()
		{
			if(!controller)
			{
				focusWanted = true;
				return;
			}
			focusWanted = false;
			const auto hr = controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
			if(FAILED(hr))
				event("keyboard focus not moved into the page (" + hresultText(hr) + ")");
		}

		void setZoom(const double _zoom)
		{
			if(std::abs(zoom - _zoom) < 0.001)
				return;
			zoom = _zoom;
			applyZoom();
		}

		void applyZoom() const
		{
			if(controller)
				controller->put_ZoomFactor(zoom);
		}

		// The controller's bounds are in the parent window's client pixels.
		void updateBounds() const
		{
			if(!controller)
				return;
			auto* peer = owner.getPeer();
			if(peer == nullptr)
				return;
			const auto area = (peer->getAreaCoveredBy(owner).toDouble() * peer->getPlatformScaleFactor()).toNearestInt();
			controller->put_Bounds(RECT{area.getX(), area.getY(), area.getRight(), area.getBottom()});
		}

		void updateVisibility() const
		{
			if(controller)
				controller->put_IsVisible(owner.isShowing() ? TRUE : FALSE);
		}

		// B-029, B-022: the page's native window changed (mdWebView2Window.h): a controller whose window was destroyed is
		// closed (WebView2's own window went with it) and a new one made in the new window; one whose window lives on is
		// moved into the new one; none yet: made once there is a window.
		void followWindow()
		{
			using namespace webView2Window;
			const auto parent = parentWindow();
			Facts facts;
			facts.controller = controller != nullptr;
			facts.creating = creating;
			facts.window = parent != nullptr;
			facts.sameWindow = parent != nullptr && parent == controllerWindow;
			facts.controllerWindowAlive = controllerWindow != nullptr && IsWindow(controllerWindow) != FALSE;
			switch(next(facts))
			{
			case Step::Nothing:
				return;
			case Step::Create:
				createController();
				return;
			case Step::Reparent:
			{
				const auto hr = controller->put_ParentWindow(parent);
				if(SUCCEEDED(hr))
				{
					controllerWindow = parent;
					event("the web view moved into the page's new window");
					return;
				}
				// not moved: a new one in the new window, as for a destroyed one
				event("the web view could not move into the page's new window (" + hresultText(hr) + ")");
				closeLostController();
				createController();
				return;
			}
			case Step::Remake:
				closeLostController();
				createController();
				return;
			case Step::Close:
				closeLostController();
				return;
			}
		}

		// B-029: the controller's page is gone (its window was destroyed, and WebView2's own window with it): closed, so
		// its browser processes can end while the editor is closed; the next controller loads the page again (ready).
		void closeLostController()
		{
			event("the page's window was destroyed (the editor was closed, or its window made again): its web view is closed; a new one loads the page in the next window");
			controller->Close();
			controller = nullptr;
			webView = nullptr;
			controllerWindow = nullptr;
			scripts.clear();
			pageLost = true;
		}

		// ComponentMovementWatcher
		void componentMovedOrResized(bool, bool) override { updateBounds(); }
		void componentVisibilityChanged() override { updateVisibility(); }
		void componentPeerChanged() override
		{
			followWindow();
			updateBounds();
			updateVisibility();
		}

		WebView2Page& owner;
		Callbacks callbacks;
		juce::NativeScaleFactorNotifier scaleNotifier;
		ComPtr<ICoreWebView2Environment> environment;
		ComPtr<ICoreWebView2Controller> controller;
		ComPtr<ICoreWebView2> webView;
		EventRegistrationToken navigationStartingToken{}, navigationCompletedToken{}, webMessageToken{}, newWindowToken{},
			processFailedToken{};
		juce::File userDataFolder;
		juce::String url;
		std::deque<juce::String> scripts;	// asked for before the web view was there
		double zoom = 1.0;
		HWND controllerWindow = nullptr;	// B-029: the window the controller was made in or moved to (its own window is in it)
		bool pageLost = false;				// B-029: a controller was closed with its page; the next one loads it again
		bool creating = false;
		bool focusWanted = false;
		int controllerRetries = 0;
		static constexpr int g_controllerRetries = 10;
		bool failed = false;
		bool comInitialised = false;
	};

	WebView2Page::WebView2Page(juce::File _userDataFolder, Callbacks _callbacks)
	{
		setOpaque(true);
		m_impl = std::make_unique<Impl>(*this, std::move(_userDataFolder), std::move(_callbacks));
		m_impl->start();
	}

	WebView2Page::~WebView2Page()
	{
		m_impl.reset();
	}

	void WebView2Page::goToURL(const juce::String& _url) { m_impl->goToURL(_url); }
	void WebView2Page::executeScript(const juce::String& _script) { m_impl->executeScript(_script); }
	void WebView2Page::setZoom(const double _zoom) { m_impl->setZoom(_zoom); }
	void WebView2Page::setOnReload(std::function<void()> _onReload) { m_impl->callbacks.onReload = std::move(_onReload); }

	void WebView2Page::paint(juce::Graphics& _g)
	{
		_g.fillAll(juce::Colour(0xff15171a));
		if(!m_impl || !m_impl->failed)
			return;
		_g.setColour(juce::Colours::white);
		_g.setFont(16.0f);
		_g.drawFittedText("The editor needs the Microsoft Edge WebView2 Runtime, which comes with Windows 10 and 11.\n"
			"Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703 and open the editor again.",
			getLocalBounds().reduced(24), juce::Justification::centred, 4);
	}

	void WebView2Page::resized() { if(m_impl) m_impl->updateBounds(); }
	void WebView2Page::focusPage() { if(m_impl) m_impl->focus(); }
	void WebView2Page::visibilityChanged() { if(m_impl) m_impl->updateVisibility(); }
	void WebView2Page::parentHierarchyChanged()
	{
		if(!m_impl)
			return;
		m_impl->followWindow();
		m_impl->updateBounds();
		m_impl->updateVisibility();
	}
}
