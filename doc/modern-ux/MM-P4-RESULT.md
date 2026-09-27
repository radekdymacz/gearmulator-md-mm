# MM-P4 result: the Monomachine Editor complete (MM OS 1.32B)

- Branch `mm/editor`, 2026-09-27, Mac16,8 (Apple M4 Pro), Release arm64, build dir `temp/cmake_mm`.
- Everything MM-P3 disabled is now real, except what the machine has no MIDI message for, over HW MIDI only (§3, with the evidence).

## Verdict

| # | Feature | Verdict | How |
|---|---|---|---|
| 1 | MIDI track mutes and solos | **GO** (emulator) | the MUTE window (FUNCTION + BANK GROUP, TRIG 9-14, EXIT); the machine's mutes read back from RAM and shown |
| 2 | POLY | **GO** (emulator and HW MIDI) | SET STATUS 0x20, read back with the status request |
| 3 | MULTI TRIG mode, split, timing | **GO** | kit bytes 0x2b6-0x2b9, decoded |
| 4 | MULTI MAP offset, length, transpose, timing | **GO** | global bytes, decoded; the whole row is edited |
| 5 | PORTAMENTO mode | **GO** | kit byte 0x2ac |
| 6 | GRID RECORD (and LIVE RECORD) | **GO** (emulator) | RECORD / RECORD + PLAY; the recorded pattern is read back while it records |
| 7 | HW MIDI | **GO, verified against the emulator as the MIDI peer** | the plug-in's MIDI in/out at DIN speed; SYSEX RECV is the person's to open |
| 8 | Undo across library slot writes | **GO** | one history for page edits and library writes |
| 9 | Any song, not only the current one | **GO** | song picker; LOAD SONG on the machine |
| – | Rebuild, reinstall (P3 copies backed up), auval | **GO** | `auval -v aumu Tmno GmPv`: AU VALIDATION SUCCEEDED |

Tests: `mmDeskFirmwareTest` smoke + trigkinds + `p4` (17 checks) + `hw` (15 checks) PASS (61 checks with smoke and trigkinds);
`mmConvertTest` 585 checks PASS; unit tests green apart from the two known `synthLib` ones;
**in-plug-in self test PASS 17/17**.

---

## 1. Found on the firmware (probe evidence)

New lab operations in `mmEditorProbeFirmwareTest lab`: `ramdiff` (RAM bytes steady before and
after an action, the machine's moving counters filtered out), `peek8`, `peek16`, `ramfind`,
`status`. Scripts and outputs are in the session's scratchpad, not in the repository.

| What | Where | Evidence |
|---|---|---|
| PORTAMENTO mode | kit 0x2ac, bit per track: set ALWAYS, clear ONLY LEGATO | TRIG window, PORTAMENTO LEFT: `2ac:3f>3e`, RIGHT: `3e>3f` |
| key tracking LPF / HPF | kit 0x2aa / 0x2ab | ASSIGN KEY tab, knob H: `2aa:3f>3e`; knob D: `2ab:3f>3e` |
| JOY mirror | kit 0x1d0 | ASSIGN JOY R/L, knob D: `1d0:3f>3e` (the tabs become JOY R, JOY L, …) |
| LEGATO FLT / LFO | kit 0x2b4 / 0x2b5 | TRIG window: `2b4:ff>fe`, `2b5:ff>fe` (MM-P1 had inferred them) |
| MULTI TRIG mode | kit 0x2b6: 0 ALL TRK, 1 SPLIT KEY, 2 SEQ START, 3 SEQ TRNSP | MULTI window, ENTER: `2b6:00>01`, `01>02`, `02>03` |
| MULTI TRIG timing | kit 0x2b7: 0 DIRECT, 1/16 2/16 4/16 8/16 16/16 32/16 | TIMING tab (seven values on the LCD), ENTER: `2b7:00>02`, `02>06` |
| split key / split track | kit 0x2b8 (note, 0x3c = C-4) / 0x2b9 (0-based first upper track, 0-5) | ZONES tab: `2b8:3c>3d`, `3d>3b`; `2b9:03>04`, `04>05` (stops at 5) |
| MULTI MAP fields | global 0x3c upper key, 0x5c pattern (0xff CUR), 0x7c offset (0xff ---), 0x9c length, 0xbc transpose (signed), 0xdc timing | MULTIMAP EDIT with the LEVEL knob: `03c:0b>0c`, `05c:ff>00`, `07c:ff>00` with `09c:10>40`, `09c:40>3f`, `0bc:00>fd` (LCD −03), `0dc:00>02` |
| CONTROL IN | global 0x05 TEMPO SYNC (0 INTERNAL, 1 EXT MIDI CLK), 0x06 TRANSPORT (0 IGNORE, 1 ACCEPT) | CONTROL IN screen, RIGHT: `005:00>01`, `006:00>01` |
| synth track mutes | RAM 0x2bfead + 4 t (non-zero = muted) | CC 3 on T1: `0x2bfead 00>02`; the MUTE window's TRIG 2: `0x2bfeb1 00>02` |
| MIDI track mutes | RAM 0x2bfedd + 4 t | MUTE window TRIG 9/10/11: `2bfedd`, `2bfee1`, `2bfee5 00>01`; TRIG 9 again `01>00` |
| GRID RECORDING | RAM 0x26bbb3 = 1 | RECORD on/off twice, reverting each time |
| LIVE RECORDING | RAM 0x2bff01 = 1 | RECORD + PLAY: 1; PLAY alone 0; RECORD while live goes to GRID (0x26bbb3 = 1) |
| POLY | SET STATUS 0x20 | status 0x20 reads 0, 1 after `71 20 01`, 0 after `71 20 00` |
| the MIDI mode | SET STATUS 0x21 | status 0x21 reads 1 after `71 21 01` |

**Corrections to MM-P1:** 0x2aa-0x2ac were taken to be mirror, HPF and LPF; the panel shows LPF,
HPF and PORTAMENTO. The kit contract's `trackMasks.mirror` now reads 0x1d0. The factory set and
every programmed read-back still re-encode byte for byte (575 of 575) and validate against the
updated schema.

**A fix found on the way:** a global dump writes the stored slot; the active global does not take
it until it is made active again. The desk now sends SET ACTIVE GLOBAL (0x56) after a dump into the
active slot, so routing mode, MIDI channels, the MULTI MAP and CONTROL IN take effect at once
(before, they waited for the next time the global was selected).

## 2. What the editor does now

| Control | Path |
|---|---|
| MIDI track mute / solo (rail, Mix, Perform) | `muteMidi`: the MUTE window keys, only when the RAM says the state differs |
| synth track mute / solo | the plug-in's mute parameter (CC 3), as before; the machine's own mutes (and those made on its panel) are read back and shown |
| POLY key | `poly`: SET STATUS 0x20; the page follows the machine's audio mode |
| rec key | stopped: GRID RECORDING (RECORD); playing: LIVE RECORDING (RECORD + PLAY); again: off. The current pattern is read back every second while recording and once when it stops, so notes recorded from the keyboard or written with the machine's TRIG keys appear |
| MULTI TRIG mode, split key/track, timing | the kit (dump to the current slot + LOAD KIT) |
| PORTAMENTO ALWAYS / ONLY LEGATO | the kit |
| MULTI MAP table | the global (dump + SET ACTIVE GLOBAL): range, pattern, offset (`---`), length, transpose, timing, split / delete |
| Song workspace | a picker of the 24 songs on the arrangement card; edits go to that slot; **Load on the machine** is LOAD SONG (the machine takes it only when stopped) |
| Undo / redo | page edits and library writes in one history. A library entry holds what each slot was and became; undo writes the old documents back (same path as the write) |
| Engine menu: HW MIDI | §3 |

## 3. HW MIDI

The engine menu's HW MIDI switches the desk to a real Monomachine on the plug-in's MIDI in and out
(`pluginLib::Processor::setExternalMidi`, the same as the MD's P4): SysEx from the ports goes to the
editor; the editor's messages go out at DIN speed (`mdDesk::DinPacer`, 3125 bytes a second); the
emulated machine's own output is held back.

- Status and dumps are SysEx both ways; kit values are CCs (and NRPN for the MIDI page and multi
  env); machine, routing, name, POLY, LOAD/SAVE KIT, LOAD PATTERN, tempo are SysEx.
- **SYSEX RECV on the real machine is the person's.** The editor cannot press its keys: pattern,
  song, global and stored-kit dumps wait, the pattern field shows **SEND n**, and its dialog says how
  to open GLOBAL › FILE › SYSEX RECV (MODE ORIG, YES). **Send now** sends them; the editor reads
  them back and says when to press EXIT.
- PLAY / STOP are MIDI Start / Stop. The factory global ignores them (CONTROL IN TRANSPORT IGNORE);
  PLAY then offers to set TRANSPORT to ACCEPT in the active global.
- Engine status: HW CONNECT until the first reply, HW MIDI while it answers, HW NO MIDI after 3.5 s
  without a reply (or none within 5 s).
- The working kit starts as the stored slot and then follows the editor's own edits: over MIDI the
  machine's memory cannot be read (edits made on the machine before connecting are not seen until
  SAVE KIT).

**Disabled over HW MIDI, with the reason (the machine has no MIDI message for them):**

| Control | Proof |
|---|---|
| MIDI track mutes / solos | Appendix B's control changes: CC 3 mutes a synth track on its channel; there is no MIDI sequencer track mute in Appendix B or C. The editor's only way is the MUTE window's keys, which exist on the panel only |
| RECORD (grid, live) | Appendix C lists every SysEx command; none switches the recording modes. Appendix B has none either |
| the playhead, the machine's tempo and the start-up LCD | read from the emulator's RAM and LCD; a real machine reports neither over MIDI (no status parameter for tempo or step in Appendix C) |

| Verified (`mmDeskFirmwareTest hw`, the emulator as the MIDI peer, DIN both ways) | Result |
|---|---|
| connect: status, the current pattern and its kit | 965 ms |
| all 128 kits | 28.4 s |
| a kit value as a CC reaches the machine's working kit | yes |
| a pattern edit waits for SYSEX RECV (SEND 1); the "person" opens it with the panel keys; Send now; read back | confirmed, 406 ms after Send |
| POLY over MIDI | yes |
| MIDI track mutes and RECORD refused over MIDI, with the reason | yes |
| MIDI Start ignored with TRANSPORT IGNORE; TRANSPORT ACCEPT set through RECV; then PLAY and STOP as MIDI Start / Stop | yes |
| unplugged, then back | HW NO MIDI, then HW MIDI |
| in the standalone with no Monomachine attached | HW CONNECT, HW NO MIDI, then EMU OS 1.32B again (self test) |
| **a real Monomachine** | **not tested: none here** |

## 4. Tests

| Test | Checks |
|---|---|
| `mmDeskFirmwareTest p4` | POLY on/off; MIDI track 1 plays on its channel, is muted through the MUTE window (no notes), unmuted (notes again); synth track mute in RAM; LIVE RECORDING records a MIDI note into the pattern read back; GRID RECORDING: a TRIG key on the machine is read back while recording; MULTI TRIG and PORTAMENTO in the working kit; MULTI MAP stored and read back. 17 PASS |
| `mmDeskFirmwareTest hw` | §3. 15 PASS |
| `mmConvertTest` | round trips (575 documents with the local factory set), plus MULTI TRIG / PORTAMENTO, a MULTI MAP row, and a song row added with the residue after END kept valid. 585 PASS |
| in the standalone, `GEARMULATOR_MMSTUDIO_SELFTEST=1` | the 10 MM-P3 checks plus: MIDI track mute, POLY, GRID RECORDING, MULTI TRIG + PORTAMENTO, MULTI MAP, another song (S24), undo of a kit paste into K100, HW MIDI with nothing attached. **17/17** (`=p4` runs the new ones) |

## 5. Known limits

- **No real Monomachine was tested**; HW MIDI is verified against the emulator as the peer.
- STEP RECORDING (STOP + RECORD) is not on the page; the rec key uses GRID and LIVE.
- The MIDI track mutes shown in HW MIDI are the editor's own (the machine cannot report them).
- Screenshots of the plug-in window are not possible from this session; the page was checked in the
  browser harness (fake host) and in the plug-in through its self test and log.

## 6. Installed

- VST3 `~/Library/Audio/Plug-Ins/VST3/Gearmulator MM.vst3`, AU `~/Library/Audio/Plug-Ins/Components/Gearmulator MM.component` (this branch, with the MM icon). **auval: AU VALIDATION SUCCEEDED.**
- The MM-P3 copies are backed up at `~/Library/Audio/Gearmulator MM P3 backup 2026-09-27/`, outside the plug-in folders.
- Standalone: `bin/plugins/Release/Standalone/Gearmulator MM.app` in the worktree.

## 7. User files

The Monomachine user folder was backed up at the start of MM-P4, with the ROM the coordinator put
in `roms/` (Radek's own copy, SHA-1 `11a37460a5f47fd1a4d911414288690e6e7da605`, now part of the
baseline and left in place). After the last plug-in run and `auval` it was restored from that backup
and compared: `diff -r` identical; `config/Gearmulator MM.xml` SHA-1
`975f92f43c9c2abfad47c7b140fbc1a193d4a45c` (the file as it was when MM-P4 started; Radek's older
version from before MM-P2 is `eaae1ea3…`), `config/midilearn` empty, the ROM unchanged. What the
runs created was removed: `logs/`, `skins/`, `~/Library/Application Support/Gearmulator MM.settings`,
and the MM caches and WebKit data.
