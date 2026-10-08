# Windows x64

The editors' Windows build: `.github/workflows/mdmm-editors-windows.yml` (ours, called by `mdmm-editors.yml` on every
push) runs upstream's `scripts/windows/build_mdmm.ps1` (unchanged; upstream's `elektron-windows.yml` runs it the same
way) to build and package both machines' standalones and VST3s once, with sccache, starts what is in the package and
runs pluginval on it; the unit tests run beside it in a job of their own, compiled without `/GL` (their links took
most of the old 43 minutes). `scripts/windows/package_mdmm_editors.sh` splits that tested package into one zip per
machine for a release (`mdmm-editors-release.yml`).

## The web view: WebView2 (B-002)

JUCE 7.0.10's `WebBrowserComponent` is the Internet Explorer control on Windows unless its WebView2 backend is
compiled in. The 0.3.1 zips used IE: the page's modern JavaScript failed ("Error in the script on this page",
then a black window). JUCE's WebView2 backend does not fit either: it loads `WebView2Loader.dll` by name, which
Windows looks for beside the host's executable (a VST3 cannot put a DLL there), and it reports only top-level
navigations, so the bridge's `gmbridge://` iframes never reach the plug-in.

So the editors drive WebView2 themselves (`source/elektron/md/mdJucePlugin/mdWebView2Page.*`):

| | |
|---|---|
| SDK | the `Microsoft.Web.WebView2` NuGet package, fetched at configure time and checked by SHA-256 (`mdmmWindowsWebView.cmake`); offline: `-DMDMM_WEBVIEW2_SDK_DIR=<unpacked package>` |
| Loader | `WebView2LoaderStatic.lib`, linked into the binaries: nothing ships beside them |
| Runtime | the Evergreen WebView2 Runtime, part of Windows 11 and an up-to-date Windows 10; **86.0.616.0 or newer** (B-022: every interface used is in it but `ICoreWebView2Settings3`, 1.0.864, which is skipped when missing). Without one, or when the page does not start, the window says what failed and what to do (no IE fallback) |
| Start-up log | `Documents\Gearmulator Preview\<machine>\logs\editor-mdStudio.log` / `editor-mmStudio.log`, every build (B-022): version, Windows, host, the runtime's version, each HRESULT, page loaded, bridge up, what failed. The editor's menu: Open Log Folder |
| Profile | `%LOCALAPPDATA%\Gearmulator\EditorWebView2` (WebView2's default is beside the host's executable, often read-only) |
| Page file | as on the other systems: the bundled page in `%TEMP%\gearmulator-<page>-<random>.html`, loaded as `file://` |
| Page -> plug-in | `window.chrome.webview.postMessage` of the same `gmbridge://` texts (`deskBridge.js`), in order; a top-level `gmbridge://` navigation is still taken |
| Plug-in -> page | `ExecuteScript` of the same `gm.recv([...], seq)` scripts the Linux files carry (`mdPageBridge.h`) |
| Zoom | `ZoomFactor` from the design width, as `WKWebView.pageZoom` on macOS |
| Browser keys | off (F5, Ctrl+R, Ctrl+P, Ctrl+F belong to the browser, a reload would restart the page); DevTools only in diagnostics builds |
| New windows | not opened; a file dropped on the page is not opened either (the page says to click) |

macOS and Linux keep their transports (javascript: URLs, script files).

## The start test (CI)

`mdmm-editors-windows.yml` runs on pushes to `main`, `release/md-mm-*`, `release/0.*`, `feat/windows-*` and `ci/**` (through `mdmm-editors.yml`). After the package is
built, `scripts/windows/smoke_mdmm.ps1` on a clean `windows-2022` runner, with no ROM, starts each standalone and
each VST3 (in `scripts/vst3EditorHost`, a minimal JUCE host that opens the editor and feeds silent blocks)
and checks:

- the process is still running;
- `msedgewebview2.exe` runs with the editors' profile folder (WebView2, not IE);
- the page shows "<machine> firmware needed", read through UI Automation: the page said `ready` (page -> plug-in),
  the plug-in answered with the machine's state (plug-in -> page);

and keeps a screenshot and the UI Automation names as the `windows-mdmm-smoke` artifact. The three systems' start
tests side by side: [FOUNDATION.md](../modern-ux/FOUNDATION.md), "CI start tests".

Not covered: a real DAW (Live, Reaper, Cubase, Bitwig, FL Studio), High-DPI monitors and moving a window between
monitors of different scale, typing into the page inside a DAW (the host's own shortcuts), a machine without the
WebView2 runtime, Windows on ARM.
