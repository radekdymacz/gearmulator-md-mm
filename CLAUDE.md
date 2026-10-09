# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## This fork: Machinedrum Editor + Monomachine Editor

`origin` (radekdymacz/gearmulator-md-mm, default branch `main`) is a fork of joelanders' Machinedrum/Monomachine emulation (`upstream`), which is built on dsp56300/gearmulator (`gearmulator`). The product is two editors (standalone, VST3, AU on macOS; Windows and Linux builds exist, not tested) that run the user's own Elektron firmware behind one web page. ROMs are never committed. The "Upstream guide" below is upstream's, for the shared code.

- **Read first:** doc/modern-ux/FOUNDATION.md (the layers; how to add an engine, document kind, command, workspace or dialog; "Build and check"), doc/modern-ux/DESIGN-P6-simple-core.md (why), doc/modern-ux/UPSTREAM.md (our code lives in our files, upstream's files get hooks only).
- **Where:** source/elektron/md/: elektronData, deskCore, deskHost, deskWire, mdDesk, mmDesk, mdDataLink, mdmmUpdate, mdLib (the emulated machines), mdJucePlugin (plug-ins, bridge, skins/ page files), mdLibTest. Version: `MDMM_EDITOR_VERSION` in mdJucePlugin/mdmmPlugins.cmake; product names: scripts/mdmm-product.env.
- **Build:** `scripts/mdmm-dev.sh configure|build [target...]|tests|stats` (tree temp/dev, ccache, diagnostics on). Release packages: scripts/macos/build_mdmm.sh, scripts/linux/build_mdmm.sh, scripts/windows/build_mdmm.ps1. CMake 3.22 or newer.
- **Check:** `scripts/mdmm-dev.sh tests` (`ctest -E "Plugin|_AU|VST|FirmwareTest"`); **before a tag `scripts/mdmm-local-gate.sh` green** (the firmware-backed gate CI cannot run: doc/release/LOCAL-GATE.md); firmware tests need your ROM (`GEARMULATOR_MD_FIRMWARE_BIN`, `GEARMULATOR_MM_FIRMWARE_BIN`); scripts/mdmm-journeys.sh, scripts/mdmm-pluginval.sh; before a merge or PR scripts/mdmm-upstream-footprint.sh; upstream sync scripts/mdmm-sync-upstream.sh (merge, never rebase).
- **Floats:** Release builds are `-Ofast` (`/fp:fast`): `std::isfinite`/`std::isnan` can fold to constants. Use `baseLib::isFinite` and put code that must see NaN on `-fno-fast-math` (examples: source/elektron/md/elektronData/CMakeLists.txt).
- **Emulation CPU:** the plan and findings are in doc/modern-ux/RESEARCH-emulation-cpu.md; the speed-ups have one tester switch (`GEARMULATOR_MDMM_SPEEDUPS=0`, or Developer > Speed-ups off): doc/md_mm_performance_diagnostics.md, "Switches for testers".
- **Releases:** `release/0.x.y` branches are merged into `main`; run `scripts/mdmm-local-gate.sh` green before tagging; the tag `mdmm-v0.x.y` runs .github/workflows/mdmm-editors-release.yml; notes in doc/release/vX.Y.Z.md; bugs and ideas in doc/release/BUGS.md and IDEAS.md (testers are never named).

## Upstream guide (shared emulation, JUCE and CMake code)

Everything from here to the Community section is upstream's: his Windows setup, his Jenkins and his remotes included.

## Project Overview

Gearmulator is a low-level IC emulator that recreates classic virtual analog synthesizers (Access Virus, Waldorf microQ/XT, Clavia Nord Lead 2x, Roland JP-8000, Ensoniq VFX/TS-10) by emulating original DSP56300 and MC68K processors and running authentic firmware ROMs as audio plugins (FST, VST3, AU, CLAP, LV2).

## Build Commands

**Current dev setup uses `temp/cmake_vs26` with Visual Studio 2026.**

```bash
# Configure (Windows)
cmake . -B temp/cmake_vs26 -G "Visual Studio 17 2022"

# Build (use Debug for quick compile checks, Release for full optimization)
cmake --build temp/cmake_vs26 --config Debug -j 4
cmake --build temp/cmake_vs26 --config Release -j 4

# Package
cd temp/cmake_vs26 && cpack -G ZIP

# Run tests
ctest -C Release
```

Per-synth CMake flags: `-Dgearmulator_SYNTH_OSIRUS=ON`, `_OSTIRUS`, `_VAVRA`, `_XENIA`, `_NODALRED2X`, `_JE8086`, `_VFX`, `_TS10`. Plugin format flags: `gearmulator_BUILD_JUCEPLUGIN`, `_CLAP`, `_LV2`, `gearmulator_BUILD_FX_PLUGIN`.

Convenience scripts: `build_win64.bat`, `build_linux.sh`, `build_mac.sh`.

## Architecture

**Emulation stack:** DSP56300 emulator (`source/dsp56300/`, JIT via asmjit) + MC68K emulator (`source/mc68k/`, Musashi) form the core. Each synth has a device library that loads firmware ROMs, initializes processor memory, and handles MIDI/audio via HDI08.

**Per-synth pattern:**
| Emulator | Hardware | Device Lib | Plugin Dir |
|---|---|---|---|
| Osirus | Virus A/B/C | `virusLib/` | `osirusJucePlugin/` |
| OsTIrus | Virus TI/TI2/Snow | `virusLib/` | `osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `mqLib/` | `mqJucePlugin/` |
| Xenia | Waldorf MW II/XT | `xtLib/` | `xtJucePlugin/` |
| Nodal Red 2x | Nord Lead/Rack 2x | `nord/n2x/` | `nord/n2x/n2xJucePlugin/` |
| JE-8086 | Roland JP-8000 | `ronaldo/je8086/` | `ronaldo/je8086/jeJucePlugin/` |

**Shared libraries:**
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juceRmlUi/` — RmlUi integration (HTML/CSS-like UI framework)
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions

**Plugin build flow:** `createJucePluginWithFX()` macro in `source/juce.cmake` → links device lib → Processor inherits `synthLib::Plugin` wrapping `synthLib::Device` → skins via RML/RCSS compiled into binary data.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`virusLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- `.clang-format` in `source/` directory
- C++17 required

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval
- Remotes here: `origin` (radekdymacz), `upstream` (joelanders), `gearmulator` (dsp56300). The list that was here (`private`, `nas`, `codeberg`, `EvilDragon`) is the upstream author's.
- DSP submodule (`source/dsp56300/`) is also owned by user — changes there are fine

## Key Build Files

- `base.cmake` — Compiler flags, platform-specific optimization settings
- `source/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/skins.cmake` — Skin asset compilation
- `scripts/Jenkinsfile` / `JenkinsfileMulti` — Private CI (Jenkins)
- `.github/workflows/cmake.yml` — Public CI (GitHub Actions)

## Where to Make Changes

1. **Device-level changes** → `<synth>Lib/` (e.g., `virusLib/device.cpp`)
2. **Plugin UI** → `<synth>JucePlugin/` (RML/RCSS for layout, processor for logic)
3. **Shared plugin infra** → `jucePluginLib/` or `synthLib/`
4. **DSP emulator** → `dsp56300/source/dsp56kEmu/`
5. **New parameter** → update `parameterDescriptions_*.json`, map MIDI in processor, update skin RML

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` vector with `insert()`, never `assign()` — the Plugin layer prepends version headers
- **RmlUi threading:** DOM modifications MUST happen on JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** Use static instance-set pattern to guard lambdas against use-after-free
- **Voice expansion (Xenia/Vavra):** Multiple DSP56300 instances connected via ESSI1 ring bus; main DSP is last (`g_mainDspIdx = g_dspCount - 1`)

## Detailed Reference

See `.github/copilot-instructions.md` for comprehensive documentation on MIDI Learn, Patch Manager, program change routing, Jenkins CI details, YouTrack workflow, release process, and voice expansion internals.

## Community: the mdmm.dev Discord server

Claude Code may check and manage the MD + MM Editor Discord server (invite https://discord.gg/8xwXwBHbtn) through the bot "MD + MM Editor": read channels, round up bugs and ideas into `doc/release/BUGS.md` / `IDEAS.md` (testers are never named in this public repo), create or tidy channels, and post in #dev-log, #general, #bugs, #ideas and #videos. **Every post or reply is shown to Radek first, with the exact text and any images, and goes out only after his OK.** Posts are written as CC (Claude Code), never as Radek. House rules: `marketing/DISCORD.md`. The bot token lives in `~/.config/mdmm/discord-bot-token` (never print it or commit it); release notes reach #announcements by the release workflow's webhook.
