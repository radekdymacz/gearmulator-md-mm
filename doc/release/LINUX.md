# Linux and Windows builds (not tested)

Every `mdmm-v*` tag attaches, besides the macOS installers, one archive per machine for Windows x64 and
Linux x64 to the draft release (`.github/workflows/mdmm-editors-release.yml`):

| Asset | Built by |
|---|---|
| `Machinedrum-Editor-Windows-x64-not-tested.zip`, `Monomachine-Editor-Windows-x64-not-tested.zip` | upstream's `elektron-windows.yml` (called as is), split per machine by `scripts/windows/package_mdmm_editors.sh` |
| `Machinedrum-Editor-Linux-x64-not-tested.tar.gz`, `Monomachine-Editor-Linux-x64-not-tested.tar.gz` | `.github/workflows/mdmm-editors-linux.yml` (`scripts/linux/build_mdmm.sh`) |

They are unsigned and nobody has used them: they go to a draft only, never to a published release, and the
release body says "Windows and Linux: not tested". Each archive has a README (`scripts/linux/README-Linux.txt`,
`scripts/windows/README-Windows-mdmm.txt`): install paths, required packages, bring your own ROM.

## Linux: the web view

The editor is a web page in JUCE 7's `WebBrowserComponent`. On Linux JUCE runs webkit2gtk in a child process
(XEmbed into the plug-in window) and loads GTK 3 and WebKit with `dlopen` at run time.

- **Build.** `mdmmEditors.cmake` turns `JUCE_WEB_BROWSER` on when pkg-config finds webkit2gtk (4.0 or 4.1; only
  the headers are used) and GTK 3. `mdJucePlugin/mdmmLinuxWebView.cmake` gives `juce_plugin_modules` what JUCE
  gives a `NEEDS_WEB_BROWSER` target: the headers, and JUCE's embedded subprocess helper
  (`JUCE_USE_EXTERNAL_TEMPORARY_SUBPROCESS`), so a VST3 starts WebKit in a helper program, not in a fork of the
  host. The standalone starts itself again with `--juce-gtkwebkitfork-child`.
- **Run.** JUCE 7 opens `libwebkit2gtk-4.0.so` and `libgtk-3.so`, the development symlinks, and Ubuntu 24.04 and
  Debian 13 have no webkit2gtk-4.0 at all. Two shims of those names (`mdJucePlugin/linux/`) lie beside the
  standalone and inside the VST3 bundle, found through the binaries' `$ORIGIN` run path: the WebKit one forwards
  JUCE's sixteen functions to `libwebkit2gtk-4.0.so.37` or `libwebkit2gtk-4.1.so.0` (the same C API); the GTK one
  only depends on `libgtk-3.so.0`. A JUCE update must check the shim's list against
  `juce_WebBrowserComponent_linux.cpp`. Without WebKit the editor is empty; the plug-in still loads.
- **The bridge** (`mdPageBridge.h`, `skins/shared/deskBridge.js`). Page -> plug-in is as on macOS: the page's
  `gmbridge://` iframe navigations reach webkit2gtk's `decide-policy`, which JUCE hands to `pageAboutToLoad`.
  Plug-in -> page is not: webkit2gtk 2.50 (Ubuntu 22.04's 4.0) runs a `javascript:` URL given to
  `webkit_web_view_load_uri`, but its web process then crashes (a null read in `libwebkit2gtk-4.0.so.37`) when the
  page's bridge iframes navigate around it; the probe (`scripts/linux/probe_webkit_bridge.py pending|answered`)
  crashed in 10 of 10 runs, and the packaged editors did on CI two runs out of three. So on Linux the plug-in writes
  each `gm.recv([...], seq)` call as a script file beside the page file (`<page>.recv-<seq>.js`), the page (loaded
  with `?recv=file`) loads them in order with `<script>` tags, polling every 8 ms for the next one, and reports
  `gmbridge://a/<seq>` every 250 ms so the plug-in deletes what was read (`a/0` when it starts: a page that loaded
  again reads from 1). The same probe in that mode (`file`) passed 5 of 5 runs of 300 round trips under the same
  load. The page loads from a `file://` temp file as on macOS. The zoom that fits the design width is macOS-only
  (`mdStudioWebZoom.mm`), as on Windows.

## Checked by CI, and not

`mdmm-editors-linux.yml` builds on Ubuntu 22.04 (glibc 2.35), runs the unit tests
(`ctest -E "Plugin|_AU|VST|FirmwareTest|synthLibMidiClockTimingTest"`), packages, and starts each packaged
standalone under Xvfb on Ubuntu 22.04 and 24.04 with runtime packages only: the app keeps running, JUCE's GTK
child and WebKit's `WebKitWebProcess` start, and a screenshot is kept as an artifact. Not checked: the VST3 in
a Linux DAW, audio and MIDI devices, a ROM, Wayland, any distribution but Ubuntu.

On Linux `synthLib::SysexBuffer` is a `std::pmr::vector` (macOS 10.13 builds have no `<memory_resource>`, so
there it is a plain `std::vector`): a few of upstream's firmware test programs assign one to the other and do not
compile on Linux (`sysexContentOracle.h`, `sdsFirmwareTest.cpp`, `userSysexFirmwareTest.cpp`,
`mmSysexExportFirmwareTest.cpp`). The build script builds everything with `-k 0`, warns, and leaves the tests of
programs that did not build out of the ctest run (they need a ROM anyway, but for the two `mmSysexWorkflowTest`
oracle tests). The fix belongs upstream.

## Windows: known risk

JUCE 7's default Windows web view is the Internet Explorer control (`WebBrowserComponent::Options::Backend::
defaultBackend`), not WebView2, and the pages use modern JavaScript. The Windows editor window has never been
seen; it may well stay empty or show script errors. WebView2 would need its SDK in the build
(`JUCE_USE_WIN_WEBVIEW2`), `withBackend(webview2)` in `mdWebPageHost.cpp`, and a check that WebView2 passes the
bridge's iframe navigations and `javascript:` URLs, on a Windows machine.
