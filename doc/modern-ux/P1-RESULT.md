# P1 result: the MD Desk data layer (Machinedrum OS 1.63)

- Branch `p1/md-data-layer` (from `p0/modern-ux-proof`), 2026-09-27, Apple silicon, build dir `temp/cmake_p0` (P0 configure).
- Monomachine is out of scope. It will reuse this design once its ROM is available.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | Codecs: pattern, kit, song, global | **GO.** Byte-exact on every firmware dump. A few firmware bytes stay opaque (§1) |
| 2 | Corpus round trip | **GO.** 464 raw firmware dumps round-trip byte for byte and through JSON. 232 come from the emulator's saved state, 232 are programmed |
| 3 | Plain-data contract + C++ JSON | **GO.** v1 doc + schema. Lossless on the whole corpus. Limits enforced in C++ |
| 4 | Transport glue | **GO WITH CAVEATS.** Kit and song pushes are not live the way pattern pushes are. The queued pattern is only partly observable (§4) |
| 5 | This report | Below. P2 recommendation in §6 |

**Overall: GO for P2.** The firmware accepts every dump silently, including
invalid ones. `validate` has to stay in front of every push.

---

## 1. Codecs (`source/elektron/md/elektronData/`)

Pure values in and out. No JUCE, device or I/O (only the corpus test executable links `baseLib` for directory walking).

| Dump | Command | Size | Layout source |
|---|---|---|---|
| Pattern | 0x67 | 5,410 B (extended). 2,763 B classic supported | P0, plus the JSON contract below. Extended 64-step dumps, per-track and pattern-wide accent/slide/swing, 64 pooled locks, tempo multiplier, kit link |
| Kit | 0x52 | 1,233 B | Diffed firmware dumps around single edits: 0x55 name, 0x5b machine, 0x5d-0x60 master FX, 0x62 LFO, 0x65/0x66 groups, CC params |
| Song | 0x69 | 26 + 12 × rows + 5 | Each 10-byte row is 7-bit packed on its own. Semantics verified by **playing crafted songs** |
| Global | 0x50 | 197 B | Routing via 0x5c, tempo via 0x61, extended mode via SET STATUS 0x20 |

**Verified semantics**
- Song rows:
  - `0xfe` = LOOP/JUMP/HALT and `0xff` = END.
  - Target before the row = loop, after = jump, equal = halt, as the manual says.
  - Loop repeats 2 play the body 3 times; 0 = infinite.
  - Pattern-row repeats 2 = 3 passes.
  - `end` is exclusive: start 4, end 12 plays steps 4-11.
  - Mute bit 0 = track 1 (row RMS 0.0023 muted vs 0.10 unmuted).
- Tempo multiplier 0-3 = 1X, 2X, 3/4X, 3/2X. Step periods at 125 BPM: 120.0, 60.0, 159.9, 80.0 ms.
- Swing: stored = (percent − 50) × 16384 / 50.
  - Factory values are exact images of integer percents.
  - Onset timing on firmware: 65 % played 64.9 %, 75 % played 73.9 %.
- Accent: stored = display × 127 / 15. Every factory value fits.
- Machine models: UW machines set bit 7. ROM-05 assigned by SysEx stores 0x84.

**Firmware normalisation (the brief asked to match it)**
- The firmware **stores every received dump exactly as sent**:
  - 232 of 232 programmed dumps read back identical.
  - Edge probes: length 40 > total 32, swing 12000, kit 70, multiplier 5, undefined model, LFO shape 9, lock-mask bit 28, a lock on a step without a trig. All kept verbatim.
  - So there is nothing to mirror on receive. `validate` must refuse these values instead.
- Songs are cut to **256 rows**. A 257-row song reads back with 256.
- **A song without END reads back with 256 rows** of whatever memory held. END is mandatory.
- Lock-row order: the firmware sorts rows by (track, param). P0 found this and the codec keeps it.
- **Correction to P0.** "Unused lock rows are zero" holds only for patterns the firmware has edited. Factory patterns hold 0x00/0xff residue in unused rows and past the total length.
  - The codec keeps it byte-exact.
  - The JSON carries it as run-length `lockPoolHidden`. It is dropped when the UI adds or removes a locked parameter.
- OS 1.63 always sends extended dumps, even in CLASSIC mode. The 2,763-byte classic layout is only tested synthetically.

**Opaque (round-tripped, not interpreted)**
- 31 LFO state bytes per track.
- The pattern byte 0xb6 (`lockedRowsField`). It is 0 in every dump; P0 guessed "locked rows" and nothing confirms it.
- The song row byte 1. It is always 0.
- The global settings after the tempo: sync, local, input, program change, trig mode.
- Global keymap values 16-31. They appear in the Elektron default map; the manual maps notes 64-89 to patterns.

## 2. Corpus (`elektronDataCorpusTest`)

**Source.** The standalone's saved device state, read-only:
- The file is `~/Library/Application Support/Gearmulator MD.settings`, where the `filterState` payload holds `MDST` with 1 MiB of patch RAM.
- It is booted headless with `mdDataLayerFirmwareTest corpus`, which requested all slots.
- The content is the firmware-initialised factory set plus P0's edits. One global and 4 patterns differ from a fresh boot.

**Composition**
- State set, 232 dumps:
  - 8 globals.
  - 64 kits using 76 distinct machines.
  - 128 patterns: 64 non-empty, total length 16/32 only, up to 34 locked parameters, 42 with locks.
  - 32 songs: one real one, "UW DEMO", 65 rows.
  - No 48/64-step patterns, no full lock pool, no loops or jumps. Hence the programmed set.
- Programmed set, 232 dumps. Random but valid values pushed through the codec, then the **raw firmware read-back** was saved:
  - 128 patterns: all four total lengths and all four multipliers. 32 use the full 64-lock pool.
  - 64 kits using 134 machines, with random LFOs, groups, master FX and names.
  - 32 songs up to 256 rows: 1,055 pattern rows, 143 loops, 134 jumps, 134 halts.
  - 8 globals with random routing and tempo.

**Result:** 464 of 464 dumps give `encode(decode(x)) == x` and `fromJson(toJson(v)) == v`. All of them validate clean.

**Where the data lives**
- Factory-derived dumps stay local: they are ROM content.
- `elektronData/testdata/` commits 11 programmed read-backs. Among them: a pattern with all 64 locks, one with none, a 256-row song and an END-only song. These run in ctest (UnitTest label).
- To run a full local backup: `-DELEKTRONDATA_CORPUS_DIR=<dir>`.

## 3. Contract

- `doc/modern-ux/data-contract.md` covers the rules, units, the mockup `S` → contract mapping, limits and the unsaved-state split.
- `doc/modern-ux/md-data-contract.schema.json` is JSON Schema 2020-12. It defines `md-desk/pattern`, `kit`, `song`, `global` and `machine`.
- Per the gap review, the contract is **per pattern**. Every pattern document carries its kit link; kits are separate documents.
- C++ lives in `elektronData`:
  - `json.h` is a small dependency-free JSON library.
  - `mdJson.h` has `*ToJson` / `*FromJson`. Errors carry JSON paths.
  - `mdValidate.h` has the hardware limits and the unit conversions.
- The 464 corpus documents validate against the schema. The check used a minimal in-house validator; the jsonschema package is not installed here. The schema cannot express cross-field limits, so those are C++-only.

## 4. Transport glue (`source/elektron/md/mdDataLink/`)

`mdDataLink::Session`:
- Takes value-level intents: request/push pattern, kit, song and global; select pattern; load/save kit and song.
- Emits SysEx through a `Send` function.
- Decodes replies fed to `onSysex` back into values.
- Keeps the machine state as plain data. `stateToJson` exposes it as `md-desk/machine`.
- Refuses invalid values before sending.

The plug-in's `StudioLink` now uses it; there is no second transport. The mdStudio page behaves as in P0: the standalone self-test measured click → firmware read-back at 56, 59 and 59 ms. Unit test: `mdDataLinkTest`.

**What the firmware does (measured, emulated time, A01 at 125 BPM)**

| Action | Result |
|---|---|
| Pattern push, via Session | Read-back value equal after **47.9 ms**. Takes effect in the playing pass (P0) |
| Kit dump to the playing kit's slot | UART drains in **4.4 ms**, request → reply in 5.8 ms. Writes the **stored slot only**: the playing sound does not change (RMS 0.0750 vs 0.0745 baseline, working kit unchanged) |
| Kit dump + LOAD KIT | LOAD KIT drains in 1.5 ms and the change is **audible 6.7 ms** after it. Session `pushKit(StoreAndLoad)`: 7.3 ms on the wire, read-back equal after 11.6 ms |
| Audio continuity, kit pushes | No dropout. Longest digital silence 24 frames (baseline 25). Largest sample jump 0.454 (baseline 0.480). Step clock moved by one block (64 frames, 1.45 ms) on 1-3 of 67 steps: the same MIDI jitter P0 saw |
| Song dump while the song plays | 256 rows = 3,103 B: **11.6 ms** on the wire, read-back after 26.1 ms. **The playing song ignores it.** LOAD SONG while playing is ignored too. After STOP, LOAD SONG, PLAY the new rows play |
| Pattern select (LOAD PATTERN) while playing | Queued. Sent at step 8 of 32: the status reply switched at +2651 ms (step 30), the kit status at +2767 ms (step 31), the audible switch at the wrap, +2791 ms. So **status reports the new pattern about 2 steps early**. It never reports "queued". When stopped, the switch is immediate (2.9 ms) |
| Kit-per-pattern, EXTENDED | A pattern linked to another kit loads it at the switch and **discards unsaved working-kit edits**. Same kit: the edits survive. CLASSIC: no kit change |
| LOAD KIT / SAVE KIT n | LOAD KIT discards unsaved edits. **SAVE KIT n makes n the current kit** |
| Dump requests | They return **stored slots, never the working kit**. Reading the working kit needs SAVE KIT, which overwrites a slot. Session never does that on its own |
| Project save (patch-RAM snapshot restored into a new machine) | Pattern and song dumps and **the unsaved working kit** all survive. The working kit is in battery RAM |

**How the transport handles unsaved and queued state**
- `workingKit`:
  - `edited` after UI live edits (`noteWorkingKitEdited`), or after a Store push over the playing kit's slot.
  - `clean` after LOAD KIT or SAVE KIT, or when the status shows another kit became current.
- `selectWouldDiscardKitEdits(p)` lets the UI warn before a linked switch.
- `queuedPattern` is held from `selectPattern` until the status reports the switch.
- `songReloadNeeded` is set when the current song's slot is rewritten.

**Caveats**
- The queued pattern is known only when **we** selected it.
  - Queues made on the panel, by MIDI notes or by chaining are invisible to status.
  - RAM candidates on OS 1.63, gated like the playhead, not yet used:
    - `0x288c8e` holds the queued pattern. Its value 0 is ambiguous between "A01 queued" and "none"; a flag byte has not been found.
    - `0x28d205` and `0x2ab2eb` hold the current pattern.
  - Chains are not decoded.
- Status polling costs about 1.5 ms of sequencer jitter per burst, as P0 found. Poll gently (≤ 10 Hz) or move to RAM telemetry.
- Pushing to the active global slot while playing was not tested.
- Real DIN MIDI would be about 100× slower than the unpaced emulated UART:
  - a kit about 0.4 s
  - a pattern about 1.7 s
  - a 256-row song about 1.0 s

## 5. Surprises

1. **No validation in the firmware.** Out-of-range values are stored and later played. Our validator is the only guard.
2. **Kits and songs have a working copy; patterns do not.** Pattern dumps are live. Kit and song dumps are "stored, not loaded".
3. **SAVE KIT n changes the current kit.** It is not a "save as" that leaves the current kit alone.
4. **The status reply runs ahead of the audio.** It switches about two steps early, so the UI must not treat it as the audible pattern.
5. The P0 "unused lock rows are zero" rule is false for factory data (§1).
6. Unsaved kit edits survive a DAW project save. So "not saved on the machine" is really its own state, separate from "not saved in the project".

## 6. Recommended P2: the mockup becomes the real mdStudio page

1. **Documents replace `S`.**
   - The page holds `patterns[slot]`, `kits[slot]`, `songs[slot]`, `global` and `machine`, exactly the contract's documents.
   - Load them lazily. The current pattern and its linked kit come first, then the rest in the background at about 30 ms per pattern.
2. **Intents up, documents down.**
   - The page sends small edit intents (`toggle t s`, `lock t p s v`, `param t i v`, `row …`) over `gmbridge://`, because JUCE 7's URL bridge cannot carry 20 KB documents.
   - C++ applies the pure `elektronData` edit, validates, pushes via `Session` and returns the firmware read-back as a JSON document through `evaluateJavaScript`.
   - Revisit JUCE 8 native functions only if intent traffic grows.
3. **Live sound edits stay on CC** through the existing parameter layer. The page marks `Session::noteWorkingKitEdited`. Dumps are for structure: machine changes, LFO routing, groups and names.
   - Debounce kit pushes.
   - Default the kit push to `StoreAndLoad` only on an explicit "apply".
4. **Honest state in the UI.**
   - A "kit not saved on the machine" badge with a Save button that calls `saveKit(current)`.
   - A warning dialog when `selectWouldDiscardKitEdits`.
   - Shown separately from the project-dirty flag.
   - A "next: A05" LCD field from `machine.pattern.queued`.
   - A song "changes apply after stop" banner from `reloadNeeded`.
5. **Telemetry off the UI thread.** Publish playhead, current and next pattern from the audio thread through atomics, as P0 recommended, gated to OS 1.63. Status polling becomes the fallback for other firmware.
6. **Data tables out of the mockup.**
   - Move the per-machine parameter names (the mockup's `MACH`) into a data file next to `mdMachines`.
   - Align the names with the manual's (`P-I-*`, `GND-SIN`).
7. **Out of scope so far:**
   - UW sample slots: names via 0x73, data via SDS.
   - Pattern chaining.
   - Copy/clear/undo.
   - Snapshots.
   - Monomachine.

   These should be decided in P2 planning. MM needs its ROM first.

## 7. Files and how to re-run

- `elektronData/`:
  - Codecs: `mdKit.*`, `mdSong.*`, `mdGlobal.*` and `mdPattern.*`, with shared I/O in `dumpIo.h`.
  - Supporting modules: `mdMachines.*`, `mdCommands.*`, `mdValidate.*`, `json.*` and `mdJson.*`.
  - Tests: `elektronDataTest`, `elektronDataCorpusTest` and `testdata/`.
- `mdDataLink/`: `Session` and `mdDataLinkTest`.
- `mdLibTest/` (manual, needs the ROM):
  - `mdDataCaptureFirmwareTest dump|script`: layout derivation.
  - `mdDataLayerFirmwareTest corpus <rom> <out> [state.mdst]`: about 20 s.
  - `mdDataLayerFirmwareTest probe <rom> [session|persist|link|queueram|queue|kit|song|mutes|normalise]`: about 3.5 min for all.
  - `mdFirmwareSession.h`: the shared harness session.
- `mdJucePlugin/mdStudioLink.*`: now backed by `mdDataLink::Session`.
- Tests: `ctest -L UnitTest` passes 44 of 46. The 2 failures are the known `synthLib` ones from P0.
- Standalone config: to run the self-test I switched the skin in `~/Documents/Gearmulator Preview/Machinedrum/config/Gearmulator MD.xml` to mdStudio. That file and `~/Library/Application Support/Gearmulator MD.settings` were restored byte for byte afterwards.
