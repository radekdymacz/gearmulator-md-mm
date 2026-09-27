# MM-P2 result: the Monomachine Editor page on the firmware (MM OS 1.32B)

- Branch `mm/editor`, 2026-09-27, Apple silicon, build dir `temp/cmake_mm` (Release, arm64).
- ROM used in place from `~/Downloads` (SHA-1 `11a37460…e605`); for the plug-in runs it was linked into the user ROM folder and removed afterwards.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | The mockup as the real UI of the MM plug-in | **GO.** `skins/mmStudio`: the approved mockup's markup, stylesheet and script run unchanged in a JUCE web view; `mmStudio` is the MM plug-in's default skin (the SFX-60 panel stays selectable) |
| 2 | Fed by the firmware's documents | **GO.** Every pattern, kit, song and global (288) is read at start; the current pattern, working kit, song and global fill the page, the rest fill the pattern chooser and the kit library |
| 3 | Edits go back to the firmware | **GO.** After every edit the page's state is converted to documents and compared with the machine's; what differs is sent. Kit values live (CC, NRPN, 0x5B, 0x5C, 0x55), patterns, songs, globals and other kit slots as dumps on SYSEX RECV, driven automatically |
| 4 | Sync script | **GO.** `doc/modern-ux/sync-mmstudio-skin.py` rebuilds the skin from `doc/modern-ux/mm-mockup/src` and checks the functions and ids the adapter relies on; `--check` reports drift |
| 5 | Boot screen | **GO.** While the firmware starts, the LCD field shows the machine's own LCD (128 x 64, drawn in the plate's LCD colours); it cross-fades to the editor's fields when the engine is ready and the current documents have arrived |
| 6 | Tests | `mmConvertTest` (page <-> contract round trip, 308 documents in ctest, 582 with the factory set), `mmDeskTest`, `mmDeskFirmwareTest` (now in ctest, skips without `-DGEARMULATOR_MM_ROM`), and the in-plug-in self test: **PASS 7/7** |

**Overall: GO for MM-P3.** What is still the mockup's own (not the machine's) is
listed in §6; MM-P3 makes each of those real or disables it with the reason.

---

## 1. Shape

```
firmware (MM OS 1.32B, mdLib)                      page (web view)
  RAM telemetry, patch RAM, LCD  ──>  MmStudioLink ──> mmDesk::Desk ──>  Bridge ──> mmAdapter.js ──> S (mockup state)
  SysEx, CC, NRPN, panel keys    <──  (plug-in edge)  <── (pure C++) <──  (JSON)  <── mmConvert.js <── the mockup's UI
```

- **mmDesk** (pure C++, no JUCE): documents in, delivery out, read-backs and the
  machine state published. Unchanged in shape from the MM-P2 desk commit.
- **MmStudioLink / MmStudioEditor** (the JUCE edge): the web view, the
  `gmbridge://` bridge (shared `mdDeskBridge.js`), panel key sequences in
  machine time, the working-kit region, the LCD, the MIDI learn commands.
- **mmConvert.js** (pure, no DOM): contract documents <-> the mockup's state.
  Enumerations become list indices (`index = floor(v*n/128)`); a raw value
  inside one enumeration step, residue and undecoded bytes ride along from the
  base document. Round trip rule: `toFirmware(toPage(doc), doc) == doc`.
- **mmAdapter.js**: the wiring. Fills `S`, wraps the mockup's edit hooks
  (`commit`, `structEdited`, `soundEdited`, `tx`, `undo`, `redo`) to schedule a
  sync, and replaces the mockup's example engine, clock, transport, pattern
  switching and kit LOAD / SAVE with the real commands.
- The mockup's script is not edited: the sync script removes only its pretend
  start-up (`startEngine("emu")`).

### What goes where

| Edit | Path to the machine |
|---|---|
| Any DATA page value, level, machine, routing, input, kit name, MIDI page, multi env | working kit, live: CC / NRPN / 0x5B / 0x5C / 0x55 (desk diff) |
| ASSIGN, TRIG POS, legato, mirror / key tracking | kit dump to the current slot + LOAD KIT (no live path) |
| Steps, trigless / pitchless trigs, notes, chords, locks, slide, swing, arp, transpose, length, multiplier, swing amount, pattern transpose | pattern dump on SYSEX RECV, read back |
| Pattern chooser: paste, clear, drag-copy | pattern dump into that slot |
| Kit library: save, save as, load, reload | SAVE KIT (0x59) / LOAD KIT (0x58) |
| Kit library: paste, clear, rename (other slots) | kit dump into that slot; into the current kit: live, then SAVE KIT |
| Song rows | song dump on SYSEX RECV |
| Routing mode, MIDI track channels and CCs | global dump on SYSEX RECV |
| Play / stop | PLAY / STOP keys |
| Pattern go / queue / now | LOAD PATTERN (0x57); "now" while playing = STOP, LOAD, PLAY |
| Tempo | 0x61 |
| Track mutes (synth tracks) | the plug-in's mute parameters |

Incoming documents are applied when the user is not dragging, not in a dialog
and has not edited in the last 0.7 s; one that equals what the page shows only
moves the baseline. A pattern or kit switch clears the editor's undo history
(an undo must not write the old pattern into the new slot); undo covers the
current pattern, kit, song and global, not the library slots.

## 2. In the plug-in: the self test

`GEARMULATOR_MMSTUDIO_SELFTEST=1` makes the page edit through the mockup's own
functions and log each firmware read-back (plug-in log
`~/Library/Caches/Gearmulator MM/gearmulator-mmStudio.log`). Standalone, factory
set, pattern A01, stopped:

| Check | Result |
|---|---|
| pattern trig, set and cleared (includes entering SYSEX RECV while the 288 documents load) | ok, 2846 ms for both round trips |
| trigless trig (ALT + click): trig bit and note, no AMP/FLT/LFO | ok, 226 ms |
| pitchless trig: trig and envelopes, no note | ok, 255 ms |
| kit value live (AMP VOL as CC), seen in the working kit in patch RAM | ok, 135 ms |
| pattern switch (LOAD PATTERN) and back | ok, 880 ms |
| play and stop | ok, 164 ms |
| song and global documents present | ok (65 rows, 3xSTEREO) |

**SELFTEST PASS 7/7.** Start to ready: the page loads in 1 s, the firmware
boots in about 8.5 s (engine `ready` when the start-up screen ends), all 288
documents are read about 10 s after that; the page is editable as soon as the
current pattern and working kit are in (under 1 s after ready).

The dump round trip once parked on SYSEX RECV is 62-69 ms (desk `roundTripMs`).
The desk leaves RECV 3 s after the last dump.

## 3. Found and fixed on the way

- **The panel key queue held 32 row states** (`md::Device::m_panelSequence`, the
  MD's). The MM's SYSEX RECV macro is 29 keys, about 60 states, so the plug-in's
  RECV session failed while the headless test (own queue) passed. Now 128.
- Factory patterns carry chord notes that belong to no chord step, and empty
  `0xffff` entries inside the count; the page leaves them where they are.
- The Control workspace's example mappings would have moved real kit values:
  the page now starts with none.

## 4. The browser harness

For UI work without the plug-in: a fake host (factory documents, a fake boot
LCD, RECV delay, queued switches) drives the same page over `window.gmDev`
(the bridge's development path). It lives outside the repository (it needs the
factory documents, which are not committed); the conversion test is the
committed guard.

## 5. Tests

| Test | What |
|---|---|
| `mmConvertTest` (ctest, UnitTest) | `mmDataCorpusTest --json` on `testdata/mm` (+ the local factory set when configured), then `node mmConvertTest.js`: every document survives page -> contract exactly; trigless, pitchless, chord, swing / transpose, machine + enums, ASSIGN amount and name edits land in the right bytes |
| `mmDeskTest` (ctest) | RECV session and desk on a scripted fake machine |
| `mmDeskFirmwareTest` (ctest, FirmwareTest; `-DGEARMULATOR_MM_ROM=`) | the desk on MM OS 1.32B: status, 288 documents, pattern / kit / song delivery, heard while playing, queued switch, trig kinds by ear |
| in-plug-in self test | §2 |

MD tests: unchanged and green (the two known `synthLib` failures aside).

## 6. Still the mockup's own (MM-P3)

These work in the page but do not reach the machine yet, or show example data:

- Perform: the keyboard does not play the machine; MULTI TRIG / MULTI MAP settings and POLY are page-only (the kit's multi trig bytes and the global's multi map fields are not decoded to the screen's fields); the joystick shows amounts only.
- Sequence/Sound: PORT (ALWAYS / ONLY LEGATO) is not decoded.
- MIDI tracks' mutes, song mode play, song load / save, tempo read-back (the machine has no tempo request).
- Control workspace sources and LEARN keys 1-8 are editor-side controllers (they move real values through the kit path); the plug-in's MIDI learn is not on the page yet.
- HW MIDI is disabled ("not available yet"); LOAD ROM opens the real ROM folder.
