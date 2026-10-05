# User journeys: features against journeys

Every user-facing feature of the two editors, from the release notes (doc/release: 0.1.0-alpha, 0.2.0, 0.2.1,
0.3.0), the P*-RESULT and MM-P*-RESULT docs and the pages' workspaces and key maps, with the journeys that
cover it and their last result. How to run them and how to add one: [FOUNDATION.md](FOUNDATION.md), Build and
check, User journeys. The journeys are `skins/mdStudio/mdDeskJourneys.js` and `skins/mmStudio/mmJourneys.js`
(names `md-…`, `mm-…`); a subset runs with `scripts/mdmm-journeys.sh md journey-seq-*`.

Last run: 2026-10-05, diagnostics build of `test/user-journeys` (Release, arm64), MD OS 1.63, MM OS 1.32B, with the
two bugs below fixed. Machinedrum 53 of 55 journeys ran and pass, 2 skipped (the editor window was covered by
another app, so the page drew no canvases); Monomachine 41 of 41 pass. Two journeys failed once in the full run
while a test build ran beside it (md-sound-copy-paste's undo read-back, mm-seq-grid-record's first RECORD) and
passed on the rerun: timing under load, not a product fault.

Status: **PASS**; **FAIL** with the bug (below); **SKIP** (needs the window on screen); **not covered** (reachable,
no journey yet); **not testable** (why). "—": the editor has no such feature.

## Coverage

| | Machinedrum | Monomachine | Both |
|---|---|---|---|
| Features in the inventory (one row a feature; "—" rows not counted) | 81 | 84 | 165 |
| Covered by a journey | 63 | 46 | 109 |
| of which PASS | 61 | 46 | 107 |
| of which FAIL (product bug) | 0 | 0 | 0 |
| of which SKIP here (window hidden) | 2 | 0 | 2 |
| Not covered yet (reachable through the page) | 7 | 28 | 35 |
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
| Tap tempo (T) | md-keys-tap-tempo PASS | not covered |
| Pattern ‹ › on the LCD | md-top-pattern-next PASS | mm-top-pattern-next PASS |
| A pattern picked while playing is queued, starts at the end | md-seq-queue-while-playing PASS | not covered |
| Plate MK1 / MK2 | md-top-plate PASS | mm-top-plate PASS |
| Workspace keys 1-5 | md-keys-workspaces PASS | mm-keys-workspaces PASS |
| ? list of keys | md-keys-help PASS | mm-keys-help PASS |
| Undo / Redo keys and Cmd+Z, Cmd+Shift+Z | md-top-undo-redo PASS | mm-top-undo-redo PASS |
| Undo works over the library | md-lib-kit-copy-paste-undo PASS | mm-lib-kit-copy-paste-undo PASS |
| Questions: start on Cancel, Esc answers Cancel | md-dialog-esc-space-cancel PASS | mm-dialog-esc-cancel PASS |
| Space presses the dialog's focused button (0.3.0) | md-dialog-esc-space-cancel PASS | not covered |
| No shortcuts behind dialogs: Delete, Backspace, digits (0.3.0) | md-lib-load-save-dialog PASS | not covered |
| Dialogs wait their turn (a plug-in question never lost) | not testable: needs two questions at once from the plug-in | same |
| Errors show once under the header, close with × | not testable: no refusal the page can cause on purpose | same |
| Notices per window with two windows open | not testable: one window per standalone | same |
| QWERTY keyboard plays the selected track | md-keys-play-notes PASS (the core takes the note) | mm-keys-play-notes PASS |
| Track select ↑ / ↓ | md-keys-track-select PASS | not covered |
| M mutes the selected track, Alt+M every track, 0 unmutes all | md-keys-mute PASS | not covered |
| Window fits the screen, remembers its size; top bar fits 1280 px | not testable: the window size is the host's | same |
| MIDI mapping (Control workspace, LEARN) | skipped: hidden by design until the controller feature exists | same |
| SysEx import / export | not testable: native file chooser | same |

## Sequence

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Trigs / notes by click (MM: piano roll) | md-seq-first-beat PASS | mm-seq-first-beat PASS |
| Drag-paint steps, one undo step (MM: SLIDE lane) | md-seq-paint-undo PASS | mm-seq-slide-paint PASS |
| Accent (Shift-click), slide (Alt-click) | md-seq-accent-slide PASS | — (MM: SLIDE lane, above) |
| Lock lane: pick a parameter, draw locks, clear | md-seq-lock-lane PASS | mm-seq-lock-lane PASS |
| Lock lane ramp (Shift-drag), erase (Alt-drag), wheel on a step | not covered | not covered |
| Pages: ALL, PAGE, [ ] | md-seq-pages PASS | not covered |
| Copy, paste, clear steps (Cmd+C, Cmd+V, Delete) | md-seq-copy-paste-clear PASS | not covered |
| Clear the whole pattern (Alt+Delete), undo | md-seq-clear-pattern-undo PASS | not covered |
| Every-N fill (Cmd-click) | md-seq-fill-every PASS | not covered |
| Rotate (Alt+← →) | md-seq-rotate PASS | not covered |
| Paste to many marked tracks (Shift-click headers) | not covered | not covered |
| Live recording (REC / Alt+Space) | md-seq-live-record PASS | not covered |
| GRID RECORDING (MM RECORD stopped) | — | mm-seq-grid-record PASS (see note 2) |
| LEN on LCD line 2 | — | mm-seq-len PASS |
| Arpeggiator dock | — | mm-seq-arp PASS |
| Clickable transpose keyboard | — | mm-seq-transpose-keyboard PASS |
| SYNTH / MIDI side switch | — | mm-sound-midi-side PASS (Sound) |

## Generators and mutation

| Feature | Machinedrum | Monomachine |
|---|---|---|
| GEN: roll a rhythm, one undo step | md-gen-mutate-undo PASS (bug 1, fixed); md-gen-defaults-mutate-undo PASS | mm-gen-mutate-undo PASS |
| GEN keys and values (R, Defaults, Hits) | md-gen-keys-r PASS | not covered |
| MUTATE a sound, one undo step | md-gen-defaults-mutate-undo PASS | mm-gen-mutate-undo PASS |
| MUTATE scope chips, amount | not covered | not covered |

## Sound

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Sound page grouped by function | md-sound-shape-undo PASS | mm-sound-shape-undo PASS |
| Drag a value, undo restores the kit | md-sound-shape-undo PASS | mm-sound-shape-undo PASS |
| A focused value: ↑ ↓ step it | md-sound-value-keys PASS (bug 2, fixed) | not covered |
| Machine picker, undo | md-sound-machine-pick-undo PASS | mm-sound-machine-pick-undo PASS |
| Copy / paste a sound | md-sound-copy-paste PASS | not covered |
| Screens (curve editors): drag a handle | md-sound-screen-drag PASS | not covered |
| Control All (Alt-drag), one undo step | md-sound-control-all PASS | mm-sound-control-all PASS |

## Mix

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Mute and solo; un-solo keeps the mutes (0.3.0) | md-mix-mute-solo PASS | mm-mix-solo PASS |
| Drag across M keys | md-mix-mute-solo PASS | not covered |
| Shift-armed mutes | md-mix-shift-mutes PASS | mm-mix-shift-mutes PASS |
| M/S off | md-mix-ms-off PASS | mm-mix-ms-off PASS |
| Volume / LEVEL fader, undo | md-mix-fader-undo PASS | mm-mix-level-mute PASS |
| Strip mute | md-mix-mute-solo PASS | mm-mix-level-mute PASS |
| PAN / trim boxes | covered by the fader journey's kind (not its own) | mm-mix-pan-undo PASS |
| Outputs: OUT key / routing mode | md-mix-out-route PASS | mm-mix-routing PASS |
| Master effects | md-mix-master-fx PASS | — |
| MIDI track mutes (MUTE window) | — | not covered (self-test p4 has it) |

## Song

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Arrange: add a pattern row, delete, undo | md-song-arrange-rows PASS | mm-song-rows PASS |
| Chain: pads chain at once, Clear ends it | md-song-chain PASS | mm-song-chain PASS |
| Song picker (any of 24), Load on the machine | — | mm-song-picker-load PASS |
| Row inspector (repeat, tempo, part, mutes), loops | not covered | not covered |
| Drag pads and rows on the grid | not covered: HTML drag and drop | not covered: HTML drag and drop |

## Sampler (Machinedrum) and Perform (Monomachine)

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Sample slots: browser of waveform tiles | md-sampler-slots SKIP (window covered: canvases not drawn) | — |
| Set up sampling (recorder, player), one undo step | md-sampler-setup-undo PASS | — |
| Audition a sample | md-sampler-audition SKIP (window covered) | — |
| Load a WAV / AIFF and audition it before keeping it | not testable: native file chooser | — |
| RAM view steps and length; capture / freeze | not covered | — |
| POLY | — | mm-perform-poly PASS |
| MULTI TRIG mode | — | mm-perform-multi-trig PASS |
| MULTI MAP: split and delete a range | — | mm-perform-multi-map PASS |
| Perform keyboard plays | — | mm-perform-keyboard PASS (the note reaches the plug-in; live notes have no read-back) |
| Joystick, assign rows, multi envelope, PORTAMENTO | — | not covered |

## Library and GLOBAL

| Feature | Machinedrum | Monomachine |
|---|---|---|
| Kit library: a click loads at once | md-lib-load-save-dialog PASS | mm-lib-kit-load PASS |
| Save as a slot, answering the overwrite question | md-lib-load-save-dialog PASS | not covered |
| Copy / paste a kit slot, undo | md-lib-kit-copy-paste-undo PASS | mm-lib-kit-copy-paste-undo PASS |
| Rename a kit slot (with its question), undo | md-lib-kit-rename-undo PASS | not covered |
| Clear a kit slot, undo | md-lib-kit-clear-undo PASS | not covered |
| Pattern chooser: a click switches | md-lib-pattern-go PASS | mm-lib-pattern-go PASS |
| Clear a pattern slot (with its question), undo | md-lib-pattern-clear-undo PASS | not covered |
| GLOBAL: a setting read back (TEMPO OUT) | md-global-tempo-out PASS | — (MM globals: the Perform and Mix journeys) |
| GLOBAL › ROUTING fits its dialog, a route read back | md-global-routing PASS | — |

## Bugs the journeys found (both fixed on this branch)

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

## Notes

1. The journeys play the person through synthetic events at an element's centre and refuse a covered element.
   What the browser does by itself on a focused button (Enter, Space click it) is done by the runner the same way.
2. Monomachine: PLAY (Space) and RECORD pressed while the plug-in has the panel in SYSEX RECV for an edit are refused
   with "The panel is busy (SYSEX RECV); try again." The journeys press again, as the toast says. By design, but
   seen right after an edit or an undo.
3. A hidden window (display asleep, covered) gets no animation frames and slow timers: the canvas journeys are
   skipped there; tap tempo checks the BPM against the taps' real spacing.
