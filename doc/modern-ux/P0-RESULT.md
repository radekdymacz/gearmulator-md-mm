# P0 result: screen-native UI proof (MD/MM)

Branch `p0/modern-ux-proof`, 2026-09-27, Apple silicon (arm64), Xcode 26.2 SDK.
This answers the P0 exit criteria in [code-review.md](code-review.md) §6.

## Verdict

| # | Question | Verdict |
|---|---|---|
| 1 | Build MD/MM on this Mac (arm64) | **GO** |
| 2 | Firmware available | **MD: GO. MM: BLOCKED.** No MM ROM on this Mac |
| 3a | MD live SysEx pattern edit while playing | **GO** |
| 3b | MM live edit and the WAITING receive gate | **NOT TESTED** (no ROM). Analysis below |
| 4 | Playhead (current step) | **GO (MD).** A RAM byte, verified. LEDs do not work |
| 5 | Web view editor in the plugin | **GO WITH CAVEATS.** Works, but the JUCE fork is 7.0.10, not 8 |

**Overall: GO for MD. MM stays open until Radek supplies the MM ROM.**

---

## 1. Build: GO

- Submodules were initialised with `git submodule update --init --recursive --depth 1`. No unshallowing was needed.
- Configure (Ninja, arm64 only, Elektron synths only, Standalone + VST3, tests on):
  ```sh
  export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.2.sdk
  cmake -S . -B temp/cmake_p0 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT=$SDKROOT -DCMAKE_C_COMPILER=$(xcrun -f clang) -DCMAKE_CXX_COMPILER=$(xcrun -f clang++) \
    -Dgearmulator_SYNTH_{OSIRUS,OSTIRUS,VAVRA,XENIA,NODALRED2X,JE8086}=OFF \
    -Dgearmulator_BUILD_JUCEPLUGIN_{CLAP,AU,VST2}=OFF -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON -DBUILD_TESTING=ON
  ```
- MD + MM Standalone, MD VST3 and a test build in **59 s wall** (403 s CPU, 12 cores). The full tree also builds.
- **Gotcha:** the default `/usr/bin/cc` picks the CommandLineTools `MacOSX27.0.sdk`. Its `libSystem.tbd` lists `arm64e.x1` and the linker rejects it ("tapi error: malformed file"). Pointing at the Xcode SDK and clang (above) fixes it.
- `ctest -L UnitTest`: 42 of 44 pass.
  - `synthLibMidiClockTimingTest` fails with "invalid host position generated a spurious transport edge". `synthLibAudioTest` is skipped because it depends on it.
  - Both are in `synthLib/`, which this branch does not touch. My guess is `-Ofast` (fast-math NaN checks) on this toolchain.
  - This matters later for DAW sync. File it upstream.

## 2. Firmware: MD found, MM missing

- **MD OS 1.63:** `elektron_sps1-1uw_os1.63.bin`, SHA-1 `a872a2f3…cadc1`, the pinned image. There are two copies:
  - `~/Downloads/`
  - `~/Documents/Gearmulator Preview/Machinedrum/roms/`, the folder the plugin searches
- **MM OS 1.32b: not on the Mac.** I searched every 8 MiB file under `~`, `/Library/Application Support` and `/Users/Shared`. Nothing was downloaded.

**What Radek must provide (from his own Monomachine):**
1. `elektron_sfx6-60_os1.32b.bin`: 8 MiB (8,388,608 bytes), SHA-1 `11a37460a5f47fd1a4d911414288690e6e7da605`.
   - Put it in `~/Documents/Gearmulator Preview/Monomachine/roms/`.
   - Any `.bin` of the right size and fingerprint is found by `RomLoader::findROM`.
2. Optional but needed by the existing MM firmware harnesses (`mmSysexExportFirmwareTest`, `mdUserSysexFirmwareTest mm`): a 1 MiB MM patch-RAM image.
   - The plugin looks for `<data folder>/nvram/mm-factory-live3-be.bin`.
   - Without it the MM boots with empty user data.

## 3a. MD SysEx round trip while playing: GO

### Harness

`source/elektron/md/mdLibTest/patternLiveEditFirmwareTest.cpp` (target `mdPatternLiveEditFirmwareTest <rom> <outdir>`) works as follows:
- Boots MD 1.63 headless and plays pattern A01 on its internal clock: 125 BPM, 32 steps, started by a panel PLAY press.
- Sends pattern dumps mid-bar and renders audio to WAV.
- Runs the same timeline six times:

| Run | What it sends |
|---|---|
| A | nothing |
| A2 | nothing, rendered again (determinism check) |
| R | a 9-byte dump request only |
| B | the unchanged 5,410-byte pattern re-sent |
| C | edits: add a trig, then a p-lock, then length 32→16 |
| L | track-solo p-lock proof |

The emulator is **bit-exact deterministic** (A vs A2 max diff = 0). So "B minus A" isolates the effect of receiving a dump, and "C minus B" isolates the edit itself.

### Codec

`source/elektron/md/elektronData/` (`elektronData` library, no dependencies):
- A pure MD 1.63 pattern value with `decode`/`encode`, `withTrig` and `withLock`.
- **Byte-exact on real firmware dumps.** Every dump the firmware produced round-trips: `encode(decode(x)) == x`.
- The layout was derived by diffing dumps before and after panel edits: trig bits, lock mask, lock-row values, and the 64-step extension block. Offsets are documented in `mdPattern.h`.
- **The firmware sorts lock rows by (track, parameter)**, whatever order the locks were entered in. Unused rows are zero, and 0xFF means no lock on that step. The codec reproduces this.
- Unit test: `elektronDataTest` (ctest, UnitTest label).

### Numbers (emulated time, MD 1.63, 5,410-byte extended pattern)

| Measure | Result |
|---|---|
| Firmware UART drains the whole dump | **768 frames = 17.4 ms** (about 100× DIN speed: emulated UART1 has no baud pacing) |
| Dump request → complete reply | **1,088 frames = 24.7 ms** |
| Initial request → reply before PLAY | 768 frames |
| Firmware read-back equals what was sent | **yes, 6 of 6** |
| MIDI RX overflow | 0 |
| Takes effect without stop or reload | **yes.** Sent at step 5.5, the new kick is heard at step 9 of the *same* pass (C vs B first differs at loop 2 step 9.00). No SET STATUS or load needed |
| p-lock audible | **yes.** Solo track 1, step-17 kick with PTCH locked to 0: zero crossings 17 → 13, peak 0.19 → 0.33; step 1 unchanged |
| Length 32 → 16 sent at step 21.5 | the current pass finishes to 32, then the pattern loops at 16 |
| Dropout or discontinuity | **none.** Largest sample-to-sample jump: A 0.50, B 0.47, C 0.48; no gaps |
| Sequencer timing | at most **±64 frames (1.45 ms)** on 14 of 164 step edges for B. A 9-byte request (R) gives the same: ±64 frames on 20 of 164. Individual voices moved by multiples of 32 frames |

**What surprised me:**
- Sequencer timing is sensitive to *any* incoming MIDI. The shift is the same whether you send 9 bytes or 5,410. So a dump costs nothing extra over a CC, but the whole transport has about 1.5 ms of jitter.
- At the real 31.25 kbaud, one pattern would take about 1.7 s. The emulator does not pace it: 17 ms.
- Wall clock: the harness renders at about 0.9× realtime (22 s for 19.7 s of audio) with 64-frame blocks and RAM probing. Watch CPU headroom.

## 3b. MM WAITING gate: not tested (no ROM)

What the code tells us:
- MM dump **requests** work from any screen. `mmSysexExportFirmwareTest` exports kit, pattern, song and global dumps while the machine is on its normal screens.
- MM **receive** needs GLOBAL > FILE > SYSEX RECV = "WAITING" (`doc/elektron_md_mm_sysex.md`).
  - The fork's own file sender (`mdturbomidi.cpp`, `WaitingForReceiveMode`) expects the user to put the machine there.
  - The test driver `enterMmReceive` (`sysexPanelDriver.h`) navigates there in about 20 panel taps, roughly 4 s of emulated time with that driver's timing. It leaves the display on the receive screen.

Workarounds to test once the ROM is here, cheapest first:
1. **Panel macro:** drive the panel to WAITING, send, then EXIT back. The same harness with an MM plan measures the cost and checks whether playback survives. Expect visible LCD churn and seconds of latency: a batch commit, not live editing.
2. **Patch-RAM write:** MM patterns live in battery patch RAM, which `Microcontroller::replacePatchRam` and `read8`/`write8` can reach. Find the playing pattern's slot by diffing, as done for MD here. Whether the playing edit buffer is a separate copy in main RAM is unknown, so a reload (SET STATUS pattern) may be needed.
3. **Firmware hook:** a fingerprint-gated patch that skips the WAITING check. There is precedent: `mdrampacking.h`, and upstream PR #43.

## 4. Playhead: GO for MD (RAM), NO-GO for LEDs

- **Step LEDs are not a playhead.** In normal play mode the 16 LEDs show which *tracks* fire on the current step (for example 1, 5, 9 and 13 on step 1), not the position. There is a running light only in grid-record mode.
- **RAM:** the harness scanned 1 MiB of main RAM plus 8 KiB of SRAM at 72 mid-step instants. It found **`0x00261aa7` = current step, 0-based, wrapping at the pattern length**. On MD 1.63 it is exact and deterministic because of the fingerprint lock.
  - It was cross-checked in the edit run: after the length changed to 16 it counts 0..15.
  - Other candidates: `0x284c5d` (step mod 16, the page step) and `0x284c61` (mod 128).
- The plugin reads it at 30 Hz under `withDeviceLocked`: `StudioLink::readPlayhead`, gated to local MD 1.63 only. That is a UI-thread device lock. Before shipping, publish the byte from the audio thread through an atomic.
- Host PPQ plus pattern length would also work under DAW sync, but the RAM byte is simpler and also covers internal clock.

## 5. Web view editor: GO WITH CAVEATS

- **The JUCE fork is 7.0.10, not 8.** `WebBrowserComponent` exists (WKWebView on macOS), but there is no `withNativeFunction`, event bridge or resource provider. The bridge used instead:
  - page → C++: navigation to `gmbridge://cmd/...`, cancelled in `pageAboutToLoad`
  - C++ → page: `goToURL("javascript:...")`, which uses `evaluateJavaScript`
  - Quirk: `goToURL` remembers the last "URL", so a re-show replays the last script. It is harmless here.
- Enabled in `source/juce.cmake` as option `gearmulator_JUCE_WEB_BROWSER`: ON on macOS and Windows, OFF on Linux, which would need webkit2gtk and `NEEDS_WEB_BROWSER`.
- **Skin `mdStudio`** (MD only) makes `PluginEditorState::createEditor` choose `StudioEditor`. The existing panel editor is unchanged.
  - The editor is a 28 dp RML header (right-click it for skins and settings) with a web view below.
  - The page (`skins/mdStudio/mdStudio.html`) shows 16 tracks × pattern length. Trigs are orange, trigs with locks yellow, and the playhead is outlined.
  - Clicking a cell sends `withTrig` as a dump. The grid redraws only from the firmware's read-back, so the page shows what the firmware actually stored.
- **The code is kept in separate layers:**

| Layer | Where | Depends on |
|---|---|---|
| Codec | `elektronData` | standard library only |
| Transport | `mdStudioLink.*` | `evDeviceSysex` (new, additive, on `mdController`) and `processor.addMidiEvent`, never raw bytes to the view |
| View | `mdStudioEditor.*` + HTML | JSON only |

- **Measured in the real standalone** (log in `~/Library/Caches/Gearmulator MD/gearmulator-mdStudio.log`; `GEARMULATOR_MDSTUDIO_SELFTEST=1` makes the page click cells itself):
  - The page loads.
  - `ready` → status → pattern arrives about 2 s after launch (the firmware is still booting).
  - Three toggles went click → firmware read-back → redraw in **53, 74 and 60 ms wall**. The dump plus read-back travel through the plugin MIDI path and the controller timer.
- **Caveats:**
  - Every read-back is a 5.4 KB SysEx that also goes to the host MIDI out.
  - Refresh is manual, plus automatic on open. Edits made on the virtual panel are not pulled automatically.
  - `evDeviceSysex` fires on the controller's drain thread; `StudioLink` marshals to the message thread.
  - I could not take a screenshot of the window: macOS refused per-window capture.

## Recommended next step

1. **Radek:** put the MM 1.32b ROM (and ideally an MM patch-RAM image) in `~/Documents/Gearmulator Preview/Monomachine/`. Then re-run P0 for MM: add an MM codec, a WAITING-macro plan and a patch-RAM diff to the same harness. MM decides whether the "dump" path is enough or whether P6's RAM writes move forward.
2. **MD is ready for P1/P2:**
   - Extend `elektronData` to the MD kit (machine types, LFO routing) and song.
   - Round-trip all 128 patterns from a real backup.
   - Move the playhead read to an audio-thread atomic.
   - Grow the studio page into the read-only global grid.
3. **Decide on JUCE:** stay on 7 with the URL bridge (works, GPLv3) or move the fork to JUCE 8 for the native-function bridge (AGPLv3 and a large merge). The URL bridge is good enough through P3.
4. Upstream the `synthLibMidiClockTimingTest` failure (fast-math on Apple clang 17).

## Files

- `source/elektron/md/elektronData/`: codec + `elektronDataTest`
- `source/elektron/md/mdLibTest/patternLiveEditFirmwareTest.cpp`: firmware harness (manual; needs ROM)
- `source/elektron/md/mdJucePlugin/mdStudioLink.*`, `mdStudioEditor.*`, `skins/mdStudio/*`: plugin proof
- `source/juce.cmake`, `mdController.*`, `mdProductSkins.h`, `mdProductSkinPolicy.h`, `mdPluginEditorState.cpp`: small hooks
