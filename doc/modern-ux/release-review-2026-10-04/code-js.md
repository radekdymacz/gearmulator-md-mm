# Pre-release review: the page side (JS/CSS/HTML) of the MD + MM editors

Repo `audio/gearmulator-md-mm`, branch `main`, HEAD `855902a8c`. Read-only review. Paths are relative to
`source/elektron/md/mdJucePlugin/skins/` unless they start with `doc/`. The items fixed in
`doc/modern-ux/DESIGN-REVIEW-2026-10-02.md` (Status 2026-10-03) are not reported again.

## Test and check results

| Check | Result |
|---|---|
| `mdStudio/mdDeskModelTest.js` | PASS (0 failures) |
| `mdStudio/mdDeskGenTest.js` | PASS |
| `mdStudio/mdDeskKeysTest.js` | PASS (0 failures) |
| `mmStudio/mmGenTest.js` | PASS (0 failures) |
| `mmStudio/mmKeysTest.js` | PASS (0 failures) |
| `mmStudio/mmSoundTest.js` | PASS |
| `mmStudio/mmViewTest.js` | PASS (79 intent cases, 78 with writes) |
| `mmStudio/mmConvertTest.js` | PASS, 1313 checks (5 patterns, 3 kits, 3 songs, 2 globals). It needs a corpus: I made one with `temp/cmake_cifix/.../mmDataCorpusTest --json` (built Oct 3) on `elektronData/testdata/mm`. Run with no argument it prints usage and exits 2. |
| `shared/deskBridgeTest.js`, `deskGenTest.js`, `deskOverlayTest.js`, `deskTogglePaintTest.js` | all PASS |
| `doc/modern-ux/sync-mdstudio-skin.py --check` (runs `page_contract_check`) | "in step with the mockup", contract check clean |
| `doc/modern-ux/sync-mmstudio-skin.py --check` (runs `page_contract_check` and the seam checks) | "in step with the mockup", contract check clean |
| `doc/modern-ux/mm-mockup/build.sh` (run into scratch) vs the committed `index.html` | identical |
| `page_contract_check.py` on its own | It is a library plus a validator CLI (`schema def < json`). With no arguments it fails with `IndexError` instead of printing a usage line (see N9). |

The in-plugin self-tests (`mdDeskSelfTest.js`, `mmSelfTest.js`) need a diagnostics build and a ROM, so I did not run them.

No CI-level failure. Nothing I found is a **Blocker** (a crash, data loss without undo, or a broken main flow).
The highest items are Should-fix.

---

## Blocker

None found.

---

## Should-fix

### S1. MD solo: un-solo unmutes tracks the page did not mute, and M during a solo makes a track play while the page shows it silent
- **Where:** `mdStudio/mdDeskApp.js:270-293` (`applySolo`, `clickTrackKeys`), `mdStudio/mdDeskLive.js:18` (`muteSet`), `:69-73` (the new M/S paint `msOn`/`msSet`), `mdStudio/mdDeskComforts.js:117`.
- **Defect:** `S.userMutes` ("the mutes the user had set") is filled only by clicks on this page. Nothing seeds it from `machine.desk.mutes`. When nothing is soloed, `applySolo` sets every mute to `S.userMutes`. During a solo, M toggles the machine's mute itself (`setMute(i, !t.mute)`), while `audible()` reads only the solo.
- **Failure scenario:** (a) A project or pattern opens with tracks 3 and 7 muted on the machine. The user solos track 1, then un-solos it. Tracks 3 and 7 are unmuted on the machine, and their mutes are lost (mutes are not an undo step). (b) During a solo the user clicks M (or paint-drags M) on a non-soloed track. That track is muted because of the solo, so the click unmutes it on the machine. The machine plays it, but the rail and Mix still show it as off, because `audible()` checks only the solo. The MM page does not have this bug: there, `trk.mute` is the user's intent, the machine's mutes are not written over it during a solo (`130-main.js:430` `show`), and `HOST.mutes` sends `!audible`.
- **Fix:** Use the MM model. While no solo is active, take `S.userMutes` from the view's mutes, so it is always the machine's. During a solo, M only edits `S.userMutes` and then calls `applySolo()`. The M key shows `userMutes` while a solo is active.
- **Confidence:** high (I traced the code. There is no other writer of `userMutes`).

### S2. Page shortcuts fire inside open dialogs and panels: Space toggles the transport instead of pressing the focused button, and Delete/Backspace clears steps behind the dialog
- **Where:** `shared/deskKeys.js:12-20` (the dispatcher), `shared/deskModal.js:71` (it stops only keys whose target is outside the dialog), `mdStudio/mdDeskRender.js:156,158,160` (Space, 1-6, Delete/Backspace), `doc/modern-ux/mm-mockup/src/130-main.js:272,274,276`.
- **Defect:** The modal layer lets a key through when its target is inside the dialog, which is always the case because the modal keeps focus inside. The dispatcher does not check whether a modal is open. Bindings with no `when` (Space, the digits, Delete/Backspace in Sequence or Song, Cmd+Z/C/V, `[ ]`; on the MD also T and 0) then run and `preventDefault()` the key.
- **Failure scenario:** The plug-in asks "Load K05? Your edits are not saved…". Focus is on Cancel and the user presses Space to cancel. Play/stop toggles instead, and the button is not pressed (keydown was default-prevented). In the GLOBAL or AUDIO panel, Backspace on a focused button runs `secAction("clear")` in Sequence and clears the visible steps of the selected track behind the panel. Pressing 1-6 switches and renders the workspace behind a dialog. Both editors are affected, through the shared dispatcher.
- **Fix:** In `Keys`, skip every binding that is not `field`/`modal` when `Modal.top()` is set and the target is inside it, or let the modal layer `stopImmediatePropagation` every key except Tab, Esc, Enter, Space and the arrows when it is the dialog's own. Keep the library's own capture handler as it is.
- **Confidence:** high for Delete, the digits and transport behind dialogs. Medium-high that WebKit also suppresses the button activation for Space.

### S3. A plug-in notice (a question the C++ side waits on) can be overwritten by an ask, so its callback never runs
- **Where:** `mdStudio/mdDeskApp.js:215-221` (`onAsk` → `ask` writes `#dlg.innerHTML` unconditionally), `mdStudio/mdDeskRom.js:36-47` (`pumpNotices` waits for `#dlg`, but `ask` does not wait for it), `mmStudio/mmAdapter.js:334-339, 353-362` (`V().ask`), `doc/modern-ux/mm-mockup/src/60-ui.js:5`. C++ side: `mdPageEditor.h:59` `m_notices` (a callback map, cleared only by `noticeAnswer`).
- **Defect:** There is one dialog element. Notices queue behind an open dialog, but an `ask` message, the LOAD ROM remove question or `firstRun` replace whatever is showing, including a notice.
- **Failure scenario:** A notice is open (an upstream `MessageBox` routed to the page) and the user's command triggers an ask. The notice disappears and is never answered. The C++ callback stays in `m_notices` for the life of the window, and whatever the question was guarding never happens. The reverse also happens: a plug-in ask shown while a notice is pending is replaced when `pumpNotices` runs later, because `ask()` does not check.
- **Fix:** One dialog queue in the page, shared by `ask` and `pumpNotices`. If replacing a notice cannot be avoided, answer it first with its last (cancel) button (`noticeAnswer`).
- **Confidence:** medium. It needs two messages close together, and how bad it is depends on which notices exist.

### S4. MD: a render held by a gesture is not flushed when step paint, LCD line-2 drag, chop drag or song drag ends
- **Where:** `mdStudio/mdDeskRender.js:34` (sets `pendingRender` while `interacting()`), `mdStudio/mdDeskGestures.js:174-178` (`endPaint`), `:115` (l2 pointerup), `:48` (chop pointerup), `:39` (song `dragend`). Only `endDrag` (`:135`), `endMutePaint` (`mdDeskLive.js:100`), `closePicker` and `closeK` call `if (pendingRender) scheduleRender()`.
- **Defect:** `paint`, `l2`, `chop` and `song` are in `HOLDS`, so a document that lands during them only does `syncControls/renderTop/renderSub/redraw`. When those gestures end, the page redraws one row or the lane, not the whole view.
- **Failure scenario:** The user paints steps while the machine records live or a pattern read-back lands. Other tracks' rows, locks and the Song grid stay as they were drawn until some later document schedules a render. Any later `machine` message whose derived view equals `Base` will not schedule one.
- **Fix:** Flush in one place: `Held.end(kind)` (or a wrapper) calls `if (pendingRender) scheduleRender()`, and the per-gesture copies go.
- **Confidence:** medium-high.

### S5. MM: an engine reset keeps showing the previous engine's pattern and kit, and leaves `modInFlight` set
- **Where:** `mmStudio/mmAdapter.js:168-180` (`onReset`), `:78`, `:232-235`, `:242`. Compare `mdStudio/mdDeskLive.js:204-210`.
- **Defect:** (a) `onReset` calls `V().show(null, true)`, which writes nothing (`show` leaves out undefined members and does not render when `!doc`). It does not call `startEmpty()`. Until the new engine's current pattern and kit both arrive, the old tracks, steps and locks stay in `S` and on screen. (b) `modInFlight` is not cleared on reset or on a not-ready to ready change. The MD page clears it in both cases, and its comment says that after an engine change "its result will never come".
- **Failure scenario:** (a) The user switches from the emulator to HW MIDI with no machine attached (hwConnecting or hwLost). The grid still shows the emulator's pattern as if it were the hardware's. (b) A modSet is in flight when the engine changes. From then on `onMod` never takes the plug-in's setup, for example when a restored project brings one.
- **Fix:** In `onReset`, call `V().startEmpty()` (plus `markReading`), and set `modInFlight = 0`. Also clear it when `machine.input` goes from false to true, as the MD page does.
- **Confidence:** (a) medium-high, (b) medium. This depends on the core dropping results on reset, which the MD comment says it does.

### S6. MD: Shift-prepared mutes survive leaving the window
- **Where:** `mdStudio/mdDeskLive.js:16-35`. The MM page clears them on blur: `doc/modern-ux/mm-mockup/src/130-main.js:66`.
- **Defect:** `PREP` is applied on the Shift `keyup` and never cleared on `blur`.
- **Failure scenario:** The user Shift-clicks M on tracks 2 and 5, then Cmd-Tabs to the DAW (or clicks it) while still holding Shift. The keyup never reaches the page and the keys keep blinking. The next time Shift is released in the editor, for example during a Shift-click accent, tracks 2 and 5 are muted unexpectedly.
- **Fix:** Add `addEventListener("blur", () => { PREP.clear(); showPrep(); })`, as the MM page does.
- **Confidence:** high.

### S7. Unescaped text into `innerHTML`: sample file names and plug-in texts, and kit names inside ask messages
- **Where:** `mdStudio/mdDeskSampler.js:120-122` (`m.file`, `m.name`, `m.text`, `m.notes`), `:293` (`s.name`, which includes names the user typed via `SENT_NAMES`). Asks: `mdStudio/mdDeskApp.js:219-221` and `mmStudio/mmAdapter.js:338` put `m.message` into `innerHTML`. The core builds those messages with raw kit names: `mmDesk/mmDeskMachineParts.h:71-84` `kitLabel` (any non-zero byte), `mdDesk/mdDeskMachine.cpp:107-122` (printable ASCII, which still includes `<` and `&`).
- **Defect:** File names and kit names are not markup, but they are rendered as markup.
- **Failure scenario:** The user loads `kick<2>.wav`, renames an MM kit `A<B` with the page's own rename, or imports a .syx whose kit name contains `<…`. The text is garbled or swallowed. A crafted name such as `<svg/onload=…>` (14 or more characters, fits a 16-character MD name) runs script in the plug-in's web view, which can send any bridge command (`removeRom` included).
- **Fix:** Pages: escape `m.file`, `m.name`, `m.text`, `notes` and `s.name` (an `escH` exists in `mdDeskLibrary.js:13`; one shared `esc` would help). Core: escape kit and pattern names when it builds ask HTML, or send `{message, args}` and let the page escape the arguments. I am telling code-cpp about the C++ half.
- **Confidence:** high for the garbling. The script-injection path is plausible but needs a hostile file.

### S8. MD keyboard and shortcut gating has drifted from the MM page
- **Where:** `mdStudio/mdDeskLive.js:238` (`kbOn`: no AUDIO panel, keys list or picker check), `:127` (T, no `when`), `mdStudio/mdDeskComforts.js:124` (0, no `when`). MM: `doc/modern-ux/mm-mockup/src/75-comforts.js:102` (`kbOn` checks `AP.open`, `#machpop`, `#keyspop`), and T, 0 and M are all `when: kbOn`.
- **Failure scenario:** On the MD, with the AUDIO/MIDI or GLOBAL panel focused, A–L play notes. T taps the tempo and 0 unmutes and unsolos everything, also behind a dialog. On the MM the same keys do nothing.
- **Fix:** One `kbOn` rule for both pages, including `Modal.top()` (this also covers S2), and `when: kbOn` on T and 0.
- **Confidence:** high.

---

## Nice-to-have

### N1. MD: an accepted optimistic edit is not re-derived when its result comes
`mdStudio/mdDeskApp.js:102-107`: `onResult` renders only when the command was refused (`Overlay.answered(r.id) && !r.ok`). When it was accepted, `V` keeps the overlay values until some later render. If the document landed more than about 16 ms before the result, or the command changed no document, the page can keep showing the optimistic value instead of the machine's. One case: a knob turned during live recording, where the core writes a lock instead of the kit value. The MM page always calls `refresh()` on an answer (`mmAdapter.js:95-98`). Fix: `if (Overlay.answered(r.id)) scheduleRender()` on every answer (or `V = view()` plus a cheap compare). Confidence medium.

### N2. MM: refused commands toast twice, and refused notes toast on every press
`mmStudio/mmAdapter.js:216-219, 394-395, 423, 476` toast `r.errors[0]` in `onResult`, then `onMessage` (`:602`) toasts every refused `result` again. The visible text is the same, but it is two toasts and two log lines. `noteOn` refusals (for example "channel OFF") toast on every key press. The MD page says each one once (`kbTell`, `mdDeskLive.js:239-240`). Fix: drop the generic `result` toast for ids that have their own handler, and reuse a once-only "told" set for notes. Confidence high.

### N3. Library and GLOBAL panels redraw on every message during the background read
`mdStudio/mdDeskLibrary.js:141`: `if (LIB.open && (doc || machine)) setTimeout(drawLib, 30)`, one timer per message with no coalescing. `mdStudio/mdDeskGlobal.js:77` does the same. With the kit library open during the first read (64 kits, 128 patterns, songs, and many machine documents), that is hundreds of `drawKitLib()` string builds. They are cheap after the HTML comparison but pointless. Fix: one pending timer (`if (!t) t = setTimeout(...)`). Confidence high, impact low.

### N4. Clicking an M/S key leaves an open key-style dropdown (`#kpop`) open, and renders stay held
`mdStudio/mdDeskLive.js:103` and `doc/modern-ux/mm-mockup/src/130-main.js:91` eat the click that follows a mute paint in the window capture phase, so `mdDeskPicker.js:79` ("click outside closes") never sees it. `menuOpen()` keeps `interacting()` true, so document renders wait until the dropdown is closed by hand. Fix: call `closeK()`/`closePicker()` (and the MM equivalent) at the start of the paint. Confidence medium.

### N5. Wheel and arrow keys on a value make one undo step per notch or press (MD and MM)
`mdStudio/mdDeskGestures.js:143, 145`: no gesture id, so 20 wheel notches on a knob take 20 Undos. The lock wheel on a step already groups a run (`"wheel"` with a 700 ms window, `:75-76`). Fix: reuse that run pattern for value wheel and arrow runs. Confidence medium (it depends on whether the core coalesces).

### N6. Dead code and stale comments
- `shared/deskOverlay.js:81-101` `DocOverlay`: no page uses it, only `deskOverlayTest.js`. `mmStudio/mmView.js:4` still documents `DocOverlay.over(Docs)`. Delete it or mark it test-only.
- `mdStudio/mdDeskLive.js:186-203`: `showFwLcd(true)` is never called (P7 moved the boot LCD to the start-up card), so `drawFwLcd`, the `data-plate` MutationObserver, `#lcdfw`/`#lcdfwc` (`mdStudio.html:30`) and `.fwboot` CSS are dead.
- `doc/modern-ux/mm-mockup/src/130-main.js:482` `setLcd`: the second `if(bits&&bits.length>=1024){…canvas…}` block can never run, because the first `if` returns.
- `doc/modern-ux/mm-mockup/build.sh` `JS` lists `$SHARED/deskAudioSelfTest.js`, so the audio self-test ships in the release `mmMockup.js:2822`. CMake says self-tests ship "only with the diagnostics", and the MD page keeps it diagnostics-only. The demo host (`54-demo.js`) also ships, though it is inert under `MMHost`. Move the self-test into `DEMO` or a diagnostics-only part.

### N7. Name tests that remain
`mdStudio/mdDeskSampler.js:124` `recTrack`/`playTrack` match `t.m === "RAM-R" + n`. Finding 16 moved this kind of check to `machineFacts`. Fix: `machineFacts(m, Cat).recorder && slot[1] === n-1`.

### N8. Bridge: per-batch iframes and lost results
`shared/deskBridge.js:22-28`: one throw-away iframe per batch (up to about 60 a second during a drag, each kept for 1 s). Separate batches carry no sequence number, so the C++ side cannot detect or fix a navigation-policy reordering, for example a `noteOff` overtaking its `noteOn` or an older drag value landing last. I have not seen this happen. Messages already carry monotonic `id`s, so C++ could at least log when they arrive out of order. Also, `pending` (`:71`) keeps an entry for every command with `onResult` until its result. After a `reset` whose results never come, the entries stay and their `onDone` callbacks could fire late. Fix: clear `pending` on `reset`. Confidence low to medium.

### N9. `page_contract_check.py` run on its own
`doc/modern-ux/page_contract_check.py:319` raises `IndexError` with no arguments. Fix: print usage (`page_contract_check.py <schema> <def> < instance.json`). Also, `mmConvertTest` needs a corpus that only ctest builds. Mention it in FOUNDATION's test list, which currently suggests running the page tests with node directly.

### N10. Small things
- `mdStudio/mdDeskTop.js:191` (follow mode) calls `render()` directly during a value drag. Every other path waits for `interacting()`. Use `scheduleRender()`.
- `shared/deskModal.js:107`: a body-wide `MutationObserver` on `title` runs for every `el.title =` in `syncControls` (every control, on every drag move) only to move LCD tooltips. Scope it to `.lcdpanel`.
- `shared/deskCaps.js:44-46`: the card status line (MM MULTI MAP) is added but never removed when the capability comes back. It is harmless only if the card is always re-rendered.
- `mmStudio/mmAdapter.js:380`, `doc/modern-ux/mm-mockup/src/54-demo.js:20`: `start()` polls `setTimeout(go, 0)` with no limit if `MmConvert`/`MmView` failed to load, which busy-loops the page. Cap it and log once.

---

## Checked and fine (no finding)
- The new M/S drag (`shared/deskTogglePaint.js`, `mdDeskLive.js:55-104`, MM `130-main.js:67-91`): the click that follows is eaten correctly, a click without a move is one toggle, Shift, Alt and the keyboard click keep their own paths, both blur and a move with no button pressed end the paint, capability-marked keys are skipped (the MD checks `data-capna` on the key, the MM checks `closest("[data-na]")`), the `seen` set stops a key toggling twice, and rail and Mix share one key per track. The pure helper is fully tested. Not tested: the page wiring (`msKeyAt`, the eaten click).
- Overlay ownership under keyed merges, docOf scoping on pattern or kit switch, and `reset` clearing the overlay (MD).
- No listeners are added per render. The only timers that repeat are guarded (`toast`, `chainSoon`, `lcdSay`, `pumpNotices`). The waveform cache is bounded (8). The MD self-tests are not in a release bundle (empty `<script>` inlined).
- No `console.log`, `debugger`, TODO or FIXME in shipped page code. `Bridge.log` lines go nowhere in release (`WebPageHost::log` is diagnostics-only), but they still cost one iframe navigation each.
- Mockup-only paths in the MM page are all gated on `HOST.*` (library, kit keys, pattern switch, clock, first run, SysEx, boot).
