# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## This project: Machinedrum Editor + Monomachine Editor

`origin` (radekdymacz/mdmm, default branch `main`) started as a fork of joelanders' Machinedrum/Monomachine emulation (joelanders/gearmulator-md-mm), which is built on dsp56300/gearmulator. Since 0.5 it stands alone (doc/release/PLAN-0.5.md, doc/ROADMAP.md D3-D5): the DSP56300 emulator (`source/dsp56300`), the 68k core (`source/mc68k`), JUCE (`source/JUCE`) and RmlUi (`source/3rdparty/RmlUi`) are plain folders we own (each README says where it came from); upstream fixes are copied one commit at a time, never merged. The product is two editors (standalone, VST3, AU on macOS; Windows and Linux builds exist, not tested) that run the user's own Elektron firmware behind one web page. ROMs are never committed. 

- **Read first:** doc/modern-ux/FOUNDATION.md (the layers; how to add an engine, document kind, command, workspace or dialog; "Build and check"), doc/modern-ux/DESIGN-P6-simple-core.md (why), doc/modern-ux/UPSTREAM.md (history: how the fork lived on top of upstream until 0.5).
- **Where:** source/elektron/md/: elektronData, deskCore, deskHost, deskWire, mdDesk, mmDesk, mdDataLink, mdmmUpdate, mdLib (the emulated machines), mdJucePlugin (plug-ins, bridge, skins/ page files), mdLibTest, upstreamTests (our tests for his bridge and synthLib). Version: `MDMM_EDITOR_VERSION` in mdJucePlugin/mdmmPlugins.cmake; product names: scripts/mdmm-product.env.
- **Build:** `scripts/mdmm-dev.sh configure|build [target...]|tests|stats` (tree temp/dev, ccache, diagnostics on). Release packages: scripts/macos/build_mdmm.sh, scripts/linux/build_mdmm.sh, scripts/windows/build_mdmm.ps1. CMake 3.22 or newer.
- **Check:** `scripts/mdmm-dev.sh tests` (`ctest -E "Plugin|_AU|VST|FirmwareTest"`); **before a tag `scripts/mdmm-local-gate.sh` green** (the firmware-backed gate CI cannot run: doc/release/LOCAL-GATE.md); firmware tests need your ROM (`GEARMULATOR_MD_FIRMWARE_BIN`, `GEARMULATOR_MM_FIRMWARE_BIN`); scripts/mdmm-journeys.sh, scripts/mdmm-pluginval.sh.
- **Floats:** Release builds are `-Ofast` (`/fp:fast`): `std::isfinite`/`std::isnan` can fold to constants. Use `baseLib::isFinite` and put code that must see NaN on `-fno-fast-math` (examples: source/elektron/md/elektronData/CMakeLists.txt).
- **Emulation CPU:** the plan and findings are in doc/modern-ux/RESEARCH-emulation-cpu.md; the speed-ups have one tester switch (`GEARMULATOR_MDMM_SPEEDUPS=0`, or Developer > Speed-ups off): doc/md_mm_performance_diagnostics.md, "Switches for testers".
- **Releases:** `release/0.x.y` branches are merged into `main`; run `scripts/mdmm-local-gate.sh` green before tagging; the tag `mdmm-v0.x.y` runs .github/workflows/mdmm-editors-release.yml; notes in doc/release/vX.Y.Z.md; bugs and ideas in doc/release/BUGS.md and IDEAS.md (testers are never named).

## Shared code (emulation, JUCE and CMake)

Since 0.5 the tree holds only the two editors and what they need; the other synths, VST2/CLAP/LV2, the consoles
and upstream's deploy scripts and workflows are gone (their history is in the tag `pre-cleanup-0.5`).

**Emulation stack:** DSP56300 emulator (`source/dsp56300/`, JIT via asmjit) + ColdFire/68k core (`source/mc68k/`,
Musashi). `source/elektron/md/mdLib` loads the firmware ROM, sets up processor memory and handles MIDI and audio
via HDI08.

**Shared libraries:**
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juceRmlUi/`, `juceRmlPlugin/`, `juceUiLib/` — RmlUi integration (HTML/CSS-like UI framework)
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions
- `pluginTester/`, `midiLearnTest/` — plug-in host and MIDI-learn tests

**Plugin build flow:** `createJucePlugin()` in `source/juce.cmake` (VST3, AU, Standalone), called from
`source/elektron/md/mdJucePlugin/CMakeLists.txt` → Processor inherits `synthLib::Plugin` wrapping
`synthLib::Device` → skins compiled into binary data.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`mdLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- `.clang-format` in `source/` directory
- C++17 required

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval
- Remote here: `origin` (radekdymacz) only. The `upstream` (joelanders) and `gearmulator` (dsp56300) remotes were dropped in 0.5; the list that was here (`private`, `nas`, `codeberg`, `EvilDragon`) is the upstream author's.
- The DSP emulator (`source/dsp56300/`), the 68k core, JUCE and RmlUi are plain folders of this repository (no longer submodules): changes there are fine

## Key Build Files

- `base.cmake` — Compiler flags, platform-specific optimization settings
- `source/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/skins.cmake` — Skin asset compilation
- `.github/workflows/mdmm-*.yml` — CI (GitHub Actions), doc/release/CI.md

## Where to Make Changes

1. **The machines** → `source/elektron/md/mdLib/`
2. **Plugin and page** → `source/elektron/md/mdJucePlugin/` (processor, skins/ page files), the desk libraries beside it
3. **Shared plugin infra** → `jucePluginLib/` or `synthLib/`
4. **DSP emulator** → `dsp56300/source/dsp56kEmu/`

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` vector with `insert()`, never `assign()` — the Plugin layer prepends version headers
- **RmlUi threading:** DOM modifications MUST happen on JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** Use static instance-set pattern to guard lambdas against use-after-free

## Detailed Reference

MIDI Learn: doc/midilearn/. Upstream's long AI notes (`.github/copilot-instructions.md`: Jenkins, YouTrack, the other synths) were deleted in 0.5; they are in the tag `pre-cleanup-0.5`.

## Development lifecycle (since 0.4.0; details: doc/release/CI.md)

Four levels. Use the lowest one that answers the question; never stack them without Radek asking.

1. **Dev loop (minutes).** A branch, `scripts/mdmm-dev.sh play md|mm` (builds one standalone and opens it), Radek plays and accepts, merge to main. No test suites. One targeted test for the code just changed is fine (seconds).
2. **Fast local check (a few minutes).** Claude runs it by itself when it thinks a change is good to go, before it says so, and when Radek asks. Pick by what changed: emulation code: the bit-exact goldens (`mdmmPerfGateTest <ROM> md|mm 8 --golden source/elektron/md/mdLibTest/goldens/mdmm-goldens.json`); page or desk code: the journeys of that area in the background, silent (`scripts/mdmm-journeys.sh --background --jobs 4 both '<selector>'`); shared code: the unit tests (`scripts/mdmm-dev.sh tests`). Windows-only code: push a `feat/windows-*` branch, CI builds it.
3. **Full local test (once per release).** `scripts/mdmm-local-gate.sh` on a quiet Mac with the ROMs. The 21-minute soak is off by default: `--soak` only when a report points at drop-outs over time. A failed stage: fix, then re-run only it (`MDMM_GATE_ONLY=<stage>`).
4. **Release CI (once per release).** Push `release/0.x.y`: the full suite on macOS, Windows and Linux runs once. Tag the tested branch head (the tag reuses that build), merge to main, push, then publish the release yourself when the files are attached (no draft left) and ask Radek to deploy the site at once (the Discord post waits for the page). Push to main only runs one light Linux job; nobody waits for it. Every night at 02:00 UTC (04:00 Polish summer time) the full suite runs on main if main changed: check it at the start of a session.

Do not add audits or verification rounds before a cut unless asked.

## Community: the mdmm.dev Discord server

Claude Code may check and manage the MD + MM Editor Discord server (invite https://discord.gg/8xwXwBHbtn) through the bot "MD + MM Editor": read channels, round up bugs and ideas into `doc/release/BUGS.md` / `IDEAS.md` (testers are never named in this public repo), create or tidy channels, and post in #dev-log, #general, #bugs, #ideas and #videos. **Every post or reply is shown to Radek first, with the exact text and any images, and goes out only after his OK.** Posts are written as CC (Claude Code), never as Radek. House rules: `marketing/DISCORD.md`. The bot token lives in `~/.config/mdmm/discord-bot-token` (never print it or commit it); release notes reach #announcements by the release workflow's webhook.
