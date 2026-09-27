# MD Desk data contract, version 1

Machinedrum OS 1.63 only. Monomachine comes later on the same design.

This is the plain-data boundary between the MD Desk UI and the machine. The
machine-readable form is [md-data-contract.schema.json](md-data-contract.schema.json)
(JSON Schema 2020-12). The C++ that produces and checks these documents lives in
the pure `elektronData` library (`mdJson.h`, `mdValidate.h`) and in
`mdDataLink` (`Session::stateToJson`).

## 1. Layers

| Layer | Code | In / out |
|---|---|---|
| Codec | `elektronData` (`mdPattern`, `mdKit`, `mdSong`, `mdGlobal`) | SysEx bytes <-> plain values. Byte-exact |
| Contract | `elektronData/mdJson` + `mdValidate` | Values <-> JSON documents, plus hardware-limit checks |
| Transport | `mdDataLink::Session` | Values and intents in, SysEx out. SysEx in, values and machine state out |
| UI | mdStudio web page (P2) | JSON documents only |

Rules:
- The UI never sees bytes and the transport never sees JSON text.
- The UI sends a whole document back. `fromJson` rebuilds the value, validates it
  and lists every problem with a JSON path (`$.tracks[3].lfo.shape1: 7 is outside 0..5`).
- The firmware stores **whatever it is sent**, invalid values included (P1-RESULT
  §3). `validate` is the only guard.

## 2. Versioning

- Every document has `"schema"` (`md-desk/pattern`, `md-desk/kit`, `md-desk/song`,
  `md-desk/global`, `md-desk/machine`) and `"version": 1`.
- Readers ignore members they do not know. Adding an optional member keeps version 1.
- Renaming, removing or re-meaning a member makes it version 2. `fromJson` refuses
  versions it does not know.
- `format.version` / `format.revision` are the firmware's own dump format bytes
  (pattern 3/1, kit 4/1, song 2/2, global 6/1). Keep them as received.

## 3. Units

Values are carried in **firmware units**, so value -> JSON -> value is lossless
for every firmware dump. The display units below are exact conversions. The
first two are helpers in `mdValidate.h`.

| Field | Firmware unit | Display | Evidence |
|---|---|---|---|
| `pattern.swingAmount` | 0-9830 | percent = 50 + v * 50 / 16384 (50-80 %) | Every factory value is the exact image of an integer percent. Measured on firmware: 4915 plays 64.9 %, 8192 plays 73.9 % (onset timing, ±1 ms) |
| `pattern.accentAmount` | 0-127 | 0-15 = round(v * 15 / 127) | Every factory value is an exact multiple of 127/15 |
| `pattern.tempoMultiplier` | named | `1X` `2X` `3/4X` `3/2X` | Measured step periods at 125 BPM: 120, 60, 160, 80 ms |
| `song.rows[].tempo`, `global.tempo` | BPM (stored as BPM x 24) | BPM | SysEx 0x61 writes the same bytes |
| steps, tracks, rows, slots | 0-based | 1-based on screen | |

## 4. Documents and the mockup's `S` state

**The contract is per pattern (EXTENDED mode).** Each pattern links to a kit
(`pattern.kit`). Selecting a pattern loads that kit. Locks, length, speed,
accent, slide and swing belong to the pattern. Machines, sounds, LFOs, groups
and master effects belong to the linked kit's document. The mockup's single
`S.tracks` kit is really "the kit that `S.pat` links to". Model the UI as
`patterns[slot]` plus `kits[slot]`:
- EXTENDED: the Sound and Mix workspaces edit `kits[patterns[current].kit]`.
- CLASSIC: patterns are not linked and the current kit comes from `machine.kit.current`.

### 4.1 `md-desk/pattern` (slot 0-127, A01-H16)

| Mockup | Contract | Notes |
|---|---|---|
| `S.pat` | `slot` (the document) / `machine.pattern.current` | `name` "A01" is derived |
| `S.len` | `length` | 1..`totalLength` |
| (page count) | `totalLength` 16/32/48/64 | The manual's "total length" of SCALE SETUP |
| `S.mult` | `tempoMultiplier` | |
| `S.swing` | `swingAmount` | Use `swingPercent()` |
| `S.accAmt` | `accentAmount` | Use `accentDisplay()` |
| `S.mode` CLASSIC/EXTENDED | `global.extendedMode`, `machine.extendedMode` | A machine setting, not a pattern one |
| `tracks[i].trigs` (64 bools) | `tracks[i].trigs` (step list) | |
| `tracks[i].acc` / `.slide` | `tracks[i].accent` / `.slide` / `.swing` | Used when `accent.editAll` / `slide.editAll` / `swing.editAll` = 0 |
| (ALL accent) | `accent.steps`, `slide.steps`, `swing.steps` | Pattern-wide sets, used when `editAll` = 1 |
| `S.locks` Map `"t:PARAM"` -> step -> value | `locks[] = {track, param, steps: [[step, value]]}` | `param` is the index 0-23: synthesis 0-7, effects 8-15, routing 16-23. Names come from the machine (the mockup's `MACH` table). At most 64 distinct (track, param) per pattern |
| (link) | `kit` | 0-63 |
| — | `lockedRowsField` | Opaque firmware byte, 0 in every dump seen. Keep it |
| — | `lockPoolHidden` | Only present for factory data with residue in unused lock rows. Pass it through untouched |

- `locks` are listed in the firmware's order: (track, param) ascending.
- The firmware re-sorts lock rows into that order.
- A lock step outside `totalLength` is not part of the view. It rides in `lockPoolHidden`.

### 4.2 `md-desk/kit` (slot 0-63)

| Mockup | Contract | Notes |
|---|---|---|
| `tracks[i].m` "TRX-BD" | `tracks[i].model` (id) + `machine` (name) | `model` is authoritative. Names are the manual's: `P-I-SD`, `GND-SIN`, `GND-EMPTY`. The mockup uses `PI-*` and `GND-SN` |
| `tracks[i].syn` | `tracks[i].synth[8]` | |
| `tracks[i].fx` AMD..SRR | `tracks[i].effects[8]` | |
| `tracks[i].rt` DIST..LFOM | `tracks[i].routing[8]` | VOL = `routing[1]`, PAN = `[2]`, DEL = `[3]`, REV = `[4]` |
| mixer fader (VOL) / track level | `routing[1]`, `level` | `level` is the kit's track level (CC 8-11 on the base channel) |
| `lfo.TRCK PARAM SHP1 SHP2 UPDTE` | `lfo.track param shape1 shape2 update` | `update`: 0 FREE, 1 TRIG, 2 HOLD. `state` is opaque: keep it |
| `lfo.SPD DEPTH SHMIX` | `routing[5]`, `routing[6]`, `routing[7]` | Not in the LFO block |
| `muteGroup`, `trigGroup` | `muteGroup`, `trigGroup` | Target track or `null` |
| `S.mfx.echo/gate/eq/dyn` | `masterFx.rhythmEcho / gateBox / eq / dynamix` | 8 values each, in the mockup's key order |
| kit name | `name` (+ `nameTail`) | Up to 16 characters, 7-bit. `nameTail` keeps factory leftovers after the NUL |

### 4.3 `md-desk/song` (slot 0-31)

`rows` holds at most 256 rows, including the final `end` (required).

| Mockup row | Contract row | Notes |
|---|---|---|
| `{pat, rep}` | `{kind: "pattern", pattern, repeats}` | **`repeats` = rep - 1**. The firmware plays repeats + 1 times |
| `ofs`, `len` | `start`, `end` | **`end` is exclusive**: `end = ofs + len`. Measured: start 4, end 12 plays steps 4-11 |
| `bpm` | `tempo` (BPM or `null`) | `null` keeps the previous tempo |
| `mutes:[8,9]` | `mutes:[8,9]` | 0-based tracks. Bit 0 = track 1, verified by audio |
| `{type: "loop", to, count}` | `{kind: "loop", target, repeats}` | `repeats` = count - 1. **0 = infinite** (the mockup's `Infinity`). `target` must be an earlier row |
| (jump) | `{kind: "jump", target}` | `target` is a later row |
| (halt) | `{kind: "halt"}` | Pauses playback |
| `{type: "end"}` | `{kind: "end"}` | Must be last. Without it the firmware reads 256 rows of whatever memory holds |
| — | `raw` | Only when a row's bytes are not what its kind rebuilds (none seen). Keep it |

### 4.4 `md-desk/global` (slot 0-7)

| Mockup | Contract | Notes |
|---|---|---|
| `tracks[i].out` MAIN/A..F | `routing[i]` | A-F skip the master effects |
| `S.bpm` | `tempo` | The global tempo. Song rows can override it |
| `S.mode` | `extendedMode` | Also SET STATUS 0x20 |
| — | `baseChannel`, `keymap`, `settings` | Kept untouched. `keymap` values 16-31 appear in the Elektron default map for notes 64-89 (the manual maps those notes to patterns). Not verified |

### 4.5 `md-desk/machine` (read-only, from `mdDataLink::Session`)

| Field | Meaning |
|---|---|
| `pattern.current` | The last reported current pattern |
| `pattern.queued` | Requested with `selectPattern` while playing. Becomes current at the end of the current pattern (P1-RESULT §4) |
| `kit.current` | The current kit number |
| `kit.working` | `clean`: the kit equals its stored slot. `edited`: **not saved on the machine**. `unknown`: nothing observed yet |
| `song.current`, `song.reloadNeeded` | A song was written into the current song's slot. It is heard after STOP, LOAD SONG and PLAY |
| `songMode`, `extendedMode`, `globalSlot`, `track` | Status values. `null` until reported |
| `patternKits` | `[pattern, kit]` links seen in pattern dumps. `Session::selectWouldDiscardKitEdits(p)` uses them |
| `desk` | Added by `mdDesk::Desk` for the page: firmware state, TX, round trip, undo counts, the audible queue, mutes, and `kitSource` (below) |

**The working kit comes from memory (P3).** On MD OS 1.63 the kit that plays,
unsaved edits included, sits in patch (battery) RAM at `0x70000a` as the kit
dump's raw fields in dump order, with the current kit number at `0x700008`
(`elektronData/mdWorkingKit.h`, found by `mdEditorProbeFirmwareTest workkit`).
`md::Device` republishes the region lock-free when it changes; the desk makes it
the current kit's document. `machine.desk.kitSource` says `memory` then, and
`kit.working` is `edited` exactly when that document differs from the stored
slot's dump (LFO running state aside). So panel encoder moves, host automation
and edits restored from a DAW project show without SAVE KIT. Without the region
(another firmware) `kitSource` is `tracked`: the stored slot plus the edits the
desk saw, as in P2.

**Live recording (P3).** `machine.desk.recording` is true while the firmware
live records (hold RECORD, press PLAY). It is read from RAM like a person reads
the panel (`md::SequencerState`): the playhead moves and the RECORD LED blinks.
While it records, the firmware owns the playing pattern: the desk refuses
pattern edits to it (a dump would overwrite what was just recorded) and reads
the pattern back every 400 ms instead. A kit value moved in the page is not sent
as a CC (the firmware does not record CCs) but as DATA ENTRY knob turns of the
selected track, after SET STATUS track and the page key (`mdDesk::KnobRecorder`),
so the firmware locks it on the track's next note. `recTrig` plays a track like
its TRIG key.

Which note (P4, `mdP4ProbeFirmwareTest lockwindow`): the track's next programmed
trig whose step has not started when the turn lands. A turn 8 ms before the step
locks it; at or after the step start it is too late and the lock goes to the
following trig. A note played live at the same moment is not reliable (3 of 9).
The desk names the trig it expects (`mdDesk::nextLockStep`) as
`machine.desk.recLock` {track, param, step}; the page marks that cell until the
read-back shows the real lock. Checked on firmware: the desk said step 9, the
firmware locked step 9.

**UW samples (P3).** `sampleName` (slot 0-47, 1-4 characters) sends the
manual's 0x73. That is all the firmware offers: it has no request for names,
memory in use or sample audio, and it ignores SDS dump requests (measured, no
reply for three slots). Names, memory and audio are therefore not part of the
contract; the page shows the controls that cannot work as disabled, with the
reason.

**Start-up (P4).** MD OS 1.63 answers MIDI about 13 s before it takes panel keys:
its start-up animation ignores them (PLAY first taken 12.7 s after MIDI ready on a
fresh machine, 9.3 s with a restored project). Main RAM 0x28998a is 00 until the
main screen starts (13.4 s / 9.4 s), then non-zero; `md::BootAnimation` latches the
first non-zero value per machine boot (30 s timeout) and `md::Device` publishes it.
The desk takes page input only after it (`Desk::isInputReady`): `desk.firmware`
stays `booting` and `desk.boot` says `animation` meanwhile; loads already run. While
it waits the host sends the firmware's own LCD, `{"type":"lcd","bits"}` (128 x 64,
one bit per pixel, row-major, 16 bytes a row, bit 7 = the left pixel, base64), which
the page draws in its LCD with the plate's `--lcd` / `--ink`, then fades out. The
host must call `onTelemetry` every tick, also without telemetry (`valid = false`).

**Chaining and mutes (P4).** `machine.desk.chain` is the firmware's own pattern
chain, read from the MC68331 internal SRAM (`md::ChainAndMutes`: 0x1001f5c active,
0x1001f60 next, 0x1001f64 length, 32-bit patterns from 0x1001f68). The page asks
for one with `{"op":"chain","patterns":[...]}`; the desk checks the machine's rules
(`mdDesk/mdDeskChain.h`: 2-16 patterns, one bank, each once) and presses the keys
the manual describes: BANK held, the TRIG keys held one after another in play
order (pressed and released one by one they only select), with BANK GROUP first
for the other half of the banks. The chain loops. A LOAD PATTERN or a single TRIG
ends it; a pattern dump into a chained pattern does not (measured). So `select`
while a chain is active answers `{"type":"ask","ask":"breakChain","p"}` and sends
nothing; `select` with `chainOk` (or `force`) goes ahead. `chainClear` is LOAD
PATTERN of the current pattern. `machine.desk.mutes` is the machine's pattern mute
mask (main RAM 0x28b34a, 16 bits big-endian, bit 0 = track 1), so mutes made in the
machine's MUTE window or by CC 12-15 show too (`mutesSource` = `memory`).

**Kit library and pattern chooser (P4).** The desk loads all 64 kits (stored
slots) in the background too. Commands (`mdDesk/mdDeskLibrary.h`, pure):
`kitCopy`/`patCopy` {k|p}, `kitPaste`/`patPaste` {k|p}, `kitCopyTo`/`patCopyTo`
{from, to} (drag-copy), `kitClear`/`patClear` {k|p}, `kitRename` {k, name}; and the
machine actions `kitLoad` {k} (LOAD KIT), `kitSaveAs` {k} (SAVE KIT n), `select`
{p, now} (switch now: STOP, LOAD PATTERN, PLAY while playing). Measured on the
firmware (`mdP4ProbeFirmwareTest library`, smoke test p4):
- SAVE KIT n makes n the current kit and, in EXTENDED, relinks the current pattern
  to it. A manual LOAD KIT relinks it too.
- A kit dump into the current slot is not heard until LOAD KIT: a paste or clear
  into the kit that plays is therefore a dump plus LOAD KIT (`Change::slotWrite`).
- There is no CLEAR command: clear writes an empty kit (every track GND-EMPTY,
  neutral values, no name) or a pattern without trigs or locks (length, speed,
  swing, accent and kit link kept). What the machine's own CLEAR leaves is unknown.
- Rename: the kit that plays live (0x55, SAVE stores it); another slot: its dump
  written back with the new name.
- A pattern dump over the current pattern that links another kit makes the machine
  load that kit. Slot writes that would lose unsaved kit edits answer
  `{"type":"ask","ask":"overwriteKit"|"relinkKit"|"loadKit","command"}`; the page
  sends the command again with `force`.
Slot writes are undoable in the editor; LOAD and SAVE are the machine's (it keeps
one UNDO KIT).

### 4.6 `md-desk/modulators` (page -> desk, P3)

App-only modulation for the Control workspace. The page owns it and sends it
whole with `{"op":"modSet","doc":...}`; the desk answers with
`{"type":"mod","doc","values","ccPerSecond","ccLimit"}`.

| Field | Meaning |
|---|---|
| `sources[]` | At most 16. `id` (unique), `label`, `kind` `lfo` or `random`, `shape` 0-5 (the page's LFO shapes), `rate` `1/16` `1/8` `1/4` `1/2` `1` `2` `4` (the cycle or hold, in 16th-note steps), `depth` 0-100 (LFO), `smooth` 0-127 (random) |
| `links[]` | At most 64. `source` (a source id), `track` 0-15, `param` 0-23 (the kit parameter index), `min`, `max` 0-127, `curve` `lin` `exp` `log`, `invert` |

Rules (`mdDesk/mdDeskMod.h`):
- Sources move once per step of the machine's own playhead, and only while it
  plays; they restart from phase 0 after STOP.
- A link sends `min + (max - min) * curve(value / 127)` as a CC, only when it
  changes, within a rolling budget of 300 CCs a second (the mockup's limit).
  Over budget, the value is sent on a later step.
- It is not part of the kit, a real Machinedrum does not play it, and live
  recording does not record CCs. The sources run while the editor is open (the
  desk lives in the editor).

### 4.7 `md-desk/setup` (kept with the project, P4)

The editor's own setup: `modulators` (4.6) and `knobCcs`, the eight controller
knob rows' CC numbers (0-127, distinct; default 21-28). The desk keeps it
(`mdDesk/mdDeskSetup.h`), publishes `{"type":"setup","doc"}` to the page and hands
every change to the host (`Port::saveSetup`), which stores the text as the plug-in
state's `MDSK` chunk. A restored project brings it back, also into an open editor;
a project without it starts from the default setup. The page changes the knob rows
with `{"op":"knobs","ccs":[8 numbers]}` and the modulators with `modSet`. It is not
machine data: a real Machinedrum never sees it.

The UI must not show `edited` and "project not saved" as one flag. Two different things can be unsaved:

| State | Meaning | Lost by |
|---|---|---|
| Not saved on the machine | `kit.working = edited` | LOAD KIT, or selecting a pattern linked to another kit (EXTENDED). Saved by SAVE KIT to its slot (`Session::saveKit`) |
| Not heard yet | `song.reloadNeeded`, `pattern.queued` | Nothing is lost. The change waits for the machine |
| Not saved in the DAW project | The plug-in state (1 MiB patch RAM) differs from the last project save | Closing the project. Patterns, songs, stored kits and **the unsaved working kit** are all in patch RAM (P1-RESULT §4) |

Pattern, song and global dumps write their slots directly. Only the kit has a
separate working copy that can be unsaved on the machine.

## 5. Hardware limits (`elektronData::validate`)

| Document | Limit |
|---|---|
| pattern | `slot` 0-127. `totalLength` 16/32/48/64. `length` 1..`totalLength`. `tempoMultiplier` one of four. `kit` 0-63. `accentAmount` 0-127. `swingAmount` 0-9830 (80 %). `editAll` 0/1. Steps below 64 (32 for classic dumps). **At most 64 locked (track, param)**, param 0-23, values 0-127 |
| kit | `slot` 0-63. `model` an OS 1.63 machine. Parameters, levels and master effects 0-127. LFO track 0-15, param 0-23, shapes 0-5, update 0-2. Groups 0-15 or `null`. Name 7-bit |
| song | `slot` 0-31. 1-256 rows, the last one `end`, no other `end`. Pattern rows: pattern 0-127, repeats 0-63, 0 <= start < end <= 64, tempo 30-300 BPM or `null`. Loop: an earlier target, repeats 0-63. Jump: a later target |
| global | `slot` 0-7. `routing` A-F/MAIN. `tempo` 30-300. `keymap` 0-31 or `null` |

The firmware stores values beyond these limits without complaint. It does not
play them meaningfully, so the UI must not send them.

## 6. C++ API

```cpp
#include "elektronData/mdJson.h"      // patternToJson / patternFromJson, kit, song, global
#include "elektronData/mdValidate.h"  // validate(...), swingPercent, accentDisplay
#include "elektronData/json.h"        // json::parse / json::write
#include "mdDataLink/mdDataLink.h"    // Session, Session::stateToJson

std::vector<std::string> errors;
auto kit = elektronData::kitFromJson(*elektronData::json::parse(text), errors);
if(kit)
	session.pushKit(*kit, mdDataLink::Session::KitApply::StoreAndLoad);
```

`elektronDataCorpusTest <dir>` checks value -> JSON -> value on every dump found
under a directory.
