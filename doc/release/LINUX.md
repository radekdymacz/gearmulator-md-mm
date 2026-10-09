# Linux and Windows builds (not tested)

Every `mdmm-v*` tag attaches, besides the macOS installers, one archive per machine for Windows x64 and
Linux x64 to the draft release (`.github/workflows/mdmm-editors-release.yml`):

| Asset | Built by |
|---|---|
| `Machinedrum-Editor-Windows-x64-not-tested.zip`, `Monomachine-Editor-Windows-x64-not-tested.zip` | `.github/workflows/mdmm-editors-windows.yml` (upstream's `build_mdmm.ps1`, as is), split per machine by `scripts/windows/package_mdmm_editors.sh` |
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
  again reads from 1, and the plug-in sends it everything once more, as on its ready). The same probe in that mode
  (`file`) passed 5 of 5 runs of 300 round trips under the same load. The plug-in writes the files strictly in order
  (`FileOutbox` in `mdPageBridge.h`): one that cannot be written (a full disk for a moment) holds back the later ones
  and is tried again on every tick; with 8 MiB waiting, or 2 s without a file written, they are dropped and the page
  is loaded again once a file can be written. The start-up log says each of these in every build. The page loads
  from a `file://` temp file as on macOS. The zoom that fits the design width is macOS-only
  (`mdStudioWebZoom.mm`), as on Windows.

## Checked by CI, and not

`mdmm-editors-linux.yml` builds on Ubuntu 22.04 (glibc 2.35), runs the unit tests
(`ctest -E "Plugin|_AU|VST|FirmwareTest|synthLibMidiClockTimingTest"`), packages, and starts each packaged
standalone and VST3 (in `scripts/vst3EditorHost`) under Xvfb on Ubuntu 22.04 and 24.04 with runtime packages only
(`scripts/linux/smoke_mdmm.sh`): the app keeps running, WebKit's `WebKitWebProcess` starts, the bridge goes both
ways and the page shows "<machine> firmware needed" (read through AT-SPI), and screenshots are kept as an
artifact. The three systems' start tests side by side: [FOUNDATION.md](../modern-ux/FOUNDATION.md), "CI start
tests". Not checked: the VST3 in a Linux DAW, audio and MIDI devices, a ROM, Wayland, any distribution but Ubuntu.

On Linux `synthLib::SysexBuffer` is a `std::pmr::vector` (a macOS build with a deployment target below 14, which
the releases have, takes a plain `std::vector`). Four of the firmware test programs assigned one to the other and
did not compile on Linux, and the build script used to leave their tests out and carry on. That is fixed: they copy
the bytes (`sysexContentOracle.h`, `sdsFirmwareTest.cpp`, `mmSysexExportFirmwareTest.cpp`), and a test program that
does not build now fails the job (`-k 0` lists every compile error in one run). The script also fails when a ctest
test's program is missing, unless its name is in the script's `known_unbuilt` list (empty), runs `ctest` with
`--no-tests=error`, and prints how many tests ran, failed and were skipped, and which. A macOS build configured
with `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0` takes the same `std::pmr` path, so it shows this kind of error without a
Linux machine. `synthLibMidiClockTimingTest` is still left out of the run, from before `midiClock.cpp` and the test
were built with `-fno-fast-math` (2026-10-07); retry it on Linux CI.

## Windows: known risk

JUCE 7's default Windows web view is the Internet Explorer control (`WebBrowserComponent::Options::Backend::
defaultBackend`), not WebView2, and the pages use modern JavaScript. The Windows editor window has never been
seen; it may well stay empty or show script errors. WebView2 would need its SDK in the build
(`JUCE_USE_WIN_WEBVIEW2`), `withBackend(webview2)` in `mdWebPageHost.cpp`, and a check that WebView2 passes the
bridge's iframe navigations and `javascript:` URLs, on a Windows machine.
