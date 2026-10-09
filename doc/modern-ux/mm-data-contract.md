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
- The machine document's `contract` (2) is the page protocol's version (`MmModel::contractVersion`, written into the schema as a `const` by `mmDeskTest --write-schema`); it follows the rule above.
- Commands (`$defs/command`) are closed: an undeclared argument is refused. What the plug-in writes (the documents, the machine document, the messages) is open for readers (`"additionalProperties": true` where the members are listed); the tests hold our own writer to what it declares by closing those again (`json::Schema::closedForWriter`), as data-contract.md 2 says.
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
| `arp.range` | 0-8 | range + 1 octaves (the RNGE knob stops at 0 and 8: measured, 0.3.5) |
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
| `trackMasks` | per-track bits: JOY mirror (0x1d0), key tracking HPF / LPF, PORTAMENTO (set = ALWAYS, clear = ONLY LEGATO), LEGATO AMP / FLT / LFO. **Corrected in MM-P4:** MM-P1 had inferred mirror/hpf/lpf at 0x2aa-0x2ac; the panel shows 0x2aa LPF, 0x2ab HPF, 0x2ac PORTAMENTO | panel (every bit) |
| `multiTrig` | MULTI TRIG: `mode` 0 ALL TRK, 1 SPLIT KEY, 2 SEQ START, 3 SEQ TRNSP; `timing` 0 DIRECT, 1/16 2/16 4/16 8/16 16/16 32/16; `splitKey` (note, the first key of the upper zone); `splitTrack` (0-based, the first upper track) | panel (MM-P4) |
| `tracks[t].multiEnv[6]` | MULTI ENV: ATK DEC SUS REL PORT and one more (NRPN 0x40-0x45 on the track). The factory set has the same values on all six tracks | live (NRPN and a kit diff) |
| `tracks[t].extra`, `firmware.x1cd`, `firmware.x1d1` | not decoded, kept | |

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
| `multiMap` | 6 fields x 32 ranges: upper key; pattern (255 = CUR); offset (255 = ---); length; transpose (signed byte); timing (0 DIR, 1 2 4 8 16 32). Unused ranges repeat the last upper key | panel (MULTIMAP EDIT, MM-P4) |
| `controlIn` | CONTROL IN: `tempoSync` 0 INTERNAL, 1 EXT MIDI CLK; `transport` 0 IGNORE, 1 ACCEPT (MIDI Start/Stop). A DAW's plug-in sets both (P7, `followHost`) | panel (MM-P4) |
| `control`, `firmware` | CONTROL OUT1/OUT2 and the rest, kept | |

A dump writes the stored global slot; the active global takes it only when its slot is made
active again (SET ACTIVE GLOBAL, 0x56), and not while the machine is on SYSEX RECV. The desk
sends 0x56 once the active slot's dump is read back and the panel is back on the main screen;
over HW MIDI once more before PLAY (the person may still have been on SYSEX RECV).

### 4.5 `mm-desk/machine` (read-only, from the desk, MM-P2)

The machine state the page shows, published by `mmDesk::Desk` whenever it changes:

| Member | Meaning |
|---|---|
| `contract` | The page protocol's version (2; section 2) |
| `engine` | `missing` (no ROM), `unsupported` (not OS 1.32B), `loading`, `booting` (the firmware's start-up screen), `ready` (the main screen came) |
| `pattern.current`, `pattern.queued` | from the status replies; a LOAD PATTERN while playing is queued until the pattern end |
| `kit.current`, `kit.working` | the kit slot, and `clean` / `edited`: the working kit in patch RAM against the stored slot |
| `song.current`, `song.songMode` | the current song; song mode when known |
| `global` | the active global slot |
| `mutes` | MM-P4: `synth` and `midi`, bit t = track t muted, from RAM (the MUTE window and CC 3; made on the machine's panel too); `null` where the engine cannot read them (HW MIDI) |
| `poly` | MM-P4: POLY, the machine's audio mode (status 0x20); `null` until it answers |
| `tempo` | BPM from RAM; `null` while it is not a tempo yet (boot) |

`mutes`, `poly` and `tempo` are memory's, but a value the page set (`mute`, `muteMidi`, `poly`, `tempo`) is
said from the moment the command is taken (so before its result) until memory shows it; memory that still
disagrees after the engine's `settleMs` (1.5 s) wins, so a change on the machine's panel shows
(DESIGN-UNIFY.md 4.4, `deskCore::FieldExpectation`). The page matches its echoes by command id, never by time.
| `desk` | MM-P8: `chain` (the firmware's pattern chain from RAM: `active`, `next`, `patterns`; `null` where not readable, HW MIDI) and `bankGroup` (0 A-D, 1 E-H, -1 unknown) |
| `recv` | the SYSEX RECV session: `state` (`idle`, `toMain`, `entering`, `parked`, `leaving`, `failed`; over HW MIDI `idle` or `waitingUser`), `waiting` (HW MIDI: messages that wait for the person to open SYSEX RECV, sent by `hwSend`), `sending` (dumps in flight), `received` / `errors` (the firmware's own counters) |
| `loading` | `done` / `total` documents read (288); `done` includes `failed`, the slots whose read was given up after its retries (no reply) |
| `roundTripMs`, `error` | the last dump's send-to-read-back time; the last problem, if any |

Other messages to the page: `doc` (`kind`, `slot`, `pending` = sent but not yet
read back, `working` = the current kit's working copy, `doc`), `telemetry` (`step`,
`playing`, at most every 25 ms, `record`: `off`, `grid`, `live` or `null`, and `songRow`: the song
row the sequencer plays, 0-based, RAM 0x2bdba1, or `null` where the engine cannot read it; the
transport is only here, never in the machine document), `host` (`bpm`, `follows`: in a DAW the host's
tempo, also while it is stopped; see the Machinedrum's data-contract.md, B-030), `audioRun` (B-035: the host's
audio calls and the machine's speed while it starts; see data-contract.md), `lcd` (the firmware's 128 x 64 LCD as 2048 hex
digits, row by row, MSB = left pixel, while the engine is not ready),
`catalogue`, `learn` and `result` (`op`, `id`, `ok`, `errors`, `note`).

Edit intents (DESIGN-UNIFY.md 4.1, `mmDesk/mmDeskEdit.cpp`): every edit the page makes, in small steps, applied by the
core to its own documents (so a step the machine recorded meanwhile stays). Pattern edits name the pattern (`p`); the
kit edits the kit that plays (`k`, its working kit); the song edits a song (`s`, any of the 24); the global edits the
active global; the library's ops name their slots. A track `t` of a pattern edit is 0-11: the six synth tracks, then
the six MIDI sequencer tracks. Values in firmware units.
- Mix: `level`, `route` (`out`: AB 1, CD 2, EF 4), `input`, `param` (`page` 0-6 a synth track's, 7 a MIDI track's MIDI
  page), `trigPos`, `legato`, `portamento`, `routing`, `midiTrack` (`ch`, `cc`).
- Sequence: `step` (`v`: null, `{off:true}` or `{n, a, f, l, notrig?}`), `slide`, `swingStep`, `lock` (`v` null
  clears), `clearLane`, `clearLocks`, `clearPattern`, `steps` (a range of tracks made exactly the rows given: the
  generators), `rotate`, `doublePattern`, `length`, `speed`, `swing` (percent), `transpose`, `arp` (`field`, `v`, `i`
  for a rhythm/offset step), `clearSteps`, `copySteps`, `pasteSteps` (the core's clipboard).
- Sound and Perform (the kit that plays): `machine` (`model`: the SysEx 0x5B id; the SYN page starts at the machine's
  defaults, without `keepFx` the other pages at their neutral values; a processing machine on a track that had none
  listens, INP A+B on track 1, the neighbour elsewhere, with AMP DEC and REL at 127), `clearSound` (GND-SIN at its
  start), `copySound` / `pasteSound` (the machine and its seven pages, the core's clipboard), `params` (`values`:
  `[[t, page, i, v]...]`, MUTATE and a screen's handle), `assign` (`src` 0-5 and `row` 0-1 with `page`, `dest`, `add`
  -64..63; `mirror`, `hpf`, `lpf`), `multiEnv` (`i`, `v`: every track's copy), `multiTrig` (`mode`, `splitKey`,
  `splitTrack` 0-5, `timing`), `kitName` (the kit that plays, live).
- The MULTI MAP (the active global): `multiMap` (`i`, `hi`, `pat` 255 CUR, `ofs` 255 ---, `len`, `trn` -64..63, `tim`),
  `multiMapSplit` (`i`: the range splits at its middle key, the lower half a copy), `multiMapDelete` (`i`: the last
  range then reaches the top key). The ranges past the last in use repeat its upper key.
- Song (`s`): `rowSet` (`i`, `row`: a contract row), `rowInsert`, `rowDelete`, `rowMove` (`from`, `to`), `copyRow`,
  `pasteRow`, as the Machinedrum's (`deskCore/deskEdits.h`): loop, jump and halt targets follow their rows, END stays
  last, 200 rows; the bytes after END stay where they are.
- The library (stored slots): `kitCopy`, `kitPaste`, `kitCopyTo` (`from`, `to`), `kitClear`, `kitRename` (a stored
  kit; `kitName` renames the kit that plays), `patCopy`, `patPaste`, `patCopyTo`, `patClear`, as the Machinedrum's.
  A copy of the kit that plays copies what it sounds like; a kit written into its slot is loaded too (LOAD KIT). A
  cleared kit is six GND-SIN tracks at their start without a name; a cleared pattern has no notes, slides or locks
  (length, speed, swing, arpeggiator, transposes and its kit link stay). The machine asks first (`clearSlot`;
  `overwriteSlot` over a kit with a name, the kit that plays or a pattern with trigs); the page sends it again with
  `force`.

The schema's `$defs/command` says each one's arguments; `doc/modern-ux/intent-cases.json` is the executable spec of
what they do. Every edit carries `g`: the edits of one gesture are one undo step. `set` (`kind`, `doc`: a whole
document) is the intent of an import or a restore (the plug-in's own); the page sends none.

Commands from the page: `ready`, `set` (`kind`, `doc`), `load` (`kind`, `slot`),
`select` (`p`), `loadKit` / `saveKit` (`k`), `loadSong` / `saveSong` (`s`),
`tempo` (`bpm`), `play`, `stop`, `mute` (`t`, `on`), `muteMidi` (`t`, `on`: the MUTE window),
`poly` (`on`), `record` (`mode`: `off`, `grid`, `live`), `hwSend` (HW MIDI: the machine is on
SYSEX RECV), `followHost`, `revealRomFolder`, `recheckFirmware`, and the `learn*` family. The
schema's `$defs/command` is generated from the command tables (`mmDeskTest --write-schema`).
`chain` (`patterns`) and `chainClear`: see Chaining below.
`noteOn` (`t` 0-5, `vel` 1-127, `pitch`) and `noteOff` (`t`, `pitch`; without it every note of the track):
the page's keyboard (the home row, the piano roll's and the transpose keyboard's keys), the note intent
both editors share (data-contract.md, the keyboard). The core sends synth track t's MIDI note 48 + pitch
(C3 is pitch 0, -48 to 79) on the track's own channel (the active global's base + t, while t is below
CHANNEL SPAN; else refused with the reason, as without a global). The note off goes where its note on
went; a second `noteOn` of a sounding pitch ends it first; other pitches sound together (POLY). Notes
change no document. The on-screen keyboard's MULTI TRIG / MULTI MAP keys and the joystick still go as
the plug-in's `midi` messages. Over HW MIDI `play` asks first (`transportIgnore`) while the active global's CONTROL IN TRANSPORT
is IGNORE; confirmed, it writes TRANSPORT ACCEPT (a global dump, so it waits for SYSEX RECV).

## 5. Hardware limits (`validate`)

| Document | Limit |
|---|---|
| pattern | slot 0-127; length 2-64; multiplier 0-3; kit 0-127; swing 0-30; transposes -64..63; scale 0-3, key 0-11; arp play 0-4, mode 0-3, range 0-8, length 1-16, trigs 0-7; notes 0-127 (a note on a step without its trig is kept: OS 1.32B takes it, old backups hold it); **lock rows = locked parameters ≤ 62**, lock values 0-127; MIDI notes ≤ 400, chord notes ≤ 192, tracks 0-5 |
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

**Song playhead (0.3.5).** `songRow` (telemetry) is the song row the Monomachine plays, one byte at
RAM 0x2bdba1 (`md::MmTelemetry::g_songRowAddress`; found with `mmEditorProbeFirmwareTest songrow`,
checked by `mmDeskFirmwareTest songrow`: at every pass the row's pattern is the one the machine
reports by status, a repeated row shows twice, a loop goes back). A LOOP row is never the row. As on the
Machinedrum the desk publishes the row heard (`deskCore::SongRowHeard`: the byte at PLAY and at each wrap
of the playhead), so a byte that queues the next row early never moves the mark before the pass ends. It is
the machine's only while `song.songMode` is true and the machine plays; the page marks a row only
then, without a render. Measured on the way: with a LOOP row the song went back to row 1 whatever
the row's target byte (+1) said (B-038; a skipped row seen at first was the test's: RAM 0x26b46e, the "running" byte, reads 0
during some passes of a playing song, so the test samples on the step alone);
the target's offset in `MmSongRow` is inferred (MM-P1) and worth a probe of its own.

**Chaining (MM-P8).** `machine.desk.chain` is the firmware's own pattern chain (manual 1-46: hold
BANK, press the TRIG keys), read from RAM (`md::MmTelemetry`, measured with
`mmEditorProbeFirmwareTest chain`): 0x2bc2c4 active, 0x2bc2c8 the next entry, 0x2bc2cc the length,
0x2bc2d0 + 4 n the patterns, 32-bit big-endian; BANK GROUP is 0x70000b. `{"op":"chain","patterns":[...]}`
checks the machine's rules (at least two, one bank, each once), switches to pattern mode first
(SET STATUS 0x10 = 0: in song mode the firmware keeps the chain but plays the song, measured), presses
BANK GROUP when the chain is in the other half and then BANK + the TRIG keys
(`DevicePort::pressBankTrigs`). Unlike the Machinedrum, a SysEx LOAD PATTERN does **not** end the
chain (the pattern plays once, then the chain goes on, measured); a pick (BANK + one TRIG) or STOP
twice does. So `select` while a chain plays asks first (`breakChain`) and, confirmed, ends it with the
pick's own keys before the LOAD PATTERN; `chainClear` is the pick of the pattern that plays. While the
desk's keys are on their way (or the panel is on its way to or from SYSEX RECV) the latest `chain` /
`chainClear` waits (one at a time, the latest wins). A chain the machine now holds drops a queued pick.
Over HW MIDI `capabilities.chains` is false: Appendix C has no message for chaining or for the keys.
The Song page's palette has ARRANGE | CHAIN; its header says what plays: `SONG nn` in song mode, else
`CHAIN A03»A05` while a chain is active, else `PATTERN A06`.
