# Rich Hickey Review — Machinedrum + Monomachine Editors (2026-09-28)

Scope: everything added on top of joelanders' base `8cea052`, reviewed on `p5/md-editor-final` (includes the `mm/editor` merge): `elektronData`, `mdDataLink`, `mdDesk`, the MM counterparts, `mdStudio*`/`mmStudio*` plug-in code, the web skins, the sync scripts, the contracts and the tests. Three independent read-only reviews (simplicity, data & state, architecture), synthesised here.

## Scorecard

| Dimension | Rating |
|---|---|
| Simplicity | **COMPLECTED** — the lower layers are simple, and three coordinating layers braid concerns |
| Data orientation | **MOSTLY DATA** — the codec, contract, edits and history are values; the pages and the Desk are place-oriented |
| Architecture | **MODERATE** — well decoupled below the Desk; the engine seam is at the wrong level |

The pattern is the same from every angle. The bottom of the stack is Hickey-clean:
- the codec;
- the contract;
- `mdDataLink`;
- `apply`;
- `History`, `PushSlot` and `RecvSession`.

The three coordinators above it are where the complecting is:
- the two `Desk` classes;
- the two `*StudioEditor` hosts;
- the MM page adapter.

## Key findings

### Complecting (simplicity)

1. **`mdDesk::Desk` does about ten jobs in one mutable object.** `mdDesk.h:52-235` has about 50 fields. Its jobs are:
   - the edit router;
   - the load queue;
   - pushes in flight;
   - the telemetry interpreter;
   - the lifecycle;
   - the HW link;
   - the knob recorder;
   - the modulators;
   - setup;
   - the scheduler.
   One `m_machineDirty` flag is set from about 20 places.
2. **The lifecycle is spread over booleans** (`m_firmware`, `m_ready`, `m_telemetrySeen`, `bootAnimation`, `m_hw`, `m_linkLost`) and rebuilt as strings in `mdDesk.cpp:1479`. Every gate re-derives part of it. MM has an `Engine` enum, but it still keeps a parallel `m_ready`.
3. **Timing is guessed, not observed.**
   - `reloadSong` does stop, then `schedule(150)`, then `schedule(300)`.
   - The Global push reads back after 60 ms.
   - `m_keyQuietUntilMs` silently holds back polling.
   - MM's `switchNow` has no timing at all.
4. **The route is complected with the recording mode.** `Desk::deliver` (`mdDesk.cpp:414`) silently sends kit edits to the knob recorder while recording.
5. **The MM page runs the mockup as the app.** The sync script string-patches the mockup (`HONEST`, `PATCHES`). The adapter then monkey-patches its globals (`window.tick`, `togglePlay`, …), and finds edits by diffing JSON of the shared global `S` against `BASE`.

### Data & state (values)

1. **The MD page mutates a global `S` and recovers intent by diffing.** `deriveView(S)` writes into `S`, and `ref()` returns `[obj, key]` places. `syncKitValues` diffs against `S.base`. The command should be recorded at the moment of the gesture instead.
2. **`Documents` fuses intended values with observed ones.** `m_docs.set(after)` runs before the push (`mdDesk.cpp:374…457`). A refused push leaves a value in `m_docs` that the firmware never took, yet the page is told these documents are "firmware truth".
3. **Per-document state is spread over parallel maps** (`m_patternPush`, `m_pushSentMs`, `m_storedKits`, `m_queued`, `m_dirty`, `m_loadQueue`). Pending effects are closures that capture `this` (`m_scheduled`).
4. **`apply()` is pure except for the `Clipboard&`,** which copy and paste mutate in place.
5. **The JSON Schemas are documentation only.** Nothing validates the emitted documents against `md-/mm-data-contract.schema.json`, so the codec and the schema are two sources of truth that will drift.
6. **Change detection is ad hoc.** It joins 14 fields with `"|"`, compares `JSON.stringify` output, and compares `onTelemetry` field by field twice.

### Architecture (decoupling)

1. **The engine seam is at the firmware-mechanics level** (`Port`: `sendSysex`, `pressKey`, `turnKnob`), not at the document level. This blocks the own-engine phase: a second engine would have to fake MD SysEx or copy `Desk`.
2. **The Desk lives in the editor window.** Its documents, undo, clipboard, pushes and HW choice die when the window closes. The P5 `ModRunner` move is the symptom.
3. **HW MIDI is an inline `if(_hw)` branch in 3 places:** `makeDesk`, `timerCallback` and `Desk::m_hw`. The emulator is a class (`StudioLink`); HW is lambdas.
4. **The page branches on the engine instead of on capabilities.** It checks `d.engine==="hw"`, and the reason strings are hard-coded (MM's `HONEST` even writes them into the markup at build time).
5. **MD and MM solve one problem in two ways.**
   - MD uses intent ops, with pure edits and undo in C++. MM sends whole documents, with undo in the page.
   - `PushSlot<T>` (by value) vs MM's `Push` (by bytes).
   - The load queue and `DocRef` are implemented twice.
   - The host code is copied (`bundlePage`, the bridge, the outbox, and about 160 lines of MIDI-learn commands each).
6. **The command vocabulary is spread over 4 dispatchers:** `handleEditorMessage`, the `onPageMessage` if-chain, `isLibraryCommand`, and the `isOneOf` lists in `apply`. Argument names and ranges are repeated inline.
7. **Contract names carry OS 1.63 residue** (`lockPoolHidden`, `nameTail`, `md-desk/*`), which a second engine would inherit.
8. **Self-test and diagnostics code sits inside product code** (tick-count branches in `timerCallback`, and env-var gates). The page temp file is shared between plug-in instances.

## Top recommendations (prioritised)

1. **Split the Desk into an engine-neutral core plus a `Machine` adapter protocol,** owned by the processor. This fixes arch 1–3 and simplicity 1 together; it is one refactor.
   - Core: `Documents`, `apply`, `History`, `Clipboard`, publish.
   - Adapter protocol, as data: `submit(changes)`, the read-back documents, the machine state, and a `capabilities` document.
   - `mdDataLink`, `kitDelivery`, `KnobRecorder` and the load queue become the `FirmwareMd` adapter.
   - `HwMidiLink` becomes a sibling adapter, chosen from a map.
   - Our own engine becomes a third adapter.
   - The editor attaches as a view over the existing queues.
2. **Pick one edit pipeline and move MM onto it:** intents, then pure document transforms, with undo in C++. Then:
   - the MM page stops monkey-patching the mockup;
   - the sync copies markup and CSS only;
   - `PATCHES` and `HONEST` disappear.
   Do this before the engine phase, so the engine is built once.
3. **Separate observed from intended values.** Keep `observed` (read-backs only) next to pending (the `PushSlot`s), and publish `merge(observed, pending)` with a pending flag. Group the per-document state into one `DocState` map, and make scheduled effects records (`{atMs, kind, arg}`) instead of closures.
4. **Capabilities as data.** The adapter publishes `machine.capabilities {liveRecord, chains, lcd, reasons}`, and the page disables controls from that document only.
5. **One lifecycle enum, and sequences driven by observed state.** Replace the booleans with `Missing|Loading|Booting|Animating|Ready|HwConnecting|HwLost` and a pure transition function. Replace the fixed delays with steps that wait for telemetry facts, with timeouts (MM's timed `Key` lists are the pattern to copy).
6. **One command table as data:** `op -> {owner, docKind, slotArg, range, handler, confirm?}`. Every dispatcher reads it, and the schema's `command` definition is generated from it.
7. **Make the spec executable.** Add a ctest step that validates every corpus document and every published message against the JSON Schemas.
8. **Shared generic parts:** `LoadQueue<Ref>`, `PushSlot<T>`, `DocRef`, a `WebPageHost` (bundling, bridge, outbox), `MidiLearnCommands`, and a `Diagnostics` observer that is kept out of release builds.
9. **Smaller items:**
   - `apply` returns the new clipboard instead of mutating one;
   - `Telemetry` gets `operator==` and a pure `diff → Events`;
   - the MM RAM screen addresses become facts in the device layer;
   - the firmware pass-through fields are grouped under `firmware:{…}`;
   - each instance gets its own page temp file.

## What's done well

- **Values at the bottom.** The `elektronData` structs have `==`, decode and encode are pure, and edits return new values (`withTrig`, `withLock`). Bytes exist only at the edges.
- **`apply(Documents, cmd) → EditResult{changes, errors}`.** Undo means inverting before/after pairs, and drags merge into one step by gesture id.
- **`RecvSession` is the model for the rest.** It is a pure state machine that decomplects the MM's "dumps only on SYSEX RECV" rule from everything else.
- **Queues where they matter:** latest-wins page batching, an outbox, lock-free `shared_ptr<const>` telemetry, one push in flight, and HW MIDI queues.
- **The contracts are open:** no `additionalProperties:false`, readers ignore unknown members, and fields they cannot show pass through untouched.
- **Firmware behaviour was measured before it was coded,** and the code cites those results.
- **Honest UI:** a control is disabled with its reason, never faked.
- **A `Port` of functions plus an injected clock** lets the unit tests drive the desk without a device.

## Readiness for the own-engine phase

- **Page and contract:** about 80% ready. They need capabilities as data and the firmware fields grouped.
- **C++ Desk:** not ready until recommendation 1 is done. Do 1 → 2 → 3 before the engine hammock.

---
*"Simplicity is a prerequisite for reliability." — Rich Hickey*
