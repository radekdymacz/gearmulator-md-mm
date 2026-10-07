# Bug log

*Reports from beta testers and users (Discord, email). GitHub issues are off on this
repository; the site sends reports to the contact address. Newest first. Each entry:
where it came from, the setup, what happens, what should happen, status.*

## B-008 · The play head is invisible on the MK1 (white) skin

- **From:** Discord tester (versonegro), macOS 12, 2026-10-07, with a screen recording.
- **What happens:** on the Sequence page the moving play head does not show with the MK1 (white) plate; with the MK2 (black) plate it shows, but faintly.
- **Likely cause:** the play-head colour uses CSS the macOS 12 WebKit drops (`color-mix`, see B-001).
- **Should:** a clearly visible play head on both plates, on every supported macOS.
- **Status:** open, with the old-WebKit fix.

## B-007 · "Settings" in the right-click menu does not open

- **From:** the same tester, macOS 12, 2026-10-07.
- **What happens:** right-click on the editor opens the editor menu (inherited from Gearmulator); choosing its Settings entry does nothing.
- **Should:** every entry in that menu works in the web-page editors, or is removed; Settings opens the editor's own settings (or the menu offers only what applies).
- **Status:** open.

## B-006 · LEN (loop length) cannot be dragged

- **From:** the same tester asked how to loop only 16 steps, 2026-10-07; Radek expected LEN to be draggable.
- **What happens:** on the Sequence LCD, LEN steps 16 / 32 / 48 / 64 on click (⌥-click steps the length inside it, the mouse wheel works), but a drag does nothing, and the click behaviour is easy to miss.
- **Should:** LEN (and the other LCD values that step on click) also follow a vertical drag, like the other values in the editor; the tooltip says so.
- **Status:** open.

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
- **Status:** open, part of the window-size review.

## B-003 · First start: the ROM loads twice

- **From:** Radek, 2026-10-07, 0.3.1 installed for the user, first start of the app.
- **What happens:** the start-up animation and the ROM loading run twice.
- **Should:** one load, one animation.
- **Status:** open.

## B-002 · The Windows editor window is empty (confirmed)

- **From:** the Linux/Windows release work, 2026-10-07 (not yet seen by a user).
- **Setup:** Windows x64 zip in 0.3.1.
- **What happens (expected):** JUCE 7's default Windows web view is the old Internet Explorer control, not WebView2. The editor pages use modern JavaScript, so the page most likely does not load.
- **Should:** use WebView2 (the WebView2 SDK in the Windows build), then check the page bridge (iframe navigations and `javascript:` URLs) on a real Windows machine.
- **Confirmed 2026-10-07:** a Discord beta tester on Windows 11 (Monomachine Editor) sees Internet Explorer's dialog "Error in the script on this page … Syntax error" for the page in `AppData/Local/Temp/gearmulator-mmStudio-….html`, then a black window.
- **Status:** open, WebView2 fix in progress. The 0.3.1 Windows zip is marked "not tested".

## B-001 · Plug-in window too big in Ableton Live

- **From:** Discord beta tester (versonegro), 2026-10-07.
- **Setup:** Ableton Live, macOS 12, Apple M1 (the "66 %" in the report was Live's CPU meter, see B-005, not a zoom). Machinedrum Editor plug-in, MIX workspace.
- **What happens:** the page is drawn larger than the plug-in window. The right part (track 13 onwards, the header's right side) and the bottom are cut off. The page cannot be zoomed out, so the settings cannot be reached.
- **Should:** the page fits the plug-in window at any host zoom, the window can be resized, and the user can zoom the page (a control and ⌘− / ⌘+).
- **To check:** how the window size and the web view's zoom follow the host's scale (Retina and non-Retina, WKWebView on macOS 12); `mdStudioWebZoom.mm`, `mdWindowFitTest`; the Monomachine Editor; Logic, Bitwig and Reaper.
- **More evidence (second screenshot, same tester):** the header LCD has no inner borders, the transport buttons are not boxed, and SETUP (UNDO/REDO/MKI) is cut off; on Radek's newer macOS all of it renders correctly.
- **Likely cause:** macOS 12's system WebKit (Safari 15 engine) does not support some CSS the page uses (e.g. container-query units, color-mix), so those rules are dropped. Fix: fallbacks for the oldest supported WebKit, and an honest minimum macOS version.
- **Update 2026-10-07:** the tester says the cut-off window was their own setting (the editor size is in the right-click menu). The missing LCD borders are still real (old WebKit, see above).
- **Status:** window size: not a bug (discoverability: add ⌘− / ⌘+ zoom). Old-WebKit rendering: open, being fixed.
