# P4 result: the Machinedrum Editor, complete on MD OS 1.63

- Branch `p4/md-editor-complete` (from `p3/md-editor-real`), 2026-09-27.
- Worktree `gearmulator-md-mm-wt/p4-md`, build dir `temp/cmake_p4`: Release, arm64, Xcode 26.2 SDK, Ninja.
- Mac: Mac16,8, Apple M4 Pro, 12 cores (8 P + 4 E), macOS 27.0.
- Design source: the main checkout's mockup, v50 to v60. The mockup is synced into the skin with `doc/modern-ux/sync-mdstudio-skin.py`.
- Monomachine is still out of scope. The MM icon files are committed for the MM branch.

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | Pattern chaining, mute overlay | **GO.** Chains are the firmware's own and are read back from its memory. Mutes are read from RAM and live on the track keys, with Shift for prepared mutes, as Radek asked. The separate MUTE window was dropped |
| 2 | Persistence | **GO.** App modulators and knob-row CCs are saved in the plug-in state (`MDSK`). Checked across a standalone restart |
| 3 | First key after boot | **GO.** Input is held until the start-up animation is over. BOOTING OS shows the firmware's own LCD, animation included, then fades |
| 4 | Knob lock timing | **GO.** The lock window was measured. The desk names the trig it will lock and the page marks it. The firmware locked exactly that trig |
| 5 | Kit library, pattern chooser | **GO.** 64 kits and 128 patterns read from the machine. Load, save, save as, copy, paste, clear, rename, drag-copy and switch now all run on the firmware. What it cannot do says so |
| 6 | HW MIDI | **GO, verified against the emulator only.** The editor drives a Machinedrum over the plug-in's MIDI in/out at DIN speed. Tested against the emulated MD as the MIDI peer, and in the plug-in with no MD attached. **No real Machinedrum was tested** |
| 7 | Remaining gaps | **PARTLY.** Done: tap tempo, parameter tweaking, page zoom for small windows, the Sampler set-up, Radek's Mix bugs (plus a DIST mapping bug found along the way), native window and menus, icons. Not done: a global-settings workspace and a shortcut overlay (§8) |
| – | Release build, install, auval | **GO.** VST3 and AU installed; `auval`: AU VALIDATION SUCCEEDED. The P3 copies are backed up |

**Overall: GO.**
- Unit tests (`ctest -L UnitTest`): 47 of 49 pass. The 2 failures are the known `synthLib` ones.
- Firmware smoke test `mdDeskFirmwareTest`: 51 checks PASS; `p4` 36 checks PASS; `hw` 10 checks PASS.
- Plug-in self-tests: `GEARMULATOR_MDSTUDIO_SELFTEST=p4`, `p4mix`, `p4hw`, `p4set`/`p4get` and `p4cpu`, all as logged below.

---

## 1. Pattern chaining and mutes

Found with `mdP4ProbeFirmwareTest` (`chain2` … `chain5`, `mutes3`).

**Chaining** (manual p.37)
- The gesture is: hold BANK, then hold the TRIG keys **together**, in play order.
  - Pressed and released one by one, the keys only select the last pattern.
  - `md::panelKeySequence("chain:<bank key>:<trigs>")` builds the key states; `"bankGroup"` switches between A-D and E-H.
- The chain lives in the MC68331 **internal SRAM**, as 32-bit big-endian values:
  - `0x1001f5c`: active;
  - `0x1001f60`: the next entry to queue;
  - `0x1001f64`: the length;
  - `0x1001f68 + 4n`: the patterns.
  - It was found by a deterministic diff: two identical runs whose chains differed in one pattern.
- Behaviour, measured on firmware (`mdDeskFirmwareTest p4`):
  - one bank only, each pattern once, and it loops: A04 A02 A05 A04 A02;
  - a chain in bank E presses BANK GROUP first: E02 E01 E02;
  - LOAD PATTERN or a single TRIG ends it;
  - a pattern dump into a chained pattern keeps it;
  - STOP then PLAY resumes at the cued pattern;
  - a chain made while stopped starts at its first pattern on PLAY.
- The desk:
  - `chain` / `chainClear` commands; CLEAR is LOAD PATTERN of the current pattern.
  - `select` while a chain is active first asks `breakChain`. The editor's own pattern pushes do not break a chain.
  - `machine.desk.chain` publishes {active, next, patterns}.
- The page: a CHAIN card in Song. You click pads in order and press CHAIN; the playing and next entries are shown, with "one bank · loops".

**Mutes** (manual p.44)
- The machine's pattern mute mask is main RAM `0x28b34a`: 16 bits, big-endian, bit 0 = track 1.
- CC 12-15 and the machine's own MUTE window both write it. The desk publishes it (`mutesSource: memory`).
- Checked on firmware: a mute set in the panel's MUTE window shows in the page.
- Page (mockup v54, Radek's rule "mutes live where the tracks are"):
  - the rail and Mix M keys;
  - Shift+click prepares a mute (+ or X, blinking), and the prepared mutes apply together when Shift is released;
  - Option+1-8 / Option+Q-I toggle tracks from any workspace. Plain 1-6 are the workspace keys and R is RECORD, so the keys needed a modifier.
  - Without track keys on screen, LCD line 2 says "MUTE 3 5 · UNMUTE 11".

In the plug-in (`p4`):
- the M key reaches the machine's mask;
- Shift-prepared 4+ 7X: nothing is sent while Shift is held, then both apply on release;
- chain A02 A03 A04 is in the firmware's chain and plays A02 A03 A04 A02;
- picking A01 asks first; afterwards the chain is gone.

## 2. Persistence

- The `md-desk/setup` document (`mdDesk/mdDeskSetup.h`, pure; in the schema) holds:
  - the app modulators;
  - the eight knob-row CCs (distinct values 0-127).
- The desk keeps it and hands every change to `Port::saveSetup`. The processor stores the text as the `MDSK` chunk of its state.
  - A restored project brings it back, also into an open editor (via a generation counter).
  - A project without it starts from the default setup.
  - The page no longer keeps knob CCs in `localStorage`.
- Tests:
  - `mdDeskTest`: setup JSON and the desk path;
  - `mdDeskSetupStateTest`: the state round trips byte for byte, with no firmware needed.
- In the standalone: `p4set` set an app LFO on track 3 DIST and knob 1 to CC 40, and the app quit. `p4get` started it again: "1 app source LFO A -> T3 p16, knob CCs 40 22 …".
- The modulators run while the editor is open, because the desk lives in the editor.

## 3. The first key after boot

Measured (`mdP4ProbeFirmwareTest keys`, `bootui`):

| Measurement | Fresh machine | Restored project |
|---|---|---|
| PLAY first taken after MIDI is ready | 12.7 s | 9.3 s |
| RAM 0x28998a turns non-zero (main screen starts) | 13.4 s | 9.4 s |

- `md::BootAnimation` latches the first non-zero value once per machine boot, with a 30 s timeout, and `md::Device` publishes it.
- 0x2a68e3 also goes 02 → 00 at the end of the animation, but every SysEx dump sets it again. It is not used.
- `Desk::isInputReady` holds the page's input until then; loads already run.
  - `desk.firmware` stays "booting" (BOOTING OS) and `desk.boot` reads "animation".
  - Commands are refused with the reason.
- While it waits, the host sends the firmware's own LCD as `{"type":"lcd","bits"}`: 128×64 pixels, about 15 frames a second.
  - The page draws it over the whole LCD in the plate's `--lcd`/`--ink`, then fades.
  - The LCD keeps its fixed 601×70 size.
- Fixed during the round: the overlay painted a box in the other plate's colours on MKII.

Results:
- Firmware test: the first PLAY after ready plays. Input was ready 15.9 s after MIDI while the desk loaded in the background.
- Plug-in:
  - 83 firmware LCD frames, the first after 37-62 ms;
  - EMU OS 1.63 at 13.5-14.1 s after the page loaded;
  - the first PLAY after ready plays, 195-1000 ms after the click (§8).

## 4. Knob locks while recording

`mdP4ProbeFirmwareTest lockwindow`, with a programmed trig on step 9 at 120 BPM:
- a DATA ENTRY turn 8 ms before the step starts locks that trig;
- a turn at or after the start is too late, and the lock goes to the track's next trig;
- with a live note played at the same moment, the lock came through only 3 times out of 9.

So P3's "at least a step after the turn" was the panel sequence's latency, not the firmware's rule.
- `mdDesk::nextLockStep` (pure) names the trig. The desk publishes it as `machine.desk.recLock` for 3 s.
- The page marks that cell (dashed) and says so in a toast; the REC key's title states the rule.
- Firmware check: the desk said step 9 and the firmware locked step 9.

## 5. Kit library and pattern chooser (mockup v50)

- `mdDesk/mdDeskLibrary` (pure) does copy, paste, drag-copy, clear and rename of kit and pattern slots.
  - Kit results are stored-slot writes (`Change::slotWrite`). They are undoable in the editor.
  - The desk also loads all 64 kits in the background now.
- Machine actions: `kitLoad` (LOAD KIT), `kitSaveAs` (SAVE KIT n), and `select` with `now` (STOP, LOAD PATTERN, PLAY).

The design agent's caveats, each verified on firmware:

| Caveat | Measured |
|---|---|
| Save As makes the slot current | Yes, and in EXTENDED the current pattern links to it too |
| Relink on a manual load | Yes: LOAD KIT relinks the current pattern |
| A paste or clear into the current kit needs LOAD KIT | Yes: a dump alone is not heard. The editor sends dump + LOAD KIT and asks first when the kit has unsaved edits |
| There is no clear command | Correct: clear writes an empty kit (every track GND-EMPTY, neutral values, no name) or an empty pattern (length, speed, swing, accent and kit link kept). What the machine's own CLEAR leaves is not known, and the tooltip says so |
| Renaming a slot that does not play needs read, rename, write | Yes: its dump is written with the new name. The kit that plays is renamed live (0x55) |
| Switch now while playing | Works: STOP, LOAD PATTERN, PLAY plays the new pattern after 289-367 ms |
| New: pasting over the current pattern with another kit link | The machine then loads that kit, so the desk asks (`relinkKit`) |

- Firmware smoke test (`p4`) checks, all against the machine's own dumps:
  - paste into K41; rename; clear; undo; drag-copy; save as; load;
  - paste into the kit that plays, heard at once (the working kit in memory);
  - live rename;
  - pattern paste and clear;
  - switch now.
- Plug-in: 64 of 64 kits and 128 of 128 patterns read; paste K01 into K64, then undo.

## 6. HW MIDI

The engine menu's HW MIDI swaps the desk's port.
- `pluginLib::Processor::setExternalMidi` (generic), while it is on:
  - SysEx that comes in (from the host or the physical ports) goes to the editor, not the device;
  - the editor's messages go out in the next block, through the host MIDI out and the physical ports;
  - the emulated device's own MIDI output is held back.
- `mdDesk::DinPacer` (pure) sends 3125 bytes a second, so a pattern dump is 1.73 s on the wire.
  - Desk timeouts follow the wire.
  - In the background the kits load first.
- Kit values and mutes go out as CCs. PLAY/STOP are MIDI Start/Stop.
- Engine status: HW CONNECT, then HW MIDI; HW NO MIDI after no reply for 3.5 s, or none within 5 s of choosing HW.
- Live recording, chaining, the working kit from memory and the boot screen need the emulator, and say so.

| Verified | Where | Result |
|---|---|---|
| Connect: status, the current pattern and its kit | emulator as the MIDI peer, DIN both ways (`mdDeskFirmwareTest hw`) | 2.6 s |
| Trig edit → confirmed read-back | same | 3.5 s, no timeout |
| Kit value as a CC | same | the machine plays it |
| All 64 kits in the background | same | 27.6 s |
| Kit paste into K51 | same | read back |
| PLAY as MIDI Start | same | the emulated MD plays (its default sync settings) |
| Unplugged, then back | same | HW NO MIDI, then HW MIDI |
| The plug-in's MIDI in/out carry it | `mdDeskSetupStateTest` | out through `processBlock`, in to the editor, not the device |
| In the standalone, no MD attached | `p4hw` | HW CONNECT, 315 bytes out, HW NO MIDI, back to EMU OS 1.63 |
| **A real Machinedrum** | — | **not tested; none here** |

## 7. Other work in the round

- **Native window** (standalone, generic in `jucePluginEditorLib/standaloneApp.h`; the MD and MM apps both use it):
  - the native title bar, titled "Machinedrum Editor";
  - the app menu with Settings… and Audio/MIDI Settings…;
  - an Editor menu (GUI scale, Skins, RAM recording, diagnostics, Settings) and an Audio menu (settings, save and load state);
  - the RML "right-click here" strip is gone; a right-click on an empty part of the page header opens the same menu.
  - Checked (log): native title bar 1, menu bar Editor and Audio.
- **Icons:** `doc/modern-ux/icons/`, with SVG sources plus `build-icons.sh` (PNGs and .icns). Radek approved option A.
  - The MD icon is on the standalone, VST3 and AU; the plug-in bundles get `CFBundleIconFile` after the build.
  - The MM PNGs are in `mdJucePlugin/icons/` for the MM branch.
  - The installed AU and VST3 carry Icon.icns, which is our icon when converted back. I could not look at Finder or the Dock (see §8).
- **Sampler** (Radek's case, a kit without RAM machines):
  - The old key also cleared track 13's trigs, silently. Now there is one call to action, "Set up sampling", which shows what changes (the machines, and whether trigs are kept or cleared) and is one undo step.
  - Then the recorder is ready: Live, Freeze, Capture, a source choice (main mix, input A, input B, A+B, following the manual: MLEV/ILEV 0 means "as is") and the chop grid.
  - ROM slots show which slots the kit uses and the names sent this session, plus what the machine does not report. MEM n/a has its reason.
- **Mix** (Radek's bugs):
  - A dragged fader or value box got the class `act`, and `.strip .act` (meant for the activity LED) made it 7×7 px. That was the vanishing fader, the re-flowing strip and the sliver in place of DEL.
  - Value boxes follow the axis that moved more.
  - On OUT A-F, DEL and REV keep their values with a MAIN tag and a tooltip.
  - Also found: the Mix DIST box moved a machine's synthesis DIST (parameter 3), because the index was looked up by name over all 24 slots. It is fixed.
- **Page zoom:** the page is laid out for 1440 px. At GUI 75 % (1080 px) the header was cut. `WKWebView.pageZoom` now fits the page to the window (`mdStudioWebZoom.mm`).
- **Tap tempo** (T) and **parameter tweaking** (Alt while moving a track value moves that knob on every track, leaving out RAM, MIDI and CTR tracks), as in manual pp.36-37.
- **Mockup rounds synced:**
  - v51 grid/lane alignment: 0 px off in the plug-in;
  - v55 equal gaps;
  - v56-v59 lock lane: lockable keys, bipolar lanes for PAN, EQG, MLEV, MBAL, ILEV, IBAL, LG, HG and PG, LED meters, one bar shape for render and drag;
  - v60 Mix fixes.

## CPU

- Method, repeatable:
  - `mdCpuBenchTest <ROM> [instances] [seconds]` runs headless, with thread and process CPU time per second of audio, in 64-frame blocks at 44.1 kHz.
  - `scripts/md-editor-cpu.sh` runs the standalone in 30 s phases and reads the CPU time of the app and of the page's WebKit content and GPU processes. It backs up and restores the user files.
- Mac16,8, Apple M4 Pro, 12 cores.

| | Stopped | Playing a busy pattern |
|---|---|---|
| Emulation, headless, per instance | 44 % of one core | 48 % of one core |
| Same with 2 and with 3 instances in parallel | 44 % each | 48 % each (no interference) |
| Standalone app (48 kHz, 512-frame blocks), editor open | 51 % | 55 % (Sequence or Mix) |
| The page's WebKit processes (content + GPU) | 0.5 % | 8 % (Sequence 4.0 + 3.9, Mix 4.3 + 3.4) |

- The whole machine has 12 cores, so one playing instance with its editor is about 5 % of it.
- What the editor itself costs, compared with the emulation:
  - the app beyond the headless emulation: about 7 points of one core (audio I/O, JUCE, the desk and the page bridge);
  - the page drawing: another 8 points while it plays.
- Dropouts:
  - Each instance needs about half a core in real time, and headless instances scale linearly (3 in parallel, unchanged).
  - A host that runs instances on separate threads carries many.
  - One audio thread carries 2 at most (2 × 48 % leaves no headroom).
  - Dropouts were not measured in a DAW; the AU was only validated.

## 8. Known issues and limits

- **No real Machinedrum** was available: HW MIDI is verified against the emulator as the peer only.
- **Real pointer drags and screenshots were not possible here.** The Browser pane is 17 px tall, this host has no Accessibility permission (`AXIsProcessTrusted` false) and `screencapture` gives black.
  - Mix drags were checked with dispatched pointer events, in Chrome (the mockup) and in WebKit (the plug-in).
  - The icon was not seen in Finder or the Dock.
- **The lock lane is keyed by parameter name.** A machine with a synthesis DIST and the routing DIST shows one lane for both names (the synthesis one). The Mix/Sound values are fixed; the lane is not.
- **The first PLAY right after ready** can take up to 1 s while the background loads run. It plays.
- The app modulators run only while the editor is open.
- **Not reachable over MIDI** (unchanged from P3):
  - sample names, memory in use, SDS send, RAM to ROM;
  - the machine's CLEAR KIT and CLEAR PATTERN (the editor writes empty slots instead).
- Not done from the gap review: a global-settings workspace (base channel, map editor, sync), a shortcut overlay, and the +Drive snapshot manager.
- `ctest` (a plug-in load) and `auval` rewrite `pluginPath_*` in the MD config XML, as JUCE's `savePluginLoadPath` does. I restored the file after every run.

## 9. User files

- For the plug-in self-tests, the config was switched to the mdStudio skin at scale 75. The standalone rewrites the settings file on exit.
- After every run both files were restored from the P3 backup, byte for byte:

  ```
  cbbc7cda67ee8e9027d45775ebb1ead31abd771b  ~/Documents/Gearmulator Preview/Machinedrum/config/Gearmulator MD.xml
  fc68a82e43b52ef83ab1864c32349e71257afa60  ~/Library/Application Support/Gearmulator MD.settings
  ```

- The `midilearn` folder was not touched.

## 10. Installed

- VST3: `~/Library/Audio/Plug-Ins/VST3/Gearmulator MD.vst3`.
- AU: `~/Library/Audio/Plug-Ins/Components/Gearmulator MD.component`. `auval -v aumu Tmdr GmPv`: AU VALIDATION SUCCEEDED.
- Both are ad-hoc signed; `codesign -v` passes.
- **The P3 copies are backed up at `~/Library/Audio/Gearmulator MD P3 backup 2026-09-27/`**, outside the plug-in folders so hosts do not scan them.
- Standalone: `bin/plugins/Release/Standalone/Gearmulator MD.app` in the worktree, where the build puts it (not installed).

## 11. Tests and how to re-run

```
ctest --test-dir temp/cmake_p4 -L UnitTest                                  # 47 of 49
temp/cmake_p4/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>"         # 51 checks
temp/cmake_p4/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>" p4      # 36: boot, mutes, chains, locks, library
temp/cmake_p4/source/elektron/md/mdLibTest/mdDeskFirmwareTest "<ROM>" hw      # 10: HW MIDI at DIN speed
temp/cmake_p4/source/elektron/md/mdLibTest/mdP4ProbeFirmwareTest "<ROM>" [boot|keys|bootui|chain2..chain5|mutes3|lockwindow|library|flaguse]
temp/cmake_p4/source/elektron/md/mdLibTest/mdCpuBenchTest "<ROM>" [instances] [seconds]
scripts/md-editor-cpu.sh
```

- New unit tests:
  - `mdDeskTest`: live controls, boot hold, setup, lock step, library, HW link;
  - `mdSequencerStateTest`: chain keys, boot latch;
  - `mdDeskSetupStateTest`: the MDSK chunk and external MIDI.
- In the plug-in: `GEARMULATOR_MDSTUDIO_SELFTEST=p4 | p4mix | p4hw | p4set | p4get | p4cpu`. The log is `~/Library/Caches/Gearmulator MD/gearmulator-mdStudio.log`, with lines starting "P4:".

## Commits

e65918f chains and mutes · 0c25001 persistence · 24fae98 first key after boot · f3f4fdb knob lock timing · a998b6b native window, icon sources · 8d1b253 Sampler · b1e6400 MD icon wired · 72ae68b kit library, pattern chooser · b721c0f v56, boot LCD colours · 5dcb913 HW MIDI, v57-v59 · 81ec735 Mix fixes, page zoom, tap tempo, tweaking · 2d6f148 CPU measurement.
