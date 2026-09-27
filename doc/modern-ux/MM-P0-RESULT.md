# MM-P0 result: live editing on the Monomachine firmware (MM OS 1.32B)

- Branch `mm/editor` (from `p3/md-editor-real`), 2026-09-27, Apple silicon, build dir `temp/cmake_mm` (the P0 configure, AU on).
- ROM: `elektron_sfx6-60_os1.32b.bin`, SHA-1 `11a37460…e605`, used in place from `~/Downloads`. No patch-RAM image: the firmware boots with its factory set (128 patterns, 128 kits, 24 songs, 8 globals).
- Harness: `source/elektron/md/mdLibTest/mmEditorProbeFirmwareTest.cpp` (manual, needs the ROM). `mdFirmwareSession::Machine` now takes the machine model, so the MD harnesses and this one share it.

## Verdict

| # | Question (mm-manual-mapping §0, §13) | Verdict |
|---|---|---|
| 1 | Pattern, kit, song, global dump round trips | **GO.** Requests work on any screen. Dumps are 7-bit packed and run-length coded; the firmware's run-length choice is reproduced exactly on all 288 factory dumps |
| 2 | The SYSEX RECV requirement | **Confirmed.** On a normal screen a dump is ignored. On GLOBAL › FILE › SYSEX RECV it is stored |
| 3 | Driving SYSEX RECV automatically | **GO.** A panel macro reaches it in **620 ms**, verified from RAM. The machine can stay parked there: it keeps playing and takes more dumps, CCs and commands |
| 4 | Playhead and state from RAM | **GO.** The step at `0x257e57`, the firmware's screen at `0x266ec8`, the working kit at `0x700028`, the RECV message counters at `0x26a3c4` |
| 5 | CC live edits | **GO.** On every screen, SYSEX RECV included. They edit the working kit, which is in patch RAM |
| 6 | Machine assign / routing / load commands outside RECV | **Not gated.** 0x5B, 0x5C, 0x55, 0x56, 0x57, 0x58, 0x59, 0x6C act on a normal screen and on SYSEX RECV. 0x71 cannot set the focus track |

**Overall: GO for MM-P1.** The Monomachine's structural edits are not live dumps
on a normal screen, but the editor can make them live: it parks the emulated
machine on SYSEX RECV while edits flow and leaves when idle. On that screen a
pattern dump is stored and heard in the playing pass, like the MD.

---

## 1. Dumps and their encoding

| Dump | Command | Wire size (factory) | Raw payload | Format bytes |
|---|---|---|---|---|
| Pattern | 0x67 / request 0x68 | 208-2,150 B | 6,520 B | version 6, revision 1 |
| Kit | 0x52 / 0x53 | 700-731 B | 698 B | 2 / 1 |
| Song | 0x69 / 0x6A | ~1.7 KB | 4,816 B | 2 / 1 |
| Global | 0x50 / 0x51 | 98-101 B | 264 B | 3 / 1 |

- Header `F0 00 20 3C 03 00 <cmd> <version> <revision> <slot>`, trailer as on the MD: 14-bit sum of bytes 9..size-6, 14-bit length size-10, F7.
- Body: 7-bit packing (as the MD) of a **run-length stream**. A byte with bit 7 set is a count `0x80|n` followed by the value; any other byte is itself.
- **The firmware's encoder, reproduced on all 288 factory dumps, byte for byte:**
  - runs of 2 to 127 equal bytes become `0x80|n, v` (longer runs split at 127);
  - a single byte with bit 7 set becomes `0x81, v`;
  - any other single byte is sent as is;
  - when the run-length stream is a multiple of 7 bytes long, one more `0x00` follows: the firmware writes each 7-bit group's high-bit byte before the group, so the last, empty group still has one (83 of 288 factory dumps).
- **An uncompressed dump is dropped.** The same pattern with every byte literal is 13,138 bytes; SYSEX RECV neither stores it nor counts it. The editor must send the firmware's own form (MM-P1 codec).
- Reply times (request → complete reply, emulated): pattern 12-26 ms, kit 7 ms, song 20 ms, global 1.5 ms.
- Known offsets so far (raw payload): pattern length at `0x424` (factory: 16, 40, 48, 64); kit name `0x000` (11 bytes), levels `0x00b` (6), parameters `0x011` (6 × 72, pages of 8), machines `0x1c1` (6), routing `0x1c7` (6). MM-P1 derives the rest.

## 2. The SYSEX RECV gate

Measured with `gate` (pattern 0's content, edited, sent to other slots):

| Sent | Screen | Stored |
|---|---|---|
| Pattern dump, unpaced | normal | **no** (the slot is unchanged) |
| Firmware's own bytes, retargeted | SYSEX RECV (ORIG) | yes |
| Edited pattern, the firmware's run-length form, unpaced | SYSEX RECV | yes |
| Same, through the plug-in's paced transfer (`startMidiSysexTransfer`) | SYSEX RECV | yes, complete after 45 ms |
| Same, 32-byte fragments 10 ms apart | SYSEX RECV | yes |
| Uncompressed (every byte literal) | SYSEX RECV | no |

- The emulated UART is not paced (as on the MD): the unpaced dump is fine. Four pattern dumps sent back to back were all stored, the last one **36 ms** after the first byte.
- **The screen stays on SYSEX RECV after a dump.** It shows `RECV n MSG. e ERR.` and takes the next one. Kit, song and global dumps were all stored in one visit.
- The two receive-mode boundaries that `mmSysexWorkflowTest` sees for a kit dump are a property of the transfer layer's pacing, not of the firmware: sent directly, a kit dump is stored on the first visit.

### Kit dumps write the stored slot

- A kit dump to the current kit's slot (0) is stored, but the working kit is unchanged: SAVE KIT to a scratch slot reads back the old sound (0 bytes differ).
- LOAD KIT 0 then makes the dumped kit the working kit (all 698 bytes equal).
- Same as the MD (P1): structure edits of the playing kit need the live commands (0x5B, 0x5C, 0x55, CCs), or a kit dump plus LOAD KIT.

### Songs and globals

A song dump (song 0) and a global dump (global 1, not active) are stored on SYSEX RECV. Whether the playing song picks a song dump up is left for MM-P2 (the MD's does not).

## 3. Driving SYSEX RECV from the editor

**The path** is `sysexPanelDriver.h`'s `enterMmReceive`:
- FUNCTION + KIT (GLOBAL), ENTER;
- LEFT × 4 and UP × 8 to put the cursor at a known place;
- DOWN × 2, RIGHT (FILE);
- UP × 8, DOWN, ENTER (SYSEX RECV);
- RIGHT, ENTER (ORIG, start).

| Key hold / gap | Macro | On SYSEX RECV |
|---|---|---|
| 40 / 40 ms | 2,479 ms | yes (LCD 0 px from the reference) |
| 20 / 30 ms | 1,534 ms | yes |
| 10 / 20 ms | 914 ms | yes |
| **10 / 10 ms** | **620 ms** | **yes** |
| 5 / 10 ms | 480 ms | 3 px off |
| 4 / 4 ms | 266 ms | no (1,628 px off) |

- **Leaving takes 5 EXIT presses** (about 100 ms at 10/10). The test driver's 8 are safe.
- **Verified from RAM, not from pixels.** `0x266ec8` holds the firmware's current screen handler (a code address). It is steady per screen and needs no LCD template:

| Screen | `0x266ec8` |
|---|---|
| start-up animation | `0x002c27f8` (from 1.9 s to 9.2 s after power-on) |
| main screen (stopped or playing) | `0x002c2908` |
| TEMPO | `0x002c2d78` |
| GLOBAL menu | `0x002c2ee8` |
| SYSEX RECV (waiting or after messages) | `0x002c3a98` |

- **The RECV counters** are u32 big-endian at `0x26a3c4` (messages received) and `0x26a3c8` (errors). A bad checksum counts as an error. So the desk knows each dump arrived, before any read-back.
- `0x268033` is 1 while SYSEX RECV waits for its first message and 0 after it. Not needed once the screen word is used.

### Parked on SYSEX RECV (`recv`)

Pattern 1 (a 64-step factory pattern) playing at 120 BPM:

| On SYSEX RECV | Result |
|---|---|
| The sequencer | keeps playing: audio RMS 0.16 (0.22 before), the playhead keeps moving |
| CC 7 = 0 on all six channels | silent (RMS 0.00000), as on the normal screen |
| Dump requests | answered |
| A dump over the **playing** pattern (the empty factory pattern 75) | **heard in the same pass**: RMS 0.039 → 0.0004 over 2 s (release tails), no reload, no stop |
| The original sent back | plays again (RMS 0.23), still playing |
| EXIT | still playing |

So the desk can keep the machine parked while structural edits flow, and leave after an idle time or when the panel is used.

## 4. Live commands (`commands`)

Each command was checked by SAVE KIT to the current slot and a kit dump diff, on a normal screen and again on SYSEX RECV. **Same result on both screens.**

| Command | Acts | What changes in the kit |
|---|---|---|
| 0x5B assign machine (track, machine, init) | yes | init 1: the machine byte and all 56 parameters of the track; init 0: the machine byte and two SYN values |
| 0x5C set track routing (track, bus bits, input) | yes | one byte at `0x1c7 + track` |
| 0x55 kit name | yes | 11 bytes at `0x000` |
| CC 48 on channel 1 (SYN 1, track 1) | yes | `0x011` |
| CC 56 on channel 2 (AMP 1, track 2) | yes | `0x061` (`0x011 + 72 + 8`) |
| CC 7 on channel 3 (level, track 3) | yes | `0x00d` |
| 0x56 set active global, 0x57 LOAD PATTERN, 0x58 LOAD KIT, 0x6C LOAD SONG | yes | status reports the new slot |
| 0x61 tempo | yes (the pattern plays at it) | not in the global dump |
| 0x71 SET STATUS 0x22 (audio track) | **no** | status 0x22 still reports track 1; the manual lists 0x22 and 0x23 for requests only |

## 5. State from RAM

- **Playhead: `0x257e57`**, current step, 0-based, wraps at the pattern length. `0x2bc287` is a copy. `0x2bc28b` is the next step (it wraps one step earlier). `0x2bdf3d` counts steps without wrapping.
- **Queued pattern (`queue`).** LOAD PATTERN 10 sent at step 8 of pattern 1 (64 steps): the switch happens at the wrap, +6,978 ms. **Status reports the new pattern at +7,011 ms, with the wrap, not two steps early** as on the MD. Until then status reports the old pattern; the queue is invisible to status.
- **Working kit: patch RAM `0x700028`**, the kit dump's raw payload (698 bytes) in dump order. A CC changes it at once without SAVE KIT (CC 48 = 42: only `0x700039` changed, 99 → 42). The same image sits at the stored slot's place (slot 100 at `0x711644`) after SAVE KIT.

## 6. Start-up and readiness

- MIDI is ready at **1.05 s** of emulated time (`isFirmwareMidiReady`).
- The start-up animation runs from **1.9 s to 9.2 s**. The LCD changes about 120 times, then stays on the main screen.
- **Ready for input = the screen word leaves `0x002c27f8`.** The editor mirrors the firmware LCD while the screen word says start-up, then fades to its own fields (MM-P2).

## 7. What this means for the editor (MM-P1/P2)

1. **Codec:** raw payloads in firmware units, the run-length encoder above, and the layouts derived by diffing (MM-P1).
2. **Delivery**, by document:

| Edit | Path |
|---|---|
| Kit parameters and level | CC (live, working kit) |
| Machine, routing, kit name | 0x5B, 0x5C, 0x55 (live) |
| Pattern (trigs, notes, locks, length, arp …) | dump **on SYSEX RECV** (the desk parks the machine there; live in the playing pass) |
| Song, global | dump on SYSEX RECV |
| Kit structure without a live command | kit dump on SYSEX RECV + LOAD KIT |

3. **The RECV session is a small state machine in the desk:** EXIT until the screen word says main screen, the macro, verify `0x002c3a98`, send, check the message counter, and leave with 5 EXITs after an idle time. Keys are timed in machine time (10 ms hold, 10 ms gap), as P3 does for the MD.
4. **Readiness and the boot LCD** from the screen word.

## 8. Files and how to re-run

- `mdLibTest/mmEditorProbeFirmwareTest.cpp` (new): `all` runs the checks above (about 3 min; 53 checks, PASS):

  ```
  temp/cmake_mm/source/elektron/md/mdLibTest/mmEditorProbeFirmwareTest <ROM> all
  temp/cmake_mm/source/elektron/md/mdLibTest/mmEditorProbeFirmwareTest <ROM> corpus <dir>   # every slot
  ```
- `mdLibTest/mdFirmwareSession.h`: `Machine` takes the model (default MD, unchanged for the MD harnesses).
- No user files were touched: the harness is headless and writes only where it is told.
