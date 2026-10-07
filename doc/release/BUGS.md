# Bug log

*Reports from beta testers and users (Discord, email). GitHub issues are off on this
repository; the site sends reports to the contact address. Newest first. Each entry:
where it came from, the setup, what happens, what should happen, status.*

## B-004 · MIX faders look far too big at some window sizes

- **From:** the same Discord screenshot as B-001, 2026-10-07.
- **What happens:** on the MIX workspace the volume faders stretch very tall and the strips look oversized.
- **Should:** the page keeps sensible proportions at every window size; faders have a maximum height.
- **Status:** open, part of the window-size review.

## B-003 · First start: the ROM loads twice

- **From:** Radek, 2026-10-07, 0.3.1 installed for the user, first start of the app.
- **What happens:** the start-up animation and the ROM loading run twice.
- **Should:** one load, one animation.
- **Cause:** not the settings migration. A Machinedrum that starts without its UW factory cache (`<data folder>/nvram/md-uw-1.63-factory-v2.cache`) and without a project that carries the sample flash first formats its sample flash, as the real machine does on its first start, and the processor then starts it again (`serviceFactoryInitialization`). The page showed that first run as a normal start, with its LCD, so the user saw two start-ups. The cache that should make this happen once per computer was never kept: the editor's status requests (the Song status is not on the read-only list) reached the machine while it formatted, which counts as outside use and disqualifies the capture. Reproduced with 0.3.1 and an empty data folder: one start-up animation, `[MD] factory flash preparation complete; rebooted in process`, a second animation, no cache; Radek's own data folder has never had one, so every start without a saved project did this (a new plug-in instance in a DAW, an app with no saved state). His own first-start log was overwritten by a later start; his migrated settings, started again with 0.3.1 in an empty data folder, restore with one start-up.
- **Fix:** the machine that formats its flash is "loading" for the page (the start-up card says Preparing…, without its LCD) and the editor does not talk to it, so the cache is kept; the start-up animation shown is the one after it. The next start has no preparation at all. Test: `mdFirstStartFirmwareTest` (needs the ROM).
- **Status:** fixed on `fix/first-start-double-load`, not yet released.

## B-002 · The Windows editor window is probably empty

- **From:** the Linux/Windows release work, 2026-10-07 (not yet seen by a user).
- **Setup:** Windows x64 zip in 0.3.1.
- **What happens (expected):** JUCE 7's default Windows web view is the old Internet Explorer control, not WebView2. The editor pages use modern JavaScript, so the page most likely does not load.
- **Should:** use WebView2 (the WebView2 SDK in the Windows build), then check the page bridge (iframe navigations and `javascript:` URLs) on a real Windows machine.
- **Status:** open. The 0.3.1 Windows zip is marked "not tested".

## B-001 · Plug-in window too big in Ableton Live

- **From:** Discord beta tester (versonegro), 2026-10-07.
- **Setup:** Ableton Live, macOS 12, Apple M1, Live's zoom at 66 %. Machinedrum Editor plug-in, MIX workspace.
- **What happens:** the page is drawn larger than the plug-in window. The right part (track 13 onwards, the header's right side) and the bottom are cut off. The page cannot be zoomed out, so the settings cannot be reached.
- **Should:** the page fits the plug-in window at any host zoom, the window can be resized, and the user can zoom the page (a control and ⌘− / ⌘+).
- **To check:** how the window size and the web view's zoom follow the host's scale (Live's zoom, Retina and non-Retina, WKWebView on macOS 12); `mdStudioWebZoom.mm`, `mdWindowFitTest`; the Monomachine Editor; Logic, Bitwig and Reaper.
- **Status:** open. Radek could not reproduce it in Live with 0.3.1; being fixed and reviewed at many window sizes.
