# Foundation: how to extend the Machinedrum and Monomachine editors

Since P6 the editors share one foundation. Read [DESIGN-P6-simple-core.md](DESIGN-P6-simple-core.md) for why it is shaped this way. This page covers the four common extensions as step lists. Paths are relative to `source/elektron/md/` unless they start with `doc/`.

## The layers, bottom to top

| Layer | Where | What it knows |
|---|---|---|
| Data | `elektronData/` (JSON and the schema validator: `elektronJson`) | The documents as values (pattern, kit, song, global), their SysEx codecs, their JSON, and validation. No JUCE. |
| Core | `deskCore/` | The model-agnostic parts: `Core<Model>` (observed and pending documents, history, publishing; results after the documents they changed), `Desk<Model, Adapter>` (the one router over the model's table; it holds whichever adapter an engine gives it), `AdapterBase` (events, observe/forget/settle/fail), `WireFacts`, `Expectation` (live edits until memory shows them), the read-back policy, `Lifecycle`, `Capabilities`, `LoadQueue`, `PushSlot`, `Sequencer`, `DinPacer`, `ModEngine` (the app modulators), the LCD encoding and the contract checks (`deskContract.h`). Only `elektronJson`, no JUCE. |
| Plug-in vocabulary | `deskHost/` | The plug-in's one command table (engine, ROM folder, MIDI learn, the page's MIDI, audio devices, menu) with an action column. Pure, so the contract tests generate the schema from it. |
| Model + adapter | `mdDesk/`, `mmDesk/` | Per machine: the Model (document kinds including the working kit, pure edits, the command table as vocabulary), the adapter interface (`MdAdapter`, `MmAdapter`: `deskCore::Machine` plus the device's facts) and the adapter both engines use today (`MdMachine`, `MmMachine`: SysEx, keys, pushes, loads; its own op -> function map). No JUCE. |
| Session | `mdJucePlugin/mdDeskSession.h`, `mdSessionMd.cpp`, `mdSessionMm.cpp` | Owned by the processor. `SessionOf<Desk>` holds the engine map (`EngineRecord{profile, make}`), acts on deskHost's rows, and runs the 8 ms step. Each `Engine` is the device edge (the emulator link or a `MidiWire`); it queues what the device says and hands it to the desk in `step()`. |
| View | `mdPageEditor.*`, `mdWebPageHost.*` | The window over the session: it hosts the session's page (`pageSpec()`) and acts on deskHost's window rows (menu, audio). |
| Pages | `mdJucePlugin/skins/mdStudio/*.js`, `skins/mmStudio/mmAdapter.js` | These depend only on the contract (`doc/modern-ux/*-data-contract.schema.json`): documents in, commands out. The label, the engine menu and disabled controls come from `machine.lifecycle`, `machine.capabilities` and `machine.engines`. |

Dependency runs downward only. Every layer is tested without the ones above it:
- `deskCoreTest` and `mdDeskTest`/`mmDeskTest` run with a fake device.
- `*DeskFirmwareTest` runs on the real firmware.
- `mdDeskModelPageTest` runs the MD page model in node.
- The in-plugin self-tests (`mdDeskSelfTest.js`, `mmSelfTest.js`) are in diagnostics builds only.

## Add an engine (e.g. a third way to reach a Machinedrum)

1. **Profile (data).** In `mdDesk/mdDeskAdapter.h`, declare `const Profile& fooProfile();`; in `mdDesk/mdDeskMachine.cpp`, return `{id, label, about, wire, kitsFirst, memory}`. For the MM, `mmDesk/mmDeskAdapter.h` and `mmDeskMachine.cpp`, where the Profile is `{id, label, about, wire, memory, telemetry, panel}`.
2. **What it can do.** Capabilities are derived in `MdMachine::capabilities()` (or `MmMachine::capabilities()`) from the Profile and from which `DevicePort` functions are set. Leave unset what the engine cannot do; for example, no `pressKey` means no transport and no panel keys. Add a Profile flag only for a fact the port cannot express.
3. **The device edge.** In `mdJucePlugin/mdSessionMd.cpp`, add `class FooEngine final : public MdEngine` with three methods:
   - `device()` returns the `DevicePort` (`baseChannel` tells it the machine's MIDI channel when it encodes CCs itself).
   - `step(desk, tick)` hands the desk what the device said since the last step (queue it, never call the desk from a callback): `onDeviceSysex`, `onTelemetry`, `setProbe`, memory, and its screen through an `LcdFeed`.
   - `probe()` reports the firmware state.

   An engine that does not speak the Machinedrum's SysEx also overrides `adapter(profile)` and returns its own `MdAdapter` implementation; the desk takes whatever adapter the engine gives. The MM equivalent goes in `mdSessionMm.cpp`, deriving from `MmEngine`.
4. **One record.** Append `{fooProfile(), [](DeskSession& s) { return std::make_unique<FooEngine>(s); }}` to `MdSession::engines()`. The plug-in's MIDI in and out go to it when its profile is `wire`.
5. **Nothing else changes.** The engine menu, the LCD label (`capabilities.label` / `about` while ready), disabled controls and their reasons all follow from the published `machine.engines` and `machine.capabilities`.
6. **Test.** Add a profile case in `mdDesk/mdDeskTest.cpp`; the pattern is `wireProfile()` and the HW tests. If you added a capability name, add it to `$defs/capabilities` in the schema. The schema test fails until you do, in both directions (`Schema::unseen`).

## Add a document kind (e.g. an MD sample-slot document)

1. **The value.** In `elektronData/`, add the struct, its SysEx codec, JSON (`mdJson.*`: `...ToJson` / `...FromJson`, plus the v2 wrapper in `jsonFirmware.*` when it has firmware pass-through fields) and `validate`. The corpus tests in `mdLibTest/` show the round-trip pattern.
2. **The model.** In `mdDesk/mdDeskEdit.h`:
   - add the `DocKind` entry;
   - add the type to the `Document` variant;
   - add a map in `Documents`, with `get` / `set` and `refOf` in `mdDeskEdit.cpp`.

   In `mdDesk/mdDeskModel.cpp`, add:
   - `kindName` and the `kindFromName` list;
   - `erase`;
   - its case in `setDocument` (the `set` op);
   - `documentToJson`.

   The working kit (`DocKind::WorkingKit`) is the pattern for a document with one identity: the kit that plays, apart from the stored kit slots.

   Pure edits for the kind go in `mdDeskEdit.cpp`.
3. **The adapter.** In `mdDesk/mdDeskMachine.cpp`:
   - add the request in `load()`'s switch;
   - add its reply size for DIN timeouts (`replyBytes` at the top);
   - add the dump reply, which becomes `observe(doc, Source::Dump)` (or `settle` through `deskCore::readBack` when it answers a push);
   - add the push in `submit()`.

   The core does the rest: observed or pending, history and publishing.
4. **The contract.** In `doc/modern-ux/md-data-contract.schema.json`, add a `$defs/<kind>`, the `kind` enum in `$defs/message`'s doc variant, and its `allOf` if/then entry.
5. **The page.** In `skins/mdStudio/mdDeskModel.js`, add the kind to `Docs`. In `mdDeskApp.js`, store it in the `doc` message handler, then read it in `deriveView`.
6. **Test.** Add a `mdDeskTest` case with the fake device's dump. Every published message is validated against the schema.

The MM steps are the same in `mmDesk/mmDeskModel.*` and `mmDesk/mmDeskMachine.cpp`: `request()`, `onDump`, the `decode` switch and `submit`.

## Add a command

1. **One row.** A model command is a row of the model's table (vocabulary only):
   - MD: `MdModel::commands()` in `mdDesk/mdDeskModel.cpp`.
   - MM: `MmModel::commands()` in `mmDesk/mmDeskModel.cpp`.

   The row is `{op, owner, gate, kind, args, help, core, group}`:
   - `owner`: `Core` (documents), `Machine` (the adapter runs it) or `Setup` (the editor's setup, the modulators).
   - `core` (for `Core`): `Edit` (a pure edit), `Set` (a whole document), `Ready`, `Undo`, `Redo`.
   - `gate`: `None`, `Midi` or `Input`. It decides when the command waits for the lifecycle.
   - `args`: typed and ranged, text with its allowed values (`oneOf`); the router checks them before anything runs.

   A plug-in command is a row of deskHost's table (`deskHost/deskHost.cpp`) with its `Action`.
2. **Its code.**
   - A `Core` edit is a pure transform in `mdDeskEdit.cpp`'s `apply`.
   - A `Machine` command is `Outcome MdMachine::cmdFoo(const Value&, const Documents&)`, entered in the adapter's `handlers()` map; a question before it (the user may lose something) goes in `askFor`'s map, and the core applies `force`.
   - A plug-in action: its case in `SessionOf::onPageMessage` (`mdJucePlugin/mdDeskSession.h`), or, for the window's (`deskHost::isWindowAction`), in `PageEditor::onPageMessage`.
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
   - MD: `V.caps`.
   - MM: `MMView.disable(capability, reason)`; the mockup maps each capability to its own controls (`NA_SEL` in `src/130-main.js`).

   Never branch on the engine id.

## Build and check

Configure a test build with `-DBUILD_TESTING=ON -Dgearmulator_MDMM_DIAGNOSTICS=ON`; diagnostics (the log, the self-tests) are off by default for every generator. Then:
- `ctest -E "Plugin|_AU|VST|FirmwareTest"` runs the unit tests.
- `mdDeskFirmwareTest <MD ROM> [hw|p4|playload]` and `mmDeskFirmwareTest <MM ROM>` run the firmware smoke tests, including the contract check; `GEARMULATOR_MD_FIRMWARE_BIN=<MD ROM> ctest -R mdSessionFirmwareTest` runs the session without an editor.
- `GEARMULATOR_MDSTUDIO_SELFTEST=1|p4|p4hw|p5|p6audio` and `GEARMULATOR_MMSTUDIO_SELFTEST=1|mmcpu|p6audio` run the in-plugin self-tests on the standalone apps.
