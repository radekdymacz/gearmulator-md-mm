# Design: the Roland TR-06 controller profile

- Branch `feat/tr06-profile`, 2026-09-29. For both editors (the Machinedrum Editor and the Monomachine Editor), in the standalone apps and the DAW plug-ins.
- **Optional and off by default.** While it is off the input filter is not in the processor's path at all, and the editor's state carries no controller chunk. Ordinary MIDI input, MIDI learn and DAW automation work as before.
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
| ACC LEVEL (71) | synthesis parameter 8 | SYN H |
| OD DRIVE (17) | DIST | filter BASE |
| DELAY TIME (18) | DEL send | filter WIDTH |
| DELAY DEPTH (19) | REV send | the track level (CC 7) |
| every other knob | not mapped | not mapped |
| Clock, Start, Continue, Stop, SPP | passed on unchanged: the machine's own sync settings decide (MD TEMPO IN / CTRL IN, MM CLOCK IN / TRANSPORT IN). The profile never changes them | same |
| All Sound Off, Reset All Controllers, Active Sensing; any other message on the channel | blocked | blocked |

The user can change every voice's track (and the MM pitch), every knob's target, and the channel. **Reset to defaults** restores the table and channel 10; the profile stays on or off.

**Where a voice goes on the Machinedrum:**
- **A real Machinedrum** (the HW MIDI engine): the base channel, at the first note that the active global's keymap gives the track.
- **The emulator:** it cannot take a note there. Upstream's emulator turns every note from 36 to 51, on any channel, into a press of TRIG key `note - 36` (`mdhardware.cpp`, `pumpScheduledMidi`). Keymap notes would press the wrong keys (track 2's note 38 would press TRIG 3). So on the emulator a voice goes as its track's TRIG note, 36 + track.

As a result, **with the profile off**, a TR-06 on any channel already presses the emulated Machinedrum's TRIG keys 1-15. SD would play track 3. `mdTr06FirmwareTest` measures this.

**Where the knobs act:** on the page's selected track. The Monomachine page sends the synth track with the selected track's number: a MIDI track counts as the synth track with its number.

## The data contract

- **The setup** (`desk/controller`): `{schema, version 1, machine "md"|"mm", profile "off"|"tr06", channel 1-16, voices [{voice, t, note?}], knobs [{cc, pg?, i}]}`. Unmapped knobs are left out.
  - Kept with the editor's state as the **MDCT** chunk (`DeskHost`). The chunk is written only once the user changes something, so a project that never used the profile is byte for byte what it was.
  - The DAW's project and the standalone app's saved state both restore it. A project without the chunk starts with the profile off.
- **Commands** (deskHost's table, schema regenerated):
  - `ctlSet {profile?, channel?}`
  - `ctlVoice {voice, t, note?}`
  - `ctlKnob {cc, pg?, i|null}`
  - `ctlReset`
  - `ctlTrack {t}`: the page's selected track. The MD page sends it on every selection and after ready. The MM page sends it through `HOST.selected` after a render.
- **The page's document:** `{"type":"controller","doc":…}`, `$defs/controller` (the same text in both schemas, which `deskControllerTest` checks). It carries:
  - the setup;
  - `profiles`, `tracks`, `selected`;
  - `known` (whether the machine's global has been read);
  - each voice's `out` ({ch, note} now, or null);
  - every knob with its target's `name`, and the `targets` to choose from;
  - `warning`;
  - `last` (the knob the TR-06 moved last).

  It is published after ready, after every `ctl*` command and when the route changes. While knobs move, it is published at most 4 times a second.

## Routing

- **The input filter.** `pluginLib::ExternalMidi` (ours, `externalMidi.h`) has an input filter. Its `takeIn`, which upstream's processor already calls for every event, offers each host or port event to the filter first. No upstream file changed; `scripts/mdmm-upstream-footprint.sh` lists the same 17 files.
- **`CtlInputFilter`** (`mdCtlProfile.*`) is installed only while the profile is on. It runs on the MIDI input threads, with no lock and no allocation (`deskController::Input`, atomic tables):
  - A **voice** becomes the machine's note in the same block, at the same offset. It goes into the emulated device, or, on the HW MIDI engine (external MIDI on), to the host's MIDI out and the ports with the audio block (a lock-free queue). A note off goes where its note on went, even if the mapping changed in between.
  - A **knob** keeps only its newest value.
  - Blocked messages go nowhere. Nothing is ever sent back to the TR-06.
- **Knobs into the edit path.** Every 8 ms step, the session (`ControllerProfile`) takes the knobs' newest values for the selected track. `deskController::KnobPump` then paces them as the Control All pump does:
  - at most one round every 50 ms;
  - the newest value of each target;
  - no repeat of the value a target just got;
  - one undo step per burst (400 ms of quiet ends it).

  A round becomes the page's own commands:
  - MD: `param` or `level` on the kit that plays, sent as a CC;
  - MM: one working-kit `set`, of which the adapter sends only the changed values as CCs.

  A single parameter never causes a dump. With the HW MIDI engine the same commands go over the wire, because the path goes through the adapter, not the emulator.
- **The route follows the machine.** Every 250 ms the session reads the route from the active global the desk holds: the MD's base channel and keymap, the MM's base channel and span. Until a global is read, voices are dropped, never passed raw.
- **The overlap warning.** The panel warns when the TR-06's channel is one the machine listens on:
  - MD: base to base + 3;
  - MM: base to base + span - 1, and AUTO, MULTI TRIG and MULTI MAP.

  The profile takes every message on its channel, so other gear on that channel stops reaching the machine.

## Connecting a TR-06

- **Standalone app.** Connect the TR-06 by USB. Roland's manual says to install Roland's TR-06 USB driver on the computer (roland.com/support). Enable the TR-06 under AUDIO/MIDI › MIDI inputs, open CONTROLLER… in the engine menu, set the profile to Roland TR-06 and match the channel.
- **DAW.** Route the TR-06's MIDI to the plug-in's track, then turn the profile on in the plug-in's engine menu (CONTROLLER…).
  - Most DAWs do not forward MIDI clock to plug-ins. So the TR-06's own clock and RUN/STOP usually do not reach the machine in a DAW.
  - There the machine follows the DAW's transport (P7, `followHost`). Let the TR-06 follow the DAW too: its SYNC is AUTO by default, so send it the DAW's clock.
- **Loops.** The TR-06 retransmits its MIDI IN on MIDI OUT (Soft Thru, on by default). If its OUT can come back to the editor (a MIDI interface that merges, or the editor's MIDI out wired to the TR-06's IN), set **Soft Thru OFF** on the TR-06. Otherwise the machine's own notes and clock can loop.
- **HW MIDI engine.** The plug-in's MIDI out goes to the real machine. If the TR-06 shares that interface, the machine's notes go to it too. That is the machine's output, not the profile's.

## Tests (simulated MIDI only)

| Test | What it checks |
|---|---|
| `deskControllerTest` (ctest) | the profile's data; the setup JSON (a round trip, and refusals with their paths); the routes (MD keymap and emulator TRIG notes, MM base + track, a track past 16); the overlap warnings; the input (off passes everything; other channels pass; notes and aliases, velocity, note off after a remap; knobs latest-wins; unmapped CCs, 120, 121, Active Sensing and program change blocked; realtime passes; no global means dropped); the pump (pacing, latest-wins, no repeats, gestures, 60 Hz for 2 s is about 40 edits); the page document against both schemas |
| `mdDeskTest`, `mmDeskTest` (ctest, the fake device) | a 60 Hz knob burst for 1 s through the pump into the desk: about 20 CCs of the selected track and nothing else, no kit or pattern dump, the last value lands, one undo step, a pause makes a new one; an MD LEVEL target is the level CC; the MM sends the CCs of what changed |
| `deskControllerPageTest.js` (node, ctest) | the panel's fixtures against both schemas; every change is a `ctl*` command with declared arguments; the page's assigned host gets them; rendering |
| `mdTr06FirmwareTest md` / `mm` (needs the ROM: `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`) | the whole processor path on the firmware, with TR-06 messages as host MIDI. MD: with the profile off, a raw SD presses TRIG 3; with it on, every voice and alias plays its mapped track (read from the notes the machine itself sends); a mapping edit moves BD to track 9. MM: silence off, sound on, the alias, OH. Both: CC 24 moves track 3's first synthesis parameter in the machine's memory; a 60 Hz burst ends on its last value; the voices still play after CC 120/121; with the profile off again, the channel is raw; the controller documents are on the contract |
| both sync scripts, `page_contract_check.py` | the pages' ops and message types against the contracts; the MODAL block the same in all three copies |

## On the hardware (for the owner: unknowns)

1. **Which physical knobs send which CC.** The chart lists the CCs, not the panel. Check how many INST knobs send LEVEL, and whether the INST knobs send per-instrument TUNE, DECAY and the rest when the instrument select changes. Turn each knob with CONTROLLER… open: the row it moves lights up.
2. **Whether TAP and live input send notes.** Press TAP in playback, and play live: does the machine play the mapped track?
3. **Whether RUN/STOP sends Start and Stop.** Press RUN/STOP. In the standalone app the machine starts only if its global accepts transport (MD CTRL IN on, MM TRANSPORT IN ACCEPT). Clock is followed only with MD TEMPO IN external or MM CLOCK IN EXT.
4. Also: the MIDI channel setting (CH) and Soft Thru (ThRv) on the TR-06, and whether MIDI OFFLINE (All Sound Off, Reset All Controllers) leaves the machine playing.
