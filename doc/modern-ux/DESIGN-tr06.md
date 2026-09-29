# Design: the Roland TR-06 controller profile

- Branch `feat/tr06-profile`, 2026-09-29. For both editors (the Machinedrum Editor and the Monomachine Editor), in the standalone apps and the DAW plug-ins.
- **Optional and off by default.** While it is off the input filter takes nothing, and the editor's state carries no controller chunk. Ordinary MIDI input, MIDI learn and DAW automation work as before. The filter is in the processor's path only while the profile is on or while the page shows the controller (see **On the page**); off and not shown, it is not in the path at all.
- **Nothing turns it on by itself.** A TR-06 among the MIDI inputs is shown and offered (USE TR-06), never switched on.
- **Sources:** Roland's TR-06 MIDI Implementation Chart v1.00 and the TR-06 Owner's Manual. **No TR-06 was connected while this was built:** every test uses simulated MIDI (see **Tests** and **On the hardware**).

## What the TR-06 sends (the chart)

- One global channel, 10 by default (1-16 or off), Mode 3. No Program Change, no SysEx.
- **Notes**, with velocity and note off: BD 36, SD 38, LT 47, HT 50, CY 49, OH 46, CH 42. It also receives 35, 40, 45, 48 and 44; the profile takes these as the same voices (BD, SD, LT, HT, CH).
- **CCs:** 12 MIX IN LVL, 17 OD DRIVE, 18 DELAY TIME, 19 DELAY DEPTH, 20-24 BD TUNE, ATTACK, COMP, DECAY, LEVEL, 25-29 SD TUNE, SNAPPY, COMP, DECAY, LEVEL, 49-51 LT TUNE, DECAY, LEVEL, 52-54 HT, 61-63 CH, 71 ACC LEVEL, 80-82 OH, 83-85 CY, 92 LT COLOR, 94 HT COLOR, the delay sends 96, 97, 103, 104, 107, 108, 109, 111 (BD, SD, LT, HT, CH, OH, CY, MIX IN), 113 MASTER PROB. CC 9 (shuffle) is received only.
- **Realtime:** Clock, Start, Continue, Stop, Song Position and Active Sensing. On MIDI OFFLINE it sends All Sound Off (CC 120) and Reset All Controllers (CC 121).

These facts are data: `source/elektron/md/deskController/tr06.json`, compiled in.

## The mapping (the defaults, in the same file)

Only messages on the profile's channel are the profile's. Everything else is left alone.

| TR-06 | Machinedrum | Monomachine |
|---|---|---|
| BD, SD, LT, HT, CY, OH, CH | tracks 1-7 | BD-CY tracks 1-5; OH and CH track 6. Pitch C4 (60), OH C5 (72) |
| INST LEVEL knobs (24, 29, 51, 54, 85, 82, 63) | the selected track's synthesis parameters 1-7 | SYN A-G |
| ACC LEVEL (71) | NOTE: the track's machine's pitch parameter (the first synthesis slot named PTCH, PITCH or TUNE); nothing on a machine without one | NOTE: the note the voices on the selected track play, in semitones |
| OD DRIVE (17) | DIST | filter BASE |
| DELAY TIME (18) | DEL send | filter WIDTH |
| DELAY DEPTH (19) | REV send | the track level (CC 7) |
| every other knob | not mapped | not mapped |
| Clock, Start, Continue, Stop, SPP | passed on unchanged: the machine's own sync settings decide (MD TEMPO IN / CTRL IN, MM CLOCK IN / TRANSPORT IN). The profile never changes them | same |
| All Sound Off, Reset All Controllers, Active Sensing; any other message on the channel | blocked | blocked |

The user can change every voice's track (and the MM pitch), every knob's target (NOTE too, on any knob), the channel and the knob mode. **Reset to defaults** restores the table, channel 10 and the relative knob mode; the profile stays on or off.

**The knob mode** (per profile, kept with the mapping; the TR-06's default is relative, `knobMode` in `tr06.json`):
- **Relative.** The TR-06's knobs send their absolute position (0-127). Per CC the value it sent last is the reference; the next value moves the target by the difference, from the value the target has now on the selected track (held at 0-127), never to the knob's position. The first value after a start, a track change, a mapping or mode change, or 2 s without that CC only sets the reference: nothing moves. Changing the selected track moves nothing. `deskController::RelativeKnobs`.
- **Absolute.** The value jumps to the knob's position (the first build's behaviour).

**NOTE** (a target like any other, `{pg: 8, i: 0}`, CC 71 by default):
- Monomachine: relative, a change transposes the stored `note` of the voice rows on the selected track, in semitones, held at 0-127 (absolute: the knob's value is the note). It is a mapping change (kept in the MDCT chunk, the route follows). A note that is sounding keeps its note-off: the input sends the note-off to the note its note-on went to (`deskController::Input`, `m_sounding`). The monitor shows `CC 71 → NOTE T1 60→62`.
- Machinedrum: the TRIGs have no pitch, so NOTE is the selected track's machine's pitch parameter (`mdPitchParam`: the first synthesis slot named PTCH, PITCH or TUNE); a machine without one shows "no pitch on this machine" and NOTE does nothing. `deskController::knobAction` decides.

**Where a voice goes on the Machinedrum:**
- **A real Machinedrum** (the HW MIDI engine): the base channel, at the first note that the active global's keymap gives the track.
- **The emulator:** it cannot take a note there. Upstream's emulator turns every note from 36 to 51, on any channel, into a press of TRIG key `note - 36` (`mdhardware.cpp`, `pumpScheduledMidi`). Keymap notes would press the wrong keys (track 2's note 38 would press TRIG 3). So on the emulator a voice goes as its track's TRIG note, 36 + track.

As a result, **with the profile off**, a TR-06 on any channel already presses the emulated Machinedrum's TRIG keys 1-15. SD would play track 3. `mdTr06FirmwareTest` measures this.

**Where the knobs act:** on the page's selected track. The Monomachine page sends the synth track with the selected track's number: a MIDI track counts as the synth track with its number.

## The data contract

- **The setup** (`desk/controller`): `{schema, version 1, machine "md"|"mm", profile "off"|"tr06", channel 1-16, knobMode "relative"|"absolute", voices [{voice, t, note?}], knobs [{cc, pg?, i}]}`. Unmapped knobs are left out. NOTE is `{pg: 8, i: 0}` on both machines (the MD writes `pg` only for it). A setup without `knobMode` is relative.
  - Kept with the editor's state as the **MDCT** chunk (`DeskHost`). The chunk is written only once the user changes something, so a project that never used the profile is byte for byte what it was.
  - The DAW's project and the standalone app's saved state both restore it. A project without the chunk starts with the profile off.
- **Commands** (deskHost's table, schema regenerated):
  - `ctlSet {profile?, channel?, knobMode?}`
  - `ctlVoice {voice, t, note?}`
  - `ctlKnob {cc, pg? 0-8, i|null}` (pg 8, i 0: NOTE)
  - `ctlReset`
  - `ctlTrack {t}`: the page's selected track. The MD page sends it on every selection and after ready. The MM page sends it through `HOST.selected` after a render.
  - `ctlWatch {on}`: the page shows the TR-06's view (on) or not (off). While on, the document carries `activity`. A page that goes (its window closes) stops it.
  - `ctlClear`: empties the MIDI monitor (its CCs and notes).
- **The page's document:** `{"type":"controller","doc":…}`, `$defs/controller` (the same text in both schemas, which `deskControllerTest` checks). It carries:
  - the setup;
  - `profiles`, `tracks`, `selected`;
  - `known` (whether the machine's global has been read);
  - each voice's `out` ({ch, note} now, or null);
  - `knobMode`, and `machineName` (the machine on the selected track, "SWAVE-SAW");
  - every knob with its target's `name` (the slot: "SYN H") and `own` (the machine's own name for it on the selected track where it differs: "TUNE"; "PTCH" for the MD's NOTE, "no pitch on this machine", "-" for a slot the machine does not use; ASCII, the page joins them: "SYN H · TUNE"), and the `targets` to choose from, named the same way. So it is plain that CC 71 moves TUNE on that machine;
  - `warning`;
  - `last` (the knob the TR-06 moved last);
  - `named` (the editor sees its MIDI inputs by name: the standalone app; in a DAW the host owns them), `inputs` (every MIDI input, `{name, on, tr06}`, enabled or not; null in a DAW: the CONTROL workspace's tiles) and `device` (the enabled MIDI input whose name has TR-06 or TR06, any case, or empty; looked at once a second);
  - `activity` (null unless the page watches): `{seq, last: {kind note|off|cc|other, n, v} | null, elsewhere: 1-16 | null, ccs: [{ch, cc, v}], notes: [{ch, n, v}]}`. `last` is the last channel message on the TR-06's channel, `seq` grows with every change, `elsewhere` is a channel that got messages in the last 2 s while the TR-06's channel got none, `ccs` every CC seen since the view opened or CLEAR (the TR-06's channel first, then the others, each by CC number, with its latest value), `notes` the last 8 note-ons, newest first.

  It is published after ready, after every `ctl*` command and when the route changes. While knobs move, it is published at most 4 times a second. While the page watches, what arrives is looked at every 66 ms and published only when it changed (at most about 15 times a second).

## Routing

- **The input filter.** `pluginLib::ExternalMidi` (ours, `externalMidi.h`) has an input filter. Its `takeIn`, which upstream's processor already calls for every event, offers each host or port event to the filter first. No upstream file changed; `scripts/mdmm-upstream-footprint.sh` lists the same 17 files.
- **`CtlInputFilter`** (`mdCtlProfile.*`) is installed only while the profile is on. It runs on the MIDI input threads, with no lock and no allocation (`deskController::Input`, atomic tables):
  - A **voice** becomes the machine's note in the same block, at the same offset. It goes into the emulated device, or, on the HW MIDI engine (external MIDI on), to the host's MIDI out and the ports with the audio block (a lock-free queue). A note off goes where its note on went, even if the mapping changed in between.
  - A **knob** keeps only its newest value.
  - Blocked messages go nowhere. Nothing is ever sent back to the TR-06.
- **Knobs into the edit path.** Every 8 ms step, the session (`ControllerProfile`) takes the knobs' newest values; `knobAction` says what each does on the selected track (a parameter, the MM's NOTE, nothing). Relative, `RelativeKnobs` turns a value into a change (latest wins on the input thread loses nothing: the changes between two looks add up to the last value minus the reference). `deskController::KnobPump` then paces them as the Control All pump does, with one way in per knob mode (`in`: a value, latest wins; `add`: a change, the changes of a round add up):
  - at most one round every 50 ms;
  - absolute: the newest value of each target; relative: the round's changes from the value the target got last in this gesture, else the working kit's (`current` hook), held at 0-127; a target whose value is not known gets nothing;
  - no repeat of the value a target just got;
  - one undo step per burst (400 ms of quiet ends it).

  A round becomes the page's own commands:
  - MD: `param` or `level` on the kit that plays, sent as a CC;
  - MM: one working-kit `set`, of which the adapter sends only the changed values as CCs.

  A single parameter never causes a dump. With the HW MIDI engine the same commands go over the wire, because the path goes through the adapter, not the emulator.
- **What arrives (the activity).** While the page watches, the filter is in the path with the profile off too, but it only counts: `deskController::Monitor` keeps each channel's last channel message and a count, every CC's latest value per channel (a fixed 16 x 128 table of atomic bytes: 0x80 | value) and the last 8 note-ons (a ring of atomic words), relaxed atomic stores on the MIDI thread, no lock, no allocation. The message thread clears it (a new look, CLEAR) and reads it every 66 ms. The session reads it (`deskController::Activity`) on the message thread. Clock, realtime and SysEx are nobody's channel and are not counted. The name of the input a message came from is not known there (the standalone's player merges the enabled inputs into one host stream), so the "TR-06 is sending on CH x" line uses the device name only to say that a TR-06 is enabled.
- **The standalone app's MIDI inputs reach the filter.** JUCE's standalone holder registers its `AudioProcessorPlayer` as the callback of every enabled MIDI input; the player hands the messages to `processBlock` as host MIDI, which offers each to `ExternalMidi::takeIn` and so the filter. `mdDeskSetupStateTest` runs that player on a stand-in device.
- **The route follows the machine.** Every 250 ms the session reads the route from the active global the desk holds: the MD's base channel and keymap, the MM's base channel and span. Until a global is read, voices are dropped, never passed raw.
- **The overlap warning.** The panel warns when the TR-06's channel is one the machine listens on:
  - MD: base to base + 3;
  - MM: base to base + span - 1, and AUTO, MULTI TRIG and MULTI MAP.

  The profile takes every message on its channel, so other gear on that channel stops reaching the machine.

## On the page

- **The CONTROL workspace** (MM tab 6, MD Control) opens on the **MIDI devices**: one tile per MIDI input (inline SVG icons in the page's colours).
  - The standalone app: every input of AUDIO / MIDI, enabled or not (a disabled one is dimmed and says NOT ENABLED). An input whose name is the TR-06's gets the TR-06 tile, with the profile's LED: lit while on, dim and steady while a TR-06 is found and the profile is off, dark otherwise. With the profile on and no TR-06 connected, a TR-06 tile says NOT CONNECTED. Without any other input a MIDI Learn tile keeps the mapping matrix reachable.
  - A DAW (no names): Host MIDI in and the TR-06 tile, always (the profile is reached there and nowhere else).
- **A tile opens its view** in the workspace, with ‹ DEVICES to go back:
  - **the TR-06:** the profile, CH, KNOBS (relative, absolute), RESET TO DEFAULTS; the found-TR-06 line (USE TR-06), the other-channel line (SET CH), the overlap warning; the **MIDI monitor**; the voices' tracks (MM: notes) and the knobs' targets with what each moves now.
  - **any other device:** the page's own mapping matrix (MIDI Learn, App LFO / Random), exactly as before. MIDI Learn listens to every input; its data and commands are unchanged, only its place moved.
- **The MIDI monitor** (the TR-06's view): a table of every CC received, sorted by CC number, the TR-06's channel first, other channels after it labelled with their channel ("CH 3"): the value, the TR-06's chart name ("BD LEVEL"), and what it moves on the selected track ("→ T1 SYN H · TUNE", "→ NOTE T1 60→62", "not mapped", "not a TR-06 knob", "not the TR-06's channel (10)"); above it the last notes with velocity and voice ("NOTE 36 · 100 (BD)"). It stays while the view shows; CLEAR empties it. **Nothing blinks or animates:** values update in place, at most about 15 times a second. Rows are dimmed while the profile is off.
- **Removed:** the bar under the mapping matrix's heading, the TR-06 MAP… key and its panel (and its entry in the MODAL layer). CONTROLLER… left the engine menu before.
- `deskController.js` (both pages) draws the workspace: the MD page's `renderControl` and the MM mockup's (through `HOST.controlView(learnHtml)`, `mmAdapter.js`) hand it their mapping matrix; it redraws its view itself when the document changes and asks the page to render again (`Ctl.rerender`) when a tile or DEVICES changes the view. Every select has a stable id (`ctl-profile`, `ctl-channel`, `ctl-knobmode`, `ctl-voice-<voice>`, `ctl-knob-<cc>`): the pages' key-style dropdowns find their select by id.

## Connecting a TR-06

- **Standalone app.** Connect the TR-06 by USB. Roland's manual says to install Roland's TR-06 USB driver on the computer (roland.com/support). Enable the TR-06 under AUDIO/MIDI › MIDI inputs, go to the CONTROL workspace and open the TR-06 tile: it says TR-06 connected; press USE TR-06 (or set the profile to Roland TR-06) and match CH. Hit a pad or turn a knob: the monitor shows what arrives, and on which channel if it is another one.
- **DAW.** Route the TR-06's MIDI to the plug-in's track, then turn the profile on in the plug-in's CONTROL workspace (the TR-06 tile). The plug-in does not see the device's name there: what arrives, and on which channel, is the check.
  - Most DAWs do not forward MIDI clock to plug-ins. So the TR-06's own clock and RUN/STOP usually do not reach the machine in a DAW.
  - There the machine follows the DAW's transport (P7, `followHost`). Let the TR-06 follow the DAW too: its SYNC is AUTO by default, so send it the DAW's clock.
- **Loops.** The TR-06 retransmits its MIDI IN on MIDI OUT (Soft Thru, on by default). If its OUT can come back to the editor (a MIDI interface that merges, or the editor's MIDI out wired to the TR-06's IN), set **Soft Thru OFF** on the TR-06. Otherwise the machine's own notes and clock can loop.
- **HW MIDI engine.** The plug-in's MIDI out goes to the real machine. If the TR-06 shares that interface, the machine's notes go to it too. That is the machine's output, not the profile's.

## Tests (simulated MIDI only)

| Test | What it checks |
|---|---|
| `deskControllerTest` (ctest) | the profile's data (CC 71 NOTE, relative by default); the machine's own names (MM SWAVE-SAW SYN H is TUNE, MD TRX-BD SYN 1 and NOTE are PTCH, GND-EMPTY has no pitch); the setup JSON (a round trip with knobMode and NOTE, and refusals with their paths); the routes (MD keymap and emulator TRIG notes, MM base + track, a track past 16); the overlap warnings; the input (off passes everything; other channels pass; notes and aliases, velocity, note off after a remap; knobs latest-wins; unmapped CCs, 120, 121, Active Sensing and program change blocked; realtime passes; no global means dropped); the pump (pacing, latest-wins, no repeats, gestures, 60 Hz for 2 s is about 40 edits; relative: a round's changes add up, from the machine's value, held at 0-127, unknown value gives nothing); relative knobs (the first message moves nothing, differences, per CC, 2 s idle, a reset on a track change, a sweep sums); what a knob does (MD NOTE is PTCH on TRX-BD, nothing on GND-EMPTY; MM NOTE); MM NOTE transposes the voices on the track, the sounding note's note-off stays paired, held at 0-127; the page document against both schemas |
| `mdDeskTest`, `mmDeskTest` (ctest, the fake device) | a 60 Hz knob burst for 1 s through the pump into the desk: about 20 CCs of the selected track and nothing else, no kit or pattern dump, the last value lands, one undo step, a pause makes a new one; an MD LEVEL target is the level CC; MD relative: +3 +4 moves SYN 2 from the kit's value by 7; the MM sends the CCs of what changed |
| `deskControllerTest`, monitor and activity | the TR-06's input names; the monitor's CC table per channel and its note ring, a new look clears it, the activity's `ccs` (the TR-06's channel first, by CC number) and `notes`, a value changes in place, CLEAR; the activity: what came before watching is old, a CC and a note on the channel, the same message again is news, a note-on of velocity 0 is a note-off, the clock counts nowhere, another channel while the TR-06's is quiet, a channel change; a document with what is seen on the contract |
| `deskControllerPageTest.js` (node, ctest) | the fixtures (standalone, DAW, activity with CCs and notes) against both schemas; every change is a `ctl*` command with declared arguments (USE TR-06, SET CH, knob mode, CLEAR, NOTE on another knob, ctlWatch); the page's assigned host gets them; the device tiles (one per input, the TR-06's LED steady, a DAW's, the MIDI Learn tile); a tile opens its view, DEVICES goes back, a generic device shows the mapping matrix; the TR-06's view and its monitor (CC · value · chart name → the machine's own name, other channels labelled, notes, sorted; MM "→ NOTE T1 60→62"; MD "NOTE · PTCH", "no pitch on this machine"); every select has an id; the view watches, the devices do not; no blink or animation, no bar, TR-06 MAP… or CONTROLLER… left |
| `deskControllerBrowserTest.js` (node + headless Chrome, ctest; skips without Chrome) | both shipped pages in dev mode on a fake host: no CONTROLLER… in the engine menu; the CONTROL workspace opens on the tiles (TR-06, IAC), the TR-06's LED, no bar or key; IAC shows the mapping matrix, DEVICES comes back; the TR-06's view: picks in the page's own lists (#kpop) become the commands (profile, channel, knob mode, a knob, a voice), also after a redraw; the found-TR-06 line and USE TR-06; the monitor's rows (CC 24 · 87 · BD LEVEL → T1 with the machine's own name, another channel, the last note), a new value in place without a redraw, nothing animates, CLEAR; the other-channel line and SET CH; a DAW's wording and tiles; ctlWatch on in the TR-06's view, off on the devices and in another workspace |
| `mdDeskSetupStateTest`, standalone | a TR-06 CC from an enabled MIDI input through JUCE's `AudioProcessorPlayer` (the standalone's path) reaches the filter: counted with the profile off, the knob's with it on; out of the path when off and not watched |
| `mdTr06FirmwareTest md` / `mm` (needs the ROM: `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`) | the whole processor path on the firmware, with TR-06 messages as host MIDI. MD: with the profile off, a raw SD presses TRIG 3; with it on, every voice and alias plays its mapped track (read from the notes the machine itself sends); a mapping edit moves BD to track 9; CC 71 (NOTE) moves the track's machine's pitch parameter, or nothing on a machine without one. MM: silence off, sound on, the alias, OH; CC 71 (NOTE) +2 makes BD play 62, the route follows, it sounds, and back. Both, relative: CC 24's first value moves nothing, the next moves track 3's first synthesis parameter by the difference in the machine's memory, a 60 Hz turn of 40 steps ends at the start value + 40, a track change moves nothing; absolute: a 60 Hz burst ends on its last value; the voices still play after CC 120/121; with the profile off again, the channel is raw; the controller documents are on the contract |
| both sync scripts, `page_contract_check.py` | the pages' ops and message types against the contracts; the MODAL block the same in all three copies |

## On the hardware (for the owner: unknowns)

1. **Which physical knobs send which CC.** The chart lists the CCs, not the panel. Check how many INST knobs send LEVEL, and whether the INST knobs send per-instrument TUNE, DECAY and the rest when the instrument select changes. Turn each knob with the TR-06's view open: the monitor lists the CC it sends, its value and what it moves, profile on or off.
2. **Relative knobs on the hardware.** Check that a turn moves the value from where it is, in both directions, and that a knob left alone for more than 2 s does not jump on its next touch.
3. **Whether TAP and live input send notes.** Press TAP in playback, and play live: does the machine play the mapped track?
4. **Whether RUN/STOP sends Start and Stop.** Press RUN/STOP. In the standalone app the machine starts only if its global accepts transport (MD CTRL IN on, MM TRANSPORT IN ACCEPT). Clock is followed only with MD TEMPO IN external or MM CLOCK IN EXT.
5. Also: the MIDI channel setting (CH) and Soft Thru (ThRv) on the TR-06, and whether MIDI OFFLINE (All Sound Off, Reset All Controllers) leaves the machine playing.
