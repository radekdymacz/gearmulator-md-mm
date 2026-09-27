# P3 result: the Machinedrum Editor stops being partly mock (MD OS 1.63)

- Branch `p3/md-editor-real` (from `p2/md-desk-ui`), 2026-09-27, Apple silicon, build dir `temp/cmake_p0` (P0 configure, now with AU on).
- Every control on the page does the real thing on the firmware, or is disabled and says why.
- Monomachine is still out of scope.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | Skin in step with the final mockup | **GO.** Two transport keys (PLAY toggles to STOP), square LCD keys, the lower LCD, the engine menu, no footer. Also the rounds that came in during P3: v43-v49 |
| 2 | Working-kit truth | **GO.** The kit that plays is read from the machine's memory. Panel edits and DAW-restored edits show without SAVE KIT |
| 3 | REC live recording | **GO.** As on the machine: hold RECORD, press PLAY. Trigs and knob locks are recorded by the firmware |
| 4 | Sampler | **PARTLY.** Rename is real (0x73). Names, memory in use, Send (SDS) and Copy RAM to ROM are not possible from outside the machine: disabled, with the reason |
| 5 | App LFO and random sources | **GO.** They run in the desk on the machine's steps, as CCs, within 300 CCs a second |
| 6 | Chaining, mute overlay, song selection | **Song selection: GO.** Chaining and the mute overlay: not done (P4) |

**Overall: GO.** Firmware smoke test `mdDeskFirmwareTest`: 51 checks, PASS, 33 s. Unit tests: 46 of 48, the same 2 known `synthLib` failures.

---

## 1. Skin in step with the mockup

- `doc/modern-ux/sync-mdstudio-skin.py` now does the whole markup and stylesheet:
  - drops mockup-only elements (first-run link, rules footer) if a round brings them back;
  - marks what the machine cannot do (HW MIDI) as disabled;
  - adds the real machine's states at the end of the stylesheet;
  - **checks the contract:** every id the page's scripts look up (`$("#x")`, `getElementById`, `closest("#x")`) must be in the markup or made by the scripts. A design round that renames one fails here. `--check` reports drift only.
- Behaviour ported to the page modules: PLAY/STOP key, engine menu (LOAD ROM opens the first-run screen, with Close while the firmware runs), PAGE control above the grid (v43, v44), gliding playhead column (v45), fixed-width LCD fields (v46), page LEDs not resized while playing (v47), engine states (v48), queued pattern shown as its own blinking name with one flash on the switch (v49).
- **Found in the plug-in, fixed:**
  - The mockup's square keys (aspect-ratio on a stretched height) collapsed to slivers in the plug-in's WebKit. The skin now sets an explicit square tied to the LCD height. Measured in the plug-in: REC and PLAY 42 × 42, side by side (window 1466 px, LCD 70 px).
  - The LCD stays 600.1 px wide through tempo 30 / 299.5 and through a queued pattern.
- **Fonts:** Barlow Condensed, IBM Plex Mono and Silkscreen (SIL OFL 1.1, licences next to them) are bundled in `skins/mdStudio/fonts/`. The coordinator supplied them from google/fonts; I downloaded nothing. The editor inlines ttf and woff2. In the plug-in all five faces load; the LCD is Silkscreen, the keys Barlow Condensed, the grid Plex Mono.
- **Engine label** from the device, no timers: NO ROM, LOADING ROM (project restore), BOOTING OS (firmware not taking MIDI), EMU OS 1.63 (first status reply), ROM ERROR. Until ready the LCD dims and REC/PLAY are disabled. Measured: BOOTING OS at 15 ms, EMU OS 1.63 at 1796 ms after the page loaded.

## 2. Working-kit truth

Found with `mdEditorProbeFirmwareTest workkit`:
- The working kit sits in **patch RAM at 0x70000a**, as the kit dump's raw fields in dump order (name, 24 × 16 parameters, levels, models, LFO blocks, master effects, groups; 1120 bytes).
  - After a CC, a machine assignment, LFO, master FX, groups and a name edit, 1120 of 1120 bytes equal a SAVE KIT read-back.
  - **0x700008 is the current kit number.**
  - The image does not change while the pattern plays (0 of 100 reads over 2 s).
- `md::Device` republishes the region lock-free (seqlock) about 90 times a second, only when it changed.
- `mdDesk::Desk::onWorkingKitMemory` makes it the current kit's document:
  - `kit.working` is `edited` exactly when it differs from the stored slot (LFO running state aside);
  - it holds an image for 150 ms after its own live edit (the CC may not have landed);
  - when memory names another kit than status, it asks for status first;
  - `machine.desk.kitSource` = `memory`.
- Measured (firmware test): a panel encoder edit the desk never sent reaches the page's document within one editor tick. A DAW project restored into a new machine shows the unsaved edit, marked edited, without SAVE KIT. In the plug-in: `kit source memory`.

## 3. REC live recording

Measured on firmware (`mdEditorProbeFirmwareTest liverec`, `livelocks`, `recknobs`, `playing`):
- Live recording is hold RECORD, press PLAY. RECORD must be held under 150 ms first (20-80 ms: 5 of 5; 150 ms: 0 of 5).
- The firmware records **TRIG keys and MIDI notes** as trigs, quantised.
- It records **DATA ENTRY knob turns of the selected track** as a lock on that track's next note. **CCs are not recorded.** One knob step is one value step, no acceleration.
- A lock lands when the note comes at least a step after the turn; a note straight after the turn was sometimes missed.
- No clean RAM flag for playing or live recording. `md::SequencerState` reads them like the panel:
  - playing = the stopped byte 0x28cdaf clear **and** the playhead moved in the last 0.7 s;
  - live recording = the RECORD LED (0x27f977 bit 4) blinks while playing; grid edit = steady.
  - **Correction to P2:** 0x28cdaf alone reads "playing" after STOP pressed twice.

In the editor:
- REC starts live recording. From PLAY the firmware does not enter it, so the desk stops first, then starts it. REC again leaves recording and keeps playing (the manual's PLAY).
- While recording, a click on a track's steps plays the track (its TRIG key). A value moved in the page becomes SET STATUS track, the page key and knob turns (`mdDesk::KnobRecorder`), so the firmware locks it.
- Grid edits of the recording pattern are refused (a dump would overwrite the take). The pattern is read back every 400 ms.

| Measured | Firmware test | Plug-in |
|---|---|---|
| REC -> recording | 163 ms | 104-174 ms |
| Knob move -> value in the machine (select, page, turn) | 237 ms | |
| Page click -> recorded trig | step 5 | read back while recording |
| Knob move, note a step later -> lock | FLTF 99 on the note | |

- **Keys in the plug-in are now timed in machine time.** The audio thread sends a key's row states 40 ms apart (`md::Device::sendPanelSequence`). The desk keeps loads and status polls off the line for 500 ms around a key.
- **Found:** the firmware's start-up animation, about 20 s after it answers MIDI, eats the first key press. The self-test waits it out; the desk cannot see it yet.

## 4. Sampler

Measured (`mdEditorProbeFirmwareTest samples`):
- **0x73 sets a name.** The firmware keeps it (its last-name buffer at RAM 0x29f300) but has **no request for names**. Rename is real; names are not shown ("sent: KIK" for names sent this session, marked as such).
- **SDS dump requests get no reply** (3 slots, after a paced SDS import that wrote flash at 0x4e0000). Send is disabled with that reason.
- Memory in use and sample audio: not reported over MIDI, not found in memory. The LCD field says n/a; waveforms stay labelled as examples.
- Copy RAM to ROM exists only in the machine's SAMPLE MGR menu. Disabled.
- Capture next loop is now real: the recorder track is muted until the wrap, records one loop, and is muted again (freeze).

## 5. App-only LFO and random sources

- `md-desk/modulators` (doc and schema): sources (LFO: shape, rate, depth; random: rate, smooth), links (track, parameter, min, max, curve, invert). Validated with paths.
- The desk moves every source once per step of the machine's playhead while it plays, and sends changed link values as CCs within **300 CCs a second** (the mockup's limit). Over budget, a value waits for a later step.
- The page: APP rows in the matrix with live values, an inspector, targets, add/remove, the CC rate.
- Measured: an app LFO moves track 2 DIST on the machine within 10..110.
- Control always shows 8 knob rows (CC 21-28, editable), learned CCs and the app rows. Unmapped rows show – and do nothing. A cell opens its row with that track's target picker, so mapping works without LEARN.

## 6. Song selection

The Song workspace's SONG field steps the song slot (LOAD SONG). The machine ignores that while it plays (P1), so the desk refuses it then and says so. Firmware test: the machine reports song 3.

## 7. Still missing

- Pattern chaining and the mute overlay (gap review §2).
- HW MIDI engine (disabled).
- Sample names, memory, SDS send, RAM to ROM (not reachable over MIDI).
- The app modulator setup is not saved with the project.
- The start-up animation eats the first key press within about 20 s of boot.
- LOADING ROM is shown during a project restore; it was not observed in the self-test.

## 8. Tests and how to re-run

- Unit (ctest, UnitTest label): `mdDeskTest` (working kit from memory, knob recorder, REC ops, sample names, modulators, song select), `mdSequencerStateTest` (playing/recording from RAM, key sequences), and the P1/P2 tests. 46 of 48.
- Firmware smoke test (needs the ROM):

  ```
  temp/cmake_p0/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>"            # 51 checks
  temp/cmake_p0/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>" playload   # PLAY while loading
  temp/cmake_p0/source/elektron/md/mdLibTest/mdEditorProbeFirmwareTest "<ROM>" [workkit|liverec|livelocks|recknobs|playing|samples]
  ```
- In the plug-in: skin mdStudio, `GEARMULATOR_MDSTUDIO_SELFTEST=1`; the log is `~/Library/Caches/Gearmulator MD/gearmulator-mdStudio.log` (P3 lines start with `P3:` and `engine:`).
- Browser harness (development only, not in the repository): the skin with a fake host, used for the page checks above.
- **User files.** For the self-tests I switched `~/Documents/Gearmulator Preview/Machinedrum/config/Gearmulator MD.xml` to mdStudio at scale 75. The standalone rewrote `~/Library/Application Support/Gearmulator MD.settings` on exit. Both were restored from a backup byte for byte: SHA-1 `cbbc7cda…d771b` and `fc68a82e…fa60`, the P2 values.

## 9. Installed

- VST3: `~/Library/Audio/Plug-Ins/VST3/Gearmulator MD.vst3`. AU: `~/Library/Audio/Plug-Ins/Components/Gearmulator MD.component` (`auval -v aumu Tmdr GmPv`: AU VALIDATION SUCCEEDED).
- Standalone: `bin/plugins/Release/Standalone/Gearmulator MD.app`, where the build puts it.
- No earlier copies were installed, so there was nothing to back up.

## 10. Recommended P4

1. **Pattern chaining and the mute overlay.** Find the chain in RAM, as for the working kit.
2. **Save the app modulators and knob rows in the plug-in state,** next to the MIDI Learn preset.
3. **Start-up animation:** find a RAM or LCD signal for "boot animation over" and keep BOOTING OS until then.
4. **A panel macro for SAMPLE MGR** (RAM to ROM, Send), verified against the LCD, if Radek wants it.
5. **HW MIDI:** the same Desk over a real MIDI port, with DIN pacing (P2 §8.5).
6. **Monomachine** once its ROM is here.

## Files

- `elektronData/mdWorkingKit.*`, `mdCommands.*` (`mdSetSampleName`).
- `mdLib/mdsequencerstate.h`, `mddevice.*` (working-kit region, telemetry, `sendPanelSequence`), `mdpanel.*` (`panelKeySequence`).
- `mdDesk/mdDeskRecord.*`, `mdDeskMod.*`, `mdDesk.*`, `mdDeskTest.cpp`.
- `mdJucePlugin/mdStudioLink.*`, `mdStudioEditor.*`; `skins/mdStudio/` (`mdDeskMod.js`, `fonts/`).
- `mdLibTest/mdEditorProbeFirmwareTest.cpp`, `mdDeskFirmwareTest.cpp`, `mdSequencerStateTest.cpp`.
- `doc/modern-ux/sync-mdstudio-skin.py`, `data-contract.md`, `md-data-contract.schema.json`.
