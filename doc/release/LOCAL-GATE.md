# The local release gate

`scripts/mdmm-local-gate.sh` says red or green for a release candidate, on the Mac that has the firmware. GitHub CI
never has a ROM, so it proves that the editors start and show their page, and nothing about a machine actually
running. The gate does the rest. **It runs green before a release is tagged** ([CLAUDE.md](../../CLAUDE.md), Releases;
[marketing/LAUNCH-CHECKLIST.md](../../marketing/LAUNCH-CHECKLIST.md)); the tag's CI then repeats the part that needs no ROM
on the shipped files ([FOUNDATION.md](../modern-ux/FOUNDATION.md), CI start tests).

```sh
scripts/mdmm-local-gate.sh                       # the release gate: everything; plan on about two and a half hours (timings below)
scripts/mdmm-local-gate.sh --quick               # no plug-ins, no window: stages 1 to 6a, for working on the emulator
scripts/mdmm-local-gate.sh --soak                # everything plus the soak (6c: 21 minutes of play; off by default)
scripts/mdmm-local-gate.sh --skip-journeys       # everything but the user journeys (8 to 16 minutes)
scripts/mdmm-local-gate.sh --skip-plugin         # build the plug-ins, but leave out 6b, 6c (the soak) and stage 7
scripts/mdmm-local-gate.sh --record-goldens      # record the playing goldens instead of comparing (see Goldens)
scripts/mdmm-local-gate.sh --build <dir> --out <dir>
```

**Run it on a quiet Mac.** Four of its checks measure time (the idle-scheduler and MIDI timing tests, the real-time work in 6a, the core capacity in 6b, the soak in 6c) and the emulator needs most of a core each; with other builds, a DAW or other agents running they fail from load alone. The first line of the run says the load, and any failed stage is annotated "the Mac is busy" when the load was high (more than half the cores).

It prints one line per stage while it runs, ends with the table below, writes the same to `temp/local-gate/<date-time>/summary.md`
(`temp/local-gate/latest` links to the newest run) and exits non-zero when red. Only a run **without** `--quick`,
`--skip-plugin`, `--skip-soak`, `--skip-journeys` and `MDMM_GATE_SKIP_BUILD` can clear a release; the verdict says `GREEN, PARTIAL` for the
others. The last thing a green run prints is the manual step it cannot do (Updater end to end, below).

## Timings

Measured on 2026-10-09 on a 12-core M4 Pro with other work running (load 25 to 200), so an idle Mac is faster and a busy one slower:

| Stage | Time |
|---|---|
| 1 build | cold 9 min; after a change 1 to 7 min |
| 2 ctest | 8 to 12 min (the slowest tests, `mmDigiproFirmwareTest` and `mmDigiproEnsembleFirmwareTest`, take 5 to 7) |
| 2b desk smoke tests | about 26 min under load, less than half of that idle |
| 3, 4, 5, 5b | 1 to 2 min, 2 to 7 min, 5 min (24 runs, 4 at once), 1 to 2 min |
| 6a rt-check | 11 min |
| 6b core capacity | about 6 min |
| 6c soak | 21 min (two machines, 10 minutes of play each) |
| 7a, 7b | under a minute |
| 7c pluginval | not measured here: the first run downloads pluginval |
| 7d journeys | diagnostics build 6 min (first time), VST3 host build a few minutes (first time), then 8 to 16 min in the background |

## What it proves

| Stage | What it runs | Red when |
|---|---|---|
| 0 Inputs | the ROMs, the SysEx fixtures; sha256 of the ROMs goes into the summary | a ROM or the fixtures are missing, or a ROM is not an 8 MiB image |
| 1 Build as shipped | `cmake` + Ninja: Release, this Mac's architecture, ThinLTO for mdLib and the DSP (`GEARMULATOR_MDMM_APPLE_THINLTO=ON`, `GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON`), `BUILD_TESTING=ON`, the JUCE plug-ins on (VST3, AU, Standalone), the Xcode toolchain and SDK (`SDKROOT`); then `cmake --build` of everything and of the targets that are not in "all" (the list of `scripts/macos/build_mdmm.sh`); the cache is read back with `scripts/macos/write_mdmm_receipt.py --validate-build-optimization` | it does not build, a registered test has no program, or the cache is not the optimised one |
| 2 ctest | `ctest -j4 --output-on-failure --output-junit` over every registered test, in a sandbox, with `GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1` and `MD_AUTOMATION_REQUIRE_FIRMWARE=1`, so a firmware test that skips is a failure; `MD_SYX` and `MM_SYX` are given, so `syxImportFileTest` runs on a real backup. The JUnit file gives executed, failed and skipped tests **by name** (the summary snippet of `scripts/linux/build_mdmm.sh`) | a test fails or times out, or any test skips that is not named in `TOLERATED_SKIPS` (empty) |
| 2b Firmware tests ctest cannot run | `mmDeskFirmwareTest <MM ROM>` (it has a ctest entry, but its 400 s limit is too short, and without `-DGEARMULATOR_MM_ROM` it only skips), its twin `mdDeskFirmwareTest <MD ROM>` (no ctest entry), and `mdSysexLifecycleTest <MD ROM> <MD cache> <MM ROM> <MM patch RAM>` when both fixtures exist. Each must print its pass line | a pass line is missing or the program fails |
| 3 ROM loading | `mdmmRomLoadTest <ROM>` per ROM: the image is recognised by its fingerprint, boots and answers SysEx; a truncated and a garbage image are refused. The fingerprints it prints go into the summary | the machine does not boot or the negative cases are accepted |
| 4 SysEx round trip | `mdmmSysexRoundTripTest <ROM> <md or mm> <fixtures...>` (`md*.syx` for the Machinedrum, `mm*.syx` for the Monomachine): a full dump goes into a fresh machine as the editors import a file and comes back byte for byte; real backups document by document | any difference |
| 5 Playing goldens | `mdmmPerfGateTest <ROM> <md or mm> 8 --scenario S --outputs <stereo or all> --golden <goldens>` for every scenario in the goldens file, both outputs modes, with the speed-ups at their default and with `GEARMULATOR_MDMM_SPEEDUPS=0`: the audio, the memory and the MIDI the machine sent must equal the recorded hashes | a hash differs, or the file has no entry for a run |
| 5b CPU bench sanity | `mdCpuBenchTest <MD ROM> 1 6`, `mmCpuBenchTest <MM ROM> 1 6`: the machine plays (the play head moves) | either exits non-zero |
| 6a Real-time work | `scripts/mdmm-rt-check.sh --plock`: the audio thread's work while the editor edits, in retired instructions (budget 100 ms hot a second per action), and B-010's sequencer timing | `rt-check: FAIL` |
| 6b Core capacity | `scripts/macos/check_mdmm_core_capacity.py` with the release script's parameters, on the built VST3 in `latency_host`: three unpaced runs per machine must stay under 0.90 of the block budget at the median. The paced tail is reported, not judged. The Machinedrum's factory flash cache is given to it (`--md-flash-cache`, an option the gate added to the script): without one a fresh Machinedrum does its first-start flash work while the notes play and the capture is silent in about one run in three | the microgate fails (needs the pinned release ROMs) |
| 6c Soak | for each machine, 10 minutes of busy play in the real VST3, at 48 kHz in blocks of 128, in real time, with no window and no audio device (`latency_host`, the host of 6b): its busiest scenario, `chords`, six voices every 251 ms (the Machinedrum's pads 1 to 6, the Monomachine's tracks 1 to 6) from second 10 on, in a sandbox that has the factory flash cache and patch RAM. `GEARMULATOR_RT_INSTRUMENTATION=1` makes the plug-in start its own performance capture ([md_mm_performance_diagnostics.md](../md_mm_performance_diagnostics.md)); the gate parses that JSON Lines file (schema 2) and prints the realtime load histogram of each machine in the summary | any callback after the first 30 s over its block budget (the `100-150%` and `>=150%` buckets, less the recorded start-up ones), any callback waiting more than 1 ms for the synth lock after the first 30 s, a capture of less than 8 minutes or with no end record, or a machine that made no sound |
| 7a VST3 start with firmware | `pluginTester -verify-audio-buses -blocks 256` per machine with the ROM (the check of `verify_mdmm_package.sh`), and `-blocks 16 -verify-audio-buses -automation-smoke` without one (`build_mdmm.sh`) | a device-start error, a short run, a non-zero exit |
| 7b auval | `auval -v` on each built AU, with no ROM, as `smoke_mdmm.sh` runs it in CI | `auval` fails |
| 7c pluginval | `scripts/mdmm-pluginval.sh`: pluginval 1.0.4 (pinned, `scripts/pluginval.env`) at strictness 5 and 8 on the VST3 and the AU, with the firmware in its own scratch data root | a run fails, or the AU was not validated |
| 7d User journeys | a diagnostics build in its own tree (`scripts/mdmm-dev.sh`, `temp/local-gate/build-diag`), then `scripts/mdmm-journeys.sh --host both --background both`: the page clicked as a person does, both editors, standalone and VST3, on the firmware, silent (below); `MDMM_JOURNEY_PERF=1` records the audio thread during those edits and the summary quotes it, not judged | a journey fails, the editor ends early, or a journey is skipped because its window was not drawing |
| M1 Updater end to end | **manual**, see below | never red; the summary lists it as open |

What it does not prove: a signed and notarised build (CI's job, [SIGNING.md](SIGNING.md)), Gatekeeper on a downloaded
app, the Windows and Linux builds, a real DAW, audio and MIDI devices, High-DPI and several monitors. CI's no-ROM start
test (the "firmware needed" card, real key presses through the HID tap, the window's chrome) is not repeated here: the
journeys cover the page, the bridge and the keys on a running machine, and the tag's CI runs the start test on the
shipped files.

### Where it differs from the shipped build

- **One architecture.** The release is universal (`arm64;x86_64`); the gate builds this Mac's architecture. The x86_64 slice is never executed here.
- **Not packaged, not signed.** `scripts/macos/build_mdmm.sh` configures, builds, signs, packages and verifies in one go and insists on all of it, so the gate repeats only its configure flags (same optimisation, same deployment target 10.13, `XCODE_VERSION` 16 for the cache) in its own tree and reads the cache back with the script the release uses. Nothing is installed, nothing is quarantined.
- **No ccache.** A cold gate build is the full tree (9 minutes on the 12-core M4 Pro, with the Mac busy); a second run reuses `temp/local-gate/build`, so only what changed is built (7 minutes after a change to the emulator, which relinks everything that uses mdLib with ThinLTO).

## Inputs, and where they live

| Input | Default | Override |
|---|---|---|
| Machinedrum ROM (OS 1.63) | first file in `~/Documents/Gearmulator Preview/Machinedrum/roms/` | `GEARMULATOR_MD_FIRMWARE_BIN` |
| Monomachine ROM (OS 1.32B) | first file in `~/Documents/Gearmulator Preview/Monomachine/roms/` | `GEARMULATOR_MM_FIRMWARE_BIN` |
| Machinedrum factory cache | `…/Machinedrum/nvram/md-uw-1.63-factory-v2.cache`: given to 6b and 6c (a machine that starts as a person's does) and to `mdSysexLifecycleTest` | `MDMM_GATE_MD_CACHE` |
| Monomachine factory patch RAM | `…/Monomachine/nvram/mm-factory-live3-be.bin` (not on this Mac) | `MDMM_GATE_MM_PATCH` |
| SysEx fixtures | `…/fixtures/sysex/**/*.syx`: `md*.syx` for the Machinedrum, `mm*.syx` for the Monomachine | `MDMM_GATE_FIXTURES` |
| Goldens | `source/elektron/md/mdLibTest/goldens/mdmm-goldens.json` (in the repo) | — |

`…` is `~/Documents/Gearmulator Preview` (`MDMM_GATE_PREVIEW`). The ROMs must be the pinned release images
(`scripts/macos/check_mdmm_core_capacity.py` checks their SHA-256). Other settings: `MDMM_GATE_JOBS` (build jobs, default
all cores), `MDMM_GATE_GOLDEN_JOBS` (golden runs at once, default 4), `MDMM_GATE_JOURNEY_ARGS` (default
`--host both --background`; add `--jobs 4` to run them in about 6 minutes), `MDMM_GATE_SOAK_SECONDS` (6c, default 600, the
most the capture and the host allow), `MDMM_GATE_SOAK_WARMUP` (seconds at the start that are listed but not judged, default 30),
`MDMM_GATE_SOAK_LOCK_US` (a synth lock wait that fails the soak, default 1000), `MDMM_GATE_ALLOW_UNMUTED=1` (run a stage
that opens an audio device although the output could not be muted), `MDMM_GATE_SCENARIOS` (extra golden scenarios when
recording). For working on the gate itself: `MDMM_GATE_SKIP_BUILD=1` (use the tree as it is), `MDMM_GATE_ONLY="3 5"` (only
those stages) and `MDMM_GATE_GOLDENS` (another goldens file); such a run is `GREEN, PARTIAL` at best.

## Silence

The gate keeps your Mac quiet. Only a stage that opens a real audio device can make a sound, and only two things do:
the standalones of the user journeys (7d) and the standalone of the manual updater step (M1). Everything else is
headless, and the gate checks that instead of assuming it:

- **No device at all, and a guard that fails a stage that opens one.** ctest (its audio tests use fake devices:
  `FakeAudioIODevice`, the tests' own headless ones), the rt-check (`mdDeskFirmwareTest`, the emulator and no plug-in),
  the goldens and the benches, `latency_host` (6b and 6c: it renders the VST3 itself), `pluginTester` (7a: a fake device),
  `auval` and `pluginval` (they call the plug-in's `processBlock` themselves) and the VST3 host of the journeys (a thread that
  feeds silent blocks) never open an audio stream. A poller watches every stage but 7d: macOS keeps a power assertion for
  each open audio stream (`pmset -g assertions`, owner `coreaudiod`, "Created for PID"), and if a process the gate started holds
  one, the stage is **FAIL** with "OPENED AN AUDIO DEVICE (process, pid, device)". `scripts/local-gate/selftest.sh` shows it
  working (a silent file played by `afplay` trips it).
- **7d: a quiet setup, then the output muted.** `--background` (the default here) runs each editor as an accessory app behind
  every window, and its standalone zeroes its output after the machine made it (`md::DeskDevice::setSilentOutput`): the audio
  device still runs, so the emulation keeps its timing, but nothing is sent to it, whichever output your saved audio setup names. The journeys use a copy of your settings; on this Mac they name no input device (`audioInputDeviceName` is empty, `shouldMuteInput` is 1)
  and JUCE's standalone does not open one by default (`mdAudioIoLayoutTest` pins that). If your saved setup ever names an input, start the gate with
  `MDMM_JOURNEY_SETTINGS=fresh` in the environment (no copy of your settings). The gate never opens a microphone or an input. A real "null" output device does not exist
  in JUCE or macOS (no device means no callbacks, and the emulation is driven by them), so the zeroed output is the quiet choice.
- **Muted as well, whatever the setup.** For 7d (and for `updater-manual.sh run`) `output muted true` is set with `osascript`, the
  previous muted state and volume remembered in `temp/local-gate/audio-state.txt`, and put back after the stage, on Ctrl-C, on
  `kill`, and on any failure (the same exit trap that restores the AU bundles). An output with no mute control (some interfaces,
  HDMI) is turned down to 0 instead and the volume is restored. The log and the stage's notes say "output muted for stage 7d (was muted
  false, volume 63)". If the gate was killed with `kill -9` and could not restore it, the next gate (or the next
  `updater-manual.sh run`) restores the output first and says so; the file also holds the old values to put back by hand. If the output
  cannot be muted, 7d does not run (`MDMM_GATE_ALLOW_UNMUTED=1` runs it anyway, still zeroed).
- `scripts/local-gate/selftest.sh` tries the mute and restore (against a stand-in for `osascript`: your real volume is not touched),
  the restore after a TERM, a stale state file, an output with no mute control and the guard; run it after changing the gate.

## What it touches

- **Firmware runs are sandboxed.** Every firmware program runs with `HOME`, the macOS user home (`CFFIXED_USER_HOME`, which JUCE follows and `HOME` does not move) and `GEARMULATOR_DATA_ROOT` under the run folder, in a working folder there too (the loader also scans the current folder for `.bin` files). The ROMs are symbolic links to your files, read only; nothing of `~/Documents/Gearmulator Preview` is written.
- **The build** runs JUCE's VST3 manifest step with a data root inside the build tree, as the release script does, with no ROM in it.
- **auval needs the AU where macOS looks.** macOS finds an AU in its registry, never by path, and this Mac normally has the editors installed there. 7b therefore sets your installed `Machinedrum Editor.component` and `Monomachine Editor.component` aside in `temp/local-gate/au-swap/`, installs the built ones in `~/Library/Audio/Plug-Ins/Components/`, runs auval and pluginval, removes them and puts yours back (also on Ctrl-C, on a failure and on any exit; `MDMM_GATE_COMPONENTS_DIR` moves the folder, for testing). If the gate is killed with `kill -9` it cannot do that: the next run refuses to start 7b while `temp/local-gate/au-swap/` holds anything, and says so. Move the files in it back into `~/Library/Audio/Plug-Ins/Components/` by hand and run again.
- **pluginval** is downloaded once (the pinned release, SHA-256 checked, `scripts/pluginval.env`) into the temporary folder; later runs reuse it. `scripts/mdmm-pluginval.sh` checksums your config and settings files before and after and restores them if anything touched them.
- **The journeys** open the standalones, each in a sandbox of its own, in the background (small, behind your windows, no Dock icon, never in front) with the system output muted ([Silence](#silence)); they do not take the keyboard.

## Reading the summary

```
# MD/MM local release gate: GREEN; manual steps still open: M1 Updater end to end
- Commit …  - ROM sha256 …  - Build …  - Audio: the guard saw no audio stream …; output muted for stage 7d
ROM fingerprints as the tests print them: …
| Stage | Result | Time | Numbers | Failures and skips |
## Goldens compared          one row per run: scenario, outputs mode, speed-ups, seconds, result
## Soak: the audio thread's load   per machine: the load histogram (callbacks per share of their block budget), the ones
                                   over budget after the warm-up, the longest synth lock wait, how the capture ended
## Known cannot-run-here     tests the gate names and leaves out, with the reason
## Skipped stages            what a flag left out
## Manual steps
```

- **Result** is PASS, FAIL, SKIP (left out by a flag), PENDING (a program the stage needs is not in the tree: red) or MANUAL.
- **Verdict.** RED if any stage is FAIL or PENDING (the run continues past a red stage, so one run lists everything that is wrong). `GREEN, PARTIAL` if nothing is red but a flag left stages out. `GOLDENS RECORDED` after `--record-goldens`. Plain `GREEN` otherwise. A MANUAL row adds "; manual steps still open".
- **A failed stage** prints the tail of its log; the logs are `temp/local-gate/<run>/logs/`, the per-run golden logs in `goldens/`, pluginval's in `pluginval/`, the journeys' (with each page log) in `journeys/`, the ctest JUnit in `ctest-junit.xml`. `ctest --test-dir <build> -R <name> --output-on-failure` runs a failed test again (use the sandbox of the run: `HOME`, `GEARMULATOR_DATA_ROOT`, `GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1`).
- **Under load**, timing-sensitive firmware tests (`mmLcdEditPagesFirmwareTest` has a 180 s limit, `mmDigiproFirmwareTest` 600 s; the MIDI timing and idle-scheduler ones) and the core-capacity check ("produced no finite audible output": the machine had not started playing when the capture ended) fail; the gate runs `-j4` and does not retry. Read a timing failure against the "busy" note and `uptime` before calling it a regression, and run the stage again alone: `MDMM_GATE_ONLY="6b" scripts/mdmm-local-gate.sh --build temp/local-gate/build`.

## Goldens

The playing goldens are the reference for "this build plays the same machine": for each scenario (`md-busy`, `md-factory`, `md-song`, `mm-a01`, `mm-busy`, `mm-song`; the song ones run headless, set up by SysEx) the file holds the FNV-1a hashes of the audio of the stopped and playing phases, of main RAM, SRAM, loader RAM and patch RAM, and of every MIDI byte out. The key of an entry is `<ROM fingerprint>/<scenario>/<stereo or all>/speedups-<on or off>/<seconds>s`. Each speed-ups position is compared with its own entry; `on` and `off` hold the same hashes (every speed-up is bit-exact). L3, the opt-in exact serial-port timing of the Machinedrum (`GEARMULATOR_MDMM_EXACT_ESSI=1`, doc/md_mm_performance_diagnostics.md "Switches for testers"), changes the sound, so its runs have entries of their own, `speedups-on+exact-essi` (Machinedrum scenarios only); the gate compares them when the file has them, and a missing one is not an error.

- **Compare** (the default): every scenario in the file, both outputs modes, both speed-up settings. A missing entry is red, so a golden cannot be forgotten by deleting it.
- **A golden changes only on purpose** (an emulator fix that changes what the machine does; a new ROM). Run `scripts/mdmm-local-gate.sh --record-goldens` (quietly: nothing else heavy running). It records, then compares the file against fresh runs, and prints a banner and a reminder: the goldens changed. Read `git diff source/elektron/md/mdLibTest/goldens/mdmm-goldens.json`, say why each changed number is right in the commit message, and get **Radek's sign-off before the file is committed**. A change to the goldens is never part of a "fix the gate" commit.
- **Not tied to the build flags.** The 24 recorded entries compare equal in the full ThinLTO tree, in the `--quick` ThinLTO tree without plug-ins, in a plain non-LTO tree and with a tool built against `main` (checked 2026-10-09), and recording from scratch into an empty file reproduced all 24 (`--record-goldens` was tried against a scratch copy via `MDMM_GATE_GOLDENS`, which is there for testing the gate).

## Updater end to end (manual)

The updater's actions (Update, then the download, the check and the swap or the Installer; Restart now; Don't check) are
the JUCE side in `mdJucePlugin/mdUpdater.*`. Its pure parts (versions, `latest.json`, the schedule, SHA-256, the ed25519
check) are in ctest (`mdmmUpdateTest`), and the release scripts that sign and write `latest.json` too
(`mdmmReleaseScriptsTest`); there is no headless rig for curl, the download and the swap, so a person has to press the
buttons. The gate cannot serve a test manifest: the app asks only `https://mdmm.dev/latest.json` and downloads only from
`https://github.com/radekdymacz/mdmm/releases/download/…` (curl runs with `-q`, HTTPS only; no setting or
variable changes that), and it offers an update only when the manifest is **newer than itself**. So the test needs a copy of
the candidate with an older version number: everything is the candidate's code but one line.

```sh
scripts/local-gate/updater-manual.sh check        # what can be tested now: the key, the published manifest (a GET of mdmm.dev/latest.json)
scripts/local-gate/updater-manual.sh prepare      # a copy of this tree in temp/local-gate/updater/src with version 0.3.0, standalones built (about 4 minutes)
scripts/local-gate/updater-manual.sh run md       # then: run mm. The standalone, sandboxed, in front; it prints the steps
```

The steps (the same text `run` prints): right-click the page, Updates, Check for Updates Now; the banner "Update
available" with Update, Later and Don't check; press Update; watch "Downloading … N %" reach 100; on macOS the app
verifies size, SHA-256 and signature, the system Installer opens the `.pkg` and the banner says "Quit the editor, then
click Install": do **not** click Install, close the Installer, press Quit. Also: Later hides the banner, Cancel stops the
download, Don't check unticks Check Daily. Windows and Linux (staged swap and Restart now) need such a machine; those
builds are "not tested".

**Update can be exercised only when** `source/elektron/md/mdmmUpdate/updateKey.h` holds the real public key (not the
placeholder) **and** the published `latest.json` carries signatures made with the matching private key
(`MDMM_UPDATE_SIGNING_KEY`). `check` says which of the two is missing; while either is, the banner offers Download (the
site) and never Update, which is all a person can see. The gate marks the stage MANUAL and prints that state; record the
outcome of the manual run in the release review.

## Known cannot-run-here

Named in the summary (`Known cannot-run-here`), not counted as skipped:

| Test | Why |
|---|---|
| `*_AU_Validate` | needs the CPack zip of an AU package; auval runs on the built AU in 7b |
| `test_*_VST` | VST2 plug-in tests; the VST2 build is off |
| `mdSysexLifecycleTest` | needs `nvram/mm-factory-live3-be.bin` (the Monomachine factory patch RAM), which this Mac does not have; runs in 2b the moment the file is there (`MDMM_GATE_MM_PATCH`) |
| tests marked DISABLED in CMake | ctest does not run them; listed in the stage's notes |
| the x86_64 slice, signing, notarisation, Gatekeeper | not part of a local build ([SIGNING.md](SIGNING.md)) |

The journeys that check a canvas (the sampler ones) need their page drawing. `--background` keeps WebKit drawing while the window
is covered, but a sleeping display still stops it: keep the Mac awake for the 8 to 16 minutes. A journey skipped for that reason
makes 7d red; others skipped for a missing capability are listed and tolerated.

**The soak (6c) does not run an edit stream.** The page's edits reach the machine through the editor's desk, which exists only with
an editor and its web page; `latency_host` and `pluginTester` host the real plug-in headless, without one. `mdDeskFirmwareTest`
and the rt-check (6a) do run the desk's edit actions, but on the emulator directly, with no plug-in processor, so there is no
performance capture of them. What the soak adds to 6a is the plug-in's own capture over ten minutes of busy MIDI play; the cost of the
edit actions is 6a's (retired instructions per block, 100 ms hot a second at most), and the audio thread during real page edits is the
capture the journeys record (7d, quoted, not judged). The capture itself stops at 10 minutes or 8 MiB. On the Machinedrum the pad
triggers are recorded as panel events (24,500 of them, 94 % of the file), so its capture reached 8 MiB after 524 of the 600 seconds
(196,388 callbacks); the Monomachine's reached the 10 minutes (225,000). The stage asks for at least 80 % of the run (8 minutes)
and names how each capture ended.
