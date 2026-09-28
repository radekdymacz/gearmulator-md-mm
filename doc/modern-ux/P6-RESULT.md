# P6 result: a simple core for the Machinedrum and Monomachine Editors

- Branch `p6/simple-core` (from `p5/md-editor-final`), 2026-09-28, 43 commits, 168 files (+23.3k / -7.9k lines).
- Design: [DESIGN-P6-simple-core.md](DESIGN-P6-simple-core.md). How to build on it: [FOUNDATION.md](FOUNDATION.md).
- Before: [DESIGN-REVIEW-2026-09-28.md](DESIGN-REVIEW-2026-09-28.md), three independent reviews of P5 (simplicity, data, architecture).
- Method: hammock first, then six rounds of fix → three fresh read-only reviews (simplicity, data, architecture) → fix, until the reviews found only polish.
- The user-facing behaviour is unchanged, except where noted under "Visible changes".

## Verdict

| Item | Verdict |
|---|---|
| Every section simple, data-driven, decoupled | **GO.** No section is COMPLECTED, CODE-DRIVEN or COUPLED any more; the final check found one bug (fixed) and otherwise only polish |
| A foundation to develop on | **GO.** Adding an engine, a document kind, a command or a workspace is a short, tested step list ([FOUNDATION.md](FOUNDATION.md)), with contract tests that fail until every table, schema and page table agrees |
| Unit tests | **PASS**: 72 of 74. The 2 failures are the known `synthLib` ones (`synthLibMidiClockTimingTest`, and `synthLibAudioTest`, which depends on it) |
| Firmware tests | **PASS**: `mdDeskFirmwareTest` default, `hw`, `p4`, `playload`; `mmDeskFirmwareTest`; `mdSessionFirmwareTest`; `mmBootFirmwareTest`. Every published message is on the contract (0 off) |
| In-plugin self-tests (diagnostics build) | **PASS**: MD `1`, `p4`, `p4hw`, `p5`, `p6audio` (7/7); MM `1` (11/11), `mmcpu`, `p6audio` (7/7) |
| Page tests | **PASS**: `mdDeskModelTest.js` (35/35, fixtures validated against the schema), `mmConvertTest.js` (300 checks); both sync scripts `--check` clean, including the page contract check |
| Release build (diagnostics OFF) | **PASS** after one fix (the editor did not compile without diagnostics since round 4, `9625ea01`). No self-test code in the release binaries |
| auval | **AU VALIDATION SUCCEEDED** for `aumu Tmdr GmPv` (MD) and `aumu Tmno GmPv` (MM) |
| Radek's config and settings | Restored after every run; hashes MD xml `cbbc7cda…`, MD settings `fc68a82e…`, MM xml `0c18a310…`, MM settings `8a06fa3f…` |
| Install | **Not installed** (see "What Radek must do") |

## Scorecard: before and after

Ratings from the three read-only reviewers. "Before" is the P5 review; R1 to R5 are the fresh reviews after each round; "Final" is the last quick check on the round-6 code.

### Overall

| Angle | Before (P5) | Final (P6) |
|---|---|---|
| Simplicity | COMPLECTED (three coordinators braid concerns) | MOSTLY SIMPLE; deskHost, deskWire, elektronData and the page host SIMPLE |
| Data orientation | MOSTLY DATA (pages and Desk place-oriented) | MOSTLY DATA-DRIVEN; deskWire and elektronData DATA-DRIVEN |
| Architecture | MODERATE (the engine seam at the wrong level) | MOSTLY DECOUPLED; deskCore, deskHost, elektronData, the page host and the MM seam WELL-DECOUPLED |

### Simplicity by section

| Section | R1 | R3 | R4 | R5 |
|---|---|---|---|---|
| deskCore | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE |
| deskHost | – | SIMPLE | SIMPLE | SIMPLE |
| deskWire | – | – | SIMPLE | SIMPLE |
| mdDesk adapter/model | COMPLECTED | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE |
| mmDesk adapter/model | COMPLECTED | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE |
| elektronData JSON/schema | MOSTLY SIMPLE | MOSTLY SIMPLE | SIMPLE | SIMPLE |
| plug-in session and engines | MOSTLY SIMPLE | MOSTLY SIMPLE | SIMPLE | MOSTLY SIMPLE |
| page host/editor/diagnostics | MOSTLY SIMPLE | MOSTLY SIMPLE | SIMPLE | SIMPLE |
| MD page JS | COMPLECTED | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE |
| MM page adapter + seam | COMPLECTED | COMPLECTED | MOSTLY SIMPLE | MOSTLY SIMPLE |
| schemas and tests | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE | MOSTLY SIMPLE |

### Data orientation by section

| Section | R1 | R3 | R4 | R5 |
|---|---|---|---|---|
| deskCore | MOSTLY | MOSTLY | DATA-DRIVEN | MOSTLY |
| deskHost | – | MOSTLY | DATA-DRIVEN | MOSTLY |
| deskWire | – | – | DATA-DRIVEN | DATA-DRIVEN |
| mdDesk adapter/model | MOSTLY (`apply` CODE-DRIVEN) | MOSTLY (`apply` CODE-DRIVEN) | MOSTLY (`apply` data-driven) | MOSTLY |
| mmDesk adapter/model | MOSTLY | MOSTLY | MOSTLY | MOSTLY |
| elektronData JSON/schema | DATA-DRIVEN | DATA-DRIVEN | DATA-DRIVEN | MOSTLY |
| plug-in session and engines | MOSTLY | MOSTLY | DATA-DRIVEN | MOSTLY |
| page host/editor/diagnostics | MOSTLY | MOSTLY | MOSTLY | MOSTLY |
| MD page JS | MOSTLY | MOSTLY | DATA-DRIVEN | MOSTLY |
| MM page adapter + seam | MOSTLY (weakest) | MOSTLY (weakest) | MOSTLY | MOSTLY |
| schemas and tests | MOSTLY | MOSTLY | MOSTLY | MOSTLY |

(R5's reviewer rated more strictly than R4's; its findings were the ones round 6 fixed.)

### Architecture by section

| Section | R1 | R3 | R4 | R5 |
|---|---|---|---|---|
| deskCore | MOSTLY | MOSTLY | WELL | WELL |
| deskHost | – | WELL | WELL | WELL |
| deskWire | – | – | MOSTLY (linked the emulator) | MOSTLY |
| mdDesk adapter/model | MOSTLY | MOSTLY | MOSTLY | MOSTLY |
| mmDesk adapter/model | MOSTLY | MOSTLY | WELL | MOSTLY |
| elektronData JSON/schema | WELL | WELL | WELL | WELL |
| plug-in session and engines | MOSTLY | MOSTLY | MOSTLY | MOSTLY |
| page host/editor/diagnostics | MOSTLY | MOSTLY | WELL | WELL |
| MD page JS | MOSTLY | MOSTLY | WELL | MOSTLY |
| MM page adapter + seam | COUPLED | MOSTLY | WELL | WELL |
| schemas and tests | MOSTLY | MOSTLY | MOSTLY | MOSTLY |

## What changed (the shape)

- **deskCore** (pure, no JUCE; links only `elektronJson`): `Core<Model>` keeps observed and pending documents, the history and one ordered outbox; `Desk<Model, Adapter>` routes by the command table and holds whichever adapter the engine gives it; `AdapterBase`; the lifecycle as a row table; `KindSpec` (a document kind as one record); `WorkingCopy` (one policy for the kit that plays, both machines); `KitState`; `Ask` (one question per command, `withAsk`); `ModEngine`; the contract checks (`deskContract.h`).
- **deskHost**: the plug-in's command table with Action and actor columns; **deskWire**: MIDI as bytes (`MidiWire`, encoders, `wirePort` per model), pure, never the emulator.
- **mdDesk / mmDesk**: the model (kinds, pure edits as op → function tables, the command table as vocabulary, the questions) and the adapter (`MdMachine`, `MmMachine`) behind a narrow interface (`MdAdapter`, `MmAdapter`) with no protocol getters.
- **Session**: owned by the processor, so documents, undo and pushes outlive the window; the engine map is typed records (`EngineRecord<DeskT, EngineT>`).
- **Contract**: closed commands (`additionalProperties: false`); the command, lifecycle, doc kind, source and ask enums are generated from the C++ tables (`--write-schema`); every published message is validated in the tests, the plug-in's own messages in `mdSessionFirmwareTest`; the pages' sends and capability tables are checked against the schema.
- **Pages**: the MD view is a value with an explicit optimistic overlay; the MM page is the approved mockup plus a host seam (no DOM in the adapter); both read the lifecycle, input, capabilities, clipboard and the catalogue's enumerations and counts; the transport comes only from telemetry.

## Visible changes

- A command that would lose two things (a chained pattern switch with unsaved kit edits; a slot write over the kit that plays) asks one question that names both.
- The MD's old "Save kit, then …" third button is gone: asks have two buttons.
- In a DAW, HW MIDI is disabled with the reason "HW MIDI works in the standalone app" (the plug-in has no MIDI output); it works in the standalone.
- Reload Kit while the kit's saved slot is still being read says so, instead of "matches its saved slot".
- MM library writes to a slot not read yet are refused with "Still reading this slot from the machine", and the view is not changed.

## What stays, and why (remaining polish)

None of these is a bug; each was judged not worth another round.

- `AdapterBase::m_known` mirrors what the core has observed. It is the adapter's own fact (what it has read), used where no view is passed (SysEx and probe paths).
- `mdDataLink::Session` still keeps its own inferred working-kit flag for its standalone users and tests; the editor no longer reads or sets it (one rule: `kitStateOf` over the view).
- `Desk::Port` still carries the whole `DevicePort` for the default adapter, and the Desk offers the default adapter's protocol facts (`linkState`, `isReady`, `current*`) for tests through a `dynamic_cast`. Moving them to free functions touches about 60 test lines for no behaviour.
- `onReadyExtra` and `Port.ready` are two hooks for one event; `DinPacer` stays in deskCore (the adapters use it directly).
- The MD solo is page state that drives machine mutes (UX policy; revisit if mutes must survive the window closing).
- The shared schema parts (engines, history, result, error, mod) are still written twice (MD and MM schemas); the tests keep them honest both ways.
- `g_published` in the model tests is a global filled by earlier tests (coverage is correct while `main` keeps its order).
- Small: MM `setupOps` repeats `"modSet"`; `mdDeskModelTest.js` treats a missing `python3` as success; the page validator skips sibling keywords after a `oneOf`; `mdSessionFirmwareTest` builds `audioLevel`/`openAudio` by hand; the pattern-select question repeats the session's link rule.

## Commits

`5d580c39` (hammock design) … `9625ea01`, 43 commits on `p6/simple-core`, author radekdymacz. The main steps:

- `c5d53d18`–`fd9e56f7`: deskCore, the MD Desk split into a core and an adapter, the MM desk on the same core, the processor owns the desk.
- `cd09d88c`–`63c40730`: one generic Desk and session, facts-driven timing, contract both ways, diagnostics off by default, FOUNDATION.md.
- `7f89395e`–`4c3dd4a3` (round 3): the working kit as its own document, one adapter base, the plug-in host table, the MM view seam.
- `16765302`–`a7c0cdd6` (round 4): the edit layer as tables, kinds as records, asks from the plug-in, closed commands, deskWire.
- `ee5c45ad`–`90c341c9` (round 5): one ask names every loss, narrower adapters, the transport only in telemetry, wirePort, typed engine.
- `b5e1c60c`–`9625ea01` (round 6): the held kit, one KitState, generated asks and slot ranges, op maps checked, page and plug-in bug fixes, the release build fix.

## What Radek must do

- **Install:** not done on purpose. The release agent moved the installed MD/MM VST3s to `~/Library/Audio/Gearmulator P6b backup 2026-09-28/`, and the v0.1.0-alpha `.pkg` files go into `/Library`. The P6 release build is in the build folder (`bin/plugins/Release/{AU,VST3,Standalone}`, diagnostics OFF, auval passed). To try P6 instead of the alpha, copy those bundles into `~/Library/Audio/Plug-Ins/` (and remove the `/Library` copies first, or the host sees two).
- **Real hardware over HW MIDI** is still untested (as in P5); the HW path is covered by the firmware rig, which now uses the plug-in's own `wirePort`.
- **Merge:** `p6/simple-core` is pushed; merging it into the release line is Radek's call (no PR was opened).
