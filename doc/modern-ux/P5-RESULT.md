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

## 10. Follow-up (2026-09-28): one UI, blank window, false NO ROM, MM mockup

- **The editor page is the only UI, for both apps.**
  - The panel skins are removed: `skins/mdDefault` and `skins/mmSfx60`, and the panel editor's pointer tests with them.
  - Each product offers one skin: "Machinedrum Editor" or "Monomachine Editor". `createEditor` always makes the page.
  - A config that names an old skin (Radek's names mdDefault / mmSfx60) falls back to the page silently. Checked with both of Radek's real configs, which are unchanged.
  - Editor › Skins is hidden when there is one skin.
  - The panel C++ (`mdEditor`, `mdPixelPerfectPanel`, the LCD interaction model) is still compiled but never made. The page's boot LCD does not use it: it reads `md::FrontPanel` directly. Deleting that code is a clean-up left for later.
- **The blank window** came only from a live switch from the panel editor to the page. Its page never loaded (no "page ready", no render); re-making the page itself works.
  - With the panel gone, that path is gone.
  - The page now logs its first render ("first render: … elements"), so a blank page fails the self-test.
  - Measured, MD: a fresh launch with the old-skin config rendered 340 elements, 1517 × 945; the page re-made twice rendered each time (`GEARMULATOR_MDSTUDIO_SELFTEST=p5skin`).
  - Measured, MM: a fresh launch rendered 164 elements.
- **False "firmware needed"** (MM page; the shared logic now covers both apps):
  - The MM page started with engine "missing" and never closed the dialog.
  - Now:
    - no device yet is LOADING, not NO ROM (both links);
    - the MM page starts as loading;
    - NO ROM shows only after the plug-in has said "missing" for 1.5 s;
    - the dialog closes by itself when the engine is anything else;
    - Check again re-queries.
  - MM check: engine booting, then ready; no NO ROM dialog; 288 documents loaded.
- **The MM mockup's latest rounds are copied into `doc/modern-ux/mm-mockup/` and synced into the MM skin:**
  - lock lane of 190 px in MM colours;
  - rail lock keys at MD size;
  - full-height workspaces;
  - high-DPI pitch roll fix;
  - rail LOCK PARAMETER block: 2 rows of chips, a 2 × 4 key grid.
- **Installed:** MD and MM VST3/AU. Both pass auval.
  - The previous copies are backed up at `~/Library/Audio/Gearmulator P5b backup 2026-09-28/`.
  - Standalones: `bin/plugins/Release/Standalone/Gearmulator MD.app` and `Gearmulator MM.app` in this worktree. They are not copied to /Applications (not approved yet).

## 11. Follow-up 2: standalone chrome, AUDIO / MIDI panel, MM playhead

- **MM transport keys:** they are explicit squares taken from `--lcdh`, as in the MD. The page logs their rects and a FAIL if they are not square and side by side. The MKII print now sits inside the header.
- **MM playhead:**
  - In the plug-in, the RAM running flag (0x26b46e) stayed 0 while the sequencer played. The page therefore never saw `playing`.
  - `mmDesk` now treats the machine as playing when the flag is set or the step byte advances (two forward steps within three step times). A unit test covers this.
  - The Sequence has the soft `#phcol` column over the roll, the ENV/SLIDE/SWING rows and the lock lane. The column is ink-tinted, glides between steps, jumps on a wrap, page flip or scroll, and fades on stop. It is in the mockup and the skin.
  - The self-test checks that the column moves and spans the roll to the lane, that POSITION follows, and that the column fades on stop. MM self-test: 9/9.
- **Icons:** a POST_BUILD step touches every bundle, and the apps are re-registered with `lsregister -f`. All six bundles have `CFBundleIconFile` = `Icon.icns`, and the file is present (MD d69adf10, MM 9635abd3).
- **No yellow feedback bar** (`standaloneApp.h`, generic):
  - The holder's mute moves to a fresh `Value` with the same state, so the bar never shows.
  - The input still starts muted, and the panel shows the mute.
  - Audio/MIDI Settings… (the app menu and Audio menu) opens the editor's own panel through `Editor::openAudioMidiSettings()`. JUCE's dialog is kept for editors without one.
- **AUDIO / MIDI panel:**
  - What it covers:
    - the output device with a TEST key;
    - the input device, a level meter and a MUTED/LIVE toggle ("Input muted (prevents feedback)");
    - the active output channels;
    - the sample rate and buffer;
    - the MIDI inputs as LED toggles;
    - the MIDI output;
    - Bluetooth MIDI.
  - It opens from the engine menu (AUDIO/MIDI…), the menu bar, or `,`.
  - One script block and one stylesheet block serve the MD mockup, the MM mockup and the MD skin (`mdDeskAudio.js`). `audio_panel_check.py` makes both sync scripts stop on drift.
  - The data is the `gm-audio/devices` document (data-contract.md 4.8, schema `audioDevices`) from `mdAudioMidiLink.cpp`. JUCE's AudioDeviceManager stays the engine.
  - In a plug-in the document says `standalone:false`: no menu entry, and the panel only says that the host owns audio and MIDI.
  - `GEARMULATOR_MDSTUDIO_SELFTEST=p6audio` and `GEARMULATOR_MMSTUDIO_SELFTEST=p6audio` each run a check that passes 7/7 in both standalones:
    - the buffer goes 512 → 128 → 512 and the output goes Speakers → Teams Audio → Speakers;
    - the machine keeps playing after each change;
    - the mute toggles and is kept.
- **Installed:** MD and MM VST3/AU. Both pass auval. The previous VST3s are backed up at `~/Library/Audio/Gearmulator P6a backup 2026-09-28/`; the AUs were not installed at that moment (the P5b backup holds the previous ones).
