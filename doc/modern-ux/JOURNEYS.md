# User journeys: features against journeys

Every user-facing feature of the two editors, from the release notes (doc/release: 0.1.0-alpha, 0.2.0, 0.2.1,
0.3.0), the P*-RESULT and MM-P*-RESULT docs and the pages' workspaces and key maps, with the journeys that
cover it and their last result. How to run them and how to add one: [FOUNDATION.md](FOUNDATION.md), Build and
check, User journeys. The journeys are `skins/mdStudio/mdDeskJourneys.js` and `skins/mmStudio/mmJourneys.js`
(names `md-…`, `mm-…`); a subset runs with `scripts/mdmm-journeys.sh md journey-seq-*`.

Last run: 2026-10-05, diagnostics build of `feat/mm-port` (Release, arm64), MD OS 1.63, MM OS 1.32B: Machinedrum
64/64 pass (the window on screen, so the sampler's canvas journeys ran too), Monomachine 71/71 pass (with the rotate
fix, MM-PORT-PLAN.md 2026-10-05; the run before it: 70/71, `mm-seq-roll-paint` found the rotate run holding the
commit). Earlier, on `test/user-journeys`: Machinedrum 60 of 62 (2 skipped, window covered), Monomachine 69/69.

Status: **PASS**; **FAIL** with the bug (below); **SKIP** (needs the window on screen); **not covered** (reachable,
no journey yet); **not testable** (why). "—": the editor has no such feature.

## Coverage

| | Machinedrum | Monomachine | Both |
|---|---|---|---|
| Features in the inventory (one row a feature; "—" rows not counted) | 82 | 86 | 168 |
| Covered by a journey | 71 | 76 | 147 |
| of which PASS | 71 | 76 | 147 |
| of which FAIL (product bug) | 0 | 0 | 0 |
| of which SKIP here (window covered) | 0 | 0 | 0 |
| Not covered yet (reachable through the page) | 0 | 0 | 0 |
| Not testable through the page (file chooser, hardware, DAW, hidden by design) | 11 | 10 | 21 |

## Start-up, ROM, engine

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Start-up card: covers the window, blocks input, mirrors the firmware LCD, goes when ready | md-boot-card PASS | mm-boot-card PASS |
| Starting without a ROM: card asks for the ROM, file chooser | not testable: needs the owner's ROM moved out of the ROM folder, and the native file chooser | same |
| LOAD ROM: the card shows the firmware in use, Close | md-engine-rom-card PASS | mm-engine-rom-card PASS |
| LOAD ROM replaces or removes the firmware | not testable: would replace the owner's ROM; native file chooser | same |
| HW MIDI with no machine: card "no machine answers", Use the emulator | md-engine-hw-no-machine PASS | mm-engine-hw-no-machine PASS |
| HW MIDI with a real machine (edits, SEND n, TRANSPORT ACCEPT) | not testable: real hardware | not testable: real hardware (the emulated peer is the firmware test's `hw`, not the page) |
| AUDIO / MIDI panel (`,` and the engine menu) | md-audio-panel PASS (opens, closes; devices not changed) | mm-audio-panel PASS |
| DAW: follows host tempo and transport, restores automation and setup | not testable: standalone only | same |

## Top bar, keys, dialogs

| Feature | Machinedrum | Monomachine |
|---|---|---|
| PLAY / STOP key, POSITION | md-seq-first-beat PASS | mm-seq-first-beat PASS |
| Space plays and stops | md-keys-space-play PASS | mm-keys-space-play PASS (see note 2) |
| Tempo: drag the BPM | md-top-tempo-drag PASS | mm-top-tempo-drag PASS |
| Tap tempo (MD: T or B; MM: B, since T is a black key there, 2026-10-05) | md-keys-tap-tempo PASS, md-keys-tap-tempo-b PASS | mm-keys-tap-tempo PASS (B) |
| Pattern ‹ › on the LCD | md-top-pattern-next PASS | mm-top-pattern-next PASS |
| A pattern picked while playing is queued, starts at the end | md-seq-queue-while-playing PASS | mm-seq-queue-while-playing PASS |
| Plate MK1 / MK2 | md-top-plate PASS | mm-top-plate PASS |
| Workspace keys 1-5 | md-keys-workspaces PASS | mm-keys-workspaces PASS |
| ? keyboard view (K-view, 2026-10-08: the drawn keyboard, the ⌥ layer, a key's words, the search; MM: the list until K7) | md-keys-help PASS | mm-keys-help PASS |
| Undo / Redo keys and Cmd+Z, Cmd+Shift+Z | md-top-undo-redo PASS | mm-top-undo-redo PASS |
| The editor's menu in the page (I-008): submenu by keys, Zoom, Updates › Check Daily | md-top-editor-menu PASS | mm-top-editor-menu PASS |
| Undo works over the library | md-lib-kit-copy-paste-undo PASS | mm-lib-kit-copy-paste-undo PASS |
| Questions: start on Cancel, Esc answers Cancel | md-dialog-esc-space-cancel PASS | mm-dialog-esc-cancel PASS |
| Space presses the dialog's focused button (0.3.0) | md-dialog-esc-space-cancel PASS | mm-dialog-keys-behind PASS |
| No shortcuts behind dialogs: Delete, Backspace, digits (0.3.0) | md-lib-load-save-dialog PASS | mm-dialog-keys-behind PASS |
| Dialogs wait their turn (a plug-in question never lost) | not testable: needs two questions at once from the plug-in | same |
| Errors show once under the header, close with × | not testable: no refusal the page can cause on purpose | same |
| Notices per window with two windows open | not testable: one window per standalone | same |
| QWERTY keyboard plays the selected track | md-keys-play-notes PASS (the core takes the note) | mm-keys-play-notes PASS |
| Black keys W E T Y U O P: the two rows play every semitone (2026-10-05) | — (white keys only) | mm-keys-black-keys PASS (the pitches sent: C♯ F♯ D♯, then A S still C D) |
| Track select ↑ / ↓ | md-keys-track-select PASS | mm-keys-track-select PASS |
| M mutes the selected track, Alt+M every track, 0 unmutes all | md-keys-mute PASS | mm-keys-mute PASS |
| Window fits the screen, remembers its size; top bar fits 1280 px | not testable: the window size is the host's | same |
| MIDI mapping (Control workspace, LEARN) | skipped: hidden by design until the controller feature exists | same |
| SysEx import | md-lib-syx-import (with GEARMULATOR_MDMM_SYX_FILE: the file the native chooser would give; kits, as from a cable, read back) | mm-lib-syx-import (the same, on SYSEX RECV) |

## Sequence

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Trigs / notes by click (MM: piano roll) | md-seq-first-beat PASS | mm-seq-first-beat PASS |
| Drag-paint steps, one undo step (MM: SLIDE lane; the roll: a new note dragged sideways paints, Alt-drag erases, 2026-10-05) | md-seq-paint-undo PASS | mm-seq-slide-paint PASS, mm-seq-roll-paint PASS (paint, erase, each one undo step) |
| Accent (Shift-click), slide (Alt-click) | md-seq-accent-slide PASS | — (MM: SLIDE lane, above) |
| Lock lane: pick a parameter, draw locks, clear | md-seq-lock-lane PASS | mm-seq-lock-lane PASS |
| Lock lane ramp (Shift-drag), erase (Alt-drag), wheel on a step | md-seq-lock-ramp-erase-wheel PASS | mm-seq-lock-ramp-erase-wheel PASS |
| Pages: ALL, PAGE, [ ] | md-seq-pages PASS | mm-seq-pages PASS |
| Copy, paste, clear steps (Cmd+C, Cmd+V; MD since K2: Delete with no selection does nothing, Clr clears the page) | md-seq-copy-paste-clear PASS | mm-seq-copy-paste-clear PASS |
| Select steps with ⌘-click / ⌘-drag (K2), the LCD's PASTE and CLR on the selection, ⌘D | md-seq-select-copy-paste PASS | — (K7) |
| The step menu (right-click a step, K3): accent, copy, paste here | md-seq-step-menu PASS | — (K7) |
| Cmd+C, Cmd+V, Cmd+Z as the operating system delivers them (real `NSEvent`s through AppKit's key path, `u.osKey`; B-015), standalone and VST3, 2026-10-08 | md-seq-os-copy-paste PASS (FAIL without the fix: no key reached the page) | not covered |
| A selected step copied and pasted with the mouse (the top bar's Copy, Paste, Clr), 2026-10-08 | md-seq-copy-paste-buttons PASS | — (no step selection yet) |
| Clear the whole pattern (Alt+Delete), undo | md-seq-clear-pattern-undo PASS | mm-seq-clear-pattern-undo PASS |
| Every-N fill (MD: the step menu since K3; MM: Cmd-click) | md-seq-fill-every PASS | mm-seq-fill-every PASS |
| Rotate (Alt+← →) | md-seq-rotate PASS | mm-seq-rotate PASS |
| A rotate run ends when Alt is seen up in any event: the next rotate is its own undo step (2026-10-05) | md-seq-rotate-undo PASS | mm-seq-roll-paint PASS (after mm-seq-rotate, its paint and erase are their own undo steps) |
| Paste to many marked tracks (Shift-click headers) | md-seq-paste-many PASS | mm-seq-paste-many PASS |
| Live recording (REC / Alt+Space) | md-seq-live-record PASS | mm-seq-live-record PASS |
| GRID RECORDING (MM RECORD stopped) | — | mm-seq-grid-record PASS (bug 4, fixed; see note 2) |
| LEN on LCD line 2 | — | mm-seq-len PASS |
| Arpeggiator dock | — | mm-seq-arp PASS |
| Arpeggiator RNGE to the machine's end (9 OCT, range 8: 0.3.5), kept | — | mm-seq-arp-range |
| Clickable transpose keyboard | — | mm-seq-transpose-keyboard PASS |
| SYNTH / MIDI side switch | — | mm-sound-midi-side PASS (Sound) |

## Generators and mutation

| Feature | Machinedrum | Monomachine |
|---|---|---|
| GEN: roll a rhythm, one undo step | md-gen-mutate-undo PASS (bug 1, fixed); md-gen-defaults-mutate-undo PASS | mm-gen-mutate-undo PASS |
| GEN keys and values (R, Defaults, Hits) | md-gen-keys-r PASS | mm-gen-keys-r PASS |
| MUTATE a sound, one undo step | md-gen-defaults-mutate-undo PASS | mm-gen-mutate-undo PASS |
| MUTATE scope chips, amount | md-gen-mutate-scope PASS | mm-gen-mutate-scope PASS |

## Sound

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Sound page grouped by function | md-sound-shape-undo PASS | mm-sound-shape-undo PASS |
| Drag a value, undo restores the kit | md-sound-shape-undo PASS | mm-sound-shape-undo PASS |
| A focused value: ↑ ↓ step it | md-sound-value-keys PASS (bug 2, fixed) | mm-sound-value-keys PASS (bug 3, fixed) |
| Machine picker, undo | md-sound-machine-pick-undo PASS | mm-sound-machine-pick-undo PASS |
| Copy / paste a sound | md-sound-copy-paste PASS | mm-sound-copy-paste PASS |
| Screens (curve editors): drag a handle | md-sound-screen-drag PASS | mm-sound-screen-drag PASS |
| Control All (Alt-drag), one undo step | md-sound-control-all PASS | mm-sound-control-all PASS |
| The GLOBAL key in the top bar (where FN was until 0.3.5): opens the panel, lit, Esc closes | md-global-key | — (the Monomachine Editor has no GLOBAL panel) |

## Mix

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Mute and solo; un-solo keeps the mutes (0.3.0) | md-mix-mute-solo PASS | mm-mix-solo PASS |
| Drag across M keys | md-mix-mute-solo PASS | mm-mix-drag-m-keys PASS |
| Shift-armed mutes | md-mix-shift-mutes PASS | mm-mix-shift-mutes PASS |
| M/S off | md-mix-ms-off PASS | mm-mix-ms-off PASS |
| Volume / LEVEL fader, undo | md-mix-fader-undo PASS | mm-mix-level-mute PASS |
| Strip mute | md-mix-mute-solo PASS | mm-mix-level-mute PASS |
| PAN / trim boxes | md-mix-pan-undo PASS | mm-mix-pan-undo PASS |
| Outputs: OUT key / routing mode | md-mix-out-route PASS | mm-mix-routing PASS |
| Master effects | md-mix-master-fx PASS | — |
| MIDI track mutes (MUTE window) | — | mm-perform-midi-mutes PASS |

## Song

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Arrange: add a pattern row, delete, undo | md-song-arrange-rows PASS | mm-song-rows PASS |
| Chain: pads chain at once, Clear ends it | md-song-chain PASS | mm-song-chain PASS |
| Song picker (any of 24), Load on the machine | — | mm-song-picker-load PASS |
| Row inspector (repeat, mutes), loops | md-song-row-inspector PASS | mm-song-row-inspector PASS |
| Drag pads and rows on the grid | md-song-drag-drop PASS | mm-song-drag-drop PASS |

## Sampler (Machinedrum) and Perform (Monomachine)

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Sample slots: browser of waveform tiles | md-sampler-slots PASS | — |
| Set up sampling (recorder, player), one undo step; the recorder samples the main mix at VOL 0 (out of the mix), an input gives VOL back | md-sampler-setup-undo PASS | — |
| Audition a sample | md-sampler-audition PASS | — |
| Load a WAV / AIFF and audition it before keeping it | not testable: native file chooser | — |
| RAM view steps; freeze / live | md-sampler-ram-view PASS | — |
| Chops right after Set up sampling keep RAM-R / RAM-P (B-025) | md-sampler-setup-chop PASS | — |
| POLY | — | mm-perform-poly PASS |
| MULTI TRIG mode | — | mm-perform-multi-trig PASS |
| MULTI MAP: split and delete a range | — | mm-perform-multi-map PASS |
| Perform keyboard plays | — | mm-perform-keyboard PASS (the note reaches the plug-in; live notes have no read-back) |
| Joystick, assign rows, multi envelope, PORTAMENTO | — | mm-perform-joystick-assign PASS, mm-perform-menv-portamento PASS |

## Library and GLOBAL

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Kit library: a click loads at once | md-lib-load-save-dialog PASS | mm-lib-kit-load PASS |
| Save as a slot, answering the overwrite question | md-lib-load-save-dialog PASS | mm-lib-kit-saveas PASS |
| Copy / paste a kit slot, undo | md-lib-kit-copy-paste-undo PASS | mm-lib-kit-copy-paste-undo PASS |
| Rename a kit slot (with its question), undo | md-lib-kit-rename-undo PASS | mm-lib-kit-rename-undo PASS |
| Clear a kit slot, undo | md-lib-kit-clear-undo PASS | mm-lib-kit-clear-undo PASS |
| Pattern chooser: a click switches | md-lib-pattern-go PASS | mm-lib-pattern-go PASS |
| Clear a pattern slot (with its question), undo | md-lib-pattern-clear-undo PASS | mm-lib-pattern-clear-undo PASS |
| GLOBAL: a setting read back (TEMPO OUT) | md-global-tempo-out PASS | — (MM globals: the Perform and Mix journeys) |
| GLOBAL › ROUTING fits its dialog, a route read back | md-global-routing PASS | — |
| Song page: PATTERN | SONG switch, lit from the machine's status (0.3.5) | md-song-mode | mm-song-mode |
| Song page: the song playhead (the row the machine plays, from RAM), What plays, PAT/SONG on the LCD (0.3.5) | md-song-playhead | mm-song-playhead |
| GLOBAL › Map a note: a note to a pattern of bank C-H and to STOP (the MAP EDITOR's 16-145, 0.3.5), read back | md-global-map-note | — |

## Bugs the journeys found (all four fixed on this branch)

1. **Machinedrum: GEN offers Keep for every track until Defaults is pressed** (md-gen-mutate-undo). The GEN specs
   are made once, at the first render (`genEnsure`, mdDeskGenUi.js), which happens before the kit document is in,
   so every track is still GND-EMPTY and gets Keep. A kick track's GEN bar shows Keep and R says "Track 1 is set to
   keep: nothing to randomise." The Monomachine page makes its specs again when a track's machine changes
   (76-gen.js, `genSpecs`) and does not have it. Log:
   `JOURNEY md-gen-mutate-undo 2/7 FAIL the GEN bar offers the track's generator, not Keep: screen: GEN mode shows keep for TRX-B2 (role kick); every track's spec: kkkkkkkkkkkkkkkk`
   Fixed: a spec the person never changed follows its track's machine (mdDeskGenUi.js `genEnsure`, `genSpecs`,
   `setGenSpec` marks a spec edited); mdDeskPageTest.js checks it.
2. **Machinedrum: a focused value loses the keys after its first step** (md-sound-value-keys). ↑ on a focused Sound
   value sends one step; the read-back re-renders the workspace, the focused box is replaced, the focus falls to the
   page, and the next ↑ selects the previous track instead (the key map says a focused value keeps ↑ ↓). Log:
   `JOURNEY md-sound-value-keys 3/4 FAIL focus an effects value and press ↑ three times: screen: shows 0, want 3 (from 0); selected track 16 (was 1); focus now BODY; machine: kit 2`
   Fixed: `render` (mdDeskRender.js) gives the focus back to the same value (by its data-g/n/t/f/src/li/gv) after
   the workspace is drawn again.

3. **Monomachine: a focused Sound value loses the arrow keys after one step** (mm-sound-value-keys), the same as
   bug 2 was on the Machinedrum: ↑ on a focused value sends one step, the read-back redraws the workspace, the focus
   falls to the page and the next ↑ selects another track. Log:
   `JOURNEY mm-sound-value-keys 3/4 FAIL focus a filter value and press ↑ three times: machine: kit 7, want 9 (from 6); selected 5; focus BODY`
   Fixed: `render` (mm-mockup/src/130-main.js) gives the focus back to the same value after the page is drawn
   again, as the Machinedrum's mdDeskRender.js does.
4. **Monomachine: a panel key pressed while the machine takes a dump on SYSEX RECV was lost** (mm-seq-grid-record).
   After edits and undos the desk parks the machine on GLOBAL › FILE › SYSEX RECV and sends the dumps. Measured
   (`mmDeskFirmwareTest <MM ROM> parked`): a key the machine gets while it is still taking a dump (about 40 ms for a
   pattern) is lost; once the dump is taken, RECORD, PLAY, STOP, the MUTE window and BANK GROUP all work on SYSEX
   RECV. The desk pressed RECORD in that window, answered ok, and the machine stayed at record off. Log:
   `JOURNEY mm-seq-grid-record FAIL step 1 "click RECORD (stopped): GRID RECORDING": machine: record off (seen off); results record:true ; recv parked -> idle; page: toast "GRID RECORDING on the machine: its TRIG keys write steps; the editor reads them back."`
   Fixed: `RecvSession::taking()` (mmDesk/mmRecv.h) says a dump sent is not taken yet (the machine's RECV count, messages
   and errors, has not caught up); `MmMachine::pressKeys` (mmDeskCommands.cpp) and `pressBankTrigs` (mmDeskChain.cpp)
   refuse then with "The panel is busy (SYSEX RECV); try again." (as while RECV enters or leaves), and
   `MmMachine::pumpRecv` (mmDeskDelivery.cpp) holds the next dump while the person's keys are on their way. mmDeskTest
   checks both (RECV session: a dump being taken; the desk against the scripted machine).

## Notes

1. The journeys play the person through synthetic events at an element's centre and refuse a covered element.
   What the browser does by itself on a focused button (Enter, Space click it) is done by the runner the same way.
2. Monomachine: PLAY (Space) and RECORD pressed while the plug-in has the panel in SYSEX RECV for an edit are refused
   with "The panel is busy (SYSEX RECV); try again." The journeys press again, as the toast says. By design, but
   seen right after an edit or an undo.
3. A hidden window (display asleep, covered) gets no animation frames and slow timers: the canvas journeys are
   skipped there; tap tempo checks the BPM against the taps' real spacing.
4. Pointer capture: a drag's moves and release go to the element the page captured (as the browser does), so a redraw
   during the drag (the lock lane's ramp) does not lose it.
5. A journey's Alt chord (`u.key(k, {alt: true})`) sends no keyup of Alt itself. The Monomachine page now ends a
   rotate run on any event without Alt as well (MM-PORT-PLAN.md 2026-10-05), so the journeys after `mm-seq-rotate` keep
   their own undo steps.
