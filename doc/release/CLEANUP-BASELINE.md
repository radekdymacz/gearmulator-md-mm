# Clean-up baseline (0.5, Phase 0)

What "unchanged" means for the 0.5 clean-up ([PLAN-0.5.md](PLAN-0.5.md)), measured on 2026-10-10 at the tag
`pre-cleanup-0.5` (`1d44505e3`, branch `cleanup/0.5`). Every later step of the clean-up compares against this.

**Machine:** Apple M4 Pro, 12 cores, macOS 27.0.1, Xcode 26.2, CMake 4.2.1, Ninja 1.13.2. **The Mac was busy**
(other agents building and testing): load 17 when the build started, up to 150 during it, about 100 when the tests
started, about 7 during the CPU benchmarks. Build times and CPU figures are therefore upper bounds; the goldens
and test results do not depend on load.

**Build:** a fresh tree (`temp/baseline`), the local gate's configure (`scripts/mdmm-local-gate.sh`, stage 1):
Ninja, Release, arm64, ThinLTO and the DSP optimisation on, no PGO, `BUILD_TESTING=ON`, Elektron only, VST3 + AU +
Standalone, VST2 / CLAP / LV2 off.

## 1. Goldens: bit-exact, 24 of 24

`mdmmPerfGateTest <ROM> md|mm 8 --scenario <s> --outputs stereo|all --golden
source/elektron/md/mdLibTest/goldens/mdmm-goldens.json`, speed-ups on (default) and off
(`GEARMULATOR_MDMM_SPEEDUPS=0`), run by the gate's stage 5:

| Machine | Scenarios | stereo, on | stereo, off | all outputs, on | all outputs, off |
|---|---|---|---|---|---|
| MD (OS 1.63, ROM fingerprint `33b7c1a9e29f43fd`) | md-busy, md-factory, md-song | PASS | PASS | PASS | PASS |
| MM (OS 1.32B, ROM fingerprint `e1c1b461b6d0f21b`) | mm-a01, mm-busy, mm-song | PASS | PASS | PASS | PASS |

Goldens file sha256 `946acc5e37437820dc52b492c8d16d226aebcbc0d219c2d3a77a94ddba6cc53a`. ROMs: MD
`elektron_sps1-1uw_os1.63.bin` (sha256 `68542e30...ca44c8`), MM `elektron_sfx6-60_os1.32b.bin` (sha256
`36984917...abec7e`). "Unchanged" = the same file, untouched, and 24 of 24 PASS.

## 2. Tests: 159 pass

`ctest -C Release -E '^mmDeskFirmwareTest$|^mdSysexLifecycleTest$|_AU_Validate$|^test_.*_VST$'` with both ROMs
(the gate's stage 2, firmware tests strict): **159 executed, 0 failed, 0 skipped**; 2 disabled
(`mdLcdEditorPointerTest`, `mmLcdEditorPointerTest`). Not run here: the AU zip validations (need a CPack zip),
`mmDeskFirmwareTest` and `mdSysexLifecycleTest` (the gate's stage 2b, an hour each). "Unchanged" = these names
pass, minus only tests that Phase 2 deletes on purpose (other synths, the MCP server), each named in its commit.

<details><summary>The 159 names</summary>

baseLibBinaryStreamTest baseLibFiniteTest bridgeLibTest bridgeServerRomPoolTest cowMemoryTest deskAboutPageTest
deskBridgePageTest deskCompatPageTest deskCoreTest deskDropPageTest deskGenPageTest deskKeyViewPageTest
deskKeymapPageTest deskKeysPageTest deskMenuPageTest deskModalPageTest deskOverlayPageTest deskSyxPageTest
deskTogglePaintPageTest deskWirePortTest deskWireTest dsp56300_accumulatorTests dsp56300_essiOnDemandTests
dsp56300_unitTests dsp56kDisassembleZeroWords elektronDataCorpusTest elektronDataTest juceRmlMouseInputTest
mc68kColdFireDivideTest mc68kColdFireTimingTest mc68kHdi08ReceiveTest mcpHttpGuardTest mcpHttpServerTest
mcpServerTest mdAboutTest mdAudioFirmwareTest mdAudioIoLayoutTest mdAudioProbePluginVST3IdentityTest
mdAudioQueueTest mdAutomationArchitectureTest mdAutomationFirmwareTest mdAutomationMidiTest
mdAutomationParameterTest mdAutomationRobustnessTest mdAutomationSoakTest mdDataLinkTest mdDeskGenPageTest
mdDeskKeysTest mdDeskModelPageTest mdDeskPageTest mdDeskSetupStateTest mdDeskTest mdDroppedFilesTest
mdEditorMenuTest mdEncoderPressFirmwareTest mdFirmwareImageTest mdFirstStartFirmwareTest mdFlashTest
mdFrontPanelPresentationTests mdHostRxFirmwareTest mdHostRxTimingTest mdIdleSchedulerFirmwareTest
mdIdleSelfBranchTest mdJucePlugin_VST3AudioIoTest mdJucePlugin_VST3ProgramChangeTest mdLcdInteractionModelTest
mdLibTests mdMachineMidiOutFirmwareTest_md mdMachineMidiOutFirmwareTest_mm mdMachineMidiOutTest
mdMemoryFastLaneTest mdMidiTimingFirmwareTest mdMidiTimingTest mdPageBridgeTest mdPanelReadinessFirmwareTest
mdPanelRenderingTest mdProcessArchTest mdProcessorHooksTest mdProgramChangeFirmwareTest mdProjectStateRestoreTest
mdRamAudioOracleTest mdRamPackingTest mdRomInstallTest mdRosettaNoticeTest mdSdsTransferTest mdSequencerStateTest
mdSessionFirmwareTest mdSessionNoRomInstallFirmwareTest_md mdSessionNoRomInstallFirmwareTest_mm
mdSessionNoRomManageFirmwareTest_md mdSessionNoRomManageFirmwareTest_mm mdSessionNoRomTest_md mdSessionNoRomTest_mm
mdSettingsMigrationTest mdShiftPanelLatchTest mdStandaloneRendererPolicyTest mdStateCaptureTest mdStateTest
mdSyxPickFirmwareTest mdTransportScorecardTest mdTurboMidiUnitTest mdUartCpuInterruptTest mdUartRegisterTest
mdUwFirmwareTest mdVst3ProgramChangeOptOutTest mdVst3ProgramChangeTest mdWebFileDropTest mdWindowFitTest
mdWindowsPolicyTest mdmmReleaseScriptsTest mdmmUpdateTest midiLearnTests midiOutputDispatcherTest
midiRoutingMatrixTest mmAudioFirmwareTest mmBootFirmwareTest mmConvertTest mmDataCorpusTest mmDataTest mmDeskTest
mmDigiProImportAudioOracleTest mmDigiproEnsembleFirmwareTest mmDigiproFirmwareTest mmEncoderPressFirmwareTest
mmGenTest mmInputFirmwareTest mmJucePlugin_VST3AudioIoTest mmJucePlugin_VST3ProgramChangeTest mmKeysTest
mmLcdEditPagesFirmwareTest mmSineFirmwareTest mmSineMidiFirmwareTest mmSineOracleTest mmSoundTest
mmSysexBlockProfile-1024 mmSysexBlockProfile-32 mmSysexBlockProfile-irregular mmSyxPickFirmwareTest mmViewPageTest
pagedArrayTest ringBufferTest semaphoreTest sharedAudioBufferTest sharedAudioReducerTest
synthLibAudioInstrumentationTest synthLibAudioTest synthLibMidiClockTimingTest synthLibMidiQueueTest
synthLibPerformanceReportTest synthLibRealtimeInstrumentationTest synthLibResamplerTimingTest
synthLibStateCaptureTest synthLibStateTransactionTest syxImportFileTest syxImportTest test_mdJucePlugin_AU
test_mdJucePlugin_VST3 test_mmJucePlugin_AU test_mmJucePlugin_VST3

</details>

## 3. CPU: one instance

`mdCpuBenchTest <MD ROM> 1 10` and `mmCpuBenchTest <MM ROM> 1 10`, speed-ups on, two runs each, load 6.6 to 8.2
(not idle):

| | stopped | playing | whole process |
|---|---|---|---|
| MD (busy pattern) | 35.3 / 34.7 % of one core | 38.1 / 37.2 % | 37.6 / 36.8 % |
| MM (A01) | 46.3 / 48.4 % | 45.3 / 47.4 % | 46.2 / 48.2 % |

"Unchanged" = within the run-to-run spread (about 1-2 points here) or lower, on a quiet Mac.

## 4. Shipped identities

Read from the built bundles (`temp/baseline/products/Release`) and `scripts/mdmm-product.env`. These are what a
DAW and an old project find the plug-ins by; none may change.

| Bundle | CFBundleIdentifier | Name / executable |
|---|---|---|
| `Machinedrum Editor.app`, `.vst3`, `.component` | `com.nativekloud.machinedrum-editor` | `Machinedrum Editor` |
| `Monomachine Editor.app`, `.vst3`, `.component` | `com.nativekloud.monomachine-editor` | `Monomachine Editor` |

- **AU:** Machinedrum `aumu` / `Tmdr` / `GmPv`, Monomachine `aumu` / `Tmno` / `GmPv`; names
  `Future Native Audio: Machinedrum Editor` / `...: Monomachine Editor`; AU version 1024 (0.4.0).
- **VST3 class IDs** (vendor `Future Native Audio`, subcategories `Instrument|Synth`):

  | | Processor (Audio Module) | Controller | Plugin Compatibility |
  |---|---|---|---|
  | MD | `ABCDEF019182FAEB476D5076546D6472` | `ABCDEF011234ABCD476D5076546D6472` | `ABCDEF01C0DEF00D476D5076546D6472` |
  | MM | `ABCDEF019182FAEB476D5076546D6E6F` | `ABCDEF011234ABCD476D5076546D6E6F` | `ABCDEF01C0DEF00D476D5076546D6E6F` |

- **Plug-in codes:** manufacturer `GmPv`, plug-in `Tmdr` / `Tmno` (the VST3 class IDs and AU codes derive from
  them, not from the names).
- **Product names** (`scripts/mdmm-product.env`): `Machinedrum Editor`, `Monomachine Editor`, vendor
  `Future Native Audio`, website `https://mdmm.dev`; legacy names (removed by the installers) `Gearmulator MD` /
  `Gearmulator MM` with legacy bundle IDs `local.gearmulator.preview.GearmulatorMD` / `...MM`.
- **Data folder:** `~/Documents/Gearmulator Preview/<Machinedrum|Monomachine>` (`g_dataFolderVendor` in
  `mdPluginProcessor.cpp`); LV2 URI `http://theusualsuspects.lv2/GearmulatorMD|MM` (LV2 is not shipped).
- **Version:** 0.4.0 (`CFBundleShortVersionString`).

## 5. Size and build time

Lines of C, C++ and Objective-C (all lines, tracked files):

| Part | Lines |
|---|---|
| This repository without its submodules | 421,872 |
| of which `source/elektron` (the editors and machines) | 109,591 |
| `source/dsp56300` (submodule; 205,013 without its bundled wxWidgets) | 2,229,086 |
| `source/JUCE` (submodule) | 998,166 |
| `source/3rdparty/freetype` (submodule) | 248,351 |
| `source/3rdparty/RmlUi` (submodule) | 200,194 |
| `source/mc68k` (submodule) | 69,259 |
| `source/3rdparty/lunasvg` (submodule) | 36,648 |
| `source/clap-juce-extensions` (submodule) | 14,888 |
| `source/cpp-terminal` (submodule) | 10,538 |

Repository: a single-branch bare clone of `cleanup/0.5` is 273 MB (pack 260 MiB), submodules not included.

Clean build, fresh tree, 12 jobs, on the busy Mac: configure 20 s; **the six editor targets
(`md|mmJucePlugin_VST3`, `_AU`, `_Standalone`) 154 s** (2.6 min); the rest of `all` (every test target) a further
353 s (5.9 min), 8.8 min in all.

## After Phase 1 (detach), 2026-10-10

dsp56300 (with asmjit), mc68k, JUCE and RmlUi are plain folders (each README says where it came from and what was
left out); the upstream plumbing is gone. Checked on a fresh `git clone --recurse-submodules` of `cleanup/0.5`
with every `github.com/joelanders/` and `github.com/dsp56300/` URL blocked: the clone has the four folders, only
`cpp-terminal` and `clap-juce-extensions` fail to fetch (still dsp56300's; used only by the Vavra console and CLAP,
both off, Phase 2 deletes them), and a fresh tree configures and builds.

| | Before | After |
|---|---|---|
| Goldens | 24 of 24 | 24 of 24, same file |
| ctest | 159 pass | the same 159 names pass |
| Identities (bundles, IDs, AU codes, VST3 class IDs) | section 4 | identical, read from the new bundles |
| CPU, MD / MM whole process (one instance, load ~7) | 37.6, 36.8 / 46.2, 48.2 % | 38.3, 38.8 / 47.0, 47.2 % (same code; noise) |
| Editors clean build (busy Mac) | 154 s | 107 s (load differs; not comparable) |
| Superproject pack (single-branch bare clone) | 260 MiB, plus four submodule clones | 273 MiB, nothing more to fetch for them |
| Files of the four in a checkout | 285 MB (dsp56300 192, JUCE 69, RmlUi 22, mc68k 2) | 68 MB (dsp56300 10, JUCE 35, RmlUi 22, mc68k 2) |
| Lines (C/C++/ObjC) dsp56300 / JUCE / RmlUi / mc68k | 2,229,086 / 998,166 / 200,194 / 69,259 | 205,013 / 824,444 / 200,194 / 69,259 |

## After Phase 2 (delete what is not MD or MM), 2026-10-10

Five slices on `cleanup/0.5`, each checked before the next on a fresh tree with the local gate's stages 1, 2 and 5
(build, ctest, goldens) and the identities read from the built bundles:

| Commit | Slice | Deleted (lines in the diff; C/C++/ObjC lines in brackets) |
|---|---|---|
| `c5533efd3` | 1. The other synths: virus*, osirus*, osTIrus*, mq*, xt*, nord, ronaldo, wLib, Android, their consoles, upstream's NSIS installer and Virus launchers, mdmm-shared-controller.yml | 119,022 (50,273) |
| `4f15a4c92` | 2. VST2 (vstsdk2.4.2, mqVst2, fst), CLAP and cpp-terminal (both submodules), LV2 packaging, portaudio, portmidi, changelogGenerator, upstream's deploy/pack/rclone/products cmake scripts and root build scripts | 239,332 (127,020 + 25,426 in the two submodules) |
| `a27d1c707` | 3. Upstream's workflows (cmake, elektron-*, nightly, release), AI notes, issue template, changelog and synth notes | 4,148 (0) |
| `416f114cb` | 4. One product set: no SYNTH_* switches, no FX plug-in option; callers updated | 94 (0) |
| `48bf68c90` | 5. The MCP server (mcpServerLib, the editor's MCP tools, the settings toggle, the constructor flag) | 5,847 (4,875) |

Kept: pluginTester and midiLearnTest (the editors' tests and CI use them; pluginTester now hosts VST3 and AU
only), bridge/networkLib/ptypes (jucePluginLib links bridgeClient and the plug-in state stores the device type
with a remote host and port: removing the bridge changes how old projects load, a decision for Radek), CPack
and the install rules in the root CMakeLists (the two `_AU_Validate` tests use them; nothing of ours runs cpack),
LICENSE.md and README.upstream.md (credit), doc/lua_scripting.md and doc/midilearn (code we keep).

| | Before (end of Phase 1, `86010f6d0`) | After (`48bf68c90`) |
|---|---|---|
| Goldens | 24 of 24 | 24 of 24, same file (after every slice but 3, which changed no code) |
| ctest | 159 pass | 156 pass: the same names minus `mcpHttpGuardTest`, `mcpHttpServerTest`, `mcpServerTest` (deleted with the MCP server) |
| Identities (bundles, IDs, AU codes, VST3 class IDs, version) | section 4 | identical after every slice, and in the fresh clone |
| Lines (C/C++/ObjC) of the repository | 1,720,848 | 1,538,544 |
| of which outside dsp56300, JUCE, RmlUi and mc68k | 421,916 | 239,612 (-43 %) |
| of which `source/elektron` | 109,591 | 108,677 (the MCP constructor flag and its tests) |
| Tracked files / bytes in a checkout (without freetype and lunasvg) | 7,314 / 278 MB | 5,761 / 94 MB |
| Submodules | freetype, lunasvg, cpp-terminal, clap-juce-extensions | freetype, lunasvg |
| Single-branch clone (pack) | 273 MiB | 278 MiB (history keeps the deleted files) |
| Clean build on a fresh clone, 12 jobs: configure / six editor targets / rest of `all` | 20 s / 154 s / 353 s (Phase 0, busy Mac) | 19 s / 126 s / 506 s (load 16 at the start, 250 at the end: other agents; not comparable) |

Fresh clone: `git clone --recurse-submodules --single-branch --branch cleanup/0.5` from GitHub with every
`github.com/dsp56300/` and `github.com/joelanders/` URL rewritten to an unreachable host: it fetches only freetype
and lunasvg, configures without a warning about unused options, and builds everything (editors and all test
programs); its bundles carry the identities of section 4.

One flaky test: `mdFirstStartFirmwareTest` failed 2 of 5 full ctest runs on these trees (after slices 1 and 4)
and 1 of 10 runs alone; it passed every other time, also on the same build. When it fails, the second start goes
"booting > ready" with 1 LCD frame and 0 resets, so the page never sees "animating". No deleted code is linked
into it; it looks like a timing race in the first-start lifecycle that load makes likelier. Watch it in Phase 3.
