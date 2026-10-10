# Machinedrum Editor + Monomachine Editor

Two screen-native editors for the Elektron Machinedrum and Monomachine: the whole machine on one page (step grid, parameter-lock lanes, kit and pattern library,
chains, mutes, a sampler view, and a beta HW MIDI mode for a real machine). They run the real firmware,
as a standalone app, VST3 and AU on macOS (Windows x64 and Linux x64 builds exist, not tested).

- **Site, screenshots, download:** https://mdmm.dev
- **Releases:** https://github.com/radekdymacz/mdmm/releases
  (latest: [0.5.0 notes](doc/release/v0.5.0.md); all release notes are in [doc/release/](doc/release/))
- **User guide** (install, ROM, DAW use, workspaces, every keyboard shortcut): https://mdmm.dev/guide/
- **Bring your own ROM.** No firmware is included. With no ROM the editor shows a start-up card:
  choose your machine's image with its file chooser (on macOS you can also drop the `.bin` or `.zip` on the window; on Windows and Linux use the chooser) and the editor
  copies it into its ROM folder. You can also copy it to `~/Documents/Gearmulator Preview/Machinedrum/roms/`
  or `.../Monomachine/roms/` yourself.
- **Installing on macOS.** Needs macOS 12 or later (Intel and Apple silicon). The installers are not signed or notarised yet. On macOS 15 and later,
  double-click the `.pkg`, then open System Settings › Privacy & Security, scroll to the message
  about the installer and click **Open Anyway** (right-click › Open no longer works for installers).
- **Uninstalling.** Delete the three bundles of each editor: the app in `/Applications`
  (`Machinedrum Editor.app`, `Monomachine Editor.app`), the VST3 and the AU
  (`Machinedrum Editor.vst3`/`.component`, `Monomachine Editor.vst3`/`.component`;
  `Gearmulator MD`/`MM` up to 0.3.1) in `/Library/Audio/Plug-Ins/VST3`
  and `.../Components`, or the `~/Applications` and `~/Library/Audio/Plug-Ins` equivalents. Firmware and
  settings are not touched. If a DAW will not load the AU, run
  `xattr -dr com.apple.quarantine "<path to the .component>"` and rescan.
- **Bugs, questions, beta builds:** the [contact page](https://mdmm.dev/contact/) or the
  [Discord](https://discord.gg/8xwXwBHbtn) (#bugs, #ideas). Issues are off on GitHub.
  Never ask for, or share, firmware: bring the ROM of the machine you own.
- **Credits:** this project grew out of [joelanders' Machinedrum and Monomachine emulation](https://github.com/joelanders/gearmulator-md-mm),
  itself built on [Gearmulator](https://github.com/dsp56300/gearmulator) by The Usual Suspects and contributors
  (the DSP56300 emulator, the 68k core and the plug-in framework). Since 0.5 it is a standalone,
  Machinedrum and Monomachine only tree: their code we use lives here as plain folders we maintain
  (`source/dsp56300`, `source/mc68k`, `source/JUCE`, `source/3rdparty/RmlUi`, each with a README naming
  its origin), the other synths are removed, and useful upstream fixes are copied one commit at a time
  ([doc/ROADMAP.md](doc/ROADMAP.md)). Their original README is kept as [README.upstream.md](README.upstream.md).
- **Third-party code in the editors:** [Monocypher](https://monocypher.org) 4.0.2 (Loup Vaillant, Michael Savage,
  Fabio Scotoni; BSD-2-Clause or CC0-1.0) verifies update signatures
  ([source/elektron/md/mdmmUpdate/monocypher/](source/elektron/md/mdmmUpdate/monocypher/LICENCE.md)).
- GPL-3.0 ([LICENSE.md](LICENSE.md)). Machinedrum, Monomachine and Elektron are trademarks of
  Elektron Music Machines MAV AB; this project is not affiliated with or endorsed by Elektron.

## Building

CMake 3.22 or newer, a C++17 compiler; on macOS Xcode (the release build is universal), on Windows Visual
Studio 2022 with WebView2, on Linux GCC 13 and webkit2gtk. Only two submodules remain (freetype and lunasvg):

```bash
git clone --recurse-submodules https://github.com/radekdymacz/mdmm.git
cd mdmm
scripts/mdmm-dev.sh play md     # build the Machinedrum Editor standalone and open it (or: play mm)
scripts/mdmm-dev.sh build       # both editors: standalone, VST3, AU
scripts/mdmm-dev.sh tests       # the unit tests
```

Release packages: `scripts/macos/build_mdmm.sh`, `scripts/windows/build_mdmm.ps1`, `scripts/linux/build_mdmm.sh`.
Tests that need the firmware read your ROM from `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`.
How we work, test and release: [doc/release/CI.md](doc/release/CI.md); the editor's layers and how to add to
them: [doc/modern-ux/FOUNDATION.md](doc/modern-ux/FOUNDATION.md).

## Where things are

| Folder | What |
|---|---|
| `source/elektron/md/` | Everything of ours: the emulated machines (`mdLib`), the data layer, the desks, the plug-ins and the page (`mdJucePlugin`, `skins/`), the updater, the tests |
| `source/dsp56300/`, `source/mc68k/` | The DSP56300 JIT emulator and the 68k (ColdFire) core |
| `source/synthLib/`, `jucePlugin*`, `baseLib/`, `hardwareLib/` | The shared plug-in framework |
| `site/` | mdmm.dev |
| `doc/` | Design, research, release notes, bug and idea logs |

## Implementation references

- [TurboMIDI negotiation](doc/turbomidi.md): a worked exchange, firmware observations and sender policy.
- [Emulation CPU research](doc/modern-ux/RESEARCH-emulation-cpu.md): where the time goes and the speed-up plan.
- [Elektron MD/MM SysEx](doc/elektron_md_mm_sysex.md).
