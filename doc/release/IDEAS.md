# Ideas from users

*Feature ideas from beta testers and users (Discord, email). Not bugs: see
[BUGS.md](BUGS.md). Newest first. Each entry: who and when, the idea, status.*

## I-015 · Pattern length per track, and microtiming

- **From:** Discord tester L, 2026-10-09, in #ideas: "pattern length per track and microtiming of course". Radek: "Nice one".
- **Note:** not in the firmware's pattern: the Machinedrum has no per-track length (`DESIGN-generators.md`) and the Monomachine's tracks share one scale length (`manual-mapping.md`). It would be an editor feature on top of the machine (or a new sequencer), not a mapping of what the firmware does. Microtiming is not in the mapped pattern format either (to check).
- **Status:** new.

## I-014 · Make the EXT / CLASSIC mode and LOAD ROM easier to find

- **From:** Discord tester D, 2026-10-08: "everything I tried to do didn't seem to work. The binary loaded automatically and I don't see where to add a different firmware." Tester A, 2026-10-09: "Seems MD editor is in Advanced Mode Always? kits and patterns are always connected? Havent seen an option to select mode."
- **What happened:** both were answered in the channel. LOAD ROM is in the engine menu in the header (Radek posted a screenshot); the mode is the small MODE EXT field in the LCD's second row (click for CLASSIC: patterns do not load their kits, locks stay in the pattern but do not play, the lock lane says "CLASSIC: locks muted").
- **Idea:** say it where people look: the mode in the guide (no word on EXT / CLASSIC today; the guide still describes version 0.3.2) and a tooltip on the field; LOAD ROM also in the editor menu next to Open Log Folder.
- **Status:** new.

## I-013 · Windows: an installer instead of copying folders

- **From:** Discord tester A, 2026-10-08, 0.3.4, on a Windows 10 PC: "there no installer on 0.3.4 / only manual install to folders". The Windows build is a zip (the app and the VST3 folder) with a README for the install paths.
- **Idea:** a setup program (app, VST3, shortcuts, uninstall), signed when the signing account exists ([SIGNING.md](SIGNING.md)).
- **Status:** new.

## I-012 · Switch from song mode to pattern mode

- **From:** Discord tester J, 2026-10-08 (0.3.4), in #bugs: "how do we switch from song mode to just pattern?" Radek: "may not be implemented fully yet".
- **Done:** 0.3.5 has the PATTERN | SONG switch on the Song page ("What plays") and PAT / SONG on the LCD's second line, on every page; the switch lights what the machine reports.
- **Status:** shipped in 0.3.5; tester J has not said whether it is what was meant.

## I-011 · A song library pop-up instead of stepping the SONG field

- **From:** Radek, 2026-10-09: the LCD's SONG nn field steps through songs (click, drag, scroll). It should open a pop-up like the kit library instead: all songs with names, and load, copy, paste, clear and save from one place.
- **Scope:** both editors (MD 32 songs, MM 24). Same layout and keys as the kit library.
- **Status:** planned, after 0.3.5.

## I-010 · Monomachine: note length like Ableton/Digitakt

- **From:** Discord tester C, 2026-10-08: dropping notes in the MM piano roll, a note runs until the next one; wants explicit trig/note length (place 1/16, then 1/8 notes as in Ableton).
- **More:** the notes run on until the next one and are then cut off (tester C's words); Radek on 2026-10-09 in #bugs: the Monomachine piano roll "needs to be better as well". C is used to the Digitakt's trig length and Ableton.
- **Status:** new.

## I-009 · Option to turn off the "kit has unsaved edits" warning on pattern change

- **From:** Discord tester C, 2026-10-08 (mostly the Machinedrum, maybe the Monomachine too).
- **Status:** new.

## I-008 · The editor menu in the editor's own style (next, after 0.3.4)

- **From:** Radek, 2026-10-08.
- **Idea:** the right-click / editor menu (GUI Scale, Page Zoom, RAM recording, Updates) is JUCE's default dark-blue menu. Draw it in the page with the themed menu from the key map (deskMenu.js): plate colours and fonts, light/dark and MK1/MK2, the same in every DAW and OS. First line "Machinedrum Editor <version>"; Performance diagnostics into a Developer submenu.
- **Status:** in progress for 0.3.5 (branch `wip/0.3.5-menu`): option A, the menu drawn in the page. The window sends the entries as data (`mdEditorMenu.h`, one tree: the page, the menu bar, and a native menu only when no page is up), the page draws them (`deskMenu.js`: submenus, ticks, keyboard, placement) and the window runs the one chosen. Content: the editor and version as the title; Zoom (the page's zoom steps, with Window Size inside: upstream's GUI Scale, the window at a size of the design); Updates; Open Log Folder; Audio/MIDI Settings (standalone); Developer (performance capture, RAM recording). Screenshots for review first.

## I-007 · Ableton-style editing on the Monomachine piano roll

- **From:** Discord tester C, 2026-10-08.
- **Idea:** B toggles draw mode on the piano roll (as in Ableton); box-select notes.
- **Status:** new; fits the keyboard-map port to the Monomachine (K7).

## I-005 · Update from inside the app (after 0.3.2)

- **From:** Radek, 2026-10-07.
- **Idea:** the standalone app checks `https://mdmm.dev/latest.json` (once a day, can be turned off; no third-party server), shows "Update available", downloads the new version, verifies it with our own signature (ed25519, independent of Apple/Microsoft), replaces the app and the VST3/AU/CLAP plug-ins and restarts. Plug-ins in a DAW only show a note ("open the app to update"). macOS, Windows (swap after exit; a DAW must be closed for plug-ins) and Linux. A system-wide install asks for the password once.
- **Status:** built on `feat/updates` (2026-10-08), design in [DESIGN-updates.md](../modern-ux/DESIGN-updates.md). As built: the standalone installs (macOS: opens the .pkg in the Installer; Windows, Linux: swaps the app after exit); the plug-ins are never replaced by the app (a DAW may have them loaded). Installs wait for the update key ([SIGNING.md](SIGNING.md)).

## I-004 · The site and latest.json update themselves on release (after 0.3.2)

- **From:** Radek, 2026-10-07.
- **Idea:** when a release is published, a GitHub workflow writes the version into `site/public/config.js` and `site/public/latest.json` and deploys the site. Download links already follow `releases/latest/download/<fixed name>`.
- **Needs:** one Cloudflare API token (Workers edit on the NativeKloud account only) saved as a GitHub secret — Radek.
- **Status:** built on `feat/updates` (2026-10-08): `.github/workflows/mdmm-site-release.yml`. Needs the secrets in [SIGNING.md](SIGNING.md) (update key, Cloudflare token).

## I-003 · Follow the last played track (external MIDI controller)

- **From:** Discord tester B, 2026-10-07.
- **Idea:** an option to switch the selected track (instrument focus) to the one last played from an external MIDI controller.
- **Status:** new.

## I-002 · Show incoming notes on the Sound page

- **From:** Discord tester B, 2026-10-07.
- **Idea:** a visual sign on the Sound page when a note arrives from a MIDI controller (which track was triggered).
- **Status:** new.

## I-001 · Select steps, copy, paste and duplicate

- **From:** Discord tester A, 2026-10-07: "copy only one step and paste it, same track".
- **Idea:** select one or more steps with a modifier-drag; copy, cut, paste, duplicate, delete.
- **Status:** being built (`feat/step-selection`).
