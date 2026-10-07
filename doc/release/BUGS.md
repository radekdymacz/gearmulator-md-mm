# Bug log

*Reports from beta testers and users (Discord, email). GitHub issues are off on this
repository; the site sends reports to the contact address. Newest first. Each entry:
where it came from, the setup, what happens, what should happen, status.*

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
- **Status:** open, being fixed.
