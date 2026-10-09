# MD Desk data contract, version 2

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
  `md-desk/global`, `md-desk/machine`) and `"version"`.
- **Version 2 (P6)** of the pattern, kit, song and global documents groups what the firmware
  stores and the editor passes through untouched under one member, `firmware`:
  pattern `{format, lockedRowsField, lockPoolHidden}`, kit `{format, nameTail, lfoState[16]}`
  (version 1's `tracks[].lfo.state`), song `{format, nameTail}`, global `{format}`. The fields
  are the same; only where they sit changed, so a second engine does not inherit OS 1.63's
  residue by accident. `fromJson` takes version 1 too (`elektronData/jsonFirmware.h`: the
  layout of what moves is data). The machine document stays version 1.
- Readers ignore members they do not know. Adding an optional member keeps the version.
- Renaming, removing or re-meaning a member makes it the next version. `fromJson` refuses
  versions it does not know.
- **The contract's own version** (DESIGN-REVIEW-2026-10-02 finding 15): the machine document's
  `contract` (2, this document's) is the page protocol's version: the messages and the commands
  together. `MdModel::contractVersion` holds it; the core writes it into every machine document and
  `mdDeskTest --write-schema` writes it into the schema as a `const`. A page reads it first. It goes up
  with the rule above (a member renamed, removed or re-meant); an added member keeps it.
- **Open for readers, closed for commands.** What the page sends (`$defs/command`) is closed: an
  undeclared argument is refused. What the plug-in writes (every other `$def`: the documents, the
  machine document, the messages) is open: an object the schema lists the members of says
  `"additionalProperties": true`, so a newer writer's member does not fail an older reader. Our own
  writer is still held to what it declares: the tests validate against the schema with those objects
  closed again (`json::Schema::closedForWriter`), and `--write-schema` keeps the documents open
  (`mdDeskTest` fails on a closed one).
- `firmware.format.version` / `revision` are the firmware's own dump format bytes
  (pattern 3/1, kit 4/1, song 2/2, global 6/1). Keep them as received.
- The schema's `$defs/message` is every message the plug-in sends the page and
  `$defs/command` every command the page may send; `command` is generated from the command
  table (`mdDesk::commandTable`, `mdDeskTest --write-schema`), and the unit and firmware tests
  check every published message and every command they send against them (P6).

**What P6 added to the page protocol** (all additive):
- `doc` messages carry `slot`, `pending` (an edit submitted and not yet read back: the core
  publishes pending over observed) and `source` (`dump`, `memory`, `tracked`).
- The machine document carries `lifecycle` (one value: missing, unsupported, loading, booting,
  animating, ready, hwConnecting, hwLost; `desk.firmware`, `desk.boot` and `desk.link` are
  derived from it), `capabilities` (what the engine can do, with `reasons` for what it cannot;
  the page decides from these only), `engines` (the engine map for the engine menu) and
  `history` (undo and redo counts; `desk.undo`.. are kept).
- `{"type":"reset"}` also follows a machine that booted again (a restored project): the
  session outlives the machine, so its documents and undo steps start over.

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
| — | `firmware.lockedRowsField` | Opaque firmware byte, 0 in every dump seen. Keep it |
| — | `firmware.lockPoolHidden` | Only present for factory data with residue in unused lock rows. Pass it through untouched |

- `locks` are listed in the firmware's order: (track, param) ascending.
- The firmware re-sorts lock rows into that order.
- A lock step outside `totalLength` is not part of the view. It rides in `firmware.lockPoolHidden`.

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
| kit name | `name` (+ `firmware.nameTail`) | Up to 16 characters, 7-bit. `nameTail` keeps factory leftovers after the NUL |

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
| — | `baseChannel`, `keymap`, `settings` | Kept untouched. `keymap`: the MAP EDITOR's targets (below) |

**GLOBAL settings (P5).** Measured on the firmware (`mdP4ProbeFirmwareTest globals`,
`elektronData/mdGlobal.h` `mdGlobalBits`): a global dump is stored at once but
**applied only when its slot is made active** (SysEx 0x56), so the desk sends 0x56
after every settings push to the active slot.

| Setting | Raw | Verified by |
|---|---|---|
| TEMPO IN external | `syncFlags` 0x01 | MIDI Start waits for MIDI clock |
| CTRL IN off | `syncFlags` 0x10 | MIDI Start/Stop ignored |
| TEMPO OUT | `syncFlags` 0x20 | the machine sends MIDI clock (25 in 0.5 s at 125 BPM) |
| CTRL OUT | `syncFlags` 0x40 | the machine sends Start/Stop |
| PRG CHANGE IN / OUT | `programChange` 0x01 / 0x02 | PC 7 selects A08 / selecting A03 sends PC 2 |
| PRG CHANGE channel | `programChange` bits 2-6 | 0 = BASE (in on the 4 base channels), n = channel n |
| Base channel | `baseChannel` 0-12 | CCs on channel 1 vs 3 |
| MAP EDITOR TRIG | `trigMode` 0 GATE, 1 START, 2 QUE | GATE stops on note off |
| Key map (MAP EDITOR) | `keymap` 0-15 track, 16-143 pattern A01-H16, 144 START, 145 STOP, `null` = `--` (255); any other byte is kept as sent | `mdDeskFirmwareTest <ROM> keymap` (0.3.5): every byte stored as sent; 17 selects A02, 47 B16, 143 H16; 144 starts, 145 stops; 146-254 no effect. A 2008 backup maps notes to 16-47 |
| LOCAL CTRL | `localControl` | stored; no effect seen in the emulator (not verified) |
| TRIG IN A/B | `inputSettings` | shown only (needs pads on the inputs; not verified) |

`md-desk/global` carries a derived, read-only `control` view of these; the page
changes them with `{"op":"globalSet","field":...,"on"|"v"}` (fields tempoIn, ctrlIn,
tempoOut, ctrlOut, programChangeIn, programChangeOut, programChangeChannel 0-16,
baseChannel 0-12, trigMode 0-2, localControl, keymap {note, target 0-145 or null})
and selects the active slot with `{"op":"globalSlot","slot"}`.

### 4.5 `md-desk/machine` (read-only, from `mdDataLink::Session`)

| Field | Meaning |
|---|---|
| `contract` | The page protocol's version (2; section 2) |
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

**The keyboard (P10), the note intent.** `noteOn` {t, vel 1-127, pitch} plays track t;
`noteOff` {t, pitch?} lets it go (without pitch: whatever the track sounds). The
same two commands play the Monomachine (mm-data-contract.md). pitch is semitones
from the track's sound; the page knows no parameter index and no MIDI byte, the
core maps it (`mdDesk/mdDeskKeys.h`, `deskCore/deskNotes.h`):

- the note is the one the active global's MAP EDITOR gives the track (the manual's
  default map without a global: C2 track 1 … D4 track 16), on its base channel, at
  vel (firmware test: 30 is about a quarter of 127's peak; the page's C / V step it
  20 40 60 80 100 127, from 100);
- on the sample machines (ROM-nn, RAM-Pn) pitch is a PTCH the machine holds while
  the key is down (manual Appendix A: 3 steps a semitone in the first octave, the
  second octave's 27 / 28 steps spread evenly, 0-127 at the ends), sent before the
  note as the machine's CC (`DevicePort::sendHeldParam`: past the plug-in's
  parameter, so a DAW sees no move and records no automation; an engine without it
  sends it as the live value). It is the working copy's held layer
  (`WorkingCopy::held`), not an edit: no undo step, no expectation, and a memory
  image taken while the key is down is masked with it (it reports the document's
  value there, so the kit never shows the held PTCH as a change). Letting go puts
  back the document's value: an edit made meanwhile is in the document, so it
  wins. SAVE KIT (and SAVE KIT n) puts held values back first; another kit drops
  them;
- other machines play at their own pitch (the result's `note` says so); GND-EMPTY
  and the recorders (RAM-Rn) are refused with the reason;
- one note sounds per track: a later key ends the one before it, and a `noteOff`
  whose pitch is not the sounding one does nothing;
- while live recording `noteOn` is the track's TRIG key (as `recTrig`: a plain
  trig), and the result's `note` says so.

The page says each `note` and refusal once.

Which note (P4, `mdP4ProbeFirmwareTest lockwindow`): the track's next programmed
trig whose step has not started when the turn lands. A turn 8 ms before the step
locks it; at or after the step start it is too late and the lock goes to the
following trig. A note played live at the same moment is not reliable (3 of 9).
The desk names the trig it expects (`mdDesk::nextLockStep`) as
`machine.desk.recLock` {track, param, step}; the page marks that cell until the
read-back shows the real lock. Checked on firmware: the desk said step 9, the
firmware locked step 9.

**UW samples (P3, P9).** `sampleName` (slot 0-47, 1-4 characters) sends the
manual's 0x73. Over MIDI that is all the firmware offers: it has no request for
names, memory in use or sample audio, and it ignores SDS dump requests (measured,
no reply for three slots). So on a real Machinedrum the waveforms, names and
memory cannot be shown (`capabilities.sampleAudio` false, with the reason). P9:
the emulated machine's memory has all of it; see 4.9 for the `md-desk/samples`
document, and a sample file into a ROM slot (`chooseSample`, SDS) on both engines.

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

**Song playhead (0.3.5).** The telemetry message carries `songRow`: the song row the
sequencer plays, 0-based, from main RAM 0x2b18f5 (`md::SongPosition`, one byte; found
with `mdP4ProbeFirmwareTest songrow`: a song A02, A03 ×2, A04, LOOP to row 2, sampled in
the middle of every pass; checked by `mdDeskFirmwareTest songrow`: the row's pattern is
the pattern that plays at every pass, through repeats and round the loop). A LOOP or
JUMP row is never the row: the byte goes straight to its target. The byte runs ahead:
it moves to the next row about two steps before the pass ends (it queues it, as the
pattern byte does; `SONGROW_TRACE=1 mdP4ProbeFirmwareTest <ROM> songrow`), so the desk
publishes the row heard: the byte as it was at PLAY and at each wrap of the playhead
(`deskCore::SongRowHeard`). It is the machine's
only while `machine.songMode` is true and the machine plays: pattern mode and STOP
leave it as it was (STOP twice sets it to 0), so the page marks a row only then. `null`
where the engine cannot read it (HW MIDI). The page moves the mark without a render
(the arrangement cell, the time bar, the What plays line, the LCD's pattern slot).

**The host's tempo (B-030).** In a DAW the plug-in sends `{"type":"host","bpm","follows"}`:
the host's tempo as its playhead reports it (also while the host's transport is stopped),
when it changed (by 0.01 BPM) or a page is new; `follows` is true in a DAW (the plug-in
sets the machine's global to follow the host's clock with `followHost`, at once when the
machine becomes ready, then every 2 s; a refusal is a `result` with `op` `followHost`
that the page logs and shows). While `follows` and the global's `control.tempoIn` is
`external`, the page shows the host's BPM as TEMPO and refuses tempo edits. The
standalone sends no `host` message.

**Chaining and mutes (P4).** `machine.desk.chain` is the firmware's own pattern
chain, read from the MC68331 internal SRAM (`md::ChainAndMutes`: 0x1001f5c active,
0x1001f60 next, 0x1001f64 length, 32-bit patterns from 0x1001f68). The page asks
for one with `{"op":"chain","patterns":[...]}`; the desk checks the machine's rules
(`mdDesk/mdDeskChain.h`: 2-16 patterns, one bank, each once) and presses the keys
the manual describes: BANK held, the TRIG keys held one after another in play
order (pressed and released one by one they only select), with BANK GROUP first
for the other half of the banks, after SET STATUS pattern mode (a chain is pattern
mode's; in SONG mode the firmware plays it but stays in SONG mode). The chain loops and
is what plays next: once the firmware holds it, `pattern.queued` (a pick still waiting
for the pattern end, which the chain replaced, measured) is dropped and the pattern and
sequencer mode are read again. A LOAD PATTERN or a single TRIG
ends it; a pattern dump into a chained pattern does not (measured). So `select`
while a chain is active answers `{"type":"ask","ask":"breakChain","p"}` and sends
nothing; `select` with `chainOk` (or `force`) goes ahead. `chainClear` is LOAD
PATTERN of the current pattern. The Song page's CHAIN palette sends the chain again
at every pad added or taken away (BACK too; 150 ms debounce, the latest wins); while
the keys of one chain are still on their way the next waits in the desk, only the
latest is pressed once the device reports none left (so key runs never interleave and
BANK GROUP is pressed from the group the machine is in then), and `chainClear` waits
for them too. A pick (`select`) drops a chain still waiting. `machine.desk.mutes` is the machine's pattern mute
mask (main RAM 0x28b34a, 16 bits big-endian, bit 0 = track 1), so mutes made in the
machine's MUTE window or by CC 12-15 show too (`mutesSource` = `memory`).

**What plays (the Song page).** The page states it from this document alone
(`mdDeskModel.js playsOf`): `CHAIN A03»A05` while `desk.chain.active` (also in SONG
mode, where the firmware plays the chain), else `SONG nn` (`song.current`) while
`songMode` is true, else `PATTERN` with `pattern.current` (`songMode` null counts as
pattern mode). The Song page has one pattern palette: ARRANGE adds a pad to the song,
CHAIN numbers it into the chain the `chain` command makes.

**Generators and mutation** ([DESIGN-generators.md](DESIGN-generators.md)). The page computes
(`mdDeskGen.js`, pure) and sends the result as two plain edits; the core never sees a recipe.
`{"op":"steps","p","rows":[{t, on:[step...], acc?:[step...]}],"from"?,"to"?}`: 1-16 rows, each
track's trigs in `[from, to)` (default the visible steps) become exactly `on`; a step turned off
loses its locks and marks (as `trig` off), a kept step keeps them, slides stay; `acc` sets the
track's accents in the range (only on its steps), and with `accent.editAll` = 1 it is left out with
a note. One pattern change: one paced dump, one undo step. `{"op":"params","k","values":[[t, i, v]...]}`:
up to 384 kit parameters (i 0-23) of the working kit in one change; `kitDelivery` sends a CC for each
value that changed. A mutation trial sends every apply with one `g`, so it is one undo step back to
the sound before it.
`{"op":"rotate","p","t","by"}` (-63..63): track t's trigs, its own accents, slides and swings and every
lock of the track move `by` steps, wrapping at the pattern's length; steps from the length on stay, the
pattern-wide marks (EDIT ALL) stay. The page sends a train of Alt + arrow presses with one `g`: one undo step.
`{"op":"doublePattern","p"}`: length x2 and the total length grown to hold it, the new half a copy of every
track's trigs, marks and locks and the pattern-wide marks (no new lock rows); refused above 64 steps, and above
32 for a CLASSIC (short) pattern.

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
  load that kit. One that links the kit that plays loads it again from its slot, so
  its unsaved edits go; the desk sends them again as live edits right after the dump
  (`MdMachine::restoreWorkingKit`, `mdDeskFirmwareTest <MD ROM> sampler`). Slot writes that would lose unsaved kit edits answer
  `{"type":"ask","ask":"overwriteKit"|"relinkKit"|"loadKit","command"}`; the page
  sends the command again with `force`. An ask may carry `alternatives`
  (`[{"label","first":[commands]}]`): other ways to go on, a button each, which send
  their `first` commands and then the command with `force`. `kitLoad` over unsaved
  edits offers "Save and load" (`saveKit` first; the wire keeps their order) beside
  "Load without saving". The kit library loads a slot on click (or Enter); arrows only
  move the highlight, since every LOAD KIT relinks the current pattern in EXTENDED.
- Kit names: the bytes up to the NUL, when each is printable ASCII
  (`mdDeskLibrary.h kitNameText`). A slot never written holds the battery RAM's
  bytes (OS 1.63, a fresh machine's K17-K64: DEL 0x7f bytes, or 0x7f then left-overs
  such as `MX KIT 1`, every track GND-EMPTY): no name, so the slot is empty.
Slot writes are undoable in the editor; LOAD and SAVE are the machine's (it keeps
one UNDO KIT).

**HW MIDI (P4).** `{"op":"engine","kind":"hw"|"emu"}` swaps the desk's port. HW:
SysEx and CCs go out through the plug-in's MIDI out (host and physical ports,
`pluginLib::Processor::setExternalMidi`), paced at DIN speed (`mdDesk::DinPacer`,
3125 bytes a second); SysEx that comes in goes to the desk, not the emulated
device, and the device's own MIDI output is held back. PLAY/STOP are MIDI Start /
Stop; panel keys, telemetry, the working kit from memory and the boot LCD do not
exist, so live recording and chaining are refused with the reason and the kit is
`tracked`. Timeouts follow the wire (a pattern is 1.7 s each way); in the background
the 64 kits load first. `machine.desk.engine` = `hw`, `link` = connect / ready /
lost. The page gets `{"type":"reset"}` on a swap and starts its documents over.

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
  recording does not record CCs. P5: with the emulator they run in the plug-in's
  processor (`mdJucePlugin/mdModRunner`, the same pure `mdDesk::ModEngine`, on the
  message thread, CCs through the parameter layer), so they keep moving with the
  editor closed; the desk only edits the setup and shows the processor's values
  (`"runs":"plug-in"` in the `mod` message). Over HW MIDI they stay in the desk
  (there is no playhead to follow there, so they do not move).

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

### 4.8 `gm-audio/devices` (the standalone's audio and MIDI, P6)

Not machine data either: the standalone app's devices, shared by the Machinedrum and
Monomachine Editors (`mdJucePlugin/mdAudioMidiLink.cpp`). JUCE's `AudioDeviceManager`
stays the engine; the page's AUDIO / MIDI panel only renders this document and sends
commands. The link publishes `{"type":"audio","doc"}` on `{"op":"audio"}`, after every
command and whenever the device manager changes.

| Field | Meaning |
|---|---|
| `standalone` | false in a plug-in: the host owns audio and MIDI, nothing else follows and the page shows no engine-menu entry |
| `driver` `{id, list}` | the audio driver (CoreAudio, ...) |
| `output` `{id, list}` | the output device by name |
| `input` `{id, list, muted}` | the input device (`""` = none) and the feedback mute. The input starts muted (JUCE's default); the panel shows the mute instead of JUCE's yellow bar |
| `outputChannels` `[{name, on}]` | the device's outputs and which are active |
| `sampleRate`, `bufferSize` `{value, list}` | current and available |
| `latencyMs`, `running`, `error` | output latency plus one buffer, the device runs, the last command's error |
| `midiInputs` `[{id, name, on}]` | every MIDI input and whether it is enabled |
| `midiOutput` `{id, list:[{id, name}]}` | the MIDI output (`""` = none) |
| `bluetooth` | Bluetooth MIDI pairing is available |

Commands, `{"op":"audioSet", ...}` (the message's own `id` is the request number, so
devices go in `device`): `set` = `driver`/`output`/`input`/`midiOutput` with `device`,
`sampleRate`/`bufferSize` with `value`, `mute` with `on`, `outputChannel` with `index`
and `on`, `midiInput` with `device` and `on`; or `do` = `test` (a test tone) or
`bluetooth` (the system's pairing dialog). Each gets a `result` and a fresh document;
changes are saved with the standalone's settings at once. `{"op":"audioMeter","on"}`
starts or stops `{"type":"audioLevel","in":0-1}` (the input before the mute, 15 Hz).

### 4.9 `md-desk/samples` and loading a sample (P9, UW)

Read from the emulated MD OS 1.63 UW's memory (`elektronData/mdSamples.h`, found with
`mdEditorProbeFirmwareTest samplemap` and `rammap`): never streamed, published as
`{"type":"samples","doc"}` when it changed and after every ready. `md::DeskDevice` reads it on the
audio thread, one slot a block, once the editor has asked, and only when the memory changed and has
been quiet a moment: the flash sector heads, the slot names and the RAM table make a signature it
looks at about 10 times a second; it reads when the signature is the same twice and flash has been
idle for 0.3 s (an SDS import, then the firmware's CLEANING / LOADING, writes flash; a RAM-R take
grows its buffer). The emulator engine picks a new bank up every 250 ms.

| Field | Meaning |
|---|---|
| `bins` | 128: peaks per slot (the overview: light, every slot; the detail is `sampleWave`, below) |
| `capacity`, `used` | DSP2's sample memory in samples (1441792: 12-bit, two to a 24-bit word, X 0x150000-0x1fffff) and what the ROM slots hold. The four RAM buffers share the rest |
| `rom[48]`, `ram[4]` | `{slot, empty, length, rate, loop {start, end} or null, name or null, peaks}`. `peaks` is min, max per bin, -127..127 of full scale; empty for an empty slot |
| `ramReadable`, `ramReason` | The RAM buffers' table was found (else why not) |

Where it is (OS 1.63 UW, measured):
- **ROM slots: the 8 MiB NOR flash**, 0x200000-0x79ffff, a record per slot from a 64 KiB sector:
  `+0` 0x18 (0x7a: a free sector; 0x1a: the record's next sector), `+1` the slot 0-47, `+2` bits (16),
  `+4` period (ns), `+8` length, `+12` loop start, `+16` loop end (32-bit, the low 16-bit word first),
  `+21` loop type (0x7f none), `+22` a word not decoded, `+24` the samples, signed 16-bit big-endian. A
  longer sample goes on in the next sectors, each after a 4-byte head (0x1a, slot, 0xff, 0xff). An SDS
  import writes a new record into a free sector and frees the old one. The 1.63 factory image fills
  slots 1-32.
- **Names: patch RAM 0x7244a**, 5 bytes a ROM slot: 4 characters and their sum (unset slots hold
  noise, which the sum refuses). 0x73 writes them; an SDS sample carries its own 0x73 after its header.
- **RAM slots: DSP2's memory only** (lost at power-off, as on the machine). X 0x147e00 is a table of 64
  entries `{start, length, loop start, rate}` (rate 0x040000 = 44.1 kHz); entries 0-31 are ROM 1-32 as
  played, 32-35 RAM 1-4 (RAM 1 starts right after the ROM samples). The samples are 12-bit codes, two
  to a word, high first, expanded through the 4096-word table at X 0x146000 (signed 24-bit, rising,
  code 0x800 = silence). The ROM samples in DSP memory are the same codes after the firmware's filter
  and companding, so the page's ROM waveforms come from flash, exactly what was stored.

**A slot's detail** (`sampleWave {bank: "rom"|"ram", slot, bins}`, the desk's own row; `bins` 1-8192):
`{"type":"sampleWave", bank, slot, length, rate, bins, scale: 32767, peaks}`, min, max per bin of the
slot's own samples, 16-bit, at most a bin a sample (`elektronData::mdWavePeaks`). The page asks for one
slot at the canvas' width in device pixels (`waveBins`: steps of 256, so a small resize asks nothing),
draws the overview until it comes, and drops what it holds with every new `samples` document. The
desk computes it from the bank it holds (the device keeps each slot's samples with the bank: ROM from
flash as stored, RAM through the expander as 16-bit), on the message thread, never on the audio thread.

**Audition** (`audition {bank, slot}`, `auditionStop`; the desk's rows): the slot plays once from the
start at its own rate on the plug-in's own output, so a DAW hears it: `md::DeskDevice` mixes it into
Main A/B after the machine's audio (`elektronData::AuditionMixer`: linear resampling to the machine's
44.1 kHz, which the plug-in resamples to the host's rate like the machine's own audio; a 64-frame fade
when one replaces another). One at a time: another audition replaces it; the page stops it when its
waveform leaves the page (another slot, track or workspace). Hand-off: the message thread owns every
request (the samples, shared and never changed) and publishes it through one atomic pointer; the
audio thread takes no lock and allocates or frees nothing, and says which requests it may still read,
so the message thread frees only older ones. `{"type":"audition", state: "playing"|"stopped", bank,
slot, length, rate}`: playing when it starts, stopped when it is stopped, played out or replaced (or
the device is gone); never per frame, the page moves its playhead from `rate` itself. A real
Machinedrum has no sound of its own here: `capabilities.sampleAudio` is false, the page's PLAY keys
are disabled with its reason and the desk refuses both rows with it. Measured on the emulator
(`mdDeskFirmwareTest <MD ROM> samples`): ROM-41's detail at 4096 bins is exactly the peaks of what was
sent, and its audition is the sent samples resampled 32 -> 44.1 kHz to within 1e-4.

**Loading a sample** (`chooseSample {slot}`, the window's row): the window's native chooser
(WAV or AIFF; drag and drop is not offered, see P7), the session reads the file and hands its bytes
to `mdDesk::Desk::loadSample`; nothing passes through the page. Pure steps (`elektronData`):
`decodeAudioFile` (WAV PCM 8/16/24/32, float 32/64, extensible; AIFF / AIFC PCM, `sowt`, `fl32`),
`prepareMdSample` (channels mixed to mono; above 44.1 kHz a short low-pass and linear interpolation
down to 44.1 kHz, lower rates kept; cut to the memory left: capacity minus the other ROM slots minus a
second for each RAM buffer; on a real machine the free memory is not known; peaks above full scale
clipped), the name from the file name (`mdSampleNameFrom`: 1-4 of A-Z 0-9 -, else `SMPL`), and
`mdSdsDump` (16-bit SDS: header, 0x73 name, packets of 40 samples). `mdDesk::SdsSender` sends it
paced by the machine's handshake: the header, after its ACK the name and packet 0, then each packet
after the ACK of the one before; NAK sends it again (3 times at most), WAIT holds (30 s at most),
CANCEL ends it, and a NAK for the next packet after a resend counts as the ACK (OS 1.63 does that
when an ACK was lost). A machine that does not answer the header within 2 s gets the packets
without a handshake, one every 60 ms (the SDS rule). While a sample goes out the adapter sends no
other SysEx (they wait and follow it) and polls nothing. Measured on the emulator: 400 packets (0.5 s at
32 kHz) in 0.63 s of machine time, no retries; the waveform read back matches what was sent bin for
bin (`mdDeskFirmwareTest <MD ROM> samples`).

`{"type":"sampleLoad", slot, state, file, name, rate, length, sent, total, handshake, retries, notes,
text}`: `sending` (at most one message a per cent), then `done`, `failed` or `cancelled`; a refusal
before anything is sent is `failed` with slot, file and text. `sampleCancel` stops it (SDS CANCEL).

**A RAM slot cannot take a file**: an SDS sample numbered 48 (the first slot after the ROM slots)
gets no answer and nothing is stored, flash or DSP table (measured, `samplemap`). The page shows Load
sample disabled on a RAM slot with that reason; `loadSample` refuses slots 48-51 the same way.

## 5. Hardware limits (`elektronData::validate`)

| Document | Limit |
|---|---|
| pattern | `slot` 0-127. `totalLength` 16/32/48/64. `length` 1..`totalLength`. `tempoMultiplier` one of four. `kit` 0-63. `accentAmount` 0-127. `swingAmount` 0-9830 (80 %). `editAll` 0/1. Steps below 64 (32 for classic dumps). **At most 64 locked (track, param)**, param 0-23, values 0-127 |
| kit | `slot` 0-63. `model` an OS 1.63 machine. Parameters, levels and master effects 0-127. LFO track 0-15, param 0-23, shapes 0-5, update 0-2. Groups 0-15 or `null`. Name 7-bit |
| song | `slot` 0-31. 1-256 rows, the last one `end`, no other `end`. Pattern rows: pattern 0-127, repeats 0-63, 0 <= start < end <= 64, tempo 30-300 BPM or `null`. Loop: an earlier target, repeats 0-63. Jump: a later target |
| global | `slot` 0-7. `routing` A-F/MAIN. `tempo` 30-300. `keymap` 0-254 or `null` |

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
