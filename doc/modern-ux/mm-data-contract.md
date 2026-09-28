# Monomachine Editor data contract, version 2

Monomachine SFX-6 / SFX-60 OS 1.32B only. The plain-data boundary between the
Monomachine Editor page and the machine, on the same design as the MD's
([data-contract.md](data-contract.md)). The machine-readable form is
[mm-data-contract.schema.json](mm-data-contract.schema.json) (JSON Schema
2020-12). The C++ lives in the pure `elektronData` library: `mmPattern`,
`mmKit`, `mmSong`, `mmGlobal` (codecs), `mmJson` and `mmValidate` (contract),
`mmMachines`, `mmCommands`, `mmDump` (wire form).

## 1. Layers

| Layer | Code | In / out |
|---|---|---|
| Wire | `mmDump` | SysEx <-> `{command, version, revision, position, raw}`: 7-bit packing and the firmware's run-length form, byte-exact |
| Codec | `mmPattern`, `mmKit`, `mmSong`, `mmGlobal` | raw payload <-> plain values. One layout function per document decodes and encodes, so the two cannot drift |
| Contract | `mmJson` + `mmValidate` | values <-> JSON documents, plus the hardware limits |
| Transport | `mmDataLink` / `mmDesk` (MM-P2) | values and commands in, SysEx out; the SYSEX RECV session |
| UI | the Monomachine Editor page | JSON documents only |

Rules, as for the MD:
- The UI never sees bytes and the transport never sees JSON text.
- `fromJson` rebuilds the value, validates it and lists every problem with a JSON path.
- **The firmware stores whatever it is sent on SYSEX RECV**, invalid values included (a pattern with length 90, multiplier 7 and a lock mask without a row was stored verbatim, MM-P1-RESULT §6). `validate` is the only guard.

## 2. Versioning

- `"schema"` is `mm-desk/pattern`, `mm-desk/kit`, `mm-desk/song`, `mm-desk/global` or `mm-desk/machine`.
- **Version 2 (P6):** the pattern, kit, song and global documents carry what the firmware stores and the editor passes through untouched under one member, `firmware`: `firmware.format` and the undecoded bytes (version 1's `hidden` members). A second engine then inherits nothing of OS 1.32B's layout by accident. Readers take version 1 too (`format` and `hidden` at the top level). The machine document stays version 1.
- Readers ignore members they do not know; adding an optional member keeps the version; renaming or re-meaning one makes it the next.
- `firmware.format.version` / `revision` are the firmware's own dump format bytes: pattern 6/1, kit 2/1, song 2/1, global 3/1.
- The schema's `$defs/message` is every message the plug-in sends the page and `$defs/command` every command the page may send; `command` is generated from the command table (`mmDesk::commandTable`, `mmDeskTest --write-schema`), and the unit and firmware tests check every published message and every command they send against them.

## 3. Units and conventions

- **Firmware units** everywhere, so value -> JSON -> value is lossless for every dump.
- Steps, tracks, rows, slots, pages and parameters are 0-based; the screen shows them 1-based.
- A step set is a list of steps (`"amp": [0, 4, 8]`).
- Bytes that are not understood yet ride in `firmware` (version 1: `hidden`) as hex (`"x54e": "ffffffff"`) or as runs `[[index, "hex"], ...]` against a default (lock rows and note pool entries past their counts, which hold firmware residue in factory patterns). The UI passes `firmware` back untouched.
- Names: `name` is the 7-bit text up to the first NUL; `nameBytes` (hex) is present as well when the bytes are not just that text padded with NULs (factory empty songs start with 0xff). When `nameBytes` is present it wins.

| Field | Firmware unit | Display |
|---|---|---|
| `pattern.swingAmount` | 0-30 | 50-80 % (`mmSwingPercent`) |
| `pattern.multiplier` | named | `1X` `2X` `3/4X` `3/2X` |
| `arp.speed` | 0-127 | ×(speed + 1); 5 = ×6 = one 16th |
| `arp.range` | 0-7 | range + 1 octaves |
| `arp.steps[]` | 0x40 + offset, 255 muted | offset in semitones (the ±24 range is the mockup's; the firmware's is not measured) |
| `kit.tracks[].assign.add` | signed | as is |
| `song.rows[].repeats` | 0-63 | plays repeats + 1 times (the editor shows repeats + 1) |
| `song.rows[].tempo` | BPM, `null` = keep | BPM |
| `global.masterTune` | tenths of Hz | 4400 = 440.0 Hz |
| notes | MIDI note number | C-4 = 60 (the MM names notes the manual's way) |

## 4. Documents

### 4.1 `mm-desk/pattern` (slot 0-127, A01-H16)

One pattern holds the six synth tracks and the six MIDI sequencer tracks.

| Member | Meaning | Evidence |
|---|---|---|
| `length` | total steps 2-64 (SCALE SETUP) | panel |
| `multiplier`, `swingAmount`, `patternTranspose`, `kit` | the pattern's settings; `kit` is the kit it plays (LOAD KIT and SAVE KIT write it) | panel, live |
| `tracks[t].trig` | steps that hold a trig. A trig can be trigless (no amp/filter/lfo) or pitchless (no note) | panel |
| `tracks[t].amp` / `filter` / `lfo` | the three trig tracks (TRIG SELECT) | panel |
| `tracks[t].noteOff` | NOTE OFF steps (FUNCTION + TRIG) | panel |
| `tracks[t].notes` | `[step, note]` for steps with a pitch | panel, MIDI |
| `tracks[t].chord` + `chordNotes` | chord steps; the extra notes are `[track, step, note]` entries (the base note stays in `notes`) | MIDI |
| `tracks[t].slide` / `swing` | slide and swing tracks (default swing: every 2nd 16th) | panel |
| `tracks[t].transpose` / `scale` / `key` | TRANSPOSE: semitones, 0 --- 1 FIX 2 MAJ 3 MIN, key 0-11 | panel |
| `tracks[t].arp` | ARPEGGIATOR: `play` (TRUE UP DOWN CYCLE RND), `ojmp`, `mode` (OFF KEY SID ADD), `range`, `speed`, `trigs` (bit 0 AMP, 1 FLT, 2 LFO), `length`, 16 rhythm/offset `steps`. `flag` is bit 3 of the play byte (set in 6 factory tracks, meaning unknown) | panel |
| `midiTracks[t]` | the MIDI sequencer: `trig`, `note` (steps with notes), `noteOff`, `slide`, `swing`, transpose, `arp` (no trig switches) | panel, MIDI |
| `midiNotes` | `[track, step, note]`, one entry per note: a chord is several entries on the same step. Up to 400. The firmware keeps them in (step, track) order | panel, MIDI |
| `locks` | `{track, page, param, steps: [[step, value]]}`. Page 0-7 = SYN AMP FLT EFX LF1 LF2 LF3 MIDI (a MIDI track's LEN and VEL are page 7 of the same track number). **At most 62 per pattern, pooled over all tracks.** In (track, page, param) order: the firmware keeps its rows sorted that way | panel |

Note length and velocity of a MIDI note are locks of LEN and VEL (page 7), as on
the machine. Synth tracks have neither: a note sounds until the next trig or a
NOTE OFF.

### 4.2 `mm-desk/kit` (slot 0-127)

| Member | Meaning | Evidence |
|---|---|---|
| `name` | up to 11 characters (0x55) | live |
| `levels[6]` | track levels (CC 7) | live |
| `tracks[t].machine` | the 0x5B id; `machineName` is the manual's name. 22 machines; DPRO-DDRW (32) and DPRO-DENS (33) are MKII only | live |
| `tracks[t].pages[8][8]` | SYN AMP FLT EFX LF1 LF2 LF3 (CC 48-119) and the MIDI page (LEN VEL PB PCHG CC1-4). The parameter names are `mmParamName`, read from the firmware's own screens | live, panel |
| `tracks[t].outputs` | bus bits AB 1, CD 2, EF 4 (0x5C) | live |
| `tracks[t].input` | 0 NEIGHBOR, 1 INP A, 2 INP B, 3 INP A+B, 4-6 BUS AB/CD/EF. Used by FX machines | factory set, 0x5C |
| `tracks[t].assign` | ASSIGN: `page`, `dest`, `add` for 6 sources x 2 rows: JOY R/L, JOY L, JOY U, JOY D, VELOCITY, KEY TRACKING. `add` is signed | panel (knobs A-H, every tab) |
| `tracks[t].trigPos` | TRIG POS: the track that forwards its notes, `null` = --- | panel |
| `trackMasks` | per-track bits: JOY mirror, key tracking HPF / LPF, LEGATO AMP / FLT / LFO | LEGATO AMP by panel, the rest from the factory set |
| `tracks[t].multiEnv[6]` | MULTI ENV: ATK DEC SUS REL PORT and one more (NRPN 0x40-0x45 on the track). The factory set has the same values on all six tracks | live (NRPN and a kit diff) |
| `tracks[t].extra`, `hidden.x1cd`, `hidden.x2b6` | not decoded, kept | |

**The working kit** (the one that plays, unsaved edits included) is the kit's raw
payload in patch RAM at `0x700028`. A kit dump writes the stored slot only; LOAD
KIT makes it the working kit (MM-P0 §2).

### 4.3 `mm-desk/song` (slot 0-23)

`rows` runs up to and including the first `end` (at most 200 rows). The bytes
after it ride in `hidden.rowsAfterEnd`.

| Member | Meaning | Evidence |
|---|---|---|
| `kind` | `pattern`, `end` (pattern 255), or `loop` / `jump` / `halt` (pattern 254; the kind follows from `target` against the row index, as the manual describes) | song editor |
| `pattern`, `repeats`, `offset`, `length`, `transpose`, `tempo` | the row (REP, OF LN, TRN, XTRA BPM) | song editor |
| `target`, `repeats` on a loop | the row to go back to, and the count (0 = infinite) | song editor |
| `mutes` | synth track mutes, bit t | song editor |
| `trackTranspose[6]` | per synth track (TR1-TR6) | song editor (TR1) |
| `midiMutes`, `midiTranspose[6]` | the MIDI tracks' versions, by position | inferred |
| `x3`, `x21` | not decoded, kept | |

### 4.4 `mm-desk/global` (slot 0-7)

| Member | Meaning | Evidence |
|---|---|---|
| `channels` | auto track, base, span, multi trig, multi map (0-based) | the MIDI CHANNELS screen's values |
| `routingMode` | `3xSTEREO+AB=MIX`, `3xSTEREO`, `6xMONO` | panel |
| `masterTune` | tenths of Hz | panel |
| `midiSeq.channels`, `midiSeq.ccs` | the MIDI sequencer tracks' channels and CL1-4 CC numbers | factory set (10-15; 1 2 7 10) |
| `multiMap` | 6 fields x 32 ranges: upper key, pattern (255 = current), then four fields (OFS LEN TRN TIM in some order, not verified) | factory set |
| `control`, `hidden` | CONTROL OUT1/OUT2/IN and the rest, kept | |

### 4.5 `mm-desk/machine` (read-only, from the desk, MM-P2)

The machine state the page shows, published by `mmDesk::Desk` whenever it changes:

| Member | Meaning |
|---|---|
| `engine` | `missing` (no ROM), `unsupported` (not OS 1.32B), `loading`, `booting` (the firmware's start-up screen), `ready` (the main screen came) |
| `pattern.current`, `pattern.queued` | from the status replies; a LOAD PATTERN while playing is queued until the pattern end |
| `kit.current`, `kit.working` | the kit slot, and `clean` / `edited`: the working kit in patch RAM against the stored slot |
| `song.current`, `song.songMode` | the current song; song mode when known |
| `global` | the active global slot |
| `playing` | the sequencer runs (RAM telemetry) |
| `recv` | the SYSEX RECV session: `state` (`idle`, `toMain`, `entering`, `parked`, `leaving`, `failed`), `sending` (dumps in flight), `received` / `errors` (the firmware's own counters) |
| `loading` | `done` / `total` documents read (288) |
| `roundTripMs`, `error` | the last dump's send-to-read-back time; the last problem, if any |

Other messages to the page: `doc` (`kind`, `slot`, `pending` = sent but not yet
read back, `working` = the current kit's working copy, `doc`), `tel` (`step`,
`playing`, at most every 25 ms), `lcd` (the firmware's 128 x 64 LCD as 2048 hex
digits, row by row, MSB = left pixel, while the engine is not ready),
`catalogue`, `learn` and `result` (`op`, `id`, `ok`, `errors`, `note`).

Commands from the page: `ready`, `set` (`kind`, `doc`), `load` (`kind`, `slot`),
`select` (`p`), `loadKit` / `saveKit` (`k`), `loadSong` / `saveSong` (`s`),
`tempo` (`bpm`), `play`, `stop`, `mute` (`t`, `on`), `revealRomFolder`,
`recheckFirmware`, and the `learn*` family.

## 5. Hardware limits (`validate`)

| Document | Limit |
|---|---|
| pattern | slot 0-127; length 2-64; multiplier 0-3; kit 0-127; swing 0-30; transposes -64..63; scale 0-3, key 0-11; arp play 0-4, mode 0-3, range 0-7, length 1-16, trigs 0-7; notes 0-127; **lock rows = locked parameters ≤ 62**, lock values 0-127; MIDI notes ≤ 400, chord notes ≤ 192, tracks 0-5 |
| kit | slot 0-127; levels and page values 0-127; an OS 1.32 machine; input 0-6; trigPos a track or none; assign page/dest 0-127 |
| song | slot 0-23; pattern 0-127, LOOP or END; pattern rows: repeats 0-63, 1 ≤ length ≤ 64, offset < length, tempo 30-300 or keep; loops: target < 200 |
| global | slot 0-7; channels 0-15 or off; span 1-16; routing mode 0-2 |

Every factory dump and every programmed read-back validates clean (575 documents).

## 6. C++ API

```cpp
#include "elektronData/mmJson.h"      // mmPatternToJson / mmPatternFromJson, kit, song, global
#include "elektronData/mmValidate.h"  // validate(...)
#include "elektronData/mmPattern.h"   // decodeMmPattern / encodeMmPattern, mmLockParams

std::vector<std::string> errors;
auto pattern = elektronData::mmPatternFromJson(*elektronData::json::parse(text), errors);
if(pattern)
	send(elektronData::encodeMmPattern(*pattern));	// on SYSEX RECV
```

`mmDataCorpusTest [--json <dir>] <dir>` checks byte-exact, JSON-exact and valid on every MM dump under a directory.
