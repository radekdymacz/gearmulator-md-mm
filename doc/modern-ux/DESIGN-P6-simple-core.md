# P6 design: a simple core for the Machinedrum and Monomachine Editors

- Branch `p6/simple-core`, from `p5/md-editor-final` (37200c0), 2026-09-28.
- Input: [DESIGN-REVIEW-2026-09-28.md](DESIGN-REVIEW-2026-09-28.md). That review synthesised three reviews: simplicity, data and architecture.
- Method: hammock first. This document is written before the code, and the code follows its migration plan.

## 1. The problem

The layers at the bottom are simple values: the codec, the contract, `mdDataLink`, `apply`, `History`, `PushSlot` and `RecvSession`. The three coordinators above them braid concerns:

| Coordinator | What it braids |
|---|---|
| `mdDesk::Desk` (50 fields) and `mmDesk::Desk` | Routing edits, the load queue, pushes in flight, reading telemetry, the lifecycle, HW vs emulator, the knob recorder, modulators, setup and a closure scheduler. One `m_machineDirty` flag is set from about 20 places |
| `StudioEditor` and `MmStudioEditor` | They own the desk, so documents, undo and pushes die with the window. They also hold HW MIDI as inline lambdas, MIDI learn, page bundling, diagnostics and self-test tick branches. Both files are copies of each other |
| The MM page | The mockup runs as the app. The page monkey-patches the mockup's globals, finds edits by diffing JSON against `BASE`, and keeps undo in the page |

Four consequences follow:

1. **The engine seam sits at the firmware-mechanics level.** The `Port` is `sendSysex`, `pressKey` and `turnKnob`. A second engine would have to fake MD SysEx or copy the Desk.
2. **Intended and observed values are fused.** `m_docs.set(after)` runs before the push. A refused push leaves a value the machine never took.
3. **Time is guessed.** Sequences use `schedule(150)`, `schedule(300)` and a read after 60 ms. They should wait for facts.
4. **The vocabulary is scattered.** Commands are defined in four dispatchers. The lifecycle is six booleans. Capabilities are `if(engine==="hw")` checks in the pages.

## 2. The target shape

```
 page (view)  ──intents──►  Core<Model>  ──Submit/Command/Load──►  Machine adapter  ──bytes──►  device / wire
      ▲                      │  Documents: observed ∪ pending        │ (FirmwareMd, HwMidiMd,
      └──docs, machine, ─────┘  History, Clipboard, publish          │  FirmwareMm, HwMidiMm)
          capabilities           ▲                                   │
                                 └───Observed / Settled / Outcome────┘
```

- **Core** (engine-neutral, one per model): the command table, `apply` (pure), `History`, `Clipboard`, the per-document `DocState {observed, pending}`, and publishing `merge(observed, pending)`. It knows nothing about SysEx, keys, RAM or the wire.
- **Machine adapter** (one per engine): it delivers a change, reads documents back, and keeps the machine's state, lifecycle and capabilities. Its interface is data in and data out.
- **Host session** (JUCE, owned by the processor): the core, the chosen adapter (from a map), the device edges, MIDI learn and the page outbox. It ticks on the processor's timer, so it survives the editor window. **The editor is a view:** it attaches a web page to the session's queues and detaches when it closes.
- **Pages:** they render views derived from the published documents and send intents at the gesture. They decide what is possible from `capabilities` only.

## 3. Candidate designs

### 3.1 Splitting the Desk

**A. One generic core, with typed documents per model (`deskCore::Core<Model>`).** A `Model` traits struct gives:
- the document variant;
- the kinds and their names;
- `apply(docs, cmd, clip)`;
- `toJson(doc)`;
- the command table.

The core is written once and serves the MD and the MM.
- *For:* one pipeline in code, not only in shape. Undo, gesture merging, pending/observed, asks and publishing are identical for both machines. A third engine plugs in below the core, and a third model (our own engine's documents) plugs in beside it.
- *Against:* the template code needs discipline. Model-specific parts (the MD's clipboard, the MM's working-kit flag) must stay out of the core as data (`DocState.source`, `Model::Clipboard`).

**B. A JSON-document core.** The core holds `json::Value` documents and History over JSON. Models convert at the adapter edge.
- *For:* truly neutral and tiny.
- *Against:* every edit would decode JSON, transform and encode again. MD `apply` works on typed values, and the tests assert on typed values. Validation would move from typed `validate` to a second place. That is two sources of truth again, which is the defect the review names. It is also slower on a knob drag (5 KB patterns).

**C. Keep one Desk per model and extract helpers only** (LoadQueue, Lifecycle, and so on).
- *For:* the least churn.
- *Against:* the engine seam stays at `Port` level, and the MM keeps a second pipeline. It does not meet recommendation 1.

**Chosen: A.** The core is `deskCore::Core<Model>`: a small, header-only generic library of about 500 lines. `mdDesk::MdModel` and `mmDesk::MmModel` are the two models.

### 3.2 The Machine adapter protocol

**A. A virtual interface of data records** (`submit(Change) → Outcome`, `command(Value) → Outcome`, `load(DocRef)`, `drain() → [Event]`, `state() → Value`, `capabilities()`, `lifecycle()`). The adapter is stateful: sessions, queues and pushes in flight. Everything that crosses the boundary is a value.
- *For:* it fits the stateful reality of MIDI round trips. Tests can drive an adapter alone.
- *Against:* it is an object interface. The discipline is that it carries no callbacks into the core and no references to core state.

**B. A pure step function** (`step(AdapterState, Input) → (AdapterState, [Output])`).
- *For:* maximally pure.
- *Against:* it would force the existing tested state machines (`mdDataLink::Session`, `RecvSession`, `KnobRecorder`, `PushSlot`) into one giant state value. It is a rewrite of the good parts, which the brief forbids ("extend them, don't rewrite them").

**C. Callbacks** (`onReadBack`, `onState`), as `mdDataLink::Session` has today.
- *Against:* callbacks re-enter the core in the middle of a command. That is the "desk never re-enters" hazard the firmware tests avoid.

**Chosen: A, with drained events** (no callbacks across the seam). The core calls the adapter. The adapter queues `Event`s, and the core drains them once per call. Inside the adapter, the existing pure parts keep doing their jobs.

### 3.3 Choosing HW or emulator

- **A. A map from engine id to factory:** `{"emu": FirmwareMd, "hw": HwMidiMd}`. The host builds the adapter from the map, and the page reads the engine list from the machine document.
- **B. One adapter with a `LinkProfile`,** where HW is a profile.

The HW and emulator adapters share most of their protocol code (the MD's `mdDataLink::Session` and the load queue). So `HwMidiMd` is a subclass-free composition of the MD protocol with a different `Wire` (a DIN pacer, no keys, no telemetry) and a different profile (timeouts, background order, capabilities). The map is A; the difference between the two is data (B). Both.

### 3.4 Moving the MM onto the pipeline

- **A. Fine-grained MM intents** (`trig`, `note`, `param`, … as the MD has). This rewrites every gesture of the 184 KB mockup.
- **B. Whole documents as the intent** (`{"op":"set","kind","doc","g"}`, emitted at the gesture). `apply` is the pure transform "replace this document with this validated value". History, gesture merging, pending/observed and delivery are the core's.
- **C. As today:** page diff and page undo.

**Chosen: B.** It is one pipeline: intent → pure transform → Change → History → adapter. A document is a value, and "this is the value I want" is the most data-oriented intent there is. What the review faults in the MM is not whole documents. It is:
- recovering the intent by diffing a shared global against `BASE`;
- monkey-patching;
- a second undo.

All three go. The mockup gets an explicit `MMHost` port: a demo host in the mockup (its example engine), and the plug-in's host in `mmAdapter.js`. The mockup tells the host which document a gesture edited (`host.edited(kind, slot, g)`). Undo, transport, library and pattern actions are host calls. The table accepts `set` for MD documents too, so the vocabulary is one. Finer MM intents can be added later without changing the pipeline.

## 4. Data contracts

### 4.1 `DocRef` and `DocState`

```cpp
namespace deskCore {
  struct DocRef { uint8_t kind; uint8_t slot; };           // kind = the model's enum value
  enum class Source : uint8_t { None, Dump, Memory, Tracked };
  template<class Doc> struct DocState {
    std::optional<Doc> observed;   // the machine's last read-back (dump, memory image) or tracked knowledge
    std::optional<Doc> pending;    // submitted and not yet observed (latest wins)
    Source source = Source::None;  // where `observed` came from
    const Doc* view() const;       // pending ? pending : observed
  };
}
```

- One `std::map<DocRef, DocState>` per core. It replaces `m_patternPush`, `m_songPush`, `m_pushSentMs`, `m_dirty`, the MM's `m_pending`, and the "set before push" in `Documents`.
- `Documents` (the model's typed maps) is kept as the **merged view** that `apply` reads. The core keeps it in step with the DocState map, so `apply` stays pure and unchanged.
- **Rules:**
  - `submit` sets `pending = after`.
  - `Observed(doc)` sets `observed = doc`. When `doc == pending`, `pending` is cleared.
  - `Settled{ref, ok:false, message}` clears `pending` and publishes the error. The view falls back to `observed`.
  - `Settled{ref, ok:true}` clears `pending` (a live edit whose read-back comes as memory).
- **The published `doc` message** is `{type:"doc", kind, slot, doc: view, pending: bool, source}`. The MM's `working` flag is `source == Memory`.

### 4.2 The adapter protocol

```cpp
template<class Model> class Machine {
  // core -> adapter
  virtual Outcome submit(const Change<Doc>&, const Documents&) = 0;  // deliver; errors refuse it
  virtual Outcome command(const Value& cmd, const Documents&) = 0;   // machine-owned ops (table owner = machine)
  virtual std::optional<Value> confirm(const Value& cmd, const std::vector<Change<Doc>>&) const = 0; // an ask, or none
  virtual void load(const DocRef&, bool urgent) = 0;
  // the device or the wire -> adapter (facts)
  virtual void onSysex(const Bytes&) = 0;
  virtual void onFacts(const Facts&) = 0;       // telemetry, device probe, memory region: model facts
  virtual void tick(double nowMs) = 0;
  // adapter -> core (data out)
  virtual std::vector<Event<Doc>> drain() = 0;  // Observed{doc, source} | Settled{ref, ok, message} | Notice{Value}
  virtual Value state() const = 0;              // machine document body (no core parts)
  virtual Capabilities capabilities() const = 0;
  virtual Lifecycle lifecycle() const = 0;
  virtual bool busy() const = 0;                // TX: something on the wire
};
struct Outcome { std::vector<std::string> errors; std::string note; std::optional<Value> ask; };
```

### 4.3 Capabilities (a document published by the adapter)

```json
"capabilities": {
  "engine": "emu",               "label": "EMU OS 1.63",
  "transport": true,             "panelKeys": true,
  "liveRecord": true,            "chains": true,
  "lcd": true,                   "workingKitMemory": true,
  "sampleNames": true,           "modulators": true,
  "dumps": "direct",             "hwMidi": true,
  "reasons": { "liveRecord": "...", "chains": "...", "hwMidi": "..." }
}
```

- It is published as `machine.capabilities`. `machine.engines` lists the adapter map: `[{id, label, available, reason}]`.
- The pages disable controls from these fields only, and show `reasons[k]` as the tooltip. There are no `engine==="hw"` branches, no `HONEST` build patch, and no hard-coded reason strings in the markup.
- The MM's `dumps` is `"recv"` on the emulator (SYSEX RECV driven by the panel) and `"manual"` over HW MIDI (the user parks the machine on SYSEX RECV).

### 4.4 The lifecycle

```cpp
enum class Lifecycle { Missing, Unsupported, Loading, Booting, Animating, Ready, HwConnecting, HwLost };
struct LifeFacts {
  enum class Probe { None, Missing, Unsupported, Loading, Booting, Running, Wire } probe;
  bool replied;                            // a status reply since the last Loading/Booting
  std::optional<bool> animating;           // telemetry: the start-up animation runs (MD RAM, MM screen word); none = no telemetry
  double sinceReplyMs, sinceWireMs;        // for HwLost / HwConnecting
};
Lifecycle next(Lifecycle, const LifeFacts&);   // pure; every edge in a table (deskCore/deskLifecycle.h)
```

| From | Fact | To |
|---|---|---|
| any | probe Missing / Unsupported / Loading | the same name |
| any emulated | probe Booting, or Running with no reply | Booting |
| Booting | Running and replied, animating true | Animating |
| Booting / Animating | Running and replied, animating false or none | Ready |
| any | probe Wire, no reply yet (since the wire was chosen) | HwConnecting |
| HwConnecting / HwLost | a reply within 3.5 s | Ready |
| Ready (wire) | no reply for 3.5 s | HwLost |

- Input is taken in `Ready` only. Loads run from `Animating` on, as today.
- The old strings (`desk.firmware`, `desk.boot`, `desk.link`, `desk.engine`) are derived from the one enum, so the contract is unchanged. The enum itself is published additively as `desk.lifecycle`.

### 4.5 Sequences on facts

```cpp
struct SeqStep { Action action; int arg; Wait until; double timeoutMs; };
enum class Wait { None, Stopped, Playing, StatusReply, PatternIs };
```

- A `Sequence` is a list of these steps. The adapter runs one at a time. A step's action runs when the previous step's wait fact holds, or when its timeout expires (the timeout is logged as a notice).
- **They replace:**
  - `reloadSong`: stop → wait Stopped → loadSong + status → wait StatusReply → play.
  - select `now`: stop → wait Stopped → LOAD PATTERN → wait PatternIs(p) → play. The MM's switch-now moves here from the page.
  - the global read-back: a request right after the edits, which the firmware takes in order.
  - REC while playing: stop → wait Stopped → recordPlay.
- **Scheduled effects are records** (`{atMs, action, arg}`), never closures.

### 4.6 The command table

```cpp
enum class Owner { Core, Machine, Setup, Host };
struct Arg { const char* name; ArgType type; double min, max; bool optional; };
struct CommandSpec { const char* op; Owner owner; int docKind /* -1: none */; const char* slotArg;
                     std::vector<Arg> args; bool needsReady; const char* help; };
```

- One table per model: `mdDesk::commandTable()` and `mmDesk::commandTable()`. There is also `deskHost::commandTable()` for the host ops: learn*, openMenu, revealRomFolder, engine, recheckFirmware, midi.
- **Every dispatcher reads it:**
  - the host session routes by owner;
  - the core checks the arguments against the table before `apply`;
  - `apply` picks the document kind from the table (the `isOneOf` lists and `isLibraryCommand` go);
  - the page's unknown-op error comes from the table.
- `deskCommandSchema` generates the `$defs/command` JSON Schema from the table. ctest fails when the schema file differs from what it generates.

### 4.7 Telemetry as a value

- `mdDesk::Telemetry` gets `operator==`, and `diff(before, after) → Events` becomes a pure function. The events are `{Stepped, PlayChanged, RecordChanged, PatternChanged, Wrapped, MachineChanged}`. It replaces the 14-field comparisons.
- MM telemetry carries device-layer facts, not RAM words. The screen is an enum `{Unknown, Boot, Main, Global, GlobalEdit, Other}`, alongside `recvActive` and `tempo`. `md::MmTelemetry` computes the screen enum from its addresses, so the screen addresses live only in `mdLib`.

### 4.8 Firmware pass-through fields: `firmware: {}`

- **MD documents, version 2:**
  - pattern `firmware {format, lockedRowsField, lockPoolHidden}`;
  - kit `firmware {format, nameTail, lfoState[16]}`;
  - song `firmware {format, nameTail}`;
  - global `firmware {format}`.
- The MM's `hidden` becomes `firmware` in its documents, version 2, with the MM's `format` inside it.
- **Readers are tolerant:** `fromJson` accepts version 1 (the old places) and version 2. The schemas describe version 2. The corpus round-trips through version 2, and a test reads version 1.

## 5. Where everything goes

| Part | Library | Notes |
|---|---|---|
| `DocRef`, `DocState`, `LoadQueue<Ref>`, `PushSlot<T>`, `History<Change>`, `Lifecycle`, `Sequence`, `CommandSpec`, `Capabilities`, `Core<Model>` | `deskCore` (new, pure, header-mostly) | No JUCE, no model |
| `MdModel`, `apply`, library edits, `kitDelivery`, `KnobRecorder`, chains, modulators, setup | `mdDesk` | Pure, kept |
| `FirmwareMd`, `HwMidiMd` (the MD protocol with two wires) | `mdDesk` (`mdDeskMachine.*`) | Pure: the wire is a `Port` of data sinks |
| `MmModel` (`set` apply), `FirmwareMm` (RecvSession), `HwMidiMm` | `mmDesk` | Pure |
| JSON Schema validator (the subset the schemas use) | `elektronData/jsonSchema.*` | Pure; ctest `contractSchemaTest` |
| `DeskSession` (the core plus adapter map plus device edges), `MidiLearnCommands`, `WebPageHost` (bundle, bridge, outbox, per-instance temp file), `Diagnostics` | `mdJucePlugin` | `Diagnostics` is compiled only with `MDMM_DIAGNOSTICS` |
| `ModRunner` | removed | The processor-owned session runs the modulators |

## 6. Migration plan (each step green, committed and pushed)

1. **Executable spec.** A JSON Schema validator plus a ctest over the corpus documents and the messages the unit tests publish. The contracts are frozen before anything moves.
2. **`deskCore` generics.** DocRef, DocState, LoadQueue, Lifecycle (with its transition table test), Sequence, CommandSpec, Capabilities. Unit tests; no users yet.
3. **Small value fixes.** `apply` returns the clipboard. `Telemetry` gets `==` and a diff. The MM screen becomes a device-layer fact.
4. **MD split.**
   - The command table drives the dispatch.
   - `FirmwareMd` / `HwMidiMd` take the machine mechanics.
   - `Core<MdModel>` takes documents, history and publishing: observed/pending, lifecycle, sequences, capabilities.
   - `mdDesk::Desk` stays as a thin composition (core + adapter) with the old public API, so the unit and firmware tests keep driving it.
5. **MM split.** `Core<MmModel>` with `set` and C++ undo, `FirmwareMm` (RecvSession), `HwMidiMm`.
6. **Host.**
   - A `DeskSession` owned by the processor, with the adapter map.
   - The editors become one `PageEditor` view over `WebPageHost`.
   - `MidiLearnCommands`, a per-instance temp file, `Diagnostics`.
   - `ModRunner` removed.
7. **MM page.** The mockup gets an `MMHost` port. `mmAdapter.js` implements it: intents at the gesture, undo in C++, capabilities. The sync copies the mockup verbatim: no `PATCHES`, no `HONEST`.
8. **MD page.** A view value derived from the documents plus a small UI store. Commands are sent at the gesture: no `S.base` diff. Controls follow the capabilities.
9. **Contract version 2** (`firmware: {}`), with tolerant readers and the schema updated.
10. **Review** on three angles, iterate, then `P6-RESULT.md`, build, install and auval.

The p4 agent's `p5/md-editor-final` is merged before steps 4, 6, 7 and 10. Their styling wins; this structure wins.
