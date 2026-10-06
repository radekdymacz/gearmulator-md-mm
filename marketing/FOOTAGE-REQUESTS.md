# Footage requests

*For the agent that owns the screen recorder and demo journeys
(`scripts/mdmm-demo-video.sh`, branch `feat/demo-recorder`) and for Radek's own
filming. Marketing never edits the recorder; it asks here. Each request names
the reel spec it feeds (`marketing/reels/specs/<id>.json`). Delivered footage
goes into the recorder's run folder; link it into `marketing/footage/` (gitignored)
under the file name the spec expects. Never commit video, ROMs or .syx files.*

## House rules for every recording

- Window 1440 x 810 pt at 2x (2880 x 1620), 60 fps, real emulator audio (as `demo-md-groove`).
- **Tempo 120 BPM** unless the request says otherwise (one beat = 0.5 s makes cut maths trivial). Swing off.
- **Press PLAY early** and keep it running: every action happens while the groove plays. The reels cut on bar lines, so
  **start each action on a bar line and hold 2 bars after it** before the next one (the cut needs a clean bar either side).
- Log every step in page.log with its demo time, as `demo-md-groove` does (step, caption, ms) — the spec's `inBeat`s are set from it.
- Cursor visible, moving at human speed; no tooltips left open; no other windows, notifications or menu-bar clutter.
- Source audio peak around -6 dBFS; the renderer normalises to -14 LUFS anyway, but avoid clipping.
- File dialogs: show only the file, not the home folder name or other personal folders (start the chooser in a neutral folder).
- No third-party content unless the request says so; sample files must be ours or CC0 (note the source in the run folder).
- Deliver: raw.mov, page.log, timeline.txt, a still per step.

---

> **Delivered 2026-10-06:** the full-song take `demo-md-full-20261006` (feat/demo-recorder, 74 bars with a bar→time timeline) covers FR-01, FR-02 and most of FR-05; it feeds `md-beat-from-scratch`, `md-gen-roll`, `md-sampler-glitch`, `md-mute-drop` and `md-chain-outro`. Link it as `marketing/footage/demo-md-full-20261006.mov` + `.timeline.json`. Still open: FR-03, FR-04/04b, FR-05 (Alt-R all tracks), FR-06, FR-07 (eight tracks), FR-08, FR-09.

## FR-01 · A beat from scratch → `md-beat-from-scratch` (Radek's #1)

File: `fr-01-md-beat-from-scratch.mov` · length ≈ 35 s · MD, Sequence workspace, MKI plate

1. Empty pattern (clear with ⌥ Delete or start a fresh one), a kit with a punchy kick/snare/hats, tempo 120. PLAY at bar 1 (silence is fine: the empty grid is the joke).
2. Bar 1: click in the kick, four-on-the-floor plus one ghost (steps 1, 5, 9, 13, 15) — fast clicks.
3. Bar 3: snare on 5 and 13, one ⇧-click accent.
4. Bar 5: select the closed hi-hat track, GEN Euclid: one R click (the hats appear).
5. Bar 7: clap, two toms, an accent or two (⌘-click every-second-step on a perc track is a good visual).
6. Bar 9: lock lane: draw a filter sweep (FLTF) on the hats or the bass drum with one drag.
7. Bar 11–13: hands off, full groove plays; camera can widen.

## FR-02 · Sampler glitch → `md-sampler-glitch` (Radek's #2)

File: `fr-02-md-sampler-glitch.mov` · length ≈ 40 s · MD, Sampler workspace

1. A playing pattern (e.g. the FR-01 groove) at 120.
2. Bar 1: Sampler workspace (key 4). Click **Set up sampling**: the centred card, recorder, then player.
3. Bar 3: arm and capture one bar of the groove into a slot.
4. Bar 5: the waveform tile appears; click it (and one neighbouring tile with a different waveform) so the browser of tiles is visible.
5. Bar 7: put the player on a track; per-step retrigger / sample start locks (slice feel) on 4–6 steps.
6. Bar 9: pitch locks on a few steps (up an octave, down a fifth), one filter lock.
7. Bar 11–15: hands off, the glitched loop plays.

Also need: 3 stills (setup card, tiles, RAM view) to set the camera regions.

## FR-03 · SysEx import, neutral → `md-sysex-import` (Radek's #3, paid cut)

File: `fr-03-md-sysex-import.mov` · length ≈ 30 s · MD

Prep: from a demo project, **Export SysEx…** to `mdmm-demo-kits.syx` (our own content), then open a fresh project.

1. 0–2 s: fresh project, Sequence workspace, stopped.
2. ~2 s: drag `mdmm-demo-kits.syx` onto the window (or Kit library → Import SysEx…).
3. ~5 s: the preview panel: hold 3 s so its list (what it holds / overwrites) reads.
4. ~9 s: import; progress; the kit library shows the new kits (hold 2 s).
5. Pattern chooser → pick the imported pattern → PLAY on a bar line; 6 bars of playing, one tab change to Mix at bar 4.

## FR-04 · SysEx import, Autechre's public 2008 backup → `md-sysex-import-ae` (organic only)

File: `fr-04-md-sysex-import-ae.mov` · length ≈ 40 s · MD first

Source file (Radek supplied; reference by path, **never copy into the repo**):
`~/Downloads/AE_LIVE_ELEKTRONS_BACKUP_010308/md010308.syx` (Machinedrum, live backup of 1 March 2008).
The import is known to work: `syxImportFileTest` parses both AE files byte-exact and the MD firmware reads back 222 of 224 documents (doc/modern-ux/P7-RESULT.md).

1. Fresh project, stopped. Kit library → **Import SysEx…**; in the chooser, navigate so only the folder `AE_LIVE_ELEKTRONS_BACKUP_010308` and the file name show (no home-folder path in frame).
2. Preview panel: hold 4 s (it lists kits, patterns, songs; globals off by default — leave them off).
3. Import; hold on the kit library with the imported names 2 s.
4. Pattern chooser: pick a full pattern; PLAY. Their tempo (not 120): note it in page.log. 8 bars of playing; one tab change to Sound at bar 4, slowly scroll through a kit.
5. No overlays or captions that name the artist in the recording itself; the credit is added by the reel's end card and caption.

**FR-04b** (follow-up post, same rules): Monomachine Editor, `~/Downloads/AE_LIVE_ELEKTRONS_BACKUP_010308/mm010308.syx`, Import SysEx, play a pattern 8 bars. File `fr-04b-mm-sysex-import-ae.mov`.

## FR-05 · Roll a new beat in one click (GEN) → `md-gen-roll`

File: `fr-05-md-gen-roll.mov` · length ≈ 25 s · MD, Sequence

1. A 16-track pattern playing at 120.
2. Bars 1, 3, 5, 7: **⌥-click GEN's R** (all tracks roll) — four clearly different grooves, each held 2 bars.
3. Bar 9: ⌘Z twice (back to roll 2), hold 2 bars; the UNDO key must be in frame when pressed.

## FR-06 · Sample into your Machinedrum → `md-sampler`

File: `fr-06-md-sampler.mov` · length ≈ 30 s · MD, Sampler

1. Stopped, Sampler workspace: the slot tiles. Click an empty slot.
2. Load a WAV (ours or CC0) via the file chooser; audition it (sound plays), keep it: the tile shows its waveform.
3. Assign it to a track; PLAY on a bar line; 6 bars with the sample in the groove.

## FR-07 · Mute 8 tracks with one drag → `md-mute-8`

File: `fr-07-md-mute-8.mov` · length ≈ 25 s · MD, Mix workspace, 16 busy tracks

1. Bars 1–4: full groove on Mix.
2. Bar 5: one drag across **eight** M keys (e.g. tracks 3–10): breakdown, hold 4 bars.
3. Bar 9: drag back on the downbeat (the drop), hold 2 bars.
4. Bar 11: drag across three S keys, then back.

## FR-08 · Monomachine piano roll → `mm-piano-roll`

File: `fr-08-mm-piano-roll.mov` · length ≈ 30 s · MM, Sequence, 120

1. A synth track with an empty page; PLAY.
2. Bar 1–3: drag-paint a bassline phrase (two or three drags), one slide, ⌥-drag erase one note.
3. Bar 5: play the QWERTY keys incl. black keys (W E T Y U) over it — on-screen keyboard reacts.
4. Bar 7–9: Perform workspace, joystick sweep, multi trig a few notes.

## FR-10 · YouTube walkthrough (16:9, 3 min) and FR-11 · Sampler in 5 minutes

Long-form, one journey each across the workspaces (Sequence, Sound, Mix, Sampler, Song); Radek adds the voice-over. Specs to follow once the reels' performance shows which features pull.

---

## FR-09 · Radek films: real Machinedrum beside the editor (HW MIDI) → `md-hw-midi`

**Only post if it works.** HW MIDI is beta and has not been tested on a real machine yet; this shoot is also that test. If anything misbehaves, file it and the reel waits.

Setup: the standalone app (HW MIDI is off in a DAW), engine menu → HW MIDI, AUDIO / MIDI (`,`) set to the MD's MIDI interface. Machinedrum audio out into the interface so the phone's sound or a line recording carries the real box. Phone vertical (9:16), 4K/60 if possible, locked exposure, on a tripod or a stack of books, desk lamp, no faces needed.

Shot list (each 4–6 s, hold still at the end of each move):

1. **Establishing:** laptop with the editor on the left, Machinedrum on the right, both in frame, pattern playing; the MD's LEDs running.
2. **Screen → box:** mouse click a trig in the editor's grid; cut to (or pan to) the MD's trig key lighting.
3. **Box → screen:** turn a knob on the MD (e.g. filter); the editor's value moves. Close-up on the screen.
4. **Mute drag:** drag across M keys in the editor; the MD's mute state changes (MD screen in focus).
5. **Kit load:** pick a kit in the editor's library; the MD's LCD shows the new kit name.
6. **Hands:** close-up of a hand on the MD's encoder with the editor softly out of focus behind.
7. **Wide closer:** both, groove playing, 6 s (end card audio).

Also record a clean line-in audio track of the MD for the same take (sync with a clap at the start). Deliver the phone files + audio into `marketing/footage/radek/` (gitignored). The reel spec for this is made after the shoot (phone footage is not a UI-region recording: camera keys then refer to the phone frame, set `footage.ui` to the video size and `uiScale` 1).
