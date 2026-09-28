# Foundation: how to extend the Machinedrum and Monomachine editors

Since P6 the editors share one foundation. Read [DESIGN-P6-simple-core.md](DESIGN-P6-simple-core.md) for why it is shaped this way. This page covers the four common extensions as step lists. Paths are relative to `source/elektron/md/` unless they start with `doc/`.

## The layers, bottom to top

| Layer | Where | What it knows |
|---|---|---|
| Data | `elektronData/` | The documents as values (pattern, kit, song, global), their SysEx codecs, their JSON, and validation. It also holds the JSON Schema validator. No JUCE. |
| Core | `deskCore/` | The model-agnostic parts: `Core<Model>` (observed and pending documents, history, publishing), `Desk<Model,Machine>` (the one router over the command table), `Lifecycle`, `Capabilities`, `LoadQueue`, `PushSlot`, `Sequencer`, `DinPacer`, and the LCD encoding. No JUCE. |
| Model + adapter | `mdDesk/`, `mmDesk/` | Per machine: the Model (document kinds, pure edits, the command table) and the Machine adapter (SysEx, keys, pushes, loads, and the device's facts, behind `deskCore::Machine`). No JUCE. |
| Session | `mdJucePlugin/mdDeskSession.h`, `mdSessionMd.cpp`, `mdSessionMm.cpp` | Owned by the processor. `SessionOf<Desk>` holds the engine map (`EngineRecord{profile, externalMidi, make}`), the host commands, and the 8 ms step. Each `Engine` is the device edge: the emulator link or the MIDI wire. |
| View | `mdPageEditor.*`, `mdWebPageHost.*` | The window over the session. It hosts the web page and routes page messages by the table's host column. |
| Pages | `mdJucePlugin/skins/mdStudio/*.js`, `skins/mmStudio/mmAdapter.js` | These depend only on the contract (`doc/modern-ux/*-data-contract.schema.json`): documents in, commands out. The label, the engine menu and disabled controls come from `machine.lifecycle`, `machine.capabilities` and `machine.engines`. |

Dependency runs downward only. Every layer is tested without the ones above it:
- `deskCoreTest` and `mdDeskTest`/`mmDeskTest` run with a fake device.
- `*DeskFirmwareTest` runs on the real firmware.
- `mdDeskModelPageTest` runs the MD page model in node.
- The in-plugin self-tests (`mdDeskSelfTest.js`, `mmSelfTest.js`) are in diagnostics builds only.

## Add an engine (e.g. a third way to reach a Machinedrum)

1. **Profile (data).** In `mdDesk/mdDeskMachine.h`, declare `const Profile& fooProfile();`. In `mdDesk/mdDeskMachine.cpp`, return `{id, label, about, wire, kitsFirst, memory}`. For the MM, use `mmDesk/mmDeskMachine.*`, where the Profile is `{id, label, about, wire}`.
2. **What it can do.** Capabilities are derived in `MdMachine::capabilities()` (or `MmMachine::capabilities()`) from the Profile and from which `Port` functions are set. Leave unset what the engine cannot do; for example, no `pressKey` means no transport and no panel keys. Add a Profile flag only for a fact the Port cannot express.
3. **The device edge.** In `mdJucePlugin/mdSessionMd.cpp`, add `class FooEngine final : public MdEngine` with three methods:
   - `device()` returns the `Port`.
   - `step(desk, tick)` feeds the desk what the device says: `onDeviceSysex`, `onTelemetry`, `setProbe`, and memory.
   - `probe()` reports the firmware state.

   The MM equivalent goes in `mdSessionMm.cpp`, deriving from `MmEngine`.
4. **One record.** Append `{fooProfile(), externalMidi, [](DeskSession& s) { return std::make_unique<FooEngine>(s); }}` to `MdSession::engines()`.
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
   - the `"set"` case in `apply`;
   - `documentToJson`.

   Pure edits for the kind go in `mdDeskEdit.cpp`.
3. **The adapter.** In `mdDesk/mdDeskMachine.cpp`:
   - add the request in `load()`'s switch;
   - add its reply size for DIN timeouts (`replyBytes` at the top);
   - add the dump reply, which becomes `Ev::observed(doc, Source::Dump)`;
   - add the push in `submit()`.

   The core does the rest: observed or pending, history and publishing.
4. **The contract.** In `doc/modern-ux/md-data-contract.schema.json`, add a `$defs/<kind>`, the `kind` enum in `$defs/message`'s doc variant, and its `allOf` if/then entry.
5. **The page.** In `skins/mdStudio/mdDeskModel.js`, add the kind to `Docs`. In `mdDeskApp.js`, store it in the `doc` message handler, then read it in `deriveView`.
6. **Test.** Add a `mdDeskTest` case with the fake device's dump. Every published message is validated against the schema.

The MM steps are the same in `mmDesk/mmDeskModel.*` and `mmDesk/mmDeskMachine.cpp`: `request()`, `onDump`, the `decode` switch and `submit`.

## Add a command

1. **One row.** Add a row to the command table:
   - MD: `MdModel::commands()` in `mdDesk/mdDeskMachine.cpp`.
   - MM: `MmModel::commands()` in `mmDesk/mmDeskMachine.cpp`.

   The row is `{op, owner, gate, kind, args, help, handler, host, group}`:
   - `owner`: `Core` (a pure document edit), `Machine` (the adapter runs it), `Setup` (MD editor setup) or `Host` (the plug-in).
   - `gate`: `None`, `Midi` or `Input`. It decides when the command waits for the lifecycle.
   - `args`: typed and ranged; the router checks them before anything runs.
2. **Its code.**
   - A `Core` command is a pure transform in `mdDeskEdit.cpp`'s `apply`.
   - A `Machine` command is `Outcome MdMachine::cmdFoo(const Value&, const Documents&)`, named in the handler column.
   - A `Host` command needs a `HostOp` in `deskCore/deskCommands.h`. The session acts on it in `SessionOf::onPageMessage` (`mdJucePlugin/mdDeskSession.h`); the window acts on `Audio` and `Menu` in `mdPageEditor.cpp`.
3. **The schema is generated.** Run `mdDeskTest --write-schema` or `mmDeskTest --write-schema`. This rewrites `$defs/command` from the table; the tests fail while the two differ.
4. **The page sends it.**
   - MD page: `cmd(op, args, key)`. Any value written into `V` before the call is the command's optimistic overlay, until its result.
   - MM page: `Bridge.send`, through a host call in `mmAdapter.js`.

   Results come after the documents they changed, from one builder (`deskCore::resultMessage`).

## Add a workspace

1. **Design first.**
   - MD: the approved mockup `doc/modern-ux/mockup/index.html` (tab markup, `data-ws`).
   - MM: `doc/modern-ux/mm-mockup/src/`: the tab in `30-body.html` and a `NNN-name.js` in `build.sh`'s order.
2. **Sync, never hand-edit.**
   - MD: `python3 doc/modern-ux/sync-mdstudio-skin.py`.
   - MM: `python3 doc/modern-ux/sync-mmstudio-skin.py` (it also checks every `MMView` member and element id the adapter and self-tests use).

   Use `--check` for drift.
3. **MD render.** Add `renderFoo()` in `skins/mdStudio/mdDeskApp.js` and its entry in `render()`'s workspace map (the `({ seq: renderSeq, ... })[S.ws]()` line). Add its top-bar fields in `renderSub()`. Read only `V` (the derived view) and `S` (UI state); send edits with `cmd`.
4. **MM render.** The mockup's code is the UI. Anything that needs the machine is a host call: add it to the host seam list in `src/55-host.js` and implement it on `window.MMHost` in `mmAdapter.js`. The sync script fails on a host method the mockup never calls.
5. **Controls the engine cannot do** are disabled from `machine.capabilities`:
   - MD: `V.caps`.
   - MM: the `NA` table in `mmAdapter.js`, which maps a selector to a capability name.

   Never branch on the engine id.

## Build and check

Configure a test build with `-DBUILD_TESTING=ON -Dgearmulator_MDMM_DIAGNOSTICS=ON`; diagnostics are off by default in a Release build. Then:
- `ctest -E "Plugin|_AU|VST|FirmwareTest"` runs the unit tests.
- `mdDeskFirmwareTest <MD ROM>` and `mmDeskFirmwareTest <MM ROM>` run the firmware smoke tests, including the contract check.
- `GEARMULATOR_MDSTUDIO_SELFTEST=1|p4|p4hw|p5|p6audio` and `GEARMULATOR_MMSTUDIO_SELFTEST=1|mmcpu|p6audio` run the in-plugin self-tests on the standalone apps.
