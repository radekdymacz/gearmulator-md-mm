# MM-P1 result: the Monomachine data layer (MM OS 1.32B)

- Branch `mm/editor`, 2026-09-27, Apple silicon, build dir `temp/cmake_mm`.
- ROM used in place from `~/Downloads` (SHA-1 `11a37460…e605`). No dumps of factory content are committed.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | Byte-exact codecs: pattern, kit, song, global | **GO.** 575 of 575 dumps re-encode byte for byte (288 factory, 287 programmed) |
| 2 | Layouts | **GO for everything the editor edits.** Every field the mockup uses has an offset found on the firmware. 23 bytes of the pattern, 5+4 of the kit and a few global bytes stay opaque and ride along (§2-§5) |
| 3 | JSON contract + schema | **GO.** `mm-data-contract.md` and `.schema.json`. value -> JSON -> value is exact on all 575; all 575 documents pass the schema |
| 4 | Validation | **GO.** 62 pooled locks, lock rows = locked parameters, pools, ranges. The firmware stores invalid dumps verbatim, so this is the guard |
| 5 | Tests | `mmDataTest` (35 checks) and `mmDataCorpusTest` (13 committed programmed read-backs) in ctest, UnitTest label. MD tests unchanged and green: 48 of 50, the 2 known `synthLib` failures |

**Overall: GO for MM-P2.**

---

## 1. Method: the layout lab

The manual calls the dump layouts "separate documentation". They were found on
the firmware, one edit at a time:

- `mmEditorProbeFirmwareTest <ROM> lab <script>` runs a line script of panel keys, chords, held keys, knob turns (DATA ENTRY A-H and LEVEL), MIDI notes, CCs and SysEx, and prints the dump diff after each `psnap` (pattern), `ksnap` (SAVE KIT to a scratch slot, then the kit), `ssnap` (song) or `gsnap` (global).
- It starts from the empty factory pattern (E-H banks are empty), so a diff shows one field.
- `lcd` renders the firmware's LCD, so each screen's labels and values could be read and matched to the bytes.
- **Knob pulses are damped in menus:** about four pulses per value step (the swing screen, the song editor, the transpose screen). Locks take one value per pulse.
- MIDI notes on the auto channel (9) while a TRIG key is held set the pitch; several held notes make a chord. This is the SFX-60's keyboard path without a keyboard.

The wire form was settled in MM-P0: 7-bit packing of the firmware's run-length
stream, reproduced exactly (`mmDump`).

## 2. Pattern: 6,520 bytes

The full offset table is in `elektronData/mmPattern.h`. Highlights:

- **13 step masks**, 6 tracks x u64 each, bit s = step s: AMP, FILTER, LFO trig tracks, NOTE OFF, MIDI trig, MIDI NOTE OFF, trig (the step holds a trig), chord, MIDI note, slide, swing, MIDI slide, MIDI swing.
  - A TRIG key in ALL sets amp, filter, lfo and trig plus a default pitch (48); after TRIG SELECT it sets only its trig track.
  - A trig without a note byte is pitchless; a trig without amp/filter/lfo is trigless (17 factory patterns have such steps).
- **Notes:** 6 x 64 note bytes, MIDI note numbers, 0xff = none.
- **Locks, 62 pooled:** a lock mask per track and page (8 bytes per track: SYN AMP FLT EFX LF1 LF2 LF3 MIDI), a row count, and 62 rows of 64 values. **Row r belongs to the r-th set bit in (track, page, parameter) order**: a lock added on track 1 after one on track 2 moved track 2's row down (lab21). The rows carry values, 0xff = not locked on that step.
- **MIDI notes and chords are pools of u16 entries** `note << 9 | track << 6 | step`: 400 MIDI notes (a chord is several entries on one step), 192 synth chord notes (the base note stays in the note table). Found by `80 00` = note 64 track 1 step 1, `78 41` = note 60 track 2 step 2.
- **Settings:** length (0x424), multiplier, kit link (LOAD KIT writes it), pattern transpose, per-track transpose / scale / key (synth and MIDI), swing amount (0-30 = 50-80 %), and two arpeggiator blocks (play | ojmp << 4, mode, range, speed, trig switches, length, 16 rhythm/offset steps).
- **Opaque, kept:** 3 bytes at 0x270, 4 at 0x54e, 0x555 (varies in factory data, 0-0x66), 0x14d7, bit 3 of the arp play byte.
- **Factory residue:** pool entries and lock rows past their counts hold leftovers; pattern 45 has 0xffff entries inside its chord count. The contract carries them as hex runs.

## 3. Kit: 698 bytes

Full table in `elektronData/mmKit.h`.

- Name (11), levels (6), then per track 72 bytes: 7 DATA pages x 8, the MIDI page x 8, 8 unknown bytes.
- Machines (6), routing (6: output bits 0-2, **input bits 3-5**; the factory set has 9 = AB + INP A and 17 = AB + INP B on FX tracks).
- ASSIGN: pages, destinations and signed amounts, 6 tracks x 6 sources x 2 rows. Each of the 8 knobs on each of the 5 tabs was turned and diffed (lab12); the second source's bytes (between JOY R/L and JOY U) are taken to be JOY L when mirror is off.
- TRIG POS per track, LEGATO AMP mask (panel), mirror / HPF / LPF / LEGATO FLT / LFO masks (factory set).
- **The machine names per slot come from the firmware's own screens** (lab22: every machine assigned, its main screen read). Several differ from the manual's two-column text: GND-NOIS is ST RED STON, SWAVE-SAW has an empty slot 4, DPRO-BBOX has no TUNE, FX-RINGMOD uses slots 1, 2, 4, 8, DPRO-DENS has no PW. `mm-manual-mapping.md` §13.4-5 is answered by `mmMachines.cpp`.
- The fixed pages (AMP FILTER EFFECTS LFO) and the MIDI page were read the same way.

## 4. Song: 4,816 bytes

A 16-byte header (14-byte name, 2 unknown) and 200 rows of 24 bytes. From the
song editor, with SAVE SONG and a diff:

- pattern (0-127, 0xfe LOOP, 0xff END), target row, repeats (the editor shows repeats + 1; on a loop it is the count, 0 = ∞), mutes, offset, length, row transpose, 6 track transposes, tempo (u16 BPM, 0xffff = keep).
- LOOP / JUMP / HALT share 0xfe; the kind follows from the target against the row, as the manual says.
- MIDI mutes and MIDI track transposes sit where the synth ones' pattern predicts; not confirmed by an edit.
- Factory: one song (MM DEMO, 64 rows); the 23 empty songs' names start with 0xff.

## 5. Global: 264 bytes

MIDI channels (auto, base, span, multi trig, multi map), MIDI sequencer channels
and CL1-4 CCs, CONTROL settings (kept), the multi map (6 x 32), routing mode
(0xfc: AB=MIX, 3xSTEREO, 6xMONO, panel) and master tune (tenths of Hz, panel).
The multi map's last four fields are not confirmed; the editor will show them
read-only with that reason.

## 6. What the firmware does with dumps (programmed corpus)

`mmEditorProbeFirmwareTest <ROM> program <dir>` builds random but valid
documents with the codec (all 4 pattern lengths classes, 62-lock patterns,
4-note MIDI chords, 199-row songs, random kits and non-active globals), sends
them on SYSEX RECV and saves the raw read-backs:

- **287 of 287 read back exactly as sent.** The firmware keeps our run-length choices, which are its own.
- **Invalid values are stored verbatim:** a pattern with length 90, multiplier 7 and a lock mask bit without a row came back unchanged. As on the MD, `validate` must refuse such documents.
- Sending takes a few ms per dump in the emulator (unpaced UART), about 5 s for all 287 plus read-backs.

## 7. Files

- `elektronData/`: `mmDump.*`, `mmLayout.h`, `mmPattern.*`, `mmKit.*`, `mmSong.*`, `mmGlobal.*`, `mmJson.*`, `mmValidate.*`, `mmMachines.*`, `mmCommands.*`; tests `mmDataTest.cpp`, `mmDataCorpusTest.cpp`, fixtures `testdata/mm/` (programmed read-backs only).
- `mdLibTest/mmEditorProbeFirmwareTest.cpp`: `lab` and `program` modes added. `mdFirmwareSession.h`: the MM settles 9 s after power-on instead of the MD's 25.
- `doc/modern-ux/mm-data-contract.md`, `mm-data-contract.schema.json`.
- Local only (not committed): the factory corpus and its JSON, `-DELEKTRONDATA_MM_CORPUS_DIR=<dir>` adds it to ctest.

## 8. Open, carried into MM-P2/P3

- LFO enumerations (PAGE, DEST, TRIG, WAVE, MULT) are stored as 0-127 values that the firmware maps to names; the mapping is not measured yet. The editor will read the names from the firmware's screen by sweeping a value and grouping identical screens, or show numbers.
- The multi map's field order; MIDI mutes and MIDI track transposes in songs; the kit's extra 8 bytes per track.
- Whether a song dump reaches the playing song (the MD's does not).
