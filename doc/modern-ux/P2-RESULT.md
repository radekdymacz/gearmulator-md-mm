# P2 result: the Machinedrum Editor on the real firmware (MD OS 1.63)

- Branch `p2/md-desk-ui` (from `p1/md-data-layer`), 2026-09-27, Apple silicon, build dir `temp/cmake_p0` (the P0 configure, Xcode 26.2 SDK and clang).
- The product name followed the mockup during the round: "MD Desk" is now **Machinedrum Editor** on screen. Code and folder names keep `mdDesk` / `mdStudio`.
- Monomachine is still out of scope.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | The mdStudio skin serves the mockup's UI, fed by firmware documents | **GO.** Mockup markup and CSS as they are. The mockup's renderers now read contract documents. No network at runtime |
| 2 | Commands up, read-backs down, validated | **GO.** 40 commands. Every changed document passes `validate` before anything is sent |
| 3 | Playhead, TX LED, error states | **GO.** Telemetry comes from audio-thread atomics. TX follows the real push state. Busy, push-failed and validation errors are shown |
| 4 | Workspaces | **Sequence, Sound, Mix, Song: real.** Sampler: real kit and pattern data, example waveforms. Control: real MIDI Learn; the app LFO and random rows are mock (§5) |
| 5 | Tests | **GO.** `mdDeskTest` (pure) and the firmware smoke test `mdDeskFirmwareTest` (manual, 31 checks) both pass. In-plug-in self-test logs below. `ctest -L UnitTest`: 45 of 47, the same 2 known `synthLib` failures as P0 and P1 |
| 6 | This report | Below. The P3 recommendation is in §8 |

**Overall: GO.** In the standalone, one click on the grid:
- shows in the page in **4-10 ms**;
- is confirmed by the firmware's read-back in **49-87 ms** (four standalone runs).

A kit value moved in Sound goes out as a CC in about 20 ms. After SAVE KIT it reads back from the firmware.

---

## 1. Architecture: four layers, each with one job

| Layer | Code | Speaks |
|---|---|---|
| Codec + contract | `elektronData` (P1) plus new pure builders | bytes <-> values <-> JSON |
| Editing model | **`mdDesk`** (new, pure, no JUCE) | commands -> document edits, undo, clipboard, live-edit delivery, the `Desk` orchestrator |
| Device edge | `mdJucePlugin/mdStudioLink.*` | SysEx in and out, `pluginLib::Parameter` (CC), panel keys, telemetry |
| Page | `skins/mdStudio/*` | contract JSON in, small JSON commands out |

`mdDesk::Desk` talks to its host only through a `Port`:
- `sendSysex`, `sendKitParam`, `sendMute`, `pressKey`, `toPage`, `nowMs`.
- The plug-in implements the Port with StudioLink.
- The firmware smoke test implements the same Port against a headless emulated MD.

So the smoke test runs the same Desk code as the plug-in.

**How each change reaches the machine** (`mdDeskDelivery.h`):

| Document | Delivery | Why |
|---|---|---|
| Pattern | validated dump, then read-back (`PushSlot`) | Dumps are live (P0) |
| Song | validated dump, then read-back, then `reloadNeeded` | The playing song ignores dumps (P1) |
| Working kit: parameters 0-23 and level | **CC through the plug-in's parameter layer**, like host automation | A kit dump writes the stored slot only |
| Working kit: machine, LFO, groups, master FX, name | the manual's live SysEx: `0x5b`, `0x62`, `0x65`/`0x66`, `0x5d`-`0x60`, `0x55` | Edits the working kit, not the slot |
| Global: routing, tempo, mode | `0x5c`, `0x61`, SET STATUS `0x20`, then a global dump read-back | |

**Push rule: latest wins, one in flight.** While a pattern dump waits for its read-back, newer edits only replace the value that goes next. A lock-lane drag therefore costs one dump per read-back (about 50 ms), not one per mouse event.

**Undo.** One command is one undo step. A drag carries a gesture id and merges into one step.

**Clipboard.** It holds values in C++:
- a track page (trigs, accent, slide and locks);
- a sound (machine, 24 values, level and LFO);
- a song row.

Copy, clear and paste call the same pure edits as everything else.

## 2. What works for real

- **Loading.**
  - Order: status, the current pattern, its kit and the global.
  - Then all 128 patterns and 32 songs load in the background, one request per 30 ms. Nothing loads while an edit is on the wire.
  - In the standalone everything was loaded about 10 s after boot.
- **Sequence.**
  - Grid: 16 × up to 64 steps. Trig, accent (shift) and slide (alt).
  - Accent and slide edit the pattern-wide set when the pattern has EDIT ALL set.
  - Paging, ALL and FOLLOW.
  - Steps past `length` are dimmed.
  - The lock lane draws and erases locks.
  - The 3 × 8 lock-parameter keys use the machine's own names.
  - Clear lane.
  - The 64-lock budget is checked in the page first, then refused in C++.
  - LCD line 2: LEN steps the total length (alt-click steps the length inside it), SPD, SWG, ACC and MODE (a global edit).
- **Sound.**
  - All three pages with the machine's parameter names.
  - The mockup's screens are real controls: amp and pitch, filter and EQ, dist and pan/vol.
  - The machine picker takes 134 OS 1.63 machines from a catalogue that C++ builds (`mdMachineParamNames`, manual names: `P-I-*`, `GND-SIN`).
  - LFO: target, parameter, both shapes, update mode, and speed/depth/mix (routing 5-7 as CC).
  - Mute and trig groups.
  - Copy, clear and paste of a sound.
- **Mix.** These are all live:
  - VOL faders, PAN, DIST, DEL and REV;
  - OUT A-F/MAIN (`0x5c`);
  - the four master effects, knobs and screens;
  - mute: the plug-in's Mute parameter.
  - Solo is the page's own idea: it mutes the other tracks and restores your mutes afterwards.
- **Song.**
  - The current song's rows, with the P1 row semantics:
    - `repeats` = rep - 1;
    - `end` is exclusive;
    - loop repeats 0 = infinite, so a finite loop plays at least twice.
  - Insert, delete, move, duplicate, loop, jump, halt, part, tempo and mutes. Drag and drop from the palette.
  - Loop and jump targets follow their rows when rows move (pure logic, tested).
  - The "Edits heard after STOP + reload" chip plus a Reload key: stop, LOAD SONG, play.
- **Header and LCD.**
  - Transport keys inside the LCD. PLAY and STOP are panel key presses.
  - POSITION shows bar.step from the machine's playhead.
  - Tempo (`0x61`), pattern with ‹ › and a queued display `A01›A06`, kit with its saved/edited state.
  - The kit dialog: Save kit / Reload kit.
  - The lock meter, and the TX LED with the last round trip in its tooltip.
  - Undo and redo keys with step counts. COPY, CLR and PASTE.
- **Kit per pattern.** Selecting a pattern that is linked to another kit while the kit is edited opens the mockup's dialog:
  - Save kit, then switch;
  - Switch and lose edits;
  - Cancel.
- **Queued pattern.**
  - While playing, the LCD shows `now›next` until the playhead wraps into the new pattern.
  - Status and RAM both switch about one step early (measured, §4). The desk waits for the wrap.
- **Unsaved state is split as the contract asks.** "Kit edited" means not saved on the machine. The DAW-project dirty state is not mixed into it.
- **First-run screen.**
  - It opens when no valid MD OS 1.63 device runs.
  - It shows the ROM folder, a "Show the ROM folder" key and "Check again".
  - An unsupported firmware gets its own status line.
- **Control.**
  - The matrix and targets list are the plug-in's **MIDI Learn preset** (`pluginLib::MidiLearnTranslator`).
  - LEARN, then click any value in Sound, Mix or the LFO: this calls `startLearning` for the right parameter and track.
  - The learned mapping is stored for that track and saved as the default preset.
  - Remove and invert work.
- **Plates.** MKI and MKII as in the mockup, remembered per viewer (`localStorage`).

## 3. The command set (page -> desk)

Every message is `{"op": ..., "id": n, "g": gesture?}` plus arguments. The reply is `{"type":"result","id","ok","errors":[...],"note"}`.

| Group | Ops (arguments) |
|---|---|
| Pattern (`p`) | `trig` (t, s, on?), `accent` / `slide` (t, s), `lock` (t, i 0-23, s, v or null), `clearLane` (t, i), `length`, `totalLength`, `speed`, `swing` (percent), `accentAmount` (0-15), `patternKit`, `clearSteps` / `copySteps` (t, from, to), `pasteSteps` (t, from) |
| Kit (`k`, the kit that plays) | `param` (t, i, v), `level`, `machine` (t, model, keepFx), `lfo` (t, field, v), `group` (t, kind, target or null), `masterFx` (fx, i, v), `kitName`, `copySound` / `pasteSound` / `clearSound` (t) |
| Song (`s`) | `rowSet` / `rowInsert` (i, row as a contract row), `rowDelete`, `rowMove` (from, to), `copyRow`, `pasteRow` |
| Global | `route` (t, out), `tempo` (bpm), `extended` (on) |
| Machine | `ready`, `load` (kind, slot), `select` (p, force?), `saveKit`, `reloadKit`, `play`, `stop`, `reloadSong`, `mute` (t, on), `undo`, `redo` |
| Plug-in | `learnStart` (t, i), `learnCancel`, `learnRemove` / `learnInvert` (index), `revealRomFolder`, `recheckFirmware` |

**Desk -> page messages:**
- `doc`, with `kind` and `pending`: a contract document.
- `machine`: `md-desk/machine` plus a `desk` block with firmware, tx, loading, roundTripMs, undo/redo counts, queued, playing and mutes.
- `telemetry`, `catalogue` (`md-desk/machines`), `learn`, `ask`, `error` and `result`.

**The bridge.**
- JUCE 7 has no native functions. The page sends each batch by navigating a throw-away iframe to `gmbridge://c/<json>`.
- An iframe navigation never cancels another one, as a main-frame `location` change would.
- Clicks go out at once. Drag values are keyed and batched every 16 ms.
- C++ answers with `evaluateJavaScript("gm.recv([...])")`.
- Timers replace animation frames for batching. WebKit stops animation frames while the plug-in window is covered; the first standalone run stalled on this until it was changed.

## 4. Measured

**In the standalone** (`GEARMULATOR_MDSTUDIO_SELFTEST=1`; the page clicks grid cells and turns the DIST knob itself). Log file: `~/Library/Caches/Gearmulator MD/gearmulator-mdStudio.log`.

```
page: selftest: trig round 1: ok click -> view 8.0 ms, -> confirmed read-back 87.0 ms (desk 80 ms)
page: selftest: trig round 2: ok click -> view 9.0 ms, -> confirmed read-back 59.0 ms (desk 57 ms)
page: selftest: trig round 3: ok click -> view 6.0 ms, -> confirmed read-back 67.0 ms (desk 65 ms)
page: selftest: trig round 4: ok click -> view 8.0 ms, -> confirmed read-back 82.0 ms (desk 81 ms)
page: selftest: trig round 5: ok click -> view 6.0 ms, -> confirmed read-back 57.0 ms (desk 55 ms)
page: selftest: lock: ok confirmed 121.0 ms
page: selftest: page shows 26 lit trigs on this page, LCD "B02 K15 RESAMPLING LEN32SPD1XSWG50%ACC3MODEEXT", lock meter 03/64
page: selftest: Sound: DIST control shows 13
page: selftest: kit DIST 0 -> 13: ok sent as CC, working copy + "edited" after 19.0 ms
page: selftest: kit read back after SAVE KIT: ok DIST = 13 (35.0 ms)
page: selftest: selftest done; kit {"current":14,"working":"clean"}, undo steps 10
page command -> document out 0.1 ms          (C++: command in -> optimistic document out)
```

- The page times are taken when the document message arrives, not by polling.
- The two self-test lock commands (set, then erase) arrived back to back, so the lock was confirmed after two read-backs: 121 ms.
- The self-test runs while background loading is still going (54 of 128 patterns).
- The window was covered during the run (`visibility hidden`). Without the timer fix above, those numbers were 2 s.

**Headless firmware, emulated time** (`mdDeskFirmwareTest <ROM> probe`, 31 checks, about 17 s wall):

| Action through the Desk | Result |
|---|---|
| Trig command -> confirmed read-back | 43.5 ms |
| Song row insert -> read-back | 7.3 ms |
| Select while playing -> status reports the switch | +3254 ms |
| Same select -> RAM `0x28d205` switches | +3246 ms |
| Same select -> the page clears the queue (playhead wrap) | +3386 ms |

Checked on firmware:
- trig; lock; CC DIST after SAVE KIT;
- machine assignment, where all 24 values of the track match the desk's kit;
- LFO shape and target; rhythm echo and gate box parameters; mute and trig groups; group removal; kit name;
- undo of group edits; routing and tempo; the song row and `reloadNeeded`;
- the queued display; the stopped flag.

**Found by the probe, and now used by the desk:**
- **Group removal is target `0x7f`.** The firmware then stores `0xff` ("none").
  - The track itself stores itself.
  - `0x10` and `0x40` are ignored.
- **RAM `0x28cdaf` reads 0 while playing and 1 when stopped.** It was consistent across two runs from different states. Other candidates varied.
- `0x28d205` is the current pattern, but it switches with the status reply, before the pattern is heard. The playhead wrap marks the audible switch.

## 5. What is still mock or missing

- **Sampler waveforms** are the mockup's models (marked "example"). Also mock:
  - ROM sample names (no `0x73` name request yet);
  - sample memory in use (`?/48`);
  - Send, Rename and Copy RAM to ROM (inert, "Not wired yet").

  Real: which tracks hold RAM-R/RAM-P/ROM machines, Put on track (machine assignment), Live/Freeze (the recorder track's mute), and the chop grid (trigs plus STRT/END/RTRG/RTIM locks).
- **REC** (live recording) is not wired; it only toggles its LED.
- **Control: the app LFO and random rows** are shown as MOCK and do nothing. The mockup's "drag a knob row to test" is dropped: the real rows are your controller's CCs.
- **Fonts.** Barlow Condensed, IBM Plex Mono and Silkscreen are SIL OFL 1.1, so they may ship with the app.
  - They were not on this Mac, and I did not download anything.
  - The stylesheet asks for `local()` first, then `skins/mdStudio/fonts/*.woff2`. The editor inlines those files if present.
  - Without them the page uses Avenir Next Condensed / Menlo. The layout holds; the LCD looks less pixel-like.
  - To finish: add the five `.woff2` files plus `OFL.txt` to `skins/mdStudio/fonts/`.
- **First-run drop zone.** The mockup's "drop the .bin here" became "Show the ROM folder / Check again". Reading an 8 MiB file through the URL bridge is not sensible, and the plug-in has no hot ROM reload. After adding the ROM, reopen the plug-in.
- **MID routing slots.** The positions of `CC5D CC5V PCHG` are inferred as routing 0-2, keeping LFOS/LFOD/LFOM at 5-7. Not verified.
- **Clear sound** sets neutral values: synthesis 64, standard effect and routing defaults. The machine's own defaults live in the firmware and cannot be read without SAVE KIT.
- **Machine change.** The new machine keeps the track's current synthesis values. Assignment resets them in the firmware; the desk then sends its eight values by CC, so the machine and the view agree.

## 6. Known issues

- **The working kit is only as good as the desk's view of it.**
  - The desk starts from the stored slot and adds every change it sees: its own, host automation and MIDI Learn through the parameter layer.
  - Edits made on the virtual panel's encoders reach the desk only if the Controller reports them as parameter changes.
  - Unsaved working-kit edits restored from a DAW project are not visible until SAVE KIT.
- **Undo restores whole documents.** If the firmware changed a pattern meanwhile (for example on the panel), undo overwrites that change.
- **Status polling** runs at 1 Hz, and 4 Hz while a pattern is queued. P1 measured about 1.5 ms of step jitter per burst.
- **Every read-back also goes to the host MIDI out,** as in P0. The background load sends 160 dumps once per editor open.
- **The window cannot be captured.** macOS refuses per-window capture, as in P0. Proof is the self-test log above plus the browser harness checks (§7).
- **The spec moved during the round.** The coordinator revised the mockup header six times. The skin follows the final file:
  - the logo and WORKSPACE on the left;
  - a taller LCD in the centre, with the TRANSPORT keys and a POSITION field inside it and COPY / CLR / PASTE at the end of line 2;
  - SETUP (UNDO, REDO, LEARN, MKI) on the right.
- **Keeping the skin in step with the mockup.** `doc/modern-ux/sync-mdstudio-skin.py` regenerates the skin's markup and stylesheet from the mockup. The behaviour in `mdDeskApp.js` is ported by hand, because the mockup's script is example state.
- **The mockup's "rules" footer** is kept as is.

## 7. Tests and how to re-run

- **Pure** (ctest, UnitTest label): `mdDeskTest`.
  - Covers: trig, lock rules, the 64-lock budget, lock-row freeing, settings validation, copy/clear/paste, kit edits and their live-edit bytes against the manual.
  - Also: song insert/delete/move with target remapping, END rules, the 256-row limit, and global edits.
  - Also: history (undo, redo, gesture merge), PushSlot, and the Desk against a scripted device.
    - Busy before ready.
    - Pending doc and TX.
    - One push in flight.
    - The kit working copy survives a stored-slot dump.
    - Undo through the CC path.
    - The discard-kit ask.
    - A push timeout is reported.
- **Unchanged and green:** `elektronDataTest`, `elektronDataCorpusTest` and `mdDataLinkTest`. The panel and editor tests `mdLcdEditorPointerTest`, `mdAudioIoLayoutTest` and `mdPanelRenderingTest` also pass.
- **Firmware smoke test** (manual, needs the ROM):

  ```
  temp/cmake_p0/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>" [probe]
  ```
- **In the plug-in:** set the standalone skin to mdStudio, then run it with `GEARMULATOR_MDSTUDIO_SELFTEST=1` and read the log.
- **Browser harness** (development only, not in the repository): the skin files served locally with a fake host, which feeds the corpus documents and logs every command.
  - All six workspaces rendered without script errors.
  - Checked commands:
    - grid click, accent, lane draw, clear lane, copy/paste, LEN/SPD/MODE and select;
    - machine picker, LFO shape and update, mute group, canvas handle and copy sound;
    - fader, OUT, mute and solo;
    - song row step, insert, delete and loop;
    - learn start and cancel;
    - ROM put on track;
    - the ask dialog and the first-run screen.
- **Standalone config.** For the self-test I switched `~/Documents/Gearmulator Preview/Machinedrum/config/Gearmulator MD.xml` to the mdStudio skin at scale 75.
  - That file was restored byte for byte afterwards (SHA-1 `cbbc7cda…b771b`).
  - `~/Library/Application Support/Gearmulator MD.settings` was backed up and is byte-identical (SHA-1 `fc68a82e…fa60`). The self-test's SAVE KIT only touched the running machine.
  - The standalone rewrites `Machinedrum/logs/standalone-startup-last.log` on every start, as before.

## 8. Recommended P3

1. **Fonts and polish.**
   - Add the OFL fonts.
   - Run `sync-mdstudio-skin.py` after every design round.
   - Move the mockup's remaining behaviour changes into the page's own modules so only markup and CSS differ.
2. **Working-kit truth.** Read the working kit without saving it. Candidates:
   - a patch-RAM or main-RAM location of the working kit (like the P0 playhead probe);
   - or accept SAVE KIT to a scratch slot plus a restore.

   Then the desk can show project-restored edits and panel encoder edits.
3. **Sampler for real.**
   - `0x73` sample names and memory use.
   - SDS send/receive (the repo has `sds*` harnesses).
   - A RAM capture waveform read from the DSP.
4. **Pattern chaining and the mute HUD** (gap review §2). Also song selection (the Song workspace edits the current song only).
5. **Hardware MIDI pacing.** On real DIN MIDI a pattern push is about 1.7 s (P1). PushSlot already coalesces. Add a visible per-document "sending" bar, and prefer lock edits as small commands if the OS offers one.
6. **Monomachine** once its ROM is here: a second catalogue, and a Desk on the MM contract.
7. **JUCE 8 native functions:** only if command traffic grows. The iframe bridge has shown no losses.

## Files

- `source/elektron/md/mdDesk/`: `mdDeskEdit.*` (commands -> documents), `mdDeskDelivery.*` (live edits), `mdDeskHistory.*` (undo, PushSlot), `mdDesk.*` (the Desk), `mdDeskTest.cpp`.
- `source/elektron/md/elektronData/`: `mdCommands.*` (live-edit builders), `mdPattern.*` (`withoutLock`, `withoutLockRow`, `visibleSteps`), `mdMachines.*` (parameter names, family).
- `source/elektron/md/mdLib/mddevice.*`: `SequencerTelemetry`.
- `source/elektron/md/mdJucePlugin/mdStudioLink.*` and `mdStudioEditor.*`; `skins/mdStudio/`: `mdStudio.html`, `mdDesk.css`, `mdDeskBridge.js`, `mdDeskModel.js`, `mdDeskApp.js`, `mdStudio.rml` (1440 × 924) and `fonts/`.
- `source/elektron/md/mdLibTest/mdDeskFirmwareTest.cpp`.
