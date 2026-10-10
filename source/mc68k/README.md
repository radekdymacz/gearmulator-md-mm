# mc68k: owned copy (Machinedrum Editor + Monomachine Editor)

The 68k / ColdFire core the editors run the Machinedrum and Monomachine firmware on (Musashi plus the
peripherals). This folder was a git submodule until 0.5. It is now a plain folder of this repository, and we
own it (doc/ROADMAP.md, D3): change it here like any other code; upstream fixes are copied one commit at a time
(D4).

- Came from: https://github.com/joelanders/mc68k-md-mm at commit `ace95b3d0a5a332db147244762dda65f9a010b9f`
  (a fork of https://github.com/dsp56300/mc68k).
- Left out: `.github/`. Every other file is as it was at that commit.
- Added: this section and `.gitattributes` (`* -text`, so git stores every file byte for byte as it came).
- Licences: `LICENSE.md` here (GPLv3) and the Musashi terms in `Musashi/readme.txt`.
