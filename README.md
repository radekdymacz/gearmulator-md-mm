# Machinedrum Editor + Monomachine Editor

This fork adds two screen-native editors on top of joelanders' Machinedrum and Monomachine
emulation: the whole machine on one page (step grid, parameter-lock lanes, kit and pattern library,
chains, mutes, a sampler view, and a beta HW MIDI mode for a real machine). They run the real firmware,
as a standalone app, VST3 and AU on macOS.

- **Site, screenshots, download:** https://mdmm.dev
- **Releases:** https://github.com/radekdymacz/gearmulator-md-mm/releases
  (latest: [0.3.0 notes](doc/release/v0.3.0.md); [0.2.1](doc/release/v0.2.1.md); [0.2.0](doc/release/v0.2.0.md); first: [0.1.0 alpha](doc/release/v0.1.0-alpha.md))
- **User guide** (install, ROM, DAW use, workspaces, every keyboard shortcut): https://mdmm.dev/guide/
- **Bring your own ROM.** No firmware is included. With no ROM the editor shows a start-up card:
  choose your machine's image with its file chooser (drag and drop is not supported) and the editor
  copies it into its ROM folder. You can also copy it to `~/Documents/Gearmulator Preview/Machinedrum/roms/`
  or `.../Monomachine/roms/` yourself.
- **Installing on macOS.** The installers are not signed or notarised yet. On macOS 15 and later,
  double-click the `.pkg`, then open System Settings › Privacy & Security, scroll to the message
  about the installer and click **Open Anyway** (right-click › Open no longer works for installers).
- **Uninstalling.** Delete the three bundles of each editor: the app in `/Applications`
  (`Machinedrum Editor.app`, `Monomachine Editor.app`), the VST3 and the AU
  (`Machinedrum Editor.vst3`/`.component`, `Monomachine Editor.vst3`/`.component`;
  `Gearmulator MD`/`MM` up to 0.3.1) in `/Library/Audio/Plug-Ins/VST3`
  and `.../Components`, or the `~/Applications` and `~/Library/Audio/Plug-Ins` equivalents. Firmware and
  settings are not touched. If a DAW will not load the AU, run
  `xattr -dr com.apple.quarantine "<path to the .component>"` and rescan.
- **Bugs and questions about the editors:** use the [contact page](https://mdmm.dev/contact/),
  not upstream. Issues are disabled on this fork.
- **Credits:** the MD/MM emulation is by [joelanders](https://github.com/joelanders/gearmulator-md-mm);
  Gearmulator, the DSP56300 and 68k emulation are by The Usual Suspects and the
  [Gearmulator](https://github.com/dsp56300/gearmulator) contributors.
- GPL-3.0 ([LICENSE.md](LICENSE.md)). Machinedrum, Monomachine and Elektron are trademarks of
  Elektron Music Machines MAV AB; this project is not affiliated with or endorsed by Elektron.

The editor design and results are in [doc/modern-ux/](doc/modern-ux/). Upstream's README follows
unchanged.

---

My fork of TUS's Gearmulator project, where I add emulations of Elektron's
Machinedrum and Monomachine.

I'm not affiliated with TUS or Elektron. Don't bug them for support :)

There is a Discord channel [here](https://discord.gg/BnkTKpmp8) at #gearmulator-development.
**Do NOT discuss firmware or ROMs in Discord.**
**DO NOT ask us for the .bin files / firmware! They're under Elektron's copyright. This emulator is for people who own the original hardware.**

[Downloads](https://github.com/radekdymacz/gearmulator-md-mm/releases) ·
[Report a bug](https://mdmm.dev/contact/)

Link to a short demo on Youtube:

<a href="https://www.youtube.com/watch?v=NmfE5xljYRU"><img width="800" alt="youtube" src="https://i3.ytimg.com/vi/NmfE5xljYRU/maxresdefault.jpg" /></a>


## Features

- **Key chording / p-locks:** shift-click one or more buttons to hold them
  down until you release the shift key.
- **Secondary functions:** rather than shift-click Function and another button,
  you can just click the secondary function text label.
- **Encoder clicking:** Alt/Option-click a DATA ENTRY encoder to press it, or
  Alt/Option-drag to press and turn. With a trig held, pressing its parameter's
  encoder toggles that parameter lock. This applies to encoders A–H, not LEVEL
  or SOUND SELECTION.
- **Send SysEx File** under the right click menu to send a `.syx` file to the
  machine. The menu shows transfer progress and lets you cancel. Follow the
  machine's normal receive procedure.
- **Panel look and feel:** adjust encoder-drag and mouse-wheel sensitivity in settings.
  An experimental crisp LCD/panel rendering option is also available.
- **Audio inputs and outputs:** route host audio to the machine's input effects or sampling
  functions. Additional output pairs are available in a multi-output VST3 host;
  the standalone apps use stereo output.

## Implementation references

- [TurboMIDI negotiation](doc/turbomidi.md): a worked exchange, firmware observations,
  and Gearmulator sender policy.

Thanks to the upstream Gearmulator contributors whose work makes this fork
possible. See [the upstream README](README.upstream.md) for the original project
overview.
