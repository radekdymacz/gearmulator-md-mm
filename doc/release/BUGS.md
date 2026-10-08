# Bug log

*Reports from beta testers and users (Discord, email). GitHub issues are off on this
repository; the site sends reports to the contact address. Newest first. Each entry:
where it came from, the setup, what happens, what should happen, status.*

## B-010 · Timing lags ("swing") while editing parameter locks

- **From:** Discord tester (versonegro), 2026-10-07, macOS 12, Apple M1.
- **What happens:** while setting parameter locks on the Sequence page, playback lags a little, like swing. When editing stops, timing goes back to normal. Upstream Gearmulator on the same Mac does not lag when editing locks.
- **Also:** reproduced by the owner on an M4 Pro, current macOS, so not the tester's CPU.
- **Should:** editing never changes the timing of playback.
- **Cause (measured, `mdDeskFirmwareTest <MD ROM> plocktiming`):** a lock edit is a whole pattern dump (5.4 KB; the MD has no SysEx for one lock). The desk already sends at most 5 a second (about 27 KB/s, 9 times what a MIDI cable carries), but the emulated MIDI UART took each dump's bytes all at once, and the firmware's receive interrupt then ran back to back for all of them: about 17 ms in which the sequencer did not run. On a real MD the bytes arrive one every 0.32 ms and nothing stalls. The same on the way out: the UART sent a read-back dump (the editor asks for one after a gesture) at once, and the transmit interrupt stalled the sequencer about 15 ms. At 120 BPM the MIDI clock was up to 17 ms off and the steps up to 19 ms; idle is under 1 ms. Upstream does not lag because its UI never sends dumps.
- **Fix (branch `fix/plock-timing`):** `md::Hardware` paces the Elektron SysEx going into the firmware (125 KB/s: a pattern in 43 ms; MIDI realtime bytes still go in between) and the MIDI UART's sending (125 KB/s; a pattern read-back in about 60 ms); the desk asks for the read-back only after 750 ms without edits, so a run of clicks gets one. Measured, 120 BPM: the clock 0.94 ms off at worst while drawing, clicking or wheeling locks (was 16.9 ms), the same as idle, except the one tick at a gesture's read-back (up to 3.8-6.7 ms: the firmware's own time to build a dump). The page's first read of the library: 8.2 ms worst (was 19.4 ms). `GEARMULATOR_MDMM_MIDI_PACING=0` turns the pacing off to compare.
- **Status:** fixed for 0.3.2, not yet checked by the tester.

## B-009 · WebKit crashed once on Ubuntu 24.04 (Linux VST3)

- **From:** the Linux CI start test, 2026-10-08.
- **What happened:** in one run, WebKitWebProcess (libwebkit2gtk-4.1 2.52.6) segfaulted when the Machinedrum VST3 opened in the test host. Not seen on Ubuntu 22.04; the next runs were clean.
- **Maybe:** caused by the test reading the accessibility tree while the page was still loading (now it waits for the bridge), or an intermittent WebKit crash that users could also hit.
- **Watch:** every start-test run now reports kernel-log segfaults in its summary.
- **Status:** watching.

## B-008 · The play head is invisible on the MK1 (white) skin

- **From:** Discord tester (versonegro), macOS 12, 2026-10-07, with a screen recording.
- **What happens:** on the Sequence page the moving play head does not show with the MK1 (white) plate; with the MK2 (black) plate it shows, but faintly.
- **Should:** a clearly visible play head on both plates, on every supported macOS.
- **Cause:** the play head (`#phcol`) was drawn only with `color-mix()`: its tint and edge, and on the MK1 plate all of it, so the macOS 12 WebKit dropped it (B-001). On MK2 it was faint even on a current WebKit (a 1 px edge at 45 %, screen-blended).
- **Fix (branch `fix/plugin-window-fit`):** `deskCompat.js` gives an older WebKit the same colours (B-001); the play head has a solid 2 px LED-coloured edge and a stronger tint on both plates (MD), and a 2 px ink edge on the Monomachine Editor's roll. Checked on MK1 and MK2, with and without the rewrite (`?compat=force`).
- **Status:** fixed for 0.3.2.

## B-007 · "Settings" in the right-click menu does not open

- **From:** the same tester, macOS 12, 2026-10-07.
- **What happens:** right-click on the editor opens the editor menu (inherited from Gearmulator); choosing its Settings entry does nothing.
- **Should:** every entry in that menu works in the web-page editors, or is removed; Settings opens the editor's own settings (or the menu offers only what applies).
- **Status:** fixed for 0.3.2. Cause: "Settings..." opened upstream's RmlUi settings page, which the web page hides. The menu now has GUI Scale, RAM recording (MD), Performance diagnostics and, in the standalone only, "Audio/MIDI Settings..." (the page's own panel); in a plug-in the host owns audio and MIDI, so there is no Settings entry.

## B-006 · LEN (loop length) cannot be dragged

- **From:** the same tester asked how to loop only 16 steps, 2026-10-07; Radek expected LEN to be draggable.
- **What happens:** on the Sequence LCD, LEN steps 16 / 32 / 48 / 64 on click (⌥-click steps the length inside it, the mouse wheel works), but a drag does nothing, and the click behaviour is easy to miss.
- **Should:** LEN (and the other LCD values that step on click) also follow a vertical drag, like the other values in the editor; the tooltip says so.
- **Status:** fixed for 0.3.2 (LEN, SPD and SONG follow a vertical drag).

## B-005 · High CPU in Ableton Live

- **From:** Discord beta tester (versonegro), 2026-10-07: Live's CPU meter at 66 % with the Machinedrum Editor, macOS 12, Apple M1.
- **What happens:** the plug-in uses a large share of the CPU.
- **To check:** how much is the emulation (DSP56300 JIT + 68k) and how much the editor page (web view redraws, the rate of plug-in → page updates, animations); CPU with the editor window open vs closed, playing vs stopped; M1 vs newer chips; Live's buffer size.
- **Should:** as low as the emulation allows; the page costs little, and nothing when its window is closed.
- **Status:** open.

## B-004 · MIX faders look far too big at some window sizes

- **From:** the same Discord screenshot as B-001, 2026-10-07.
- **What happens:** on the MIX workspace the volume faders stretch very tall and the strips look oversized.
- **Should:** the page keeps sensible proportions at every window size; faders have a maximum height.
- **Cause:** the page laid itself out in whatever height the window had: the MD faders grew with it up to 320 px, and below the master effects (MD) or between the strips and the routing (MM) an empty band was left (Radek's Live window showed it too, about 80 pt).
- **Fix (branch `fix/plugin-window-fit`):** a window larger than the design (1440 x 924) both ways zooms the page up by its smaller side, so the page keeps the design's proportions (`mdPageZoom.h`); the MD faders stop at 240 px and the master effects' screens take the height that is left, so the page ends at the window's bottom; the MM routing follows right under the strips. Checked in a browser harness from 864 x 554 to 3440 x 1440 and at portrait sizes, every workspace of both editors.
- **Status:** fixed for 0.3.2.

## B-003 · First start: the ROM loads twice

- **From:** Radek, 2026-10-07, 0.3.1 installed for the user, first start of the app.
- **What happens:** the start-up animation and the ROM loading run twice.
- **Should:** one load, one animation.
- **Cause:** not the settings migration. A Machinedrum that starts without its UW factory cache (`<data folder>/nvram/md-uw-1.63-factory-v2.cache`) and without a project that carries the sample flash first formats its sample flash, as the real machine does on its first start, and the processor then starts it again (`serviceFactoryInitialization`). The page showed that first run as a normal start, with its LCD, so the user saw two start-ups. The cache that should make this happen once per computer was never kept: the editor's status requests (the Song status is not on the read-only list) reached the machine while it formatted, which counts as outside use and disqualifies the capture. Reproduced with 0.3.1 and an empty data folder: one start-up animation, `[MD] factory flash preparation complete; rebooted in process`, a second animation, no cache; Radek's own data folder has never had one, so every start without a saved project did this (a new plug-in instance in a DAW, an app with no saved state). His own first-start log was overwritten by a later start; his migrated settings, started again with 0.3.1 in an empty data folder, restore with one start-up.
- **Fix:** the machine that formats its flash is "loading" for the page (the start-up card says Preparing…, without its LCD) and the editor does not talk to it, so the cache is kept; the start-up animation shown is the one after it. The next start has no preparation at all. Test: `mdFirstStartFirmwareTest` (needs the ROM).
- **Status:** fixed for 0.3.2.

## B-002 · The Windows editor window is empty (confirmed)

- **From:** the Linux/Windows release work, 2026-10-07 (not yet seen by a user).
- **Setup:** Windows x64 zip in 0.3.1.
- **What happens (expected):** JUCE 7's default Windows web view is the old Internet Explorer control, not WebView2. The editor pages use modern JavaScript, so the page most likely does not load.
- **Should:** use WebView2 (the WebView2 SDK in the Windows build), then check the page bridge (iframe navigations and `javascript:` URLs) on a real Windows machine.
- **Confirmed 2026-10-07:** a Discord beta tester on Windows 11 (Monomachine Editor) sees Internet Explorer's dialog "Error in the script on this page … Syntax error" for the page in `AppData/Local/Temp/gearmulator-mmStudio-….html`, then a black window.
- **Fix (branch `feat/windows-webview2`):** the editors drive WebView2 themselves (static loader, no extra DLL), the bridge goes over postMessage and ExecuteScript, no IE fallback (a missing runtime shows a message with Microsoft's download link). CI start test on Windows: both standalones and both VST3s open the page and round-trip with the plug-in.
- **Status:** fixed for 0.3.2; not yet tried in a real Windows DAW.

## B-001 · Plug-in window too big in Ableton Live

- **From:** Discord beta tester (versonegro), 2026-10-07.
- **Setup:** Ableton Live, macOS 12, Apple M1 (the "66 %" in the report was Live's CPU meter, see B-005, not a zoom). Machinedrum Editor plug-in, MIX workspace.
- **What happens:** the page is drawn larger than the plug-in window. The right part (track 13 onwards, the header's right side) and the bottom are cut off. The page cannot be zoomed out, so the settings cannot be reached.
- **Should:** the page fits the plug-in window at any host zoom, the window can be resized, and the user can zoom the page (a control and ⌘− / ⌘+).
- **To check:** how the window size and the web view's zoom follow the host's scale (Retina and non-Retina, WKWebView on macOS 12); `mdStudioWebZoom.mm`, `mdWindowFitTest`; the Monomachine Editor; Logic, Bitwig and Reaper.
- **More evidence (second screenshot, same tester):** the header LCD has no inner borders, the transport buttons are not boxed, and SETUP (UNDO/REDO/MKI) is cut off; on Radek's newer macOS all of it renders correctly.
- **Likely cause:** macOS 12's system WebKit (Safari 15 engine) does not support some CSS the page uses (e.g. container-query units, color-mix), so those rules are dropped. Fix: fallbacks for the oldest supported WebKit, and an honest minimum macOS version.
- **Update 2026-10-07:** the tester says the cut-off window was their own setting (the editor size is in the right-click menu). The missing LCD borders are still real (old WebKit, see above).
- **Cause, rendering:** the editor's web view is the system WebKit, the one the installed Safari brought; macOS 12 with Safari 15 is WebKit 15. Both stylesheets use `color-mix()` (WebKit 16.2) about 70 times (no container queries, no `@layer`, no nesting): WebKit 15 drops each declaration that has one, and the LCD's rule variable (`--lrule`, a mix) makes every LCD divider invalid; that is the second screenshot. `:focus-visible` (WebKit 15.4) drops whole rules that list it (hover states), and `structuredClone` (15.4) is missing for the Monomachine page. The play head (B-008) is the same cause.
- **Cause, size:** the window size was the tester's GUI Scale, but nothing kept it on the screen: a plug-in's editor opened at whatever size was set or last used (the standalone's included), larger than a 1440 x 900 screen, its right part, SETUP and resize corner off the screen; and the page had no zoom of its own.
- **Fix (branch `fix/plugin-window-fit`):**
  - `skins/shared/deskCompat.js`, first in both pages' `<head>`: where the engine lacks `color-mix()` or `:focus-visible` it rewrites the stylesheets before the first paint (each mix as the same `rgba()`, through channel variables beside each colour variable, so themes and plates still apply; `:focus-visible` as `:focus`), and adds `structuredClone`. A current engine keeps the stylesheets as written. Forced on (`?compat=force`) in Chrome it draws the MD page pixel for pixel like the original. `deskCompatTest.js` checks every mix in both stylesheets is rewritten. Left alone (an older WebKit skips them, nothing breaks): four `:has()` rules (WebKit 15.4) and `subgrid` (16) in alignment details, `scrollbar-gutter`, `overscroll-behavior`, `accent-color`.
  - A plug-in's editor is never larger than its screen (less 64 pt for the host's bars): `windowFit::fitPluginSize`, applied once the host shows the window, not stored as the user's size.
  - The page always fits its window: narrower than 1440 or shorter than 720 CSS px zooms it out, larger both ways zooms it up (`mdPageZoom.h`); where the web view has no `pageZoom` (macOS 10.15 and older) the page's CSS zoom does the same.
  - The page's own zoom: **Page Zoom** in the editor's menu (right-click on the page's header, the standalone's Editor menu) with Zoom In / Zoom Out / Actual Size and 50-200 % steps, and ⌘− / ⌘+ / ⌘0 on the page (`deskZoom.js`, the `pageZoom` command), remembered in the editor's config.
- **Minimum macOS:** 12 (Monterey), any Safari 15 or later. macOS 11 with Safari 15 should work the same (not tested); macOS 10.15 and older lack flex `gap` and `inset` (Safari 14.1) unless Safari was updated, and are not supported. The build's deployment target (10.13) is not a claim.
- **Status:** fixed for 0.3.2. To confirm with the tester: Safari's version (Safari > About Safari).
