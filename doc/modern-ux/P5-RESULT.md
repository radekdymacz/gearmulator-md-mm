# P5 result: the Machinedrum Editor, final gaps closed (MD OS 1.63)

- Branch `p5/md-editor-final` (from `p4/md-editor-complete`), 2026-09-27.
- Worktree `gearmulator-md-mm-wt/p4-md`, build dir `temp/cmake_p4`: Release, arm64.
- Mac: Mac16,8, Apple M4 Pro.
- Mockup: v61, in the main checkout (GLOBAL panel, the ? key list, SYN·/RTG· lane labels), synced into the skin.

## Verdict

| # | Item | Verdict |
|---|---|---|
| 1 | Lock-lane parameter ambiguity | **GO.** Every page key maps to one (page, index). The labels read SYN·DIST vs RTG·DIST |
| 2 | GLOBAL settings | **GO.** The GLOBAL menu's settings were measured on the firmware, then made editable: a panel opened from the engine menu or G, and the one-row header is kept (1254 px at 1280). Local control and trig in A/B are marked "not verified" |
| 3 | Keyboard-shortcut overlay | **GO.** The ? key shows a list generated from the page's key map, which now dispatches the keys: 43 entries in 9 groups |
| 4 | App LFOs with the editor closed | **GO.** They run in the processor, on the message thread, and never touch audio. Verified headless with no editor |
| 5 | The first PLAY after boot | **GO, it was a measurement artifact.** The real time is 203-235 ms; the 1 s came from WebKit throttling the self-test's timers in a covered window |
| – | Shared with the MM | **Merged `mm/editor`.** The MD's behaviour is unchanged, and the MM's tests pass in this build |
| – | Build, install, auval | **GO.** The P4 copies are backed up; AU VALIDATION SUCCEEDED |

- Unit tests: 51 of 53. The 2 failures are the known `synthLib` ones; the MM tests are included.
- Firmware smoke test `mdDeskFirmwareTest`: 51 checks; `p4`: 40; `hw`: 10. All PASS.
- `mdModRunnerFirmwareTest`: PASS, 3 runs out of 3.

## 1. Lock lanes by (page, index)

- Seven OS 1.63 machines have a synthesis parameter with an effects or routing name:
  - TRX-XT, TRX-RS, TRX-XC and TRX-B2 (DIST);
  - TRX-MA (REV);
  - INP-GA and INP-GB (VOL).
- The page keyed locks, lanes and values by name, so both parameters shared one lane.
- Fix: `slots()` (in `mdDeskModel.js`) names the synthesis one SYN·NAME.
  - Every page key is now one kit index.
  - `laneLabel` shows the other one as RTG·NAME or FX·NAME, in the lane chips and the lane title.
  - The same change is in the mockup (`pages()`, `laneLabel`).
- Checked: with TRX-XT, locks on parameters 5 and 16 are two lanes, SYN·DIST and RTG·DIST.

## 2. GLOBAL

**Measured** (`mdP4ProbeFirmwareTest globals`). A global dump is stored at once but **applied only after SysEx 0x56** (set active global). The desk sends 0x56 after every settings push.

| Setting | Raw | Evidence |
|---|---|---|
| TEMPO IN external | `syncFlags` 0x01 | MIDI Start alone does not play; with MIDI clock it does |
| CTRL IN off | 0x10 | MIDI Start is ignored, with or without clock |
| TEMPO OUT | 0x20 | 29 clocks in 0.5 s while playing |
| CTRL OUT | 0x40 | Start and Stop go out |
| 0x02 / 0x04 / 0x08 | | no effect found |
| PRG CHANGE IN / OUT | `programChange` 0x01 / 0x02 | PC 7 selects A08 / selecting A03 on the panel sends PC 2 |
| PRG CHANGE channel | bits 2-6 | 0 = BASE (in on channels 1-4); 1, 2, 3, 4, 8, 16 = that channel, measured |
| Base channel | `baseChannel` 0-12 | a CC on channel 1 vs 3 |
| Map editor TRIG | `trigMode` | 0 GATE: the pattern stops on note off (measured). 1 START, 2 QUE, as in the manual |
| Key map | 16-31 | patterns: note 65 → 17 selects A02 |
| LOCAL CTRL | `localControl` | stored, but **no change seen**: the TRIG key's audio was the same for 0/1/2/127 |
| TRIG IN A/B | `inputSettings` | shown only; needs pads on the inputs |

**Code**
- `elektronData::mdGlobalBits` and `mdSetActiveGlobal`.
- `md-desk/global` gets a derived, read-only `control` view (schema and contract). `settings` stays the value.
- `mdDesk` commands:
  - `globalSet {field, on|v}` (pure, validated). A track mapped to another key frees its old key.
  - `globalSlot {slot}`.

**Page** (mockup v61 first)
- The GLOBAL panel opens from the engine menu ("GLOBAL…") or with G. It is the kit library's big panel, so the header is unchanged.
- Contents:
  - the 8 slots;
  - Control: base channel, program change in/out/channel, local control;
  - Sync: tempo/ctrl in/out;
  - Trig in A/B;
  - Map editor: pattern trig mode and one note per track;
  - Routing: the 16 track outputs.
- Checks:
  - firmware test `p4`: TEMPO OUT on gives 25 clocks in 0.5 s, off gives 0; PRG CHANGE IN on and PC 9 selects A10;
  - plug-in `p5`: TEMPO OUT on reads back, then off again.

## 3. The ? key list

- `mdDeskKeys.js` is the page's key map.
- The main keys are dispatched from it; the old hand-written handler is gone.
- Keys handled next to their own code are described in the same map: the library, mutes, focused values and mouse modifiers.
- The overlay is generated from it: 43 keys in 9 groups, in the plug-in.
- The mockup carries the same overlay, describing its own keys.

## 4. App LFOs with the editor closed

- `mdDesk::ModEngine` (pure) takes the setup and the machine's playhead, and gives back the CCs to send within the 300-a-second budget.
- `mdJucePlugin::ModRunner` is owned by the processor.
  - It runs the same engine on the message thread every 8 ms.
  - The playhead comes from the audio thread's lock-free telemetry, and CCs go through the parameter layer like host automation.
  - It never runs on the audio thread, and nothing in the UI is involved.
  - It follows the project's `md-desk/setup` through a version counter, whichever thread set it.
  - It pauses in HW MIDI, because the emulator's playhead is not what plays then.
- The desk gets `Port::modulatorsElsewhere`: it only edits the setup and shows the processor's values (`"runs":"plug-in"`). The Control page says "runs with the editor closed".
- Without it (the firmware rig, HW MIDI) the desk runs its own engine, as before.
- `mdModRunnerFirmwareTest`: a processor with no editor, an LFO on track 2 DIST and MIDI Start. The machine's working-kit DIST took 5 values within 11..110 in 32 steps, 3 of 3 runs.
  - The test gives the processor a device home, because without one it re-prepares the factory flash and reboots the machine again and again. That is an existing quirk of the ephemeral config and is unchanged.

## 5. The first PLAY after boot

- P4's "up to 1 s" was the self-test: WebKit throttles page timers to 1 Hz when the window is covered (`visibilityState` hidden).
- Timed in the message handler instead, the page has "playing" **203-235 ms** after the click:
  - 40 ms key hold;
  - the playhead's first step (125 ms at 120 BPM), which `md::SequencerState` needs before it calls the machine playing;
  - the 30 Hz editor tick.
- The C++ log agrees: key → telemetry playing takes the same time.
- Nothing is lost at boot: 4 of 4 PLAYs right after ready played.

## 6. Merged from `mm/editor`

The MM branch had already merged P4. It brings the Monomachine Editor, and these parts are shared with the MD:
- the firmware session for both models;
- panel-sequence capacity of 128 row states;
- `md::MmTelemetry` beside the MD telemetry;
- the product skin policy.

The MD's tests are unchanged and pass. The MM's `mmDesk` and codec tests are in the 51 passing tests.

## 7. Still open

- **No real Machinedrum:** HW MIDI is verified against the emulator only (P4).
- **Local control** and **TRIG IN A/B** are stored and shown, but their effect is not verified.
- MAP EDITOR: START vs QUE were not told apart (both start when stopped). CTRL keymap values (32+), if any, are not shown.
- Real pointer drags and screenshots are still not possible on this host (P4 §8).
- `ctest` and `auval` rewrite `pluginPath_*` in the MD config, as before. It was restored after every run.

## 8. User files, install

- Restored after every run, byte for byte:

  ```
  cbbc7cda67ee8e9027d45775ebb1ead31abd771b  ~/Documents/Gearmulator Preview/Machinedrum/config/Gearmulator MD.xml
  fc68a82e43b52ef83ab1864c32349e71257afa60  ~/Library/Application Support/Gearmulator MD.settings
  ```

- VST3 and AU are installed in `~/Library/Audio/Plug-Ins`. `auval -v aumu Tmdr GmPv`: AU VALIDATION SUCCEEDED.
- **The P4 copies are backed up at `~/Library/Audio/Gearmulator MD P4 backup 2026-09-27/`.** The P3 backup is still there too.
- The standalone is at `bin/plugins/Release/Standalone/` in the worktree.

## 9. Re-run

```
ctest --test-dir temp/cmake_p4 -L UnitTest                                   # 51 of 53
temp/cmake_p4/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>" [p4|hw]
temp/cmake_p4/source/elektron/md/mdLibTest/mdP4ProbeFirmwareTest "<ROM>" globals
GEARMULATOR_MD_FIRMWARE_BIN="<ROM>" temp/cmake_p4/source/elektron/md/mdJucePlugin/mdModRunnerFirmwareTest
GEARMULATOR_MDSTUDIO_SELFTEST=p5   (the standalone: PLAY timing, GLOBAL, the ? list, where the modulators run)
```
