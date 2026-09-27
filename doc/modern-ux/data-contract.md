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
