# P8 result: v0.2.0 complete (MM-P4 on the P6/P7 core, the site, "Install for me only")

- **Branch:** `p8/mm-p4-port` (from `release/v0.2.0` @ e3e845c6: P7 plus the release packaging), 2026-09-29.
- **Why:** the MM-P4 work (`mm/editor`: 05e4ef47, efdbfc3a, 17a36648) landed after the P5 merge, so
  v0.2.0 still greyed out the Monomachine features MM-P4 had made real. P8 re-implements them the
  P6 way instead of merging the old structure back. The evidence (byte offsets, RAM addresses) is
  MM-P4's: [MM-P4-RESULT on `mm/editor`](https://github.com/radekdymacz/mdmm/blob/mm/editor/doc/modern-ux/MM-P4-RESULT.md).

## What was ported, and where it lives now

| MM-P4 feature | P8 (P6 architecture) |
|---|---|
| Codec facts: kit 0x1d0 JOY mirror, 0x2aa LPF, 0x2ab HPF, 0x2ac PORTAMENTO, 0x2b6-0x2b9 MULTI TRIG; global 0x05/0x06 CONTROL IN, the MULTI MAP's six fields | `elektronData/mmKit.*`, `mmGlobal.*`, `mmJson.cpp`, `mmValidate.cpp`; contract `multiTrig`, `trackMasks.portamento`, `controlIn`. P7's `hostFollowing` now names `tempoSync` / `transportIn` |
| The machine's mutes and recording mode (RAM 0x2bfead, 0x2bfedd, 0x26bbb3, 0x2bff01) | `mdLib/mmtelemetry.h` -> `mmDesk::Telemetry` (`mutes`, `recording`); published as `machine.mutes` and `telemetry.record` (the transport stays in telemetry) |
| MIDI track mutes (the MUTE window: FUNCTION + BANK GROUP, TRIG 9-14, EXIT) | the `muteMidi` command row; `MmMachine::cmdMuteMidi` presses the keys only when RAM differs |
| POLY (SET STATUS 0x20) | the `poly` row; status 0x20 polled with the others; `machine.poly` |
| GRID / LIVE RECORDING | the `record` row (`mode` off/grid/live); the current pattern is read back every second while recording and once when it stops |
| MULTI TRIG, PORTAMENTO | kit fields; they have no live message, so the existing working-kit path sends a dump to the current slot plus LOAD KIT |
| MULTI MAP | global fields; `mmConvert.mapToPage/mapToFw`; the mockup's range edits now say they edited the global |
| The 24 songs | the mockup's song picker and **Load on the machine** (`HOST.songSlot`, `HOST.loadSong`) |
| Undo across library slot writes | already in P6 (the core's one history: a library write is a `set` of that slot with the gesture id). P8 adds the tests |
| HW MIDI: SEND n, the RECV guidance, Send now | without the panel (`Profile.panel` false) the adapter keeps the dumps (and the LOAD KIT after a kit dump) in order: `machine.recv.waiting`; the page shows SEND n and the mockup's dialog; **Send now** is the `hwSend` row |
| HW MIDI: the MIDI Start offer | an ask from the adapter, `transportIgnore`, before `play` while the active global's TRANSPORT is IGNORE; confirmed, it writes TRANSPORT ACCEPT (a global dump, so SEND 1) |
| "Reload the active global after writes" | already in P7 (0x56 once the active slot's dump is read back and RECV is left); over HW MIDI, P8 sends it once more before PLAY |
| Capabilities | `MmModel::unsupported()` is empty. `midiMutes` and `gridRecord` are true where the adapter has the panel and RAM, false over HW MIDI with the reason (Appendix B/C: no MIDI message); `poly`, `multiTrig`, `multiMap`, `portamento` are true on both engines |
| Probe lab ops (`ramdiff`, `peek8`, `status`) | `mdLibTest/mmEditorProbeFirmwareTest.cpp` |

The schema's commands and asks are generated from the tables (`mmDeskTest --write-schema`); the
hand-written parts (kit, global, machine, telemetry) gained the new members.

P7's work is kept as it was: the modals, the LCD slot, drag-paint, Shift mutes, LEN, window fit,
SysEx import and export, DAW follow, the boot card and ROM install.

## Not ported, and why

- Nothing of the feature list. MM-P4's own limits stay: STEP RECORDING is not on the page; over HW
  MIDI the Monomachine cannot report its mutes, playhead or tempo; **no real Monomachine was
  tested** (HW MIDI is verified against a second emulated machine as the MIDI peer).

## Tests

| Test | Result |
|---|---|
| Unit tests (`ctest -E "Plugin|_AU|VST|FirmwareTest"`) | 75 pass, 2 skipped; the 2 known `synthLib` failures (`synthLibMidiClockTimingTest`, `synthLibAudioTest`) |
| `mmDeskTest` | PASS (HW MIDI: the dump waits for SEND 1, `hwSend` sends it, a missing read-back is an error) |
| `mmConvertTest` (with the local factory set) | 321 checks PASS; `mmDataCorpusTest` 288 dumps byte-exact |
| `mmDeskFirmwareTest` (all: smoke, trigkinds, hostclock, patterns, `p4`, `hw`) | PASS, 84 checks, 0 off the contract |
| `mmDeskFirmwareTest p4` | 23 checks: POLY, MIDI track 1 plays / muted through the MUTE window / unmuted, synth mute in RAM, LIVE RECORDING records a note, GRID RECORDING reads a TRIG key back, MULTI TRIG and PORTAMENTO, MULTI MAP, song 24 edited and loaded, undo of a K100 write |
| `mmDeskFirmwareTest hw` | 22 checks: connect 1.3 s, 128 kits in 30.5 s at DIN speed, a CC reaches the working kit, SEND 1 then Send now (read back in 457 ms), POLY, mutes and RECORD refused with the reasons, MIDI Start ignored with TRANSPORT IGNORE, the offer, TRANSPORT ACCEPT through RECV, PLAY and STOP, unplug and replug |
| `mdSessionFirmwareTest`, `mmBootFirmwareTest` | PASS |
| In-plugin self-tests (diagnostics build, standalone MM) | `p4` **8/8**, `1` 11/11, `p7` 11/11, `p6audio` 7/7; MD `1` no failures |
| Release build (`scripts/macos/build_mdmm.sh`: diagnostics OFF, arm64 + x86_64, ThinLTO, the firmware-backed release tests, pluginTester on both VST3s, the package verification) | PASS (resumed once past the known `synthLibAudioTest`, as for v0.2.0); no self-test code in the release binaries |
| auval, from a "for me only" install of both packages (no sudo) | **AU VALIDATION SUCCEEDED** for `aumu Tmdr GmPv` and `aumu Tmno GmPv` |

## Installers: "Install for me only"

The distribution enables both domains (`enable_localSystem`, `enable_currentUserHome`). The
payload paths are relative, so the person's choice decides the root: `/Applications` and
`/Library/Audio/Plug-Ins/...` for all users (administrator), `~/Applications` and
`~/Library/Audio/Plug-Ins/...` for them only (no administrator; managed Macs). The welcome and
readme pages, the release notes and the site's download page say both;
`verify_mdmm_pkg_install.sh` finds either location and warns when both hold a copy.

Tested for real on this Mac without sudo: `installer -pkg <pkg> -target CurrentUserHomeDirectory`
for both packages ("Installing at base path ~"); the receipts are on the home volume
(`pkgutil --volume ~ --pkgs`); `verify_mdmm_pkg_install.sh` passed (both bundles universal,
signatures valid, auval). The install was removed and made again, and left in place for Radek.

## The release

- The draft `mdmm-v0.2.0` holds the two rebuilt packages and the updated notes; it stays a draft.
- The tag `mdmm-v0.2.0` points at this branch's last commit. The tag workflow now finds an
  existing release in the release list, drafts included (`gh release view <tag>` does not see a
  draft), keeps its assets and creates no second release.
- `main` is created at the same commit.
