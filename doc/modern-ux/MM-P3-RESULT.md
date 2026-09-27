# MM-P3 result: the Monomachine Editor without mocks (MM OS 1.32B)

- Branch `mm/editor`, 2026-09-27, Mac16,8 (Apple M4 Pro, 12 cores: 8 performance + 4 efficiency), macOS 27.0, build dir `temp/cmake_mm` (Release, arm64).
- Merged with `p4/md-editor-complete` (the finished Machinedrum Editor, the shared native standalone window, the icons).

## Verdict

| # | Deliverable | Verdict |
|---|---|---|
| 1 | No mocks left | **GO.** Nothing on the page shows the mockup's example data. Every control either reaches the machine or is disabled, with the reason as its tooltip and as a note when clicked (§2) |
| 2 | Engine status real | **GO.** NO ROM (with the real ROM folder), NOT OS 1.32B, LOADING ROM, BOOTING OS (the firmware's own LCD), READING MACHINE, then EMU OS 1.32B with the library's progress (`EMU OS 1.32B · 120/288`) |
| 3 | Kit library and pattern chooser real | **GO.** All 128 kits and 128 patterns from the machine; load, save, save as, reload, copy, paste, clear, rename, go, queue, now are the machine's commands or dumps; slots still being read are disabled ("still reading this slot") |
| 4 | Trigless and pitchless trigs | **GO.** Codec, contract, validation, page (ALT + click = trigless; a step without a note = pitchless), the conversion test, the firmware test by ear, and the in-plug-in self test |
| 5 | CPU measured | **GO.** §4; `mmCpuBenchTest` and `scripts/mm-editor-cpu.sh`, the same method as the MD's |
| 6 | Self test in the standalone | **PASS 9/9** (§3) |

---

## 1. Made real in MM-P3

| Control | How |
|---|---|
| Tempo field | **Read from the machine.** Found with the lab (`ramfind`, `peek16`): OS 1.32B keeps BPM x 24 at RAM `0x2bc2a6`, for 0x61 and for the TEMPO screen's own knob. `MmTelemetry` publishes it, the desk puts it in `mm-desk/machine` (`tempo`), the page shows the machine's tempo and sends its own edits (0x61). `mmDeskFirmwareTest` checks 133 and 120 |
| Perform keyboard | Plays the machine: note on/off into the plug-in on the channel the mode uses, from the global: AUTO TRACK = the track's own channel (base + track), MULTI TRIG channel, MULTI MAP channel. A channel set to OFF says so. New page command `midi` (channel messages only) |
| Joystick pad | Pitch bend (R/L), CC 1 (up), CC 2 (down) on the track's channel, and back to centre on release, as the SFX-60 joystick |
| Solo | The synth tracks' mute parameters follow the page's mute and solo together |
| LEARN | The plug-in's MIDI learn: click a value, then move a knob on the controller (`learnStart`), or press 1-8 for CC 21-28 (`learnAdd`). Stored with the plug-in; the Control workspace's Knob 1-8 drive the same value on screen |
| MULTI MAP table | The global's ranges: each range's upper key and pattern (255 = CUR), read-only |
| Control workspace | Starts with no mappings (the mockup's examples are gone). Its LFO and Random sources move with the machine's steps and drive real values through the kit path |
| Right-click on the header | The editor's menu (skins, GUI scale, settings), as the MD Editor (P4); the standalone has it in the native menu bar. The RML header strip is gone and the page is zoomed as a whole below 1440 px |

## 2. Disabled, with the reason

| Control | Reason shown |
|---|---|
| MIDI track mutes and solos (rail, Mix, Perform) | set on the machine (FUNCTION + a track key in MIDI mode); the plug-in has no command for them yet |
| POLY | switched on the machine; the editor does not drive it yet |
| MULTI TRIG mode, split, timing | settings the editor does not decode yet; the keys do play on the MULTI TRIG channel |
| MULTI MAP offset / length / transpose / timing, range edits | fields in the global not decoded yet (the card says so); edit on the machine (GLOBAL › CONTROL › MULTIMAP EDIT) |
| PORTAMENTO mode (ALWAYS / ONLY LEGATO) | not decoded in the kit yet |
| GRID RECORD | runs on the machine; in the editor you draw steps |
| HW MIDI engine | "not available yet" (MM-P2) |
| Library slots not read yet | "still reading this slot from the machine" (the first ~10 s after ready) |

Everything else on the six workspaces was already real in MM-P2 (MM-P2-RESULT §1).

## 3. Self test in the standalone

`GEARMULATOR_MMSTUDIO_SELFTEST=1`, factory set, A01, stopped, after the merge:

| Check | Result |
|---|---|
| pattern trig set and cleared (enters SYSEX RECV while the library loads) | ok, 2852 ms for both |
| trigless trig | ok, 214 ms |
| pitchless trig | ok, 247 ms |
| kit value live (AMP VOL) | ok, 141 ms |
| pattern switch and back | ok, 864 ms |
| play and stop | ok, 160 ms |
| tempo out and read back (0x61, RAM) | ok, 199 ms, 120 -> 133 -> 120 BPM |
| solo mutes the other synth tracks | ok |
| song and global documents | ok (65 rows, 3xSTEREO) |

**PASS 9/9.** The plug-in log also records `audio: 48000 Hz, block 512 frames`.

## 4. CPU

Method, repeatable, the same as the MD's (P4) so the two compare:

- `mmCpuBenchTest <ROM> [instances] [seconds]`: headless, N emulated MM OS 1.32B machines in parallel threads, each rendering as fast as it can in 64-frame blocks at 44.1 kHz, first stopped, then playing the factory A01 (six synth tracks, 116 trigs in 64 steps; the test counts the steps it played). Reports thread CPU time per second of audio (= the share of one core needed in real time) and the process total.
- `scripts/mm-editor-cpu.sh`: the Release standalone with the editor page and `GEARMULATOR_MMSTUDIO_SELFTEST=mmcpu` (30 s phases: stopped, playing in Sequence, playing in Mix); reads the CPU time of the app process and of the page's WebKit content and GPU processes. It backs up and restores the MM config file and the standalone settings byte for byte.

Mac16,8, Apple M4 Pro, 12 cores; standalone at 48 kHz, 512-frame blocks (the default output device).

| | Stopped | Playing A01 |
|---|---|---|
| Emulation, headless, 1 instance | 57.3 % of one core | 55.4 % of one core |
| 2 instances in parallel | 57.1 %, 57.0 % | 56.1 %, 56.0 % |
| 3 instances in parallel | 57.6 %, 57.6 %, 59.4 % | 57.5 %, 57.4 %, 59.2 % |
| Standalone app, editor open | 66.5 % | 64.6 % (Sequence), 65.4 % (Mix) |
| The page's WebKit processes (content + GPU) | 1.4 % (0.7 + 0.7) | 7.4 % in Sequence (3.5 + 3.9), 6.2 % in Mix (3.3 + 2.9) |

- **Per instance: about 0.57 of one core**, stopped or playing (the MM's DSP runs the same whether the sequencer plays or not). The MD is 0.44-0.48 (P4-RESULT).
- **Of the whole machine** (12 cores): one instance is about 4.8 %; with its editor open and playing about 6 %.
- **The editor against the emulation:** the app beyond the headless emulation is about 9 points of one core (audio I/O at 48 kHz, JUCE, the desk, the page bridge; the headless run has none of these), and drawing the page another 6-7 points while it plays, under 1.5 stopped.
- **Instances before dropouts:** headless instances scale linearly (three in parallel cost about the same each: 57-59 %). A host that runs each instance on its own thread carries as many as it has free cores for at 0.57 each; **one audio thread carries one instance** (two need 1.1 cores). Dropouts were not measured in a DAW; the AU was validated with `auval` only.

## 5. Tests

- ctest (UnitTest): `mmConvertTest` (133 patterns, 131 kits, 27 songs, 10 globals with the local factory set), `mmDataTest`, `mmDataCorpusTest`, `mmDeskTest`; MD tests green (the two known `synthLib` failures aside).
- ctest (FirmwareTest, `-DGEARMULATOR_MM_ROM=`): `mmDeskFirmwareTest` (now with the tempo read-back).
- Manual: `mmCpuBenchTest`, `scripts/mm-editor-cpu.sh`, the in-plug-in self test.

## 6. Known limits

- MULTI TRIG settings, the MULTI MAP fields past key and pattern, PORTAMENTO mode and MIDI track mutes are not decoded / not driven (§2).
- The editor's undo covers the current pattern, kit, song and global; library writes (paste, clear, rename of other slots) are not in it. The machine's UNDO KIT still works.
- The song workspace edits the machine's current song; song slots are not switched from the page.
- Screenshots of the plug-in window were not possible from this session (screen capture shows the desktop only); the page was checked in a browser harness with a fake host and in the plug-in through its self test and log.

## 7. Installed

- Release, arm64, from this branch (commit `7cfe57f`), with the MM icon on all three bundles.
- VST3: `~/Library/Audio/Plug-Ins/VST3/Gearmulator MM.vst3`.
- AU: `~/Library/Audio/Plug-Ins/Components/Gearmulator MM.component`. `auval -v aumu Tmno GmPv`: **AU VALIDATION SUCCEEDED**.
- No earlier MM copies were installed, so there was nothing to back up (the MD ones were left alone).
- Standalone: `bin/plugins/Release/Standalone/Gearmulator MM.app` in the worktree, where the build puts it (as the MD's; not installed). Window title "Monomachine Editor".
- The editor page (`mmStudio`) is the MM plug-in's default skin; the SFX-60 panel stays in the menu.
- **The ROM is not installed**: put the OS 1.32B image into `~/Documents/Gearmulator Preview/Monomachine/roms/` (the page's NO ROM dialog opens that folder).

## 8. User files

For the plug-in runs the ROM was linked into the ROM folder and removed afterwards.
After the last run (and after `auval`, which rewrites `pluginPath_*` in the config
as JUCE does) the Monomachine user folder was restored from the backup taken at
the start and compared with it: `diff -r` identical, `Gearmulator MM.xml` SHA-1
`eaae1ea33f2daf557f032b8416eabb8b5fa40dda` as at the start, `config/midilearn`
and `roms` empty. The files the runs created and that did not exist at the start
were removed: `logs/`, `skins/` in the user folder, `~/Library/Application
Support/Gearmulator MM.settings`, and the MM caches and WebKit data in
`~/Library/Caches` and `~/Library/WebKit`.
