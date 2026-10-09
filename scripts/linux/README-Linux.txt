@EDITOR@ @VERSION@ for Linux x86_64 - NOT TESTED
=================================================================

NOT TESTED: this build comes from CI. It compiles, passes the unit tests
and was started on a virtual X11 display, but no person has used it on a
Linux desktop or in a Linux DAW yet. Reports are welcome.

Bugs: https://mdmm.dev/contact/
Discord (#bugs): https://discord.gg/8xwXwBHbtn
Say which editor and version, your distribution and desktop (X11 or
Wayland), and whether it is the standalone or the VST3 (and in which DAW).
Please do not ask anyone, in any channel, for firmware files.

What is in here
---------------
  Standalone/@PRODUCT@            the standalone app (run it from here)
  Standalone/libwebkit2gtk-4.0.so  web view shim, keep beside the app
  Standalone/libgtk-3.so           web view shim, keep beside the app
  VST3/@PRODUCT@.vst3/             the VST3 plug-in (a folder, keep it whole)
  LICENSE.md                       GPL-3.0

Install
-------
  VST3:        copy the whole folder "VST3/@PRODUCT@.vst3" into ~/.vst3/
               (for all users: /usr/lib/vst3/ or /usr/local/lib/vst3/),
               then rescan plug-ins in your DAW.
  Standalone:  run Standalone/@PRODUCT@ in place; no installation needed.
               Keep the two .so files beside it.

Bring your own ROM
------------------
No firmware is included. On first start the editor asks for the ROM of
your own machine; follow the card in the editor window.

Required packages
-----------------
The editor window is a web page shown by WebKitGTK (webkit2gtk 4.0 or 4.1,
either works) through GTK 3. Audio is ALSA; the UI is X11 (under Wayland it
runs through XWayland). Built on Ubuntu 22.04: needs glibc 2.35 or newer.

  Ubuntu 22.04 / Debian 12:
    sudo apt install libwebkit2gtk-4.1-0 libgtk-3-0 libasound2 libgl1 \
      libx11-6 libxext6 libxcursor1 libxinerama1 libxrandr2 libxrender1 \
      libxcomposite1 libfreetype6 libfontconfig1
    (libwebkit2gtk-4.0-37 instead of -4.1-0 works as well)
  Ubuntu 24.04 and newer:
    sudo apt install libwebkit2gtk-4.1-0 libgtk-3-0t64 libasound2t64 libgl1 \
      libx11-6 libxext6 libxcursor1 libxinerama1 libxrandr2 libxrender1 \
      libxcomposite1 libfreetype6 libfontconfig1
  Fedora:  sudo dnf install webkit2gtk4.1 gtk3 alsa-lib mesa-libGL
  Arch:    sudo pacman -S webkit2gtk-4.1 gtk3 alsa-lib

Troubleshooting
---------------
  Empty (grey) editor window: WebKitGTK is missing; install the package
    above. The two shims must stay beside the app / inside the VST3 folder.
  "GLIBC_2.35 not found": your system is older than this build supports.
  No window at all: check that X11 or XWayland is available.
