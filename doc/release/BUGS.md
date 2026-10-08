# Bug log

*Reports from beta testers and users (Discord, email). GitHub issues are off on this
repository; the site sends reports to the contact address. Newest first. Each entry:
where it came from, the setup, what happens, what should happen, status.*

## B-024 · Tempo drag fails when starting from a saved project

- **From:** the journeys (md-top-tempo-drag), 2026-10-08.
- **What happens:** the tempo drag does nothing when the editor starts from a saved project at 105.8 BPM; from fresh settings it works. Maybe host clock sync in the saved settings blocks it.
- **Cause:** not the product, and not the host clock (the saved global has tempo in INTERNAL; the drag reached the machine every time). The machine keeps the tempo in 1/24 BPM steps and the LCD shows it to one decimal. The journey compared the LCD's text with the tempo itself: from a saved project at 93.79 BPM (2251/24) the drag goes to 105.79, the LCD says 105.8, and 105.8 is not 105.79; a fresh 120.0 is on the 0.1 grid, so it passed. The same check was in `mm-top-tempo-drag`.
- **Fix (branch `fix/0.3.5-bugs`):** both journeys compare the LCD with the tempo as the LCD rounds it (`lcdBpm`), and the drag back with a tolerance below one step; the failure line now says the tempo, where it started and the global's tempo in. Checked from a saved project at 93.79 in both hosts.
- **Status:** fixed for 0.3.5 (journey only).

## B-023 · Undo after an Alt-drag doesn't restore every track

- **From:** the journeys (md-sound-control-all right after md-sound-value-keys), 2026-10-08, both hosts.
- **What happens:** after an Alt-drag (Control All), ⌘Z does not bring every track back; it looks like separate edits get merged into one undo step.
- **Cause:** not the undo grouping: the arrow keys are three steps and Control All one (traced in the core: each edit recorded with its gesture, the Alt-drag's merged into one step). ⌘Z never reached the plug-in. `md-sound-value-keys` leaves its value focused (a value is `role=slider`), and the key dispatcher (`deskKeys.js`) treated a focused slider like a text field: every shortcut without `field: true` was off, ⌘Z included. A person meets it too: a real click on a value focuses it (a synthetic one in the journeys does not, which is why the journey only failed after the arrow-key journey), so ⌘Z (and Space, ⌘C ⌘V, the digits) after dragging a value did nothing.
- **Fix (branch `fix/0.3.5-bugs`):** a focused value keeps only the keys that move it (arrows, Page Up/Down, Home, End: its own handler's); every other key is the page's. A text field keeps every key as before. Both editors (shared `deskKeys.js`). Tests: `deskKeysTest.js` (⌘Z, Space, ⌘C with a value focused; ↑ stays the value's), `md-sound-control-all` focuses the value it drags as a real press does (fails without the fix); its failure line lists what did not come back.
- **Status:** fixed for 0.3.5.

## B-022 · Doesn't work on Windows 10 with WebView2 installed

- **From:** several Discord users, 2026-10-08 (details still missing: editor version, what they see).
- **What happens:** the Windows editors "don't work" on Windows 10 although the WebView2 runtime is installed.
- **Not the cause:** the C runtime (the build links it statically).
- **To check:** the version used (0.3.2 still had the old IE engine; WebView2 came in 0.3.3); the WebView2 runtime version on those machines vs what our SDK (1.0.3856.49) needs — any newer ICoreWebView2_N interface we query may be missing on an old runtime; file:// loading of the page from %TEMP%; the user-data folder in %LOCALAPPDATA%\Gearmulator; the emulator itself (CPU features); the plug-in in a DAW vs the standalone. Add a startup log the user can send, and a message on screen that says what failed.
- **Done for 0.3.5 (branch `fix/0.3.5-bugs`), so the next report says what failed:**
  - **A start-up log every build writes**, for the user to send: `<data folder>\logs\editor-mdStudio.log` / `editor-mmStudio.log` (Windows: `Documents\Gearmulator Preview\<machine>\logs`; the start before kept as `editor-*-previous.log`): the editor's version, Windows version, standalone or the host's executable, the CPU, the WebView2 runtime found (`GetAvailableCoreWebView2BrowserVersionString`) against the oldest the editors need, the user-data folder, each HRESULT of the environment and the controller, the runtime the environment uses, `ICoreWebView2Settings3` missing (an older runtime), page loads and errors, the WebView2 process failing, and "page up" when the bridge answers. **Open Log Folder** in the editor's menu (right-click the header; the standalone's Editor menu).
  - **The window says what failed** instead of staying blank: no runtime, the environment or controller refused, the page failing to load, or the page's script not answering within 30 s; with what to do (install or update the Evergreen WebView2 Runtime, a button to its download page; send the log, with its path and an Open the log folder button).
  - **Older runtimes:** every WebView2 interface the editors use is in the first stable runtime (86.0.616) except `ICoreWebView2Settings3` (1.0.864, the browser's own keys off), which is asked for and skipped when missing: the page works, F5 and Ctrl+F stay the browser's. Minimum runtime: 86.0.616.0 (WINDOWS.md); an older one is marked in the log.
  - **Tests:** the Windows start test checks every run's log (the runtime's version, page up) and runs the Machinedrum standalone three more times: as an old runtime (`GEARMULATOR_MDMM_WEBVIEW2_TEST=old`: no `ICoreWebView2Settings3`, the page must still work), as a machine without one (`=fail`) and with a page that never starts (`GEARMULATOR_MDMM_PAGE_TEST=nostart`): the window must say "The editor page could not start" (UI Automation). Checked on macOS by hand (`nostart`: the log and the message).
  - **Found by the new log on CI's runner:** the standalone's first WebView2 controller is refused with `E_ABORT` (0x80004004): its parent window is made again while the standalone starts. It was retried only when the window changed again, so the page came up 25 to 35 s late on the runner; on a slower machine it may never have. A controller refused with `E_ABORT` is now made again at once in the new window (up to 10 times), and a failure message goes away if the page starts after all. A likely cause of the reports; to be confirmed by a tester's log.
- **Not verified:** a real Windows 10 machine, a runtime actually older than the SDK's (CI's windows-2022 has a current one). The cause on the testers' machines is still unknown: their logs will say.
- **Status:** diagnosable for 0.3.5; the cause waits for a tester's log.

## B-021 · Monomachine: PLAY refused as "panel busy" after edits, a pattern edit not read back while playing

- **From:** the 0.3.4 journeys (`mm-seq-first-beat`, both hosts), 2026-10-08, after B-014's stream was merged. Not in 0.3.3.
- **What happens:** after a few clicks on the piano roll, PLAY is refused ("The panel is busy (SYSEX RECV); try again") for many seconds; once playing, STOP is refused the same way and the page says "The machine did not read back the pattern that was sent".
- **Cause:** the Monomachine takes a dump only on SYSEX RECV, and a panel key pressed while it is still taking one is lost, so the desk refuses keys until the dump it sent is taken. With B-014 the dump the SYSEX RECV session sends waits in the stream (at cable speed while playing), so "taking" lasted far longer; and the session left SYSEX RECV after its idle time counted from when it handed the dump to the stream, before the dump had arrived, so the machine never took it.
- **Fix (release 0.3.4):** the session stays on SYSEX RECV while the stream still delivers (`pumpRecv`), and PLAY or STOP asked while the panel is busy are accepted and pressed once it is free (the newest wins, given up after 10 s; RECORD and the other keys are still refused as busy). Tests: `mmDeskTest` (PLAY then STOP while a dump is taken: STOP pressed once, after it), `mmDeskFirmwareTest` (the session leaves SYSEX RECV once the stream is quiet), the MM journeys (142 pass in both hosts).
- **Status:** fixed for 0.3.4.

## B-020 · A big SysEx import ends with "Push failed" errors and old patterns on the page

- **From:** the 0.3.4 import measurements (`mdDeskFirmwareTest <MD ROM> syximport`, a full backup imported as the session does it: four documents every 8 ms), 2026-10-08. Not reported by a user.
- **What happens:** after the import, about 130 "Push failed: the machine did not read back pattern …" errors, and the page shows the old content of about half the patterns. The same with 0.3.3's pacing (`GEARMULATOR_MDMM_EDIT_RATE=0`) and at any stream speed; importing one document at a time is fine.
- **Cause:** not the firmware: asked directly afterwards, it holds every pattern as in the file. Each pattern's read-back is asked for 750 ms after its push, so an import asks for hundreds at once; the firmware answers one dump at a time, the answers come seconds late, the desk gives each up after its timeout and shows a late or stale answer.
- **Fix (release 0.3.4):** the desks ask for at most two read-backs at a time (`deskCore::g_maxReadBacks`, `Pushes::pump`), and a read-back's clock starts once the editor's stream is quiet (`Pushes::restartAsked`). Measured: 224 of 224 documents read back equal, no errors, stopped and while playing; `syximport` checks the firmware directly too.
- **Status:** fixed for 0.3.4.

## B-019 · SysEx import says "wrong OS" for most of a backup

- **From:** Discord tester D, 2026-10-08 (Machinedrum, probably a public backup from an older OS).
- **What happens:** importing a SysEx file shows mostly errors saying the OS is wrong.
- **To check:** which OS versions' kit/pattern/song dumps we accept (only 1.63?), whether older dumps can be converted or imported partly, and that the message says what the file is and what to do.
- **Cause:** the import re-encoded each document as a "set" and left out whatever the model's `SyxTraits::fits` called another OS's format. On the Autechre 2008 backups (Machinedrum and Monomachine) that refused the MD's 8 globals (format 5.1) and, on the Monomachine, all 128 kits (an encoded-size check that RLE makes wrong), 31 patterns and the 8 globals: 167 of 288 items "wrong OS". The firmware takes every one of them.
- **Fix (branch `fix/sysex-import`, release 0.3.5, owner's direction):** the import is a MIDI cable: the file's messages go to the machine as they are, in file order, a couple at a time; the firmware decides; the editor then reads every document back and reports, per item, taken, converted, ignored, differs, changed, unknown or no reply. Left out up front, with the reason, is only what cannot be sent at all (another device's SysEx, broken framing, Elektron OS update packets). The preview informs (format, what it overwrites, what plays, no Undo); kinds and single items can be unticked. Measured on the firmware (`md/mmDeskFirmwareTest <ROM> syximport` with the Autechre backups): Machinedrum 232 of 232 items taken, Monomachine 288 of 288; the editors' own exports read back equal; over HW MIDI (`SYX_HW=1`, the Monomachine on SYSEX RECV and SEND) too. Tests: `syxImportTest`, `syxImportFileTest` (`MD_SYX`, `MM_SYX`), the `syximport`/`syxexport` firmware tests, the journeys `md-lib-syx-import`, `mm-lib-syx-import`. The data contract was widened to what the firmware accepts (the MD's global key map, the MM pattern arpeggiator length), so those files' values show and stay on the page.
- **Status:** fixed for 0.3.5.

## B-018 · "?" just beeps on the Mac (Monomachine)

- **From:** Discord tester C, 2026-10-08, M1, latest macOS.
- **What happens:** pressing ? (or ⇧/) gives the macOS "beep", as if no one took the key.
- **To check:** whether the web view has keyboard focus before the first click, whether the MM page binds ? at all, and the standalone vs plug-in path (see B-015).
- **Cause:** not the MM page (it binds ?), the window: whenever the standalone's window becomes the key window (at start, after switching back to it, or on the click that activates it), JUCE gives its own view the keyboard (`NSViewComponentPeer::becomeKeyWindow`: `makeFirstResponder:` on the peer's view). A key that view does not use goes up the responder chain to AppKit's beep, so keys pressed before a click inside the page never reached it (the Machinedrum Editor too). The journeys missed it: their window is never the key window.
- **Fix (branch `fix/0.3.5-bugs`):** the page's host hands the keyboard to the web view when the page is up, after the window became the key window (`NSWindowDidBecomeKeyNotification`, `mdWebFocus.h`), and when JUCE's focus lands on the window around the page (a host making the plug-in's view first responder); never away from another control of the window (a host's). Windows: the same moments move the focus into WebView2 (`MoveFocus`). ? on the Monomachine Editor now opens the keyboard view both editors share (`deskKeyView.js`) instead of the plain list. Tests: `mm-keys-os-help` and `md-keys-os-help` (real `NSEvent`s: `activate` does what the window does when it becomes key, then ? with no click; fail without the fix, both hosts); `mdOsKeys` refuses to send a key whose first responder is not the page and logs it, so the failing test makes no sound; `mm-keys-help` checks the drawn keyboard.
- **Status:** fixed for 0.3.5.

## B-017 · Monomachine Perform: dragging DEC resizes the window

- **From:** Discord tester C, 2026-10-08.
- **What happens:** in Perform mode, moving the envelope's DEC (and similar) on the left side makes the page grow downwards to fit the envelope, then it jumps back when the mouse is released.
- **Should:** the layout stays put while dragging.
- **Cause:** the envelope's screen was a canvas placed straight in the card's grid row with `height: 100%`. A canvas's drawing size (its width and height attributes, set on every redraw to the box times the display's scale) is its intrinsic size, which the row took into account: each redraw while dragging made the row, the card and the page a little taller; the render on release put a fresh canvas in, so the page jumped back.
- **Fix (branch `fix/0.3.5-bugs`):** the canvas sits absolutely in a box of its own (`.menvplot`), which takes the row; the canvas no longer sizes anything (as the Sound page's plots already did). Test: `mm-perform-menv-layout` drags DEC on the value and the DEC dot on the screen and samples the card's, the page's and the screen's heights every 10 ms (grew before the fix, steady after; both hosts).
- **Status:** fixed for 0.3.5.

## B-016 · Standalone can't go full screen; Settings does nothing (Monomachine)

- **From:** Discord tester C, 2026-10-08, Monomachine, M1, latest macOS (version not given).
- **What happens:** the window cannot go full screen; "Settings" does nothing.
- **To check:** the window's full-screen button / maximise in the standalone; Settings: B-007 fixed this in 0.3.2 (plug-ins have no Settings entry, the standalone's opens Audio/MIDI) — confirm the tester's version.
- **Cause:** JUCE's standalone window asks for the minimise and close buttons only. Without the maximise button the window had no full-screen behaviour on macOS (`NSWindowCollectionBehaviorFullScreenPrimary` needs it and a resizable window), and no maximise box on Windows and Linux. Had it gone full screen, the screen fit (P7) would have pulled it back: the window was fitted to the visible area (menu bar and Dock left out) after every resize, and its full-screen size would have been remembered as the user's. Settings: B-007 fixed the editor's menu; the macOS app menu still had upstream's "Settings..." (it opens the RmlUi settings page the web page hides: nothing happened).
- **Fix (branch `fix/0.3.5-bugs`):** the standalone's window asks for all three buttons (`standaloneApp.h`), so the green button goes full screen on macOS and Windows and Linux maximise; a window in full screen, maximised or minimised is neither fitted nor remembered (`EditorWindowFit::sizedBySystem`), and the page zooms to the window (`mdPageZoom.h`, from 0.3.2). The app menu shows "Settings..." only for an editor without its own audio and MIDI panel; the web-page editors have Audio/MIDI Settings... (the app menu and the Audio menu). Test: the macOS start test reads the window's title-bar buttons and the menus through the Accessibility API (`ui_probe chrome`): the full-screen button enabled, Audio/MIDI Settings... in the app menu, no "Settings...".
- **Not verified here:** full screen by hand on a Mac with a person (this Mac's shell may not use the Accessibility API; CI's start test reads it), Windows maximise and Linux by hand.
- **Status:** fixed for 0.3.5.

## B-015 · Cmd+C and Cmd+V do nothing (macOS: Ableton Live and the standalone)

- **From:** the owner, 0.3.3, Ableton Live on macOS (VST3 and AU), 2026-10-08.
- **What happens:** with steps selected, Cmd+C and Cmd+V do nothing. Cmd+Z, Cmd+D and the plain keys work.
- **Cause:** not the host. JUCE's web view on macOS (`juce_WebBrowserComponent_mac.mm`, `WebViewKeyEquivalentResponder`) catches Cmd+X, Cmd+C, Cmd+V and Cmd+A in `performKeyEquivalent:` and sends the edit commands `cut:`, `copy:`, `paste:`, `selectAll:` to the first responder instead of letting WebKit see the key. WebKit runs them as editing commands, so the page gets the document's `copy` / `cut` / `paste` events and never a keydown; the page's key map listened to keydowns only. The same in the standalone, in the VST3 host and in every DAW: the window, JUCE's components and the menu bar take nothing (measured with real `NSEvent`s: "taken by a view's key equivalent", first responder `WebViewKeyEquivalentResponder_…`). Cmd+Z and Cmd+D pass JUCE's responder and reach the page as keys. The page's own tests sent DOM key events inside the page, which is why nothing caught it.
- **Fix (branch `feat/select-shift`, its own commit):** the shared key dispatcher (`skins/shared/deskKeys.js`, both editors) takes a `copy`, `cut` or `paste` event outside a text field as Cmd+C, Cmd+X or Cmd+V (not twice when the keydown came first, as in a browser or WebView2). The mouse path was there already and is now tested: with steps selected, the top bar's Copy, Clr and Paste act on the selection. Tests: `deskKeysTest.js`; journeys `md-seq-os-copy-paste` (real key events through AppKit, `mdOsKeys.h`; fails without the fix in both hosts) and `md-seq-copy-paste-buttons`; the start tests of all three systems press real keys into the shipped standalones and VST3s and read the page's key probe (`GEARMULATOR_MDMM_KEYPROBE=1`).
- **Not proven here:** Live itself (the journeys and the start tests run in the standalone and the minimal VST3 host); Windows and Linux (no machine here; their start tests check it on the runners).
- **Status:** fixed for 0.3.4; real-key tests green on macOS 14/15, Windows and Linux.

## B-014 · Audio glitches while playing in Ableton (M1)

- **From:** Discord tester A, 2026-10-08, MacBook Pro M1, Ableton Live, 0.3.3.
- **What happens:** glitches (drop-outs) all the time while playing the Machinedrum Editor plug-in live; a recording of the same performance (Live's File Recorder) is clean. Upstream Gearmulator's Machinedrum on the same Mac runs at 65-70 % CPU and never glitches.
- **Also:** only while editing parameter locks (playing alone is fine); started with 0.3.3 (0.3.2 was fine); worse the more steps are edited.
- **Cause (measured, `mdDeskFirmwareTest <MD ROM> plocktiming`, retired instructions per block):** the whole emulation runs on the host's audio thread. A lock edit is a whole pattern dump (5.4 KB); while the firmware reads and applies one, the 68k is busy and its idle loop cannot be skipped, so the audio thread does about 1.5 times its idle work for 50-70 ms. The desk sent up to five dumps a second during a drag. 0.3.3's B-010 pacing (the dump read at 125 KB/s) made each push's busy time longer (46 -> 69 ms) and 25 % more work, so hot time went from about 230 to 345 ms a second and heavy host buffers came about four times as often: late buffers on a slower Mac. The emulator's cost does not grow with the number of locks; the page's per-edit pattern document does (2.2 -> 4.4 KB at 288 locks, about 55 a second while drawing).
- **Fix (branch `perf/audio-spikes`, release 0.3.4):** one stream for everything both editors send to their machine (`deskCore::Stream`, `deskCore/deskStream.h`; `mdDesk::SysexOut` in the MD desk), for every user action on the Machinedrum and the Monomachine, in lanes: dumps no faster than a MIDI cable carries them (3125 B/s: a pattern every 1.73 s, the newest dump of a document replacing a waiting one), nothing sent while the firmware reads and applies a dump (125 KB/s and 250 ms: no read-back over a dump), value SysEx (tempo, LFO, master effects, routing) at most every 100 ms per value, kit CCs, NRPN and mutes on a cable-speed budget (the newest value wins), notes passing waiting values; the TX LED lit while anything waits. The MD's live global edits (tempo, routing) are read back once at quiet, not at every value. B-010's 125 KB/s read stays. `GEARMULATOR_MDMM_EDIT_RATE` raises the rate (0: off, as 0.3.3). Measured (`scripts/mdmm-rt-check.sh`, every user action on both machines against a hot-time budget; table in `doc/modern-ux/DESIGN-edit-flow.md`): dumps in a 6 s lock drag 31 -> 5, hot time 257 -> 45 ms a second, heavy 128-frame buffers 21.7 -> 3.7 a second, 256-frame 8.5 -> 1.5; a tempo drag 71 -> 12 ms a second; `plocktiming-strict` passes. A lock change is heard at once for the first edit of a gesture, at most 1.8 s later during a drag. The page: the MD's step cells are no longer marked at every step while playing (the play head repainted 16 cells with their glows each step), WebKit's GPU process about half a core -> 6-10 % while playing.
- **Owner decision for 0.3.4:** the cable pace only while the machine's sequencer plays. Stopped there is no audio timing to protect, so the stream goes as fast as the machine reads: dumps back to back at 125 KB/s (as `md::Hardware` feeds them), a request still waiting for the read and the 250 ms settle after the last dump, the newest dump of a document still winning, values and value SysEx at once. Play starting mid-transfer slows the rest to cable speed; play stopping speeds the queue up. Measured with `mdDeskFirmwareTest <MD ROM> syximport` (a full backup imported as the session does it, emulated time): 128 patterns stopped 13.7 s (0.3.3's pacing with B-020's fix: 12.3 s), while playing 231 s (cable speed); with the settle between dumps too, 46.9 s. Every pattern is in the firmware as in the file in each case. Test: `mdDeskTest` (the mode switch).
- **Workaround (0.3.3):** a larger buffer in Live (512 or 1024 samples).
- **Should:** no drop-outs wherever upstream Gearmulator has none.
- **Status:** fixed for 0.3.4, not yet checked by the tester.

## B-013 · Monomachine "first beat" journey fails in the VST3 (host path)

- **From:** journeys in the minimal VST3 host, 2026-10-08.
- **What happens:** `mm-seq-first-beat` fails 5 of 5 in the VST3: the clicks reach the machine (trigs 0, 1, 16, 32, 48), but the piano roll shows only the first cell. The standalone passes.
- **Maybe:** the plug-in follows the host's clock and the test host gives no play position; to be checked in a real DAW.
- **Cause:** not the host. The VST3 starts from a factory machine (no saved state), the standalone from the person's saved one, so the pattern differs. On the factory pattern 1 of track 1 has notes on steps 1, 17, 33 and 49 only; the first click puts a note on step 2, and on a synth track a note's bar runs on to the next step that holds anything (step 17). The next clicks (steps 5, 9, 13, the same pitch) land on that bar, and a click on a bar takes the note (a pitch drag), so nothing is added: what the editor is meant to do, and what a person would see too. The clock is not involved: with the journey fixed, PLAY and STOP pass in the VST3. Turning the host following off (`followHost`) did not change the failure.
- **Fix (branch `fix/first-start-sampler`):** the journey picks cells that no bar of the clicked pitch covers and clicks them right to left, so a new note's bar never reaches the next cell. The test host needs no play position.
- **Status:** fixed (journey only), 2026-10-08.

## B-012 · First start on a new install: the Sampler has no samples

- **From:** journeys in the VST3 host on a fresh data folder, 2026-10-08 (`md-sampler-slots`, `md-sampler-audition`).
- **What happens:** on the very first start (the Machinedrum prepares its factory flash, about 17 s, then restarts in process), the samples document is never published, so the Sampler workspace stays empty. After a restart of the editor (the factory cache now exists) it works.
- **Where:** the sample scan after the in-process restart, `md::DeskDevice::scanSamples` (`mdLib/mddeskdevice.cpp`); related to the B-003 change in 0.3.2.
- **Should:** the Sampler fills on the first start too.
- **Cause:** not the scan. The page asks for the samples as soon as it is up, so the device reads them already from the machine that prepares its flash, and the desk publishes that bank. When the machine started again (in process), the desk sent the page a reset (the machine started over: the page drops every document) and read the kits, patterns, global and song again; the samples are not the core's documents, so the desk kept its bank, and the bank the new machine read was the same (the factory samples), so `mdDesk::Desk::onSampleBank` saw no change and published nothing. The page held no samples until the editor restarted. Any reset with an unchanged bank did the same.
- **Fix (branch `fix/first-start-sampler`):** a reset makes the desk forget what it holds beside the core's documents (`deskCore::Desk::onStartOver`; the Machinedrum desk: the samples), and a desk without a bank gets the device's current one again (`StudioLink::readSampleBank(_, again)`), so the page gets the samples after every reset. MM has nothing of the kind (its page's documents are all the core's). Tests: `mdFirstStartFirmwareTest` (the page ends the first start holding what the next start's page holds, the samples with 48 ROM slots among them), `mdDeskTest` (a reboot forgets the samples; the same bank is published again); journeys `md-sampler-*` pass in the VST3 host on a fresh data root.
- **Status:** fixed for 0.3.3.

## B-011 · A Monomachine DigiPRO voice goes silent at some MIDI input speeds

- **From:** the B-010 work, 2026-10-08 (test `mmDigiproFirmwareTest`).
- **What happens:** with the emulated MIDI input paced at 3.1 or 62.5 KB/s, the sixth DigiPRO voice stays silent; at 31, 125 and 250 KB/s it plays. Timing-dependent, cause unknown.
- **Now:** B-010's pacing uses 125 KB/s, which passes. Not seen by users.
- **Should:** every pacing rate plays all voices; understand the cause (possibly the same DSP1→DSP2 timing as joelanders' PR #98, "dropped Monomachine notes on tracks 4-6").
- **Status:** open.

## B-010 · Timing lags ("swing") while editing parameter locks

- **From:** Discord tester A, 2026-10-07, macOS 12, Apple M1.
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

- **From:** Discord tester A, macOS 12, 2026-10-07, with a screen recording.
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

- **From:** Discord beta tester A, 2026-10-07: Live's CPU meter at 66 % with the Machinedrum Editor, macOS 12, Apple M1.
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
- **Follow-up, 2026-10-08:** `mdSessionNoRomInstallFirmwareTest_md` and `mdSessionNoRomManageFirmwareTest_md` failed on main with "lifecycle still 'loading'". Not a stuck machine: a ROM installed from the start-up card into a data folder without the factory cache gets the same preparation (about 15 to 20 s of "Preparing the Machinedrum…", then one start, cache kept), and the tests looked 15 s after the first run took MIDI, inside it. The tests now wait for the preparation to end and check one preparation, the cache kept, REPLACE and REMOVE-then-reinstall without a second one; each run has its own temp folder (they raced under `ctest -j`).

## B-002 · The Windows editor window is empty (confirmed)

- **From:** the Linux/Windows release work, 2026-10-07 (not yet seen by a user).
- **Setup:** Windows x64 zip in 0.3.1.
- **What happens (expected):** JUCE 7's default Windows web view is the old Internet Explorer control, not WebView2. The editor pages use modern JavaScript, so the page most likely does not load.
- **Should:** use WebView2 (the WebView2 SDK in the Windows build), then check the page bridge (iframe navigations and `javascript:` URLs) on a real Windows machine.
- **Confirmed 2026-10-07:** a Discord beta tester on Windows 11 (Monomachine Editor) sees Internet Explorer's dialog "Error in the script on this page … Syntax error" for the page in `AppData/Local/Temp/gearmulator-mmStudio-….html`, then a black window.
- **Fix (branch `feat/windows-webview2`):** the editors drive WebView2 themselves (static loader, no extra DLL), the bridge goes over postMessage and ExecuteScript, no IE fallback (a missing runtime shows a message with Microsoft's download link). CI start test on Windows: both standalones and both VST3s open the page and round-trip with the plug-in.
- **Status:** fixed for 0.3.2; not yet tried in a real Windows DAW.

## B-001 · Plug-in window too big in Ableton Live

- **From:** Discord beta tester A, 2026-10-07.
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
