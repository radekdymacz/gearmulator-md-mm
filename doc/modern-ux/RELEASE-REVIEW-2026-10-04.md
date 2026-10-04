# Release review: MD + MM Editors (2026-10-04, main @ 855902a8c)

Five read-only reviews, merged here into one fix list. Full reports with file:line, failure scenarios and
fixes: [release-review-2026-10-04/](release-review-2026-10-04/) (`ux-md.md`, `ux-mm.md`, `code-cpp.md`,
`code-js.md`, `release.md`). Screenshots were left in the session scratchpad (not committed).

Tests: C++ unit tests 14 pass, 2 skipped (need firmware). Node page tests 12/12 pass, mmConvert 1313 checks
pass, both sync checks and `page_contract_check.py` clean. No firmware/ROM test ran: build locally with the
ROMs before tagging.

## Tier 1: fix before tagging

Bugs a user will hit, and anything that looks broken.

| # | Item | Where (report id) |
|---|---|---|
| 1 | MM push parked for SYSEX RECV never times out: stuck pending, panel-key spam, transport refused as "panel busy" | code-cpp A1 |
| 2 | A load with no reply is dropped and never retried: on HW MIDI every edit is refused "not loaded yet" | code-cpp A2 |
| 3 | Negative run index writes before a heap buffer (x86_64 build) | code-cpp S1 (`mmJson.cpp:267`) |
| 4 | JSON robustness: `-Ofast` lets inf/nan out, locale-dependent numbers, UTF-8 mangled; any one loses a whole bridge batch | code-cpp S2, S3, S10 |
| 5 | Last `gm.recv` batch replayed when the web view is shown again; notice sink cleared by another window | code-cpp S5, S4 |
| 6 | MD solo/un-solo loses the machine's mutes; M during solo and the new M/S drag desync page and machine | code-js S1 |
| 7 | Shortcuts fire behind dialogs and panels (Space = play, Delete clears steps, digits switch workspace) | code-js S2, S8 |
| 8 | A plug-in notice can be replaced by another dialog, so its C++ callback never runs | code-js S3 |
| 9 | MD: a render held during a drag is never flushed; MM: old engine's pattern/kit stay after an engine switch | code-js S4, S5 |
| 10 | MD GLOBAL routing row overflows the dialog ("MAINMAIN…") | ux-md 1 |
| 11 | MD error line pushes the whole page down 53 px and is shown twice (line + toast) | ux-md 2 |
| 12 | HW MIDI connecting/lost/no machine looks like a working empty editor (both editors) | ux-md 3, ux-mm 12, 13 |
| 13 | MM Perform › MULTI MAP: joystick covers the PATTERN column | ux-mm 1 |
| 14 | MM text names the Machinedrum ("FUNCTION + knob on the Machinedrum", FX-REVERB tooltip) | ux-mm 2 |
| 15 | Release paperwork: version mismatch (0.2.1 / 0.2.0 / 0.1.0-alpha), stale drafts, no notes for 28 commits | release 1, 3 |
| 16 | README says drag a ROM onto the window (removed); site advertises MIDI learn and LFOs (hidden); macOS install steps wrong for unsigned .pkg (needs Privacy & Security > Open Anyway) | release 4, 5, 6 |

## Tier 2: should fix in this release if time allows

- Minimum window unreadable (5 px hit targets at zoom 0.6); large windows never scale up (ux-md 4, 5).
- Setup (GLOBAL, AUDIO/MIDI, LOAD ROM) hidden in the "EMU OS ▾" dropdown; "SETUP" header group holds Undo/Redo (ux-md 8).
- Truncation at 1440: GEN summary "E…", rail tags, kit names, Alt shifts the GEN bar (ux-md 10, ux-mm 4-6, 8, 10, 11).
- "NOT VERIFIED" badges, host-never-answers spins forever, focus ring lost on LCD (ux-md 6, 7, 9).
- Stray leading comma in the shared AUDIO/MIDI footer; start-up card links run together (ux-mm 3, 14).
- .syx import says "Imported" when items were refused (code-cpp S6).
- UW sample scan allocates on the audio thread (code-cpp S7).
- MM LOAD KIT piles up behind a waiting kit dump on HW (code-cpp A3).
- Held notes and held PTCH not released on window close / engine switch (code-cpp A4).
- Modulators notify the DAW as user gestures (code-cpp S8); lock-row helpers unbounded (S9); gigabyte allocation from a hidden lock pool (S12).
- Unescaped text into innerHTML, kit names in ask HTML (code-js S7, code-cpp N8). Low harm.
- MD Shift-prepared mutes survive focus loss (code-js S6).
- MUTATE looks live on MM MIDI tracks but does nothing (ux-mm 7).
- Windows CI flaky (4 of 5 recent pushes failed before going green) (release 2).

## Tier 3: after release

Dead code and the duplicate first-run dialog (ux-md 20, ux-mm 24, code-js N6), one undo step per wheel notch
(code-js N5), redraw per message during background read (N3), bridge iframe-per-batch (N8), hint contrast,
toast placement, Mix polish (ux-mm 15-23), the remaining code-cpp N items and A5-A9.

## Parity (ux-mm)

The MM port of keys, GEN, MUTATE, comforts, Sound by function, library ask, Song MORE and CHAIN is complete.
Two gaps remain: no MM equivalent of the MD GLOBAL panel, and MIDI tracks cannot be played from the keys.

## Housekeeping

`AGENTS.md` (untracked, repo root) is a copy of the upstream CLAUDE.md for another tool. Do not commit it;
delete it or add it to `.git/info/exclude`.
