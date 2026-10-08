# Ideas from users

*Feature ideas from beta testers and users (Discord, email). Not bugs: see
[BUGS.md](BUGS.md). Newest first. Each entry: who and when, the idea, status.*

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
