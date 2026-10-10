# Windows x64

The editors' Windows build: `.github/workflows/mdmm-editors-windows.yml` (ours, called by `mdmm-editors.yml` on every
push) runs `scripts/windows/build_mdmm.ps1` to build and package both machines' standalones and VST3s once, with sccache, starts what is in the package and
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
| Window closed or made again | B-029: the page outlives its window (the processor keeps the editor while a host has it closed; the standalone makes its window again while it starts), and WebView2's own window is destroyed with the window it was made in. The controller remembers that window: when it is gone the controller is closed and a new one made in the next window, which loads the page again, and the page gets everything as a first page does (`mdWebView2Window.h`, `WebPageHost::pageLoadsAgain`). A window that still exists: the controller is moved into the new one |

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
- B-029: the Machinedrum VST3 once more with its editor closed after 30 s and opened again (`mdmmVst3EditorHost
  --reopen 30`: the editor deleted, so the plug-in's view is removed, as a DAW closing the window does): the page must
  come back with the machine's state and take real keys, and the start-up log must say "the page loads again";

and keeps a screenshot and the UI Automation names as the `windows-mdmm-smoke` artifact. The three systems' start
tests side by side: [FOUNDATION.md](../modern-ux/FOUNDATION.md), "CI start tests".

Not covered: a real DAW (Live, Reaper, Cubase, Bitwig, FL Studio), High-DPI monitors and moving a window between
monitors of different scale, typing into the page inside a DAW (the host's own shortcuts), a machine without the
WebView2 runtime, Windows on ARM.

## Timer resolution, audio and MIDI (B-036, B-037)

- **Timer resolution:** every JUCE binary asks Windows for a 1 ms timer period when it loads
  (`juce_SystemStats_windows.cpp`, `JUCE_WIN32_TIMER_PERIOD` 1; this build does not change it): the standalone and the
  plug-in in a DAW alike. Nothing more to set.
- **Audio in the standalone:** JUCE's first driver with a device, "Windows Audio" (WASAPI shared), unless the AUDIO /
  MIDI panel chose another ("Windows Audio (Exclusive Mode)", "(Low Latency Mode)", DirectSound; no ASIO in this
  build). WASAPI shared runs at the Windows mixer's rate, usually 48 kHz: the 44.1 kHz machine is resampled. The whole
  emulation runs on the audio thread, so a machine that cannot keep up shows as one busy core: on a 16 to 32 thread CPU,
  3 to 6 % in Task Manager.
- **MIDI:** JUCE's WinMM backend (WinRT MIDI is off). A WinMM port belongs to one program at a time: a port a DAW or
  another editor has open cannot be opened by the standalone, and the AUDIO / MIDI panel says so since 0.4.0. No MIDI
  input is on until it is switched on in the panel. Since 0.4.0 the machine's own MIDI out (notes, CCs, program
  changes, clock and transport; never its SysEx) goes to the panel's output, and in a DAW to the plug-in's MIDI out
  (B-037, `mdMachineMidiOut.h`).

## What a Windows tester sends

1. The editor's version: the first line of the editor's menu (right-click the header), or Help > About in the
   standalone.
2. The start-up logs: the editor's menu > **Open Log Folder** (`Documents\Gearmulator Preview\<Machinedrum or
   Monomachine>\logs`): `editor-mdStudio.log` or `editor-mmStudio.log`, and the `-previous.log` beside it (the start
   before). Taken right after the problem, before the editor is opened again (each start moves the last log to
   `-previous`). The first line says the version, Windows, standalone or the DAW's executable, and the CPU. What to
   look for:
   - a blank window or "no GUI": `WebView2 runtime ...` (none, or OLDER), each HRESULT, `page up`, `FAILED: ...`,
     and, after closing and opening the editor, `the page's window was destroyed ...` then `the page loads again ...`;
   - stuck at the start-up card: the `boot` lines (once a second for 30 s): `blocks=0` after 5 s (the host runs no
     audio for the plug-in), `realtime` well below 1.00x (too slow: a larger buffer), `cycles` rising with
     `firmwareMidiReady=0` for 30 s (the start stalls); none after `page up` (the plug-in's timer stopped). In 0.3.5 the
     Machinedrum's lines come every 4 s;
   - crackles: the `boot` lines' `realtime`, and (standalone, 0.4.0) the `audio:` line: driver, device, rate, buffer;
   - MIDI (standalone, 0.4.0): the `audio: ...; MIDI in: ...; MIDI out: ...` line and any `MIDI: ... could not be
     opened`.
3. For crackles or high CPU, a performance capture: the editor's menu > Developer > Start Performance Capture, play
   for a minute, Stop; the `performance-*.jsonl` file in the same folder.
4. The setup: the DAW and its version, its audio driver (ASIO or WASAPI), sample rate and buffer size; for the
   standalone, what the AUDIO / MIDI panel shows; which other programs were open that use MIDI or audio.
5. A screenshot of the window.
