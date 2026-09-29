# Foundation: how to extend the Machinedrum and Monomachine editors

Since P6 the editors share one foundation. Read [DESIGN-P6-simple-core.md](DESIGN-P6-simple-core.md) for why it is shaped this way. This page covers the four common extensions as step lists. Paths are relative to `source/elektron/md/` unless they start with `doc/`.

## The layers, bottom to top

| Layer | Where | What it knows |
|---|---|---|
| Data | `elektronData/` (JSON and the schema validator: `elektronJson`) | The documents as values (pattern, kit, song, global), their SysEx codecs, their JSON, and validation. No JUCE. |
| Core | `deskCore/` | The model-agnostic parts: `Core<Model>` (observed and pending documents, history, publishing; results after the documents they changed), `Desk<Model, Adapter>` (the one router over the model's table; it holds whichever adapter an engine gives it), `AdapterBase` (events, observe/forget/settle/fail), `WireFacts`, `Expectation` (live edits until memory shows them), `WorkingCopy` (one policy for the kit that plays: seed, memory image, expected edits; `deskWorkingCopy.h`), `KindSpec` (a document kind as one record; `deskKinds.h`), `Ask` (a question before a command that loses something; the core resends nothing, the page resends with `force`), the read-back policy, `Lifecycle`, `Capabilities`, `LoadQueue`, `PushSlot`, `Sequencer`, `DinPacer`, `ModEngine` (the app modulators), the LCD encoding and the contract checks (`deskContract.h`). Only `elektronJson`, no JUCE. |
| Plug-in vocabulary | `deskHost/` | The plug-in's one command table (engine, ROM folder, MIDI learn, the page's MIDI, audio devices, menu) with an `Action` and an actor column (`Session` or `Window`). Pure, so the contract tests generate the schema from it. |
| Wire | `deskWire/` | MIDI as bytes, pure and header-only (links `deskCore` and the pure CC maps `mdAutomation`, never the emulator): `MidiWire` (DIN pacing, whole messages in), CC and NRPN encoders, `realtimeOf(Key)`; `mdWire.h` and `mmWire.h` per machine. `mdDesk/mdDeskWirePort.h` and `mmDesk/mmDeskWirePort.h` build a whole `DevicePort` from a `MidiWire` (`wirePort`), used by the wire engines and the MD HW firmware rig. |
| Model + adapter | `mdDesk/`, `mmDesk/` | Per machine: the Model (document kinds including the working kit, pure edits, the command table as vocabulary), the adapter interface (`MdAdapter`, `MmAdapter`: `deskCore::Machine` plus the device's facts) and the adapter both engines use today (`MdMachine`, `MmMachine`: SysEx, keys, pushes, loads; its own op -> function map). No JUCE. |
| Session | `mdJucePlugin/mdDeskSession.h`, `mdSessionMd.cpp`, `mdSessionMm.cpp` | Owned by the processor. `SessionOf<DeskT, EngineT>` holds the engine map (`EngineRecord{profile, make, available}`), acts on deskHost's Session rows, keeps the setup (`SetupStore`), and runs the step (`g_stepMs`; periodic work through `due(tick, ms)`). Each `Engine` is the device edge (the emulator link, or a `PluginWire` that owns the plug-in's MIDI while it lives); it queues what the device says and hands it to the desk in `step()`. |
| View | `mdPageEditor.*`, `mdWebPageHost.*` | The window over the session: it hosts the session's page (`pageSpec()`) and acts on deskHost's window rows (menu, audio). |
| Pages | `mdJucePlugin/skins/mdStudio/*.js`, `skins/mmStudio/mmAdapter.js` | These depend only on the contract (`doc/modern-ux/*-data-contract.schema.json`): documents in, commands out. The label, the engine menu and disabled controls come from `machine.lifecycle`, `machine.lifecycleText`, `machine.input`, `machine.capabilities` (nested: `{engine, label, about, can, reasons, values}`) and `machine.engines`. Paste reads `machine.clipboard`. Asks come as `{"type":"ask", ask, message, confirm, command}`; the page shows them and resends `command` with `force`. |

Dependency runs downward only (the session includes the view only through the data-only `mdPageSpec.h`; the MD page model never calls the app layer). The transport (playing, recording, step) is only in the `telemetry` message, never in the machine document. Every layer is tested without the ones above it:
- `deskWireTest` and `deskWirePortTest` check the encoders, pacing and the wire ports (`mdDeskWirePort`/`mmDeskWirePort` INTERFACE targets). `mdSessionFirmwareTest` validates the plug-in's own messages (learn, audio) against the schema.
- `deskCoreTest` (a toy model, the working-copy policy, asks) and `mdDeskTest`/`mmDeskTest` run with a fake device; `mdDeskTest` also runs the desk over a fake `MdAdapter` (`FakeMdAdapter`): the adapter interfaces carry no protocol getters, so a fake implements only protocol-free methods. `deskWireTest` checks the encoders and pacing.
- `*DeskFirmwareTest` runs on the real firmware.
- `mdDeskModelTest.js` runs the MD page model in node; `mmConvertTest.js` the MM conversions against `doc/modern-ux/mm-catalogue.json`.
- `doc/modern-ux/page_contract_check.py` (run by both sync scripts) checks that every op a page sends is a `$defs/command` variant with declared arguments, and that the pages' capability tables match the schema's names.
- The in-plugin self-tests (`mdDeskSelfTest.js`, `mmSelfTest.js`) are in diagnostics builds only.

## Add an engine (e.g. a third way to reach a Machinedrum)

1. **Profile (data).** In `mdDesk/mdDeskAdapter.h`, declare `const Profile& fooProfile();`; in `mdDesk/mdDeskMachine.cpp`, return `{id, label, about, wire, kitsFirst, memory, panel}`. For the MM, `mmDesk/mmDeskAdapter.h` and `mmDeskMachine.cpp`, where the Profile is `{id, label, about, wire, memory, telemetry, panel}`.
2. **What it can do.** Capabilities are derived in `MdMachine::capabilities()` (or `MmMachine::capabilities()`) from the Profile and from which `DevicePort` functions are set. Leave unset what the engine cannot do; for example, no `pressKey` means no transport and no panel keys. Add a Profile flag only for a fact the port cannot express.
3. **The device edge.** In `mdJucePlugin/mdSessionMd.cpp`, add `class FooEngine final : public MdEngine` with three methods:
   - `device()` returns the `DevicePort`. A wire engine owns a `PluginWire` (the plug-in's MIDI is its own for the engine's lifetime) and returns `mdDesk::wirePort(...)` (or `mmDesk::wirePort`), which encodes with `deskWire` on the base channel the adapter pushes through `baseChannel`. The MD HW firmware rig uses the same port.
   - `step(desk, tick)` hands the desk what the device said since the last step (queue it, never call the desk from a callback): `onDeviceSysex`, `onTelemetry`, `setProbe`, memory, and its screen through an `LcdFeed`. Use `due(tick, periodMs)` for periodic work (`g_stepMs` is the step).
   - `probe()` reports the firmware state.

   An engine that does not speak the Machinedrum's SysEx also overrides `adapter(profile)` and returns its own `MdAdapter` implementation; the desk takes whatever adapter the engine gives. The MM equivalent goes in `mdSessionMm.cpp`, deriving from `MmEngine`, which also implements `sendMidi` (the page's keyboard); `EngineRecord<DeskT, EngineT = Engine<DeskT>>` carries `EngineT` through `make`, which returns `unique_ptr<EngineT>`; `SessionOf<DeskT, EngineT>` holds `m_engine` as `unique_ptr<EngineT>`, so `currentEngine()` returns it typed with no cast (`MmSession` is `SessionOf<mmDesk::Desk, MmEngine>`).
4. **One record.** Append `{fooProfile(), [](DeskSession& s) { return std::make_unique<FooEngine>(s); }, available}` to `MdSession::engines()`. `available` is empty (always) or a function `Availability(const DeskSession&)`, e.g. `midiOutAvailability`; it becomes `machine.engines[].available`/`reason`, and a switch to an unavailable engine is refused with the reason.
5. **Nothing else changes.** The engine menu, the LCD label (`capabilities.label` / `about` while ready), disabled controls and their reasons all follow from the published `machine.engines` and `machine.capabilities`.
6. **Test.** Add a profile case in `mdDesk/mdDeskTest.cpp`; the pattern is `wireProfile()` and the HW tests. A new adapter can start from `FakeMdAdapter` there. If you added a capability name, add it to `$defs/capabilities` in the schema (the schema test fails until you do, in both directions) and to the pages' tables: `CAP_CONTROLS`/`CAP_INFO` in `mdDeskApp.js`, `NA_SEL`/`NA_INFO` in the MM mockup (`page_contract_check.py` fails until you do).

## Add a document kind (e.g. an MD sample-slot document)

1. **The value.** In `elektronData/`, add the struct, its SysEx codec, JSON (`mdJson.*`: `...ToJson` / `...FromJson`, plus the v2 wrapper in `jsonFirmware.*` when it has firmware pass-through fields) and `validate`. The corpus tests in `mdLibTest/` show the round-trip pattern.
2. **The model.** In `mdDesk/mdDeskEdit.h`:
   - add the `DocKind` entry;
   - add the type to the `Document` variant;
   - add a map in `Documents`, with `get` / `set` and `refOf` in `mdDeskEdit.cpp`.

   In `mdDesk/mdDeskModel.cpp`, add one `KindSpec` record to `MdModel::kinds()`: `{kind, name, slots, replyBytes, loadable, toJson, fromJson, messageSlot}`. Its name, the `set` and `load` vocabularies and the doc message's `kind` enum (generated into the schema), the page's doc message, `set` (a whole document) and the adapter's DIN timeouts all come from that record. Also add its case to `MdModel::erase` (`mdDeskModel.cpp`).

   The working kit (`DocKind::WorkingKit`) is the pattern for a document with one identity: the kit that plays, apart from the stored kit slots (`slots` 1, `loadable` false, `fromJson` admits only the kit that plays).

   Pure edits for the kind go in `mdDeskEdit.cpp`.
3. **The adapter.** In `mdDesk/mdDeskMachine.cpp`:
   - add the request in `load()`'s switch;
   - add the dump reply, which becomes `observe(doc, Source::Dump)` (or `settle` through `deskCore::readBack` when it answers a push);
   - add the push in `submit()`.

   The core does the rest: observed or pending, history and publishing.
4. **The contract.** In `doc/modern-ux/md-data-contract.schema.json`, add a `$defs/<kind>` and the doc variant's `allOf` if/then entry (which shape the kind has); `--write-schema` writes the `kind` and `source` enums, and the test fails for a kind with no shape.
5. **The page.** In `skins/mdStudio/mdDeskModel.js`, add the kind to the document shape (`emptyDocs`) and its row to `DOC_STORE` (how an incoming document is stored), then read it in `deriveView`.
6. **Test.** Add a `mdDeskTest` case with the fake device's dump. Every published message is validated against the schema.

The MM steps are the same in `mmDesk/mmDeskModel.*` (`MmModel::kinds()`) and `mmDesk/mmDeskMachine.cpp`: `request()`, `onDump`, the `decode` switch and `submit`. Enumerations the MM page needs come from the catalogue (`doc/modern-ux/mm-catalogue.json` is written by `mmDeskTest --write-schema`).

## Add a command

1. **One row.** A model command is a row of the model's table (vocabulary only):
   - MD: `MdModel::commands()` in `mdDesk/mdDeskModel.cpp`.
   - MM: `MmModel::commands()` in `mmDesk/mmDeskModel.cpp`.

   The row is `{op, owner, gate, kind, args, help, core, group}`:
   - `owner`: `Core` (documents), `Machine` (the adapter runs it) or `Setup` (the editor's setup, the modulators).
   - `core` (for `Core`): `Edit` (a pure edit), `Set` (a whole document), `Ready`, `Undo`, `Redo`.
   - `gate`: `None`, `Midi` or `Input`. It decides when the command waits for the lifecycle.
   - `args`: typed and ranged, text with its allowed values (`oneOf`), `Bytes` with a length; the router checks them before anything runs. The schema is closed: an undeclared argument is refused (only `id`, `g` and `force` are always allowed).

   A plug-in command is a row of deskHost's table (`deskHost/deskHost.cpp`): add its `Action` value in `deskHost.h`, and give the row its actor (`Actor::Session` or `Actor::Window`). Declare every argument; text vocabularies are `oneOf` lists (e.g. `audioSet`'s from `AudioSetting`).
2. **Its code.**
   - A `Core` edit is a pure transform in `mdDeskEdit.cpp`'s `apply`.
   - A `Machine` command is `Outcome MdMachine::cmdFoo(const Value&, const Documents&)`, entered in the adapter's `handlers()` map; a question before it (the user may lose something) is an `Ask {what, message, confirm, details, also}` from the `askers()` map, and the core skips it when the command carries `force`. Force answers every question, so a command that loses two things asks once: join them with `deskCore::withAsk` (the ask names the others in `also`). A question before a `Core` edit (overwriting or clearing a stored slot) comes from the adapter's `review`. Add the ask's name to `MdModel::asks()` / `MmModel::asks()` and regenerate with `--write-schema` (`sameAsks` fails until you do; the adapters assert they ask only declared questions).
   - A plug-in action: its case in `SessionOf::onPageMessage` (`mdJucePlugin/mdDeskSession.h`) for a Session row, or in `PageEditor::onPageMessage` for a Window row; both dispatch on the row's actor.
3. **The schema is generated.** Run `mdDeskTest --write-schema` or `mmDeskTest --write-schema`. This rewrites `$defs/command` from the model's table and deskHost's; the tests fail while they differ, and also when a machine row has no adapter function or the adapter has one for no row.
4. **The page sends it.**
   - MD page: `cmd(op, args, key, optimistic)`. `optimistic` is a list of `[path, value]` writes into the view, shown at once and kept until the command's result.
   - MM page: `Bridge.send`, through a host call in `mmAdapter.js`.

   Results come after the documents they changed, from one builder (`deskCore::resultMessage`).

## Add a workspace

1. **Design first.**
   - MD: the approved mockup `doc/modern-ux/mockup/index.html` (tab markup, `data-ws`).
   - MM: `doc/modern-ux/mm-mockup/src/`: the tab in `30-body.html` and a `NNN-name.js` in `build.sh`'s order.
2. **Sync, never hand-edit.**
   - MD: `python3 doc/modern-ux/sync-mdstudio-skin.py`.
   - MM: `python3 doc/modern-ux/sync-mmstudio-skin.py`. It also checks the seam both ways: every `HOST.*` call the mockup makes is implemented by the adapter and every host method is called, every `MMView` member and element id the adapter uses exists, and the adapter never reaches into the mockup's state.

   Use `--check` for drift.
3. **MD render.** Add `renderFoo()` in `skins/mdStudio/mdDeskApp.js` and its entry in `render()`'s workspace map (the `({ seq: renderSeq, ... })[S.ws]()` line). Add its top-bar fields in `renderSub()`. Read only `V` (the derived view) and `S` (UI state); send edits with `cmd`.
4. **MM render.** The mockup's code is the UI. Anything that needs the machine is a host call: add it to the host seam list in `src/55-host.js` and implement it on `window.MMHost` in `mmAdapter.js`; what the adapter shows goes through `MMView`'s setters.
5. **Controls the engine cannot do** are disabled from `machine.capabilities`:
   - MD: `CAP_CONTROLS` in `mdDeskApp.js` maps each capability to its controls (a disabled control shows the reason and blocks the gesture); `CAP_INFO` lists the informational ones. A name the machine does not publish counts as not allowed.
   - MM: `MMView.disable(capability, reason)`; the mockup maps each capability to its own controls (`NA_SEL` in `src/130-main.js`, informational ones in `NA_INFO`). The MM self-test (`mmSelfTest.js`) loads first and sets `window.MMDiagnostics`; only then does the mockup hand it its state.

   Never branch on the engine id.

## Add a dialog (P7)

Every dialog uses the one modal layer: the MODAL blocks of script and stylesheet. The same text is in the MD mockup, the MM mockup (`src/57-modal.js`, `src/20-mm.css`) and the MD skin (`mdDeskModal.js`), and `modal_check.py` fails both sync scripts on drift.

1. Give the dialog an element with `hidden`, plus its own open and close functions, as the libraries have.
2. Add one row to `DIALOGS` in the MODAL block: `[selector, kind, closeFunctionName]`. The kind is `confirm` (Esc answers its last key, a click outside keeps it) or `panel` (Esc and a click outside close it through its close function). Change the block in the MD mockup, then copy it to the other two places.
3. The layer centres the dialog over the backdrop, moves the focus in and returns it, and stacks dialogs. Do not position the dialog yourself.

## The shared page blocks (P7)

Four blocks of script and stylesheet are the same text in the MD mockup, the MM mockup and the MD skin. `modal_check.py` and `audio_panel_check.py` fail both sync scripts on drift:

| Block | What it does |
|---|---|
| MODAL | The modal layer, plus the LCD's tips under the display. |
| BOOT | The start-up and first-run card. The app sets `Boot.host`, and its lifecycle calls `Boot.update`. |
| SYX | The SysEx import panel. The app sets `Syx.host`, and the library headers call `Syx.keys()`. |
| AUDIO-MIDI PANEL | The audio and MIDI panel. |

The files never pass through the page:
- the window's drop zone and native choosers (`mdPageEditor.cpp`) hand them to the session;
- `DeskSession::installRom` checks and copies a ROM, then restarts the machine;
- `openSyx` sends the preview, the `syxImport` row queues the import (`SyxJob`, `mdSyxSession.h`), and `exportSyx` writes the file.

A model's SysEx rules (what its OS takes as it is) are its `SyxTraits`, in `mdSessionMd.cpp` and `mdSessionMm.cpp`.

## The host's clock (P7)

- **In a DAW** the plug-in sends the host's transport and tempo to the firmware as MIDI clock (`synthLib::MidiClock`). Each model says what its active global must be to follow them (`hostFollowing`, pure), and the `followHost` machine command sets it without an undo step. The session sends that command every 2 s, in a DAW's plug-in only (`followsHost`).
- **A new machine** gets a `hostFollowing` of its own, plus the command row. See [DESIGN-P7-sync.md](DESIGN-P7-sync.md).

## Build and check

Code of ours goes in files of ours; upstream's files carry hooks only ([UPSTREAM.md](UPSTREAM.md), checked by `scripts/mdmm-upstream-footprint.sh`).

Configure a test build with `-DBUILD_TESTING=ON -Dgearmulator_MDMM_DIAGNOSTICS=ON`; diagnostics (the log, the self-tests) are off by default for every generator. Then:
- `ctest -E "Plugin|_AU|VST|FirmwareTest"` runs the unit tests.
- `mdDeskFirmwareTest <MD ROM> [hw|p4|playload]` and `mmDeskFirmwareTest <MM ROM>` run the firmware smoke tests, including the contract check; `GEARMULATOR_MD_FIRMWARE_BIN=<MD ROM> ctest -R mdSessionFirmwareTest` runs the session without an editor.
- `GEARMULATOR_MDSTUDIO_SELFTEST=1|p4|p4hw|p5|p6audio|p7` and `GEARMULATOR_MMSTUDIO_SELFTEST=1|mmcpu|p4|p6audio|p7` run the in-plugin self-tests on the standalone apps (MM `p4`: the MM-P4 features ported in P8).
- `mdDeskFirmwareTest <MD ROM> hostclock` and `mmDeskFirmwareTest <MM ROM> hostclock` check that the machines follow a host's clock after `followHost`.
- `mmDeskFirmwareTest <MM ROM> p4` checks the MM-P4 features on the firmware (POLY, MIDI track mutes through the MUTE window, LIVE and GRID RECORDING, MULTI TRIG, PORTAMENTO, the MULTI MAP, another song, undo of a slot write); `mmDeskFirmwareTest <MM ROM> hw` drives a second emulated Monomachine as the HW MIDI peer through the plug-in's `wirePort` at DIN speed, with the test playing the person at the machine (SYSEX RECV, `hwSend`, TRANSPORT ACCEPT).

## HW MIDI without the panel (P8, from MM-P4)

Where the editor cannot press the machine's keys (`Profile.panel` false), a dump reaches the Monomachine only if the person has opened GLOBAL › FILE › SYSEX RECV. The adapter keeps such dumps (and the LOAD KIT that follows a kit dump) in order and publishes `machine.recv.waiting`; the page shows **SEND n** and a dialog with the steps, and its **Send now** is the `hwSend` command. What the machine has no MIDI message for (the MIDI track mutes, RECORD) is a capability that is false, with the reason; never a branch on the engine.
