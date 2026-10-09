# Living on top of upstream

This fork keeps merging joelanders' work (`upstream`, [joelanders/gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm))
with `git merge`, never a rebase. He in turn merges dsp56300/gearmulator, which moves whole folders
(for example `source/jucePluginLib` to `source/framework/juce/jucePluginLib`). Every line we change
in a file he owns is a future conflict; a file we add is not.

## The rule

- **Our code lives in our files.** New libraries, sources, tests, CMake and CI workflows are files
  of ours, in our folders or beside his.
- **Upstream files get hooks only:** one include, one call, one member, one argument. A hook calls
  into our file and carries a comment that points here.
- **Never delete an upstream file.** Keep it out of our build and product from our own files
  (a filtered source list, `EXCLUDE_FROM_ALL`, a disabled test). A deleted file becomes a
  modify/delete conflict every time he edits it.
- **An upstream file that needs no change is byte-identical to the merge base.**
- Check before a merge or a PR: `scripts/mdmm-upstream-footprint.sh` lists every upstream file we
  change, with line counts, and fails if one was deleted.

## The hooks

| Upstream file | Hook | Why it cannot be ours |
|---|---|---|
| `README.md` | the top section | the fork's front page |
| `CLAUDE.md` | the "This fork" section at the top; the Community section at the end | the fork's guide for Claude Code |
| `CMakeLists.txt` | one line right after `cmake_minimum_required`: `include(source/elektron/md/mdmmCMakeVersion.cmake)`, the CMake 3.22 check (his minimum, and the policies it sets, stay his) | JUCE needs 3.22; the message names JUCE instead of failing inside it |
| `.github/workflows/mdmm-integration-policy.yml` | `if: github.repository == 'joelanders/gearmulator-md-mm'` on the job | his gate for pull requests into his release line; this fork merges into `main`, so the job is skipped here (the file stays, byte-identical but for those two lines, the `if` and its comment) |
| `source/elektron/md/mdLibTest/sysexContentOracle.h`, `sdsFirmwareTest.cpp`, `mmSysexExportFirmwareTest.cpp` | copy `event.sysex` into a `std::vector` (`Sysex(begin, end)`, `assign`) instead of assigning it | `synthLib::SysexBuffer` is a `std::pmr::vector` on Linux, which does not assign to a `std::vector`: the three files did not compile there |
| `source/elektron/md/CMakeLists.txt` | `include(mdmmEditors.cmake)` after `mdLib` | adds our libraries (`elektronData`, `desk*`, `mdDesk`, `mmDesk`, `mdDataLink`), our part of `mdLib` (`mdAutomation` split out, `mddeskdevice`, `mdpanelsequence`, the telemetry headers), the JUCE web view switch and our `mdLibTest/mdmmTests.cmake` |
| `source/elektron/md/mdJucePlugin/CMakeLists.txt` | `include(mdmmPlugins.cmake)` after the skin globs; the product names in `createJucePlugin` are `${MDMM_PRODUCT_NAME_MD}` / `_MM` (`scripts/mdmm-product.env`, read by `mdmmPlugins.cmake`) | the editors' version (0.3.0), sources, page files in the binary data (instead of the panel skins) and icons must be values before his `juce_add_binary_data` and `createJucePlugin`; the rest (libraries, definitions, bundle steps, tests, the disabled panel pointer tests) runs deferred at the end of his file |
| `source/juce.cmake` | `${GEARMULATOR_PLUGIN_EXTRA_ARGS_<target>}` last in `juce_add_plugin` (a repeated one-value argument wins over his: the editors' icons, maker `Future Native Audio`, website, copyright, microphone text, the pinned LV2 URI); `GEARMULATOR_PLUGIN_BUNDLE_ID_<target>` instead of his `local.gearmulator.preview.*` when set | JUCE takes these only as `juce_add_plugin` arguments, at configure time; the editors' own identifiers and names: `doc/release/SIGNING.md`, Identifiers and Names |
| `mdJucePlugin/mdPluginProcessor.h` | `getDeskHost()`, `m_desk`; the `usesMidiLearn` override | the processor owns the editors' state (`mdDeskHost.h`: the setup chunk and the session); MIDI mapping is off, so no learn translator is made (`doc/midilearn/THREADING.md`) |
| `mdJucePlugin/mdPluginProcessor.cpp` | includes; the `MDSK` chunk saved and read; a load starts from the default setup; the host made and started; the host destroyed **after** `destroyEditorState()` (the page detaches from the session); `md::DeskDevice` in `createDevice`; with no ROM `createDevice` returns `makeNoRomDevice()` (`mdDeskHost.h`) instead of throwing `FirmwareMissing` (the throw ended in `jucePluginLib/processor.cpp`'s native "Device Initialization failed" alert and Finder; now the page asks for the ROM); the state-restore alert is `genericUI::MessageBox` (routed to the page) instead of `juce::NativeMessageBox`; the config file named `editorConfigFileName` and copied once from his (`prepareConfig`, `mdSettingsMigration.h`); `productName` returns `MDMM_PRODUCT_NAME_MD` / `_MM` and the data folder's vendor is pinned to his `Gearmulator Preview` (`g_dataFolderVendor`: the maker the DAW shows is ours, the folder stays); the storage-image message names the MM product; `getStateInformation` / `setStateInformation` / `restoreHeldState` (one line each: with no ROM the stand-in keeps the project it was given and hands the same bytes back, so a session without a ROM cannot overwrite the saved project, `DeskHost::holdState`); `usesMidiLearn` returns `deskHost::midiMappingEnabled`; B-037: `machineMidiOut::route` after `getController()` in the constructor and a `machineMidiOut::RouteAfterLoad` at the top of `loadCustomData` (the machine's own MIDI out, all but its SysEx, to the host and the standalone's MIDI output, `mdMachineMidiOut.h`); one `processArch::addSessionFields(context)` in the performance report's session (`mdProcessArch.h`: whether the editor runs translated) | the processor's lifetime and state are upstream's; his routing matrix (`jucePluginLib`) has no Device -> Host route and a project's state carries the whole matrix, so the route is set where the processor is made and where a state is loaded |
| `mdJucePlugin/mdPluginEditorState.cpp` | includes; `editorPageSkins`, `keepEditorPage`, `createEditorPage` (`mdEditorPages.h`); the `addSettingsEntry` override (B-007: none in a plug-in, "Audio/MIDI Settings..." in the standalone); the `fillMenu` override: the editor's whole menu as data (`mdEditorMenu.h`, I-008; the page's Zoom, B-001) | the editor page is the only UI; his panel skins and skin policy stay unchanged; the editor's menu is built there |
| `mdJucePlugin/mdController.h`, `.cpp` | `evDeviceSysex` and its one call in `parseSysexMessage` | the only place every SysEx from the machine passes |
| `jucePluginLib/processor.h`, `.cpp` | `ExternalMidi` member and accessor; `takeIn`, the device output dropped, `flushOut`; `protected:` / `private:` around `get/setStateInformation` (`.h`); `Properties::configFileName` and its two lines in `getConfigFile`; the virtual `usesMidiLearn()` (true by default) checked where the MIDI learn translator is made; `requestLatencyUpdate()` after `setResamplerMode` and `setPreferredDeviceSamplerate` | HW MIDI: the processor's MIDI paths (`externalMidi.h` holds the logic); the md processor overrides the two state calls; the base constructor opens the config file, so its name must be a property, not an override (the editors' own config file, `mdSettingsMigration.h`); the translator is not thread-safe and the editors do not use it (codex review 2026-10, item 8); each resampler has its own delay and hosts compensate only what they are told (item 7) |
| `jucePluginEditorLib/pluginEditorState.h`, `.cpp` | `openMenu` split into a virtual `fillMenu` (+ a null check on Settings); the Settings entry moved into the virtual `addSettingsEntry` | the menu is built inline in `openMenu`; the menu bar and the page's context menu need it as data (`editorPopupMenu.h`); the web page editors have no RmlUi settings page, so `mdPluginEditorState` replaces the entry; since I-008 it builds the whole menu (`mdEditorMenu.h`), which the page draws |
| `jucePluginEditorLib/pluginEditorWindow.h`, `.cpp` | an `EditorWindowFit` member and five calls | free-size pages and the screen fit are the window's sizing (`editorWindowFit.h`, `windowFit.h`) |
| `juceRmlUi/rmlMenu.h` | `forEachEntry` | a menu's entries are private |
| `juceUiLib/messageBox.cpp` | one `messageRoute::offer...` line at the top of each of the five functions (`messageRoute.h` is ours, header only) | every question and warning of the shared code passes here; the editors show them in the page, not as native alerts |
| `synthLib/device.h`, `plugin.cpp` | `Device::StateCapture` and the virtual `beginStateCapture` (null by default: the other synths keep the old path); `Plugin::getState` begins the capture under the process lock and encodes after it; `addMidiEvent` checks the full MIDI ring again under the process lock before it makes room | every synth's plug-in wrapper: a state save no longer holds the audio thread (codex review 2026-10, item 3; the Machinedrum's and Monomachine's capture is ours, `mdLib/mdstatecapture.*`), and a full ring no longer replays a stale event (item 2) |
| `mdLib/mddevice.h`, `.cpp` | the `beginStateCapture` override (defined in our `mdLib/mdstatecapture.cpp`); `getState` is a capture encoded at once | the device's state and its members are upstream's |
| `mdLib/mdhardware.h`, `.cpp` | the ROM held as `shared_ptr<const Rom>` (`sharedRom()`); `factoryFlashBaseline()` and `m_factoryBaseline` (defined in our `mdLib/mdfactorybaseline.cpp`); the captured factory image held by `shared_ptr` (shared with a save, not copied); `replaceFactoryFlashCache` keeps the baseline it decoded; the flash exchange swaps it; the include | a save that encodes without the lock keeps the ROM and the baseline alive after the device swapped its Hardware, and decodes the factory cache once instead of at every save |
| `mdLib/mdstate.h`, `.cpp` | `crc32` declared; the CRC-32 by tables (slicing by eight, the same values) | the state and cache formats' checksum, 15 times faster |
| `networkLib/tcpServer.h`, `.cpp`, `tcpStream.h`, `.cpp` | `BindScope` (the bridge binds every interface as before, the MCP server 127.0.0.1); the accept thread catches `std::exception`; `TcpStream::interrupt()` and `setReadTimeout()` | the MCP server must not listen on the LAN; a thread or memory failure must not end the host; a blocked client thread must be woken at shutdown |
| `mcpServerLib/httpServer.h`, `.cpp`, `httpResponse.h`, `mcpServer.h`, `.cpp`, `mcpPluginServer.h`, `.cpp` | loopback, the Host and Origin check (our `httpGuard.h`), no CORS (`setCorsHeaders` gone); clients reaped and capped, request limits, an idle read timeout (a constructor argument for the tests), shutdown that interrupts the clients and wakes the SSE wait; tool handlers run outside the tools mutex; `send_note`'s duration clamped (`noteDurationMs`) | his MCP server (codex review 2026-10, items 11 and 12): a web page or another machine could drive it, and it could hang the host at shutdown |
| `bridge/bridgeLib/commandReader.cpp`, `commands.cpp`, `error.h`, `tcpConnection.h`, `.cpp`, `types.h`; `bridge/client/deviceConnection.h`, `.cpp`; `bridge/server/clientConnection.h`, `.cpp`, `romPool.h`, `.cpp` | sizes, counts and enum values from the peer checked (`g_maxCommandSize`); an exception on the receive thread ends that connection; the receive thread started by the most derived constructor (`final`); the missing `return` after a ROM hash mismatch; the ROM cache named by the hash, written exclusively, 1 byte to 16 MiB, kept in memory when the file cannot be written, the peer's name logged sanitised | his DSP bridge (codex review 2026-10, items 9 and 10): a peer could write past buffers, write files outside the ROM folder, or end the process |
| `.github/workflows/mdmm-core.yml`, `scripts/macos/build_mdmm.sh`, `scripts/windows/build_mdmm.ps1` | our test names in his lists of test targets and ctest patterns | our CI runs his scripts and his core workflow; the tests the fork carries (`upstreamTests/`, `mdStateCaptureTest`, `mdProcessorHooksTest`) must run there too |
| `doc/mcp_server.md` | loopback, the Host and Origin check, the limits and the accepted risk (no authentication); the `exit` tool removed (the code no longer has it) | his document of his server |

What moved out of upstream's files: `md::Device`'s telemetry and panel sequences into `md::DeskDevice`
(`mdLib/mddeskdevice.*`, the plug-in makes it); `panelKeySequence` into `mdLib/mdpanelsequence.*`;
the processor's setup chunk and session into `mdJucePlugin/mdDeskHost.*`; the skin choice into
`mdJucePlugin/mdEditorPages.*`; the window title, Audio/MIDI Settings and free size into
`mdStandaloneApp.cpp` and `jucePluginEditorLib/editorTraits.h` (`FreeSizeEditor`,
`AudioMidiSettingsEditor`: the page says so, `Editor` stays his; his "I Agree" disclaimer alert is silenced by `createEditorPage` setting its config value `disclaimerSeen`, no hook); our mdLibTest and mdJucePlugin
tests into `mdLibTest/mdmmTests.cmake` and `mdJucePlugin/mdmmPlugins.cmake` (same targets, labels
and output folders); the tag-time installers into `.github/workflows/mdmm-editors-release.yml`
(his `elektron-prerelease.yml` and `elektron-macos.yml` are his again; a tag also runs his, which
builds without uploading); the build and start test of every push into our `mdmm-editors.yml`, which calls
`mdmm-editors-macos.yml`, `-windows.yml` and `-linux.yml`: they run his `scripts/macos/build_mdmm.sh` and
`scripts/windows/build_mdmm.ps1`, whose only change of ours is our test names in their lists (the hooks; a compiler
cache and Ninja come in through the environment, not through his files), and no longer call his
`elektron-windows.yml`; the release takes their packages ([doc/release/WINDOWS.md](../release/WINDOWS.md), [doc/release/LINUX.md](../release/LINUX.md)). His
`elektron-macos.yml` and `elektron-windows.yml` are byte-identical to his again and still run on their own triggers
(`main`, `release/md-mm-*`), each a second build on a push to our `main` that nothing waits for; turning them off for
this fork is a repository setting (`gh workflow disable`), not an edit of his files.

The tests the fork carries for his code (the DSP bridge, networkLib, the MCP server, synthLib's wrapper) are in
`source/elektron/md/upstreamTests/`, added from `mdmmEditors.cmake`: no line in his CMakeLists, and his
`EXCLUDE_FROM_ALL` on `networkLib`, `bridgeLib` and `mcpServerLib` stays (a test that needs one builds it as a
dependency). `mcpServerTest` needs juce_core and is declared with the JUCE tests in `mdmmPlugins.cmake`. Tests of
ours that once were appended to his test files are files of ours: `mdLibTest/mdStateCaptureTest.cpp`,
`mdJucePlugin/mdProcessorHooksTest.cpp`, `upstreamTests/synthLibStateCaptureTest.cpp` and
`upstreamTests/synthLibMidiQueueTest.cpp`. The state capture and the factory baseline are `mdLib/mdstatecapture.*`
and `mdLib/mdfactorybaseline.*` (his `mddevice.*` and `mdhardware.*` keep the hooks above); the CMake 3.22 check is
`source/elektron/md/mdmmCMakeVersion.cmake`.

Upstream files that stay in the tree but not in our product: the panel skins
`skins/mdDefault/*` and `skins/mmSfx60/*` (not in the sources or the binary data),
`mdLcdEditorPointerTest.cpp` (its two tests are `EXCLUDE_FROM_ALL` and disabled),
`mdProductSkins.h` (not called).

## Files we add in his folders

Our headers in `jucePluginEditorLib/` (`standaloneApp.h`, `windowFit.h`, `editorPopupMenu.h`,
`editorTraits.h`, `editorWindowFit.h`) and `jucePluginLib/` (`externalMidi.h`) sit beside the files
that include them. When upstream moves those folders (the `source/framework/juce/` move), git
moves them along and reports a "file location" conflict for each; accept the move, or merge with
`git -c merge.directoryRenames=true merge ...` and they merge cleanly.

Ours in his other folders: `mdLib/` (`mddeskdevice.*`, `mdpanelsequence.*`, `mdsequencerstate.h`,
`mmtelemetry.h`, `mdromcheck.h`, `mdstatecapture.*`, `mdfactorybaseline.*`; listed in `mdmmEditors.cmake`),
`mdLibTest/` (the rigs in `mdmmTests.cmake`) and `mcpServerLib/httpGuard.h` (header only, included by his
`httpServer.cpp`).

## Fixes to offer upstream

The fork carries these fixes in upstream files (the hooks above). Each is worth a pull request, so that the hook can
go:

- **to dsp56300/gearmulator** (the shared code; joelanders merges it from there):
  - `synthLib/plugin.cpp`: the MIDI ring checked again under the process lock before `addMidiEvent` pops (one `if`).
  - `jucePluginLib/processor.cpp`: `requestLatencyUpdate()` after a resampler mode or device rate change.
  - the DSP bridge: the command size limit, the audio count and enum checks, exceptions on the receive thread, the
    receive thread started after construction, the ROM cache named by its hash. dsp56300's `main` has changed
    `bridge/bridgeLib/tcpConnection.cpp` and `bridge/server/clientConnection.cpp` since (a send mutex, a protocol
    version check, the same missing `return`): the sync that brings it conflicts there; take that side and put ours
    back.
  - the MCP server: loopback and the Host and Origin check without CORS; `httpServer`'s shutdown, reaping, limits
    and idle timeout; the SSE wait woken by `stop()`; tool handlers outside the mutex; the `send_note` clamp; the
    accept thread's `std::exception` catch in `networkLib`.
- **to joelanders/gearmulator-md-mm**: the state save outside the lock (`Device::StateCapture` beside his
  `StateTransaction`, the Machinedrum's capture, the factory baseline decoded once, the CRC by tables); the three
  `std::pmr` SysEx test fixes (Linux).

## Syncing

- `scripts/mdmm-sync-upstream.sh` (on the `chore/upstream-sync-oss-prep` branch) fetches upstream
  and merges it.
- Before and after a merge, run `scripts/mdmm-upstream-footprint.sh` (`--committed` for HEAD only):
  the list should stay the hooks above. A conflict in a hook is resolved by taking his side and
  putting the hook back.
- To see what a branch or PR of his would do before it lands:
  `git merge-tree --write-tree --name-only HEAD upstream/<branch>` (a commit is needed; for
  uncommitted work, make one with `git commit-tree` on a temporary index).
