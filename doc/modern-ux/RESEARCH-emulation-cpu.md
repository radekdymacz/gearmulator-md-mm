# Research: lowering the emulation CPU cost (Machinedrum and Monomachine)

Date: 2026-10-08. Research only: no code or build in the repo changed. This is step 1 of
[FUTURE-emulation-ideas.md](FUTURE-emulation-ideas.md) §6.
Machine: Apple M4 Pro, 12 cores, macOS 27. Firmware: Machinedrum (MD) OS 1.63, Monomachine (MM) OS 1.32B.
Words used below:
- **JIT** (just-in-time compiler): it turns DSP code into host machine code while the plug-in runs.
- **Block**: one piece of JIT code. After each block, a small dispatcher (the **trampoline**) checks the peripherals and starts the next block.
- **Peripheral due**: the moment the emulator must update a DSP peripheral (serial port, DMA, host port, timers).
- **Bit-exact**: the output is the same, bit for bit, as before the change.
- **PGO** (profile-guided optimisation): the compiler uses a recorded run to arrange the code.
- **UC**: the ColdFire microcontroller that runs the MD/MM main firmware.

## 1. The answer

- **Half is not shown.** The known techniques cut the emulation CPU by about one third. Half is possible only at the optimistic end, and mostly when the machine is stopped.
- The DSPs do not compute most of the time. They wait in poll loops and NOP (no-operation) loops. The firmware never uses WAIT, so the emulator runs every wait cycle. This is the largest lever.
- The second lever is overhead around the chips: the UC loop, the peripheral checks and the compiler settings.
- The combination: bit-exact quick wins (step 1), then exact MD serial-port timing plus a DSP idle skip (step 2), then PGO (step 3). The table below has the arithmetic.
- Confidence: high for step 1 (four of its five levers were measured bit-exact). Medium for step 2 (prototypes were measured). Low for the codegen pass.
- What is left after all levers: real DSP maths, the link between the two DSPs (an event every 96 DSP cycles), and real ColdFire work. To go past about 40 % needs a new idea for the link events or a much better JIT. No evidence exists for either yet.

| Point in the plan | Levers | MD stopped | MD playing | MM stopped | MM playing |
|---|---|---|---|---|---|
| Today, shipped ThinLTO build (% of one core, quiet machine) | – | 41.8 | 44.4 | 54.9 | 54.1 |
| Today, bench build without ThinLTO (% of one core, quiet) | – | 44.5 | 48.0 | 60.3 | 60.5 |
| After step 1 (% of shipped cost) | L1, L2, L5, L11, L13a | 87 % | 87 % | 86.5 % | 86 % |
| After step 2 (% of shipped cost) | + L3, L4, L7, L9, L10 | 63 % | 71 % | 67 % | 75.5 % |
| After step 3 (% of shipped cost) | + L6 (×0.92), L13b (×0.98) | 57 % | 64 % | 60 % | 68 % |
| After step 3, % of one core | – | 23.9 | 28.5 | 33.2 | 36.8 |
| Same, as % of the bench build | – | 54 % | 59 % | 55 % | 61 % |
| Optimistic end of the ranges | all | 45 % | 52 % | 48 % | 58 % |
| Pessimistic end of the ranges | all | 67 % | 76 % | 69 % | 77 % |
| Target for "half" (% of one core, shipped) | – | 20.9 | 22.2 | 27.5 | 27.1 |

Basis: per-lever central values from §3 (after overlap with earlier levers), added within a step because their cost pools barely overlap after adjustment; PGO and codegen applied as factors. Baselines from the PGO verifier's quiet-machine ABBA runs on current main. The 52-69 % numbers measured earlier were on a loaded machine (about 15 % inflated). ThinLTO is already shipped, so its 6-10 % is not counted as a gain.

## 2. What costs what today

### 2.1 Where the host time goes

Sampled profile (macOS `sample` on the `mdCpuBenchTest` / `mmCpuBenchTest` thread; share of `md::Hardware::advance()` inclusive time):

| Bucket | Main items (self %, MD) | MD | MM |
|---|---|---|---|
| JIT code (self) | generated code of both DSPs | 38.8 | 35.8 |
| Peripheral tick (self) | EsxiClock::exec 5.7, dspExecPeripherals 5.5, HDI08::exec 3.3, DmaChannel::execTransfer 2.0, Peripherals56303::exec 1.8, Essi TX+RX 2.4, Dma::trigger 1.1, Timers::exec 1.1 | ~27 | execPeripherals 20.5 inclusive |
| Scheduler glue (self) | schedStep 6.2, schedCatchUpDspToDsp 4.2, processUC 2.1, ownsMidiWire 2.1, pumpScheduledMidi 0.5 | ~15 | schedStep 4.6, processUC 4.4, ownsMidiWire 2.1 |
| ColdFire | m68k_execute_one 10 inclusive, Sim::exec 1.3, memory decode 1.5 | ~13 | processUC 39.7 inclusive (Microcontroller::exec 32.5, m68k_execute_one 23.6) |

Two corrections found by the verifiers:
- Most of "schedStep self" plus "ownsMidiWire" is one loop: the ColdFire idle skip re-checks its inputs once per skipped instruction (mdhardware.cpp:1404-1413). Disassembly puts it at 6.9 % of MD and 3.3 % of MM. That is lever L1.
- The serial-port TX callback runs the other DSP inside it. `Essi::execTX` is 23.5 % inclusive on MD, of which `schedCatchUpDspToDsp` is 17.4 %.

Thread split from a second, independent sampler (% of emulation-thread samples):

| ROM, phase | DSP1 (mixer) | DSP2 (voices) | UC | Scheduler | Other |
|---|---|---|---|---|---|
| MD stopped | 43.5 | 34.0 | 14.7 | 6.8 | 1.0 |
| MD playing | 38.2 | 34.0 | 20.3 | 6.6 | 0.9 |
| MM stopped | 29.8 | 34.0 | 29.4 | 6.0 | 0.8 |
| MM playing | 27.8 | 32.3 | 33.0 | 6.0 | 0.9 |

Stopped and playing differ by only about 3 points. The cost is fixed per emulated cycle, not per note.

### 2.2 Idle versus work in each DSP (exact guest-cycle counts)

| Measure | MD stopped | MD playing | MM stopped | MM playing |
|---|---|---|---|---|
| DSP1 cycles spent idle | 30.6 % | 30.4 % | 45.7 % | 40.7 % |
| DSP2 cycles spent idle | 97.5 % | 44.8 % | 45.5 % | 42.6 % |
| Host time at idle code (% of thread) | 43 | 21 | 32 | 18 |
| Lower bound of an idle-skip saving (% of thread) | 27.9 | 14.9 | 23.4 | 12.5 |

The hot idle code:

| Machine | DSP | Address | What it does | Share of that DSP's cycles |
|---|---|---|---|---|
| MD | DSP1 | p:3c-43 | polls DMA0 address DDR0 (codec buffer); cut into 5 tiny blocks | 19.8 % |
| MD | DSP1 | p:260 | 15 NOPs inside `do #$20` (padding) | 10.4 % |
| MD | DSP2 | p:10008f-9a | silent-voice stub `do #$32 {nop; nop}`; body p:100095 | 74.4 % stopped |
| MD | DSP2 | p:bb-bf | polls Port C bit 1 (block sync from DSP1) | 22.9 % stopped, 30.1 % playing |
| MD | DSP2 | p:cf | `jset #23,x:DCR0,*` (waits for DMA0) | 14.7 % playing |
| MM | both | p:100164 | silent-voice stub `do #$c80 {nop}` | 26.0 % stopped |
| MM | DSP1 | p:17f, p:87-91 | polls DMA1 DSR1 | 19.7 % stopped, 40.7 % playing |
| MM | DSP2 | p:18d, p:195 | polls DDR0 and Port C | 19.5 % stopped, 42.6 % playing |

The hottest ~200-byte JIT region in the first profile (18 % of JIT time) is block p:100095, the MD voice stub.

| Count per codec frame (2304 DSP cycles) | MD DSP1 | MD DSP2 (stop / play) | MM DSP1 (stop / play) | MM DSP2 (stop / play) |
|---|---|---|---|---|
| Blocks run | 299 | 436 / 392 | 314 / 262 | 286 / 215 |
| Guest cycles per block | 7.7 | 5.3 / 5.9 | 7.3 / 8.8 | 8.1 / 10.7 |
| Peripheral dues | 53 | 81 / 36 | 29-30 | 35-36 |
| Interrupts | 0.23-0.26 | 0.04 | 0.22-0.24 | 0.22-0.24 |
| Blocks that start below P:$100 | 64 % | 45 % / 85 % | not counted | ~2 % |

| Other measured facts | Value |
|---|---|
| Host time per block, MD / MM | ~11.5 ns / ~15 ns (JIT plus trampoline ~5.5 / ~8.9 ns) |
| Host instructions per machine frame, MD stopped / playing | 210.6 k / 223.7 k |
| UC real instructions per frame, MD / MM (stop / play) | 80 / 117 and 217 / 237 |
| UC cycles skipped by the BRA.B -2 idle skip, MD / MM | 84 % / 76 % and 55 % / 51 % |
| DSP WAIT instructions executed | 0 |

### 2.3 How it was measured

- **Profiles:** macOS `sample` on the bench thread (coordinator), plus a custom `thread_suspend` sampler at ~2.6 kHz that reads the JIT register x20 to tell the DSPs apart. 52,286 samples (MD) and 67,220 (MM), 20 s per phase.
- **Exact counts:** a hook on each DSP's per-block peripheral function counted cycles and instructions per block start address, over 8 s per phase. Counts with and without the hook were equal.
- **A/B tests:** scratch copies of the sources linked against copies of the `temp/cmake_review` Release libraries (arm64, -O3/-Ofast, no ThinLTO unless stated). Nothing in the repo or in `temp/cmake_review` was changed.
- **Load-independent metrics:** host instructions and cycles per frame from `thread_selfcounts`, and retired instructions from `/usr/bin/time -l`.
- **Bit-exactness:** an FNV hash of the codec output, RAM hashes, and for L4 a hash of DSP state at every peripheral due (more than 100 M events per run).
- **Workloads:** MD busy pattern (every track on 16ths, as in `mdCpuBenchTest`); MM factory A01 playing.

### 2.4 Limits of this research

- The machine was loaded during most runs (load average 4-118, other agents and Ableton). CPU % figures are noisy. Instruction counts are reliable.
- Only two firmwares and two workloads. SysEx dumps, kit loads, UW recording, TurboMIDI and song mode were not run.
- Not measured: Windows, Linux, Intel Macs, a real plugin host callback, and quiet-machine wall time for most levers.
- Prototypes, not products: L4 used hard-coded loop addresses; L7 used a vector whitelist (unsafe).
- Not explained: why janne808's profile shows 15 % JIT time where ours shows 38.8 %; Joe's MD callback overruns with PR #20.
- Not read: Discord, Joe's private notes, Reddit.

## 3. The levers, ranked

Gains are % of today's emulation-thread CPU, after overlap with the levers before it in the plan (§5). "Alone" means without the other levers.

| Rank | Lever | Verdict | MD stop | MD play | MM stop | MM play | Bit-exact | Effort |
|---|---|---|---|---|---|---|---|---|
| 1 | L4 DSP idle-loop fast-forward | confirmed (prototype) | 12 | 6 | 15 | 6 | yes, proven | days to weeks |
| 2 | L6 PGO in release builds | shipped (perf/pgo, "L6 measured") | 9 today, ×0.92 at the end; measured ×0.924 after step 2 | 7 | 5.5 | 5 | yes, goldens 24 + 6 | days |
| 3 | L1 UC idle skip: check inputs once | confirmed (measured) | 5 | 5 | 3 | 3 | yes, measured | hours |
| 4 | L3 MD exact serial-port deadlines | confirmed, smaller | 6 | 3 | 0 | 0 | no, audio changes once | hours plus days of tests |
| 5 | L5 lean ColdFire loop | weakened | 3 | 3 | 6 | 6 | by design | days |
| 6 | L13a Joe's DSP PR #20 | weakened (overlaps L4) | 1.5 (9 alone) | 2 (3.6 alone) | 1.5 (14 alone) | 1.5 (3 alone) | yes, measured | days |
| 7 | L9 per-peripheral gating | weakened | 3 | 2.5 | 2 | 2 | by design | days |
| 8 | L10 ColdFire HI08 poll skip | weakened | 1 | 2 | 2.5 | 2.5 | by design | days |
| 9 | L11 ColdFire memory fast lane | confirmed (measured) | 1.2 | 1.2 | 2.5 | 3 | yes, measured | hours |
| 10 | L2 inline due test, catch-ups via execUntilCycles | weakened | 2 | 2 | 0.5 | 0.5 | yes, measured | hours to days |
| 11 | L7 long JIT blocks below P:$100 | weakened after L4 | 2 (7.5 alone) | 2 (7.5 alone) | 0 | 0 | no | days |
| 12 | L13b aarch64 codegen pass | weakened | ~2 | ~2 | ~2 | ~2 | must be | months |

### 3.1 L4 — DSP idle-loop fast-forward (one verifier confirmed, one weakened the size)

- **What.** Find poll loops and NOP-only DO loops. Skip whole loop turns in one step, up to the next peripheral due, the scheduler slice end or the catch-up target. Land on a turn boundary, so all checks run at the same points. This is the DSP version of the ColdFire BRA.B -2 skip.
- **Where.** dsp56kEmu `jitops_jmp.cpp:49-135` (today it only skips ESSI TFS/RFS self-loops, which these firmwares do not use); `jitblock.cpp` getInfo/emit; `dsp.h:477-481` fastForward; `peripherals.h:117-128`; `mdhardware.cpp:1460, 1546, 1615` (publish the slice and catch-up targets).
- **Measured.** A C++ hook prototype. Retired instructions, mixed workload: MD −8.1 % on top of L3 (−4.7 % without L3), MM −7.8 %. Time: MD about −13 % stopped and −6 % playing; MM about −15 % and −6 %. The DSP state hash at every due event is the same with and without the skip.
- **Why L3 first.** Without L3, the next MD due is often 0-4 instructions away, so the skip rarely fits. janne808 rejected a NOP skip for this reason (PR #88 doc). With L3, NOP skips double.
- **Risk.** Medium, in detection. Port D comes from the instruction counter (`mdhardware.cpp:522-529`). A Port C read can release a pending edge (`mdhardware.cpp:938-953`). The P:$10xxxx stubs are loaded and patched at run time (`mdhardware.cpp:1653-1671`), so check the opcode words, not only the address. A hook on every block wastes most of the gain: put the check at loop heads only.
- **Needs.** L3 for the MD gain; L2's deadline accessors; a list of "pure" registers declared by mdLib.
- **Cheapest experiment.** Emit the check only at the 8 known loop heads (MD p:3c, p:bb, p:cf, p:100095; MM p:17f, p:18d, p:195, p:100164). Measure long-minus-short runs. Keep the lever at ≥ 11 % of the mixed workload; cut it to 8 % if it stays at the hook level.

### 3.2 L6 — ship PGO in the release builds (one confirmed, one weakened)

- **What.** Record a profile per architecture on MD and MM, stopped and playing. Build on top of the existing ThinLTO.
- **Where.** `source/elektron/md/optimization.cmake:153-196`; `scripts/macos/build_mdmm.sh:159-163`; `.github/workflows/mdmm-editors-macos.yml:81-83` (PGO is pinned to `none`); the release reuses CI artifacts (`mdmm-editors-release.yml:3-10`).
- **Measured on current main**, quiet machine, 4 ABBA rounds, % of one core: MD 41.8 → 38.0 stopped and 44.4 → 40.3 playing; MM 54.9 → 49.4 and 54.1 → 48.1. A profile trained only on MD kept 86 % of the MM gain.
- **Correction.** The "101 % → 86 %" in `doc/mdmm-apple-optimization.md:119-121` is ThinLTO without PGO. It is already shipped.
- **Risk.** A universal macOS binary cannot use PGO (`optimization.cmake:155-158`), so the package must be split per architecture. Each mdLib change makes the profile stale, and the build then fails (`:193-194`). Training needs ROMs, so CI cannot do it. -Ofast float code may change bits: compare renders.
- **After the other levers.** PGO helps only C++, not JIT code, so the factor falls to about 0.91-0.94.
- **Cheapest experiment.** Done. Next: repeat after step 1, run `scripts/macos/check_mdmm_core_capacity.py` on an arm64 PGO build, diff the audio.

#### L6 measured (2026-10-10, shipped on branch perf/pgo)

- **Design.** One profile, trained on this Mac (arm64) with the ROMs, committed as LLVM's text profile
  (`source/elektron/md/pgo/mdmm-macos.proftext`, 9184 functions, 1.5 MB, 130 KB compressed; `mdmm-macos.json` records
  the commit, the git tree of each profiled folder, the compiler and the workload). The build converts it with its
  own `llvm-profdata` (text is the one format every version reads, so a profile from Xcode 26 works on CI's Xcode 16)
  and applies it to both slices of the universal build (`GEARMULATOR_MDMM_APPLE_PGO_MODE=committed`, the default of
  `build_mdmm.sh`, CI and the local gate). This replaces the per-architecture split the risk above called for: clang
  matches counts per function hash, so the x86_64 slice uses every function it shares with arm64 (in a universal
  build only 15 functions, the architecture-specific ones, reported mismatched) and compiles the rest as before.
  Stale (a profiled folder changed since training) only warns: changed functions get no data. Missing: warns, ThinLTO
  only. CI never trains, so it never needs a ROM. Rejected: a fixture-free training workload (the hot paths are the
  firmware's own loops through the JIT, the ColdFire core and the scheduler; a synthetic program would train other
  branches), and the indexed `.profdata` (its format version is the compiler's, and Apple's clang refuses newer ones).
- **No firmware in it.** `scripts/macos/mdmm_pgo_profile.py check` parses every line as profile data (a symbol name,
  a decimal number, a comment or header; nothing else is accepted), and scans the file for any run of 31 bytes or more
  of either ROM (16-byte chunks of the ROM indexed, every offset of the profile looked up): none. CI repeats the parse
  on every build; the training script runs both before it writes the file. Local function names carry only the file's
  base name (`audio.cpp:...`), so no path of this Mac is in it either.
- **libc++.** A third of the counts are in small libc++ functions whose names carry libc++'s version
  (`B8ne200100`). The build renames that tag to its own SDK's version, so CI's older libc++ keeps them; a function
  whose body changed is still rejected by its hash.
- **Workload.** `mdmmPerfGateTest` for every scenario (md-busy, md-factory, md-song, mm-a01, mm-busy, mm-song), all
  outputs, 8 s stopped then 8 s playing, speed-ups as shipped: about 75 s of six parallel runs.
- **Bit-exact.** The committed goldens, all 24 plus the 6 exact-ESSI entries, PASS on the arm64 PGO build. The x86_64
  slice is built with the profile but cannot be run on this Mac (no Rosetta); CI's start tests and pluginval run on
  the universal package.
- **Gain.** Release configuration (ThinLTO, DSP libraries), arm64, `mdmmPerfGateTest md|mm 8` (md-busy, mm-a01,
  stereo, speed-ups on), 6 paired ABBA rounds of the plain ThinLTO build against the PGO build, on a loaded Mac
  (load 20-46: the pairing, not the absolute values, is the measurement). Medians of the per-round ratio PGO / ThinLTO:

  | | host cycles per frame | retired host instructions per frame | thread CPU |
  |---|---|---|---|
  | MD stopped | ×0.924 (0.915-0.932) | ×0.939 | ×0.925 |
  | MD playing | ×0.928 (0.915-0.954) | ×0.945 | ×0.925 |
  | MM stopped | ×0.943 (0.904-0.969) | ×0.922 | ×0.941 |
  | MM playing | ×0.948 (0.926-0.955) | ×0.929 | ×0.944 |

  About 7.5 % less on the Machinedrum and 5-6 % on the Monomachine, on top of steps 1 and 2: inside the ×0.91-0.94
  expected above and over the 5 % gate of §5. Measured on the local toolchain only; CI's build (macos-15, Xcode 16.4,
  run 38061313333, all jobs green) converted the same profile, renamed libc++ ne200100 to ne190102 and reported 15
  of the profiled functions mismatched over both slices, the same count as the local universal build, so it applies
  the same counts.
- **Not done.** Windows: MSVC's `.pgd` belongs to the exact instrumented build and toolset, so a profile trained
  elsewhere cannot be reused in CI; the way there is clang-cl with this same text profile (a compiler change for the
  Windows build, its own project). Linux: GCC cannot read LLVM profiles (same answer, clang). Not measured: the plug-in
  in a host (`check_mdmm_core_capacity.py`), the x86_64 slice.

### 3.3 L1 — UC idle skip: check the inputs once per batch (confirmed twice)

- **What.** The BRA.B -2 skip re-tests five input conditions, including an out-of-line `ownsMidiWire()`, for every skipped instruction. Test them once.
- **Where.** `mdhardware.cpp:1404-1413`.
- **Measured.** Two independent A/Bs. Audio and RAM hashes identical. Host cycles: MD −5.5 to −6.6 % stopped, −4.5 to −5.5 % playing; MM −2.1 to −4.7 %. ThinLTO inlines `ownsMidiWire` but the loop stays, so the gain stays.
- **Why it is safe.** `Plugin::process` holds the device lock (`synthLib/plugin.cpp:109-120`). Panel and SysEx producers take the same lock (`mdEditor.cpp:168-183`, `mdStudioLink.cpp:143-150`). `trySendRealtimeMidi` has no production caller (`mdhardware.h:211-224`).
- **Risk.** Very low. Update the comment at `mdhardware.cpp:1405-1406`. A future lock-free producer would wait up to one batch (about 110 µs of machine time).
- **Note.** This pool is the profile's "schedStep self" and most of "ownsMidiWire". Do not count it again in L5 or L10.
- **Cheapest experiment.** Done in scratch. In the repo: the 4-line change, then `idleSelfBranchTest`, `idleSchedulerFirmwareTest`, `transportScorecardFirmwareTest`, the audio-hash tests and the CPU benches.

### 3.4 L3 — Machinedrum: exact serial-port cycle deadlines (one confirmed, one weakened)

- **What.** Set `exactEssiCycleDeadlines` to true for MD, as MM already does (`mdtransportpolicy.h:28`). Today the MD serial clock returns half the remaining cycles as an instruction delay (`esaiclock.cpp:20-24`). The DSPs average 1.2-1.6 cycles per instruction, so each 96-cycle link slot is reached through several empty ticks.
- **Measured.** Peripheral dues per frame (both DSPs): 134 → 52 stopped, 90 → 53 playing. Retired instructions: −4.8 % stopped, −1.1 % playing. Time: about −5 to −8 % stopped, −1 to −5 % playing. With the flag forced, these MD tests passed: `mdIdleSchedulerFirmwareTest`, `mdUwFirmwareTest`, `mdAudioFirmwareTest`, `mdDeskFirmwareTest plocktiming-strict`, `mdMidiTimingTest`, `mdHostRxTimingTest`, `mdTransportScorecardTest`.
- **Risk.** Medium: MD audio changes once (hash 85fb562c → 440960e7). janne808 saw the first trigger about 24 ms later. No hardware reference says which is right. `mdRuntimeTest.cpp:76` asserts the old value. Host-command acceptance can move by up to one slot (~1 µs). The inline due test in `jittrampoline.cpp:150-166` ignores the cycle deadline: any new inlining must include it.
- **Value.** About half of its value is that it lets L4 work on MD.
- **Cheapest experiment.** Flip the flag in a worktree. Run the full firmware suite and the journeys. Render 16 s of a fixed pattern both ways for a listening A/B. Radek signs off.

### 3.5 L5 — lean ColdFire loop (weakened twice)

- **What.** Step the SIM timers and both UARTs at their next event, not every instruction (`mdsim.cpp:610-617`). Replace the ~10 per-instruction checks in `processUC` with one "work pending" word. Use an integer slice end that reproduces the fractional test exactly (PR #88 commit f75166c17).
- **Where.** `mdhardware.cpp:1007-1061, 1350-1430`; `mdsim.cpp:469-517, 605-700`; `mdmc.cpp:432-439, 537-553`.
- **Why weakened.** ThinLTO already inlines `ownsMidiWire`, the UART steppers, `processUC` and `pumpScheduledMidi`. The MD `pumpDsp2HostRequest` gate is already in the function (`mdhardware.cpp:1069-1074`). The largest profile pool is L1's loop.
- **Risk.** Low to medium. The B-010 MIDI TX shifter (`mdsim.cpp:451-484`) is newer than PR #88. The sync deadline must be the timer reference latch, not the interrupt deadline (`mdsim.cpp:636-664` returns "none" when masked). Every SIM register access must sync first. A pad event kept while UART2 is full must retry each instruction.
- **Cheapest experiment.** On top of L1, in scratch, with ThinLTO libraries. Kill it if MD < 2 % and MM < 4 %, or if any hash differs.
- **Tester switch (landed).** L5 is one of four step 1 speed-ups (L1, L11, L2b, L5) that one switch turns off at run time: `GEARMULATOR_MDMM_SPEEDUPS=0`, or the menu item *Developer > Speed-ups off (legacy emulation, slower)*. Off runs the code each lever replaced (for L5, a step after every instruction), with identical audio and RAM hashes. `GEARMULATOR_MDMM_SIM_DEFERRAL=0` turns off L5 alone. The `processUC` gating and inline exec (part B) are equivalent by construction and have no switch. See `doc/md_mm_performance_diagnostics.md`.

### 3.6 L13a — Joe's DSP PR #20 (weakened: overlaps L4)

- **What.** Draft PR joelanders/dsp56300-md-mm #20 (head d8f74b72): less PC and loop-state work, X/Y memory bases kept in host registers, NOP turns combined in short DO bodies, 24 DMA registers read inline, shorter ARM64 flag masks.
- **Measured on this fork.** The production diff applies cleanly to the pinned DSP commit 1378c43. Audio hashes identical. Host cycles: MD −8.7 to −9.4 % stopped, −2.6 to −5.6 % playing; MM −12.4 to −14.6 % stopped, −1.7 to −3.6 % playing.
- **Overlap.** 60-70 % of the gain is in the NOP stubs and DMA polls that L4 removes. After L4 it is worth about 1-2 % (MD) and 1-1.5 % (MM).
- **Risk.** Draft and not mergeable on GitHub (conflicts look test-only). Joe's own gate saw 7 of 11 warm callback overruns on MD paced chords; not reproduced or explained here. It changes the `JitConfig` layout, so rebuild everything.
- **Advice.** If L4 will take weeks, PR #20 gets part of the idle gain now, bit-exact.
- **Cheapest experiment.** Run PR #20 on top of an L4 prototype, and Joe's paced and cold-first-note checks on the M4.

### 3.7 L9 — per-peripheral gating (weakened twice)

- **What.** Inside `Peripherals56303::exec`, run `HDI08::exec`, `Timers::exec` and `Dma::exec` only when their own next event is due or a register write arms them. Make `usesExactCycleDeadline` inline (`esaiclock.cpp:113-124`).
- **Why weakened.** The top-level peripheral run is already deadline-driven (`peripherals.cpp:369-388`, `dsp.h:311-330`). The "98.87 % no-op" figure belongs to the per-block call (L2). Most peripheral time at idle code is real link and DMA work, so L9 does not unlock L4's upper bound. Lock-free rings save ≤ 0.6 %; the link `Frame` copy is 16 bytes, not 516 (`audio.h:93-103`).
- **Risk.** Hidden pollers. External interrupts and host-command completion are seen only inside the peripheral run (`dsp.h:330`, `hdi08.cpp:144-183, 256`). Gating HDI08 could delay "busy clear" by up to 16384 instructions. Every `setDelayCycles(0)` site must mark its peripheral. The DSP core is shared with other synths: make it a per-host option.
- **Cheapest experiment.** Count, per due, which sub-peripherals changed state. Then patch with "all dirty on any re-arm". Kill it below 3 % (MD) and 2 % (MM) instructions, or on any hash change.

### 3.8 L10 — ColdFire HI08 poll skip and immediate idle probe (weakened twice)

- **What.** Skip whole turns of the firmware's HI08 status polls (MD `$209060`, `$209086`; MM `$24829e`, `$243926`, `$248214`, `$24395e`) while the DSP catch-up is a no-op, bounded by the next event. Probe the BRA.B -2 skip right after any `0x60fe`, not every 16th step (`mdhardware.cpp:1380`). On MM, bound it by the deferred host-RX cycle instead of disabling it (`:1385`).
- **Measured counts.** MM: 98 % of CVR reads see the host command pending; only 7.3 % move the DSP. MD: 0.7-1.4 % of DSP1 ISR reads move the DSP.
- **Why weakened.** On MD the cited BRA.B cost is L1's loop; the immediate probe is worth ~0.1-0.3 % on MD and 0.6-1.3 % on MM. Values in the table are after L5, which shrinks the per-instruction cost.
- **Risk.** Medium. The MD ISR read publishes DSP TX words (`mddsp.cpp:398, 452-458`): require "no TX pending". MM backpressure can make a catch-up a no-op for another reason (`mdhardware.cpp:1540-1546`). Detect the loop shape, not the address.
- **Cheapest experiment.** Count skippable turns per poll address. Kill it if MD < 80 % or MM < 70 % are skippable.

### 3.9 L11 — ColdFire memory fast lane (confirmed, one says MM a little high)

- **What.** Inline fast path for RAM and SRAM (four 64 KiB-aligned ranges), plus native 32-bit reads and writes through the existing hooks (`memoryOps.h:49-73, 120-139`). Flash, SIM, HI08 and patch RAM stay on the slow path.
- **Why.** `resolve()` is a chain of 12 range checks, and SRAM (MM's hottest data) is the 9th (`mdmc.cpp:286-329`). 83 % of MM 16-bit reads are halves of 32-bit reads.
- **Measured.** 5 paired rounds, medians of host cycles: MD −1.4 %; MM −2.6 % stopped, −3.4 % playing. Audio, UC register and RAM hashes identical. Tuth's commit aee17bb8a (PR #97, not merged) is the same idea.
- **Risk.** Low. HI08 32-bit writes must stay two 16-bit writes in order. Patch RAM keeps its lock (`mdmc.cpp:598-601`). Keep flash out of any table.
- **Cheapest experiment.** Repeat on a ThinLTO build; add an inline opcode fetch (`m68kcpu.h:1051-1080`) as a variant.

### 3.10 L2 — inline due test and catch-ups through execUntilCycles (weakened twice)

- **What.** (a) Emit the full due test (instruction target and cycle deadline) in the `execUntilCycles` trampoline; today it calls the peripheral function before every block (`jittrampoline.cpp:322-325`). (b) Run the MD catch-ups as one `execUntilCycles` call (`mdhardware.cpp:1543-1546, 1611-1615`); MM keeps its per-block backpressure test.
- **Why weakened.** The earlier −3.6 / −4 % came from a harness whose peripheral function carried an extra hook. The real function is an 8-instruction leaf. Clean retired instructions: (a) 0.0 %, (b) −0.7 %, both −1.0 % (MD); (a) −0.1 % (MM). All hashes identical.
- **Risk.** Low. The test must include the cycle deadline (`peripherals.h:125-128`), or MM serial slots slip. x64 is not prototyped. Its value halves after L4 and L7. After (a), L4 cannot hook every block.
- **Cheapest experiment.** Quiet machine, 5 interleaved runs of base, (a), (b), (a)+(b). Ship (b) first if (a)+(b) ≥ 2 %.

### 3.11 L7 — long JIT blocks below P:$100 (one confirmed, one weakened)

- **What.** `jitblock.cpp:47-49` caps every block below P:$100 at 2 words, because it might be a fast-interrupt entry. MD runs its main loops there. Give interrupts their own entry blocks and compile ordinary code there as normal blocks.
- **Measured** with a vector whitelist hack: MD retired instructions −7.1 to −7.8 %, block entries −32 %; MM about 0.
- **Overlap.** About two thirds of it is inside the idle polls of L4. After L4, about 2 % on MD.
- **Risk.** Medium. Not bit-exact. A whitelist is unsafe: a whitelist of MM's steady-state vectors hung MM boot. Interrupt blocks need their own cache, because `getInfo` stops at existing code (`jitblock.cpp:90-94`), and that cache must be cleared on P writes. Put it behind a `JitConfig` flag for MD/MM only.
- **Cheapest experiment.** Run the hack with and without the idle skip. Drop it if the residual is < 1.5 % on MD.

### 3.12 L13b — aarch64 codegen pass (weakened twice)

- **What.** Keep DSP registers in host registers across DO turns; count cycles in a register; cheaper `regLastModAlu` (`jitregtypes.h:33`).
- **Why weakened.** Removing the parallel-op `nop` (`jitops.cpp:403`) measured about 0 %. The X/Y base registers are already in PR #20. Bookkeeping is about 55 of 330 host instructions in the 12×MAC block p:209. On the M4, cut instructions turn into cut cycles at only ~0.45-0.5. The existing optimiser measured 0 %.
- **Risk.** High for flag (CCR) correctness. Deferred counters must flush before C++ code that reads them (`mddsp.cpp:153`, `mdhardware.cpp:520`, `peripherals.cpp:143`).
- **Cheapest experiment.** An upper-bound test that skips the per-turn write-back (it breaks the hash on purpose). Drop step 2 if it saves < 2 % on MD playing.

## 4. Refuted or already done

Refuted:
- **L8 deadline-checked DO back-edges (no fixed 4-turn cap):** MD needs 3-8 % more host work, because finer service lengthens the halving due chain (+40-60 dues per frame). The bit-exact variant saves ≤ 0.4 %. The 32-instruction cap ends 0.02 % of blocks.
- **L12 block linking with a deadline guard:** measured about 0 % (−0.8 to +0.4 % cycles). Unguarded linking crashes at boot (`mddsp.cpp:105-107`). Eager child compiles change block boundaries and the hash.
- **L14 one chip on a worker thread:** total CPU rises 30-80 %, because both threads must spin (sync about 1 M times a second at 85-450 ns). Only audio-thread time drops (MD ≤ 25-35 %, MM ≤ 10-25 %). For headroom only, a safer variant is to run the whole serial emulation one block ahead on a worker (reasoning, not measured).
- **JIT optimiser on:** 0 % steady state, bit-exact; already off on Apple arm64 (`mddsp.cpp:118-122`).
- **Plain maxDoIterations 16 or 64:** changes MD hashes; MM fails `mmSineFirmwareTest` and `mmSineMidiFirmwareTest` at 64.
- **Batched or delayed DSP-to-DSP link:** at most one 96-cycle slot of lookahead; the MM strobe rejects any delay; ≤ 3-4 % left after L2.
- **Scheduler quantum tuning:** < 1 %; MD runs only 0.42 slices per frame.
- **DSP speed percent (under-clock):** the frame is fixed at 2304 cycles; speed only scales the mixer's codec clock (`mddevice.cpp:649-652`). Gain 0 %.
- **ColdFire JIT, Unicorn or QEMU:** ≤ 1-3 %; the cost is the wrapper, not Musashi (3-4 ns per instruction). No cycle model.
- **Replace Musashi with upstream's new ColdFire core (3c0de7e6):** no benchmark exists.
- **Tiered LLVM recompiler, AOT, HLE, per-ROM profiles (FUTURE §2):** no data; burns a second core or needs firmware we cannot ship.
- **UC batches of 32+ instructions (Tuth):** 32 loses the boot MIDI status; 64 stalls boot.
- **Serve all due serial slots in a burst (upstream ddcb6ea5):** reverted upstream; TX underrun.
- **Cooperative WAIT:** the firmware never executes WAIT.
- **DSP "give up the slice" on HI08 polls:** not applicable; the DSPs poll DMA and Port C.

Already done (in main):
- Bounded DSP dispatch, processor-only ColdFire stepping, idle MIDI skip (PR #30): MD −13.2 %, MM −11.3 %.
- MM idle BRA.B -2 batching (PR #44, 83b67651d).
- MD host-pump gating (PR #73, 334fb3f44): paced p50 1.18× → 0.71× of budget.
- Optimiser off on Apple arm64 (PR #68): faster cold start, steady state ±1.4 %.
- ThinLTO plus DSP ThinLTO in the macOS release (`build_mdmm.sh:162-163`): −6 to −10 % against the bench build.
- Inline due test in the `execLoop` trampoline (upstream ad63840a): present, but only `DSPThread` uses it, not MD/MM.
- janne808's NOP skip "tried and rejected": the cause (halving deadlines) goes away with L3; kept inside L4.

## 5. Plan

### Step 1 — bit-exact quick wins (1-2 weeks)

Do L1, L11, L2 (b first, then a), L5, and evaluate PR #20 (L13a). If PR #20 lands here, step 1 shows a bigger stopped drop (PR #20 alone: MD −9 %, MM −14 %) and L4 later gains less. The end total does not change.

Landed: L1, L11, L2b and L5 sit behind one run-time switch (`GEARMULATOR_MDMM_SPEEDUPS=0`, or *Developer > Speed-ups off (legacy emulation, slower)*), so a tester can compare against the pre-step-1 emulation in minutes; both positions are bit-exact. See `doc/md_mm_performance_diagnostics.md`.

Step 1 measured (2026-10-09, `cba2ace1f`, M4 Pro, `mdmmPerfGateTest`): the shipped set is L1, L11, L2b and L5 (L2a and L13a/PR #20 not taken; step 2 starts from this set). Host work per frame: MD −15 % stopped / −16 % playing, MM −19 % / −21 %; CPU of one instance −16 to −19 %; the longest buffer while editing −17 to −27 %. The forecast row above (87 %) was beaten because L2b alone removed more scheduler glue than L2 was credited with.

| Gate | Pass condition |
|---|---|
| `mdmmPerfGateTest` audio FNV hash and RAM hash, MD and MM, stopped and playing | identical before and after each change |
| `mdCpuBenchTest` / `mmCpuBenchTest`, plus host cycles per frame from `thread_selfcounts` (`mdmmPerfGateTest`) | ≤ 0.88 (MD) and ≤ 0.87 (MM) of the same build's baseline |
| Firmware tests | `idleSelfBranchTest`, `idleSchedulerFirmwareTest`, `transportScorecardFirmwareTest`, `mdAudioFirmwareTest`, `mdMidiTimingTest`, `mdHostRxTimingTest` green |
| `scripts/mdmm-rt-check.sh --plock` | idle 128-frame buffers below today's ~27 M instructions; no action over budget |

### Step 2 — structural (3-6 weeks; MD audio changes once)

Do L3 first, then L4 (signature table first, generic detector later), then L9 and L10. Do L7 last, only if its measured residual is ≥ 1.5 %.

Step 2 measured (2026-10-10, branch `perf/step2`, M4 Pro under other load, `mdmmPerfGateTest` md-busy / mm-a01,
speed-ups on, medians of 4 paired ABBA rounds; host instructions and cycles per frame, each row against the row
before it):

| Lever | Commit | MD stopped | MD playing | MM stopped | MM playing | Bit-exact |
|---|---|---|---|---|---|---|
| Upstream 7d69d7a9 (loop-end assert) | not taken | – | – | – | – | – |
| Upstream e8989e41 (serial-status poll fast-forward) | not taken | – | – | – | – | – |
| CMPM JIT fix | `79f2ee6e2` | 0 | 0 | 0 | 0 | yes (goldens 24/24) |
| L3 exact MD ESSI deadlines | `fcb7a9b11` | −2.0 % / −5.1 % | −1.0 % / −5.7 % | 0 | 0 | MD no (by design), MM yes |
| L4 NOP stubs | `438b8c882` | −5.3 % / −5.4 % | −0.4 % / +0.8 % | −14.0 % / −15.1 % | 0.0 % / +0.4 % | yes |
| L4 DMA poll loops (movep) | `c93d9098c` | 0 | 0 | −4.4 % / −3.5 % | −7.2 % / −3.7 % | yes |
| L4 DMA bit-test polls (jset) | `990f85436` | −3.4 % / −2.9 % | −3.4 % / −2.8 % | 0 | 0 | yes |
| **Step 2 against main (step 1), with L3 (as first built)** | | **−9.3 % / −12.3 %** | **−3.7 % / −6.5 %** | **−17.7 % / −17.7 %** | **−7.3 % / −5.7 %** | MD changes (L3) |

**Shipped shape (Radek, 2026-10-10): no lever that changes the sound is on by default.** Every bit-exact lever (step 1,
L4, the CMPM fix) sits under the speed-ups switch and is on by default; off is the legacy emulation exactly, CMPM
bug included. L3 stays in the code, **opt-in and off by default** (`GEARMULATOR_MDMM_EXACT_ESSI=1` or Developer >
"Exact MD audio timing (experimental, changes the sound)", config key `exactEssiTiming`), acting only with the
speed-ups on; its goldens have their own keys (`speedups-on+exact-essi`, MD only). Measured on the final branch
(main merged in), same method:

| Configuration | MD stopped | MD playing | MM stopped | MM playing | Bit-exact |
|---|---|---|---|---|---|
| **Default (step 2 without L3) against main** | **−4.9 % / −3.7 %** | **−2.9 % / −2.8 %** | **−17.9 % / −18.7 %** | **−7.4 % / −6.2 %** | yes, committed goldens 24/24 both positions |
| L3 opted in, on top of the default | −5.1 % / −7.4 % | −1.4 % / −3.2 % | 0 | 0 | no (MD, by design) |

Without L3 the MD gains less from L4's NOP stubs: the skips are bounded by the next peripheral due, and without L3
the MD's dues are several times closer together (the halving instruction delays of §3.4).

Notes:
- **Upstream batch.** 7d69d7a9 only relaxes an `assert` (Debug builds); no gain, skipped. e8989e41 widens the
  ESAI/ESSI status-poll fast-forward to TDE/RDF and brclr/brset: a probe on the JIT saw no block in either firmware
  (all six golden scenarios) that tests an ESSI or ESAI status register at all, so it cannot change anything here;
  skipped. (It also fixes `op_Jset_qq` passing `Jclr_qq` as its template argument; harmless for our firmwares.)
- **CMPM.** `cmpm S,D` with S the other accumulator took |S| in its live JIT register (A became |A| for the rest of
  the block). Fixed with a unit test, behind the speed-ups switch (`DSP::setJitCmpmFix`, read at run time: off writes
  |S| back as before). A probe saw only `cmpm y0,b` (MM p:9c1) in our firmwares, whose source is a
  temporary, so nothing changes (goldens 24/24).
- **L3** changes the MD's audio, RAM, SRAM and MIDI-out timing (playhead moves and MIDI event counts equal; the
  firmware tests and `plocktiming-strict` pass with it). Radek listened and kept it, then decided that nothing that
  changes the sound ships on by default: L3 is opt-in (above), and its goldens are recorded under their own keys.
- **L4** is two mechanisms, both bit-exact by construction (they skip only block executions after which the
  dispatcher would find nothing due, and land on a boundary where it still finds nothing): NOP-only DO bodies
  (`JitConfig::nopLoopFastForward`, `DSP::fastForwardNopLoop`) and self-looping blocks that poll a DMA register
  and repeat their own state (`JitConfig::pollLoopFastForward`, `JitBlock::isIdlePollLoop`, `DSP::fastForwardPollLoop`).
  An inline test in the block calls only when a whole block fits before the stop and the deadline: an unconditional
  call cost +3.9 % cycles with the switch off. Not covered at first: the Port C polls (MD p:bb, MM p:195; their reads
  go through Hardware's edge logic) and the MD's DDR0 poll at p:3c, a loop of five two-word blocks below P:$100.
  Both are covered since, without L7: "L4 finished" below.
- **L7** is not bit-exact by nature (block boundaries move), so it is outside "each bit-exact" and was not done.
  **L9** was measured, not built: on the step 2 build `HDI08::exec`, `Timers::exec` and `Dma::exec` together are
  about 1.6 % (MD playing) and 2 % (MM playing) of samples, below the kill line of §3.7 (3 % / 2 %) before any
  overlap. **L10** was not started (1–2.5 % forecast, within the noise of these runs).
- (With L3, as first built.) Against the forecast table in §1 (after step 2: 63 / 71 / 67 / 75.5 % of the shipped cost), chaining step 1's
  figures (0.85 / 0.84 / 0.81 / 0.79) with step 2's cycles (0.88 / 0.94 / 0.82 / 0.94) gives roughly 75 % (MD
  stopped), 79 % (MD playing), 67 % (MM stopped) and 75 % (MM playing). The MM is on the forecast; the MD is behind it
  because its idle time is in the Port C and p:3c polls that L4 could not reach yet (now covered, with less than
  hoped: "L4 finished" below). Approximate: different runs, a loaded machine.

#### L4 finished: the two Machinedrum waits (2026-10-10, branch `perf/l4-sync-waits`)

**Go/no-go, measured first.** Exact counts from a per-block hook (a scratch build, not committed; the hashes with the
hook equal the goldens), `mdmmPerfGateTest md 8` (md-busy), per audio frame (352,832 frames per phase), on main
(`49a3a7aff`, speed-ups on, L3 off):

| | MD stopped | MD playing |
|---|---|---|
| DSP1 p:3c-43 (DDR0 poll, five blocks: 3c, 3f, 40, 42, 43; 6 instructions, 17 cycles a turn): block runs, DSP cycles | 134.7 of 299, 458 (19.9 %) | 135.1 of 302, 460 (19.9 %) |
| DSP2 p:bb-bf (Port C bit 1 poll, three blocks: bb, bd, bf; 4 instructions, 9 cycles a turn): block runs, DSP cycles | 230.0 of 341, 690 (30.0 %) | 233.2 of 320, 700 (30.1 %) |
| Block runs a perfect skip saves (every turn but one between two peripheral dues), p:3c + p:bb | 101.9 + 203.0 | 103.5 + 205.0 |
| Host cycles per skipped block (calibrated on the jset poll skip at p:cf, 4 ABBA rounds of it off and on) | 13.7 | 14.1 |
| Estimate if both were skipped perfectly (of 28.0 k / 29.7 k host cycles per frame) | 5.0 % + 9.9 % = **14.9 %** | 4.9 % + 9.7 % = **14.6 %** |

Over the 8 % line: go. The MM has the same kind of Port C wait at DSP2 p:195, one block above P:$100 (27.9 / 50.7
skippable block runs per frame stopped / playing: about 1 % of the MM).

**Design.** No block boundary moves (not L7), so both levers keep the goldens:
- *Poll loops over several blocks* (`JitConfig::pollCycleFastForward`, `JitBlock::findPollCycle`,
  `DSP::fastForwardPollCycle`, `jitpollcycle.h`). Found from the opcodes in P memory, from each block: a head, the
  ops `isIdlePollLoop` allows, conditional branches out of the loop (its exits) and a branch back to the head; the
  turns must repeat (no register read before the turn writes it). Each block of the loop passes a marker on (the
  loop's id, a hash of its words, and the next block's address) when it went on in the loop, and clears it otherwise;
  a peripheral run, an interrupt, a PC set from outside and the start of a run clear it too. When the marker comes
  round to the head, the last turn was straight and nothing could change what it read since: the head (before its own
  turn, where no host register is live, so the call is cheap) skips the turns that fit before the next due, the run's
  stop and the cycle deadline, as `fastForwardPollLoop` does. A gate in the head calls only when a whole turn fits.
  Needs `linkJitBlocks` off (the MD/MM's setting): the skip lands on dispatcher boundaries.
- *Pure registers* (`JitConfig::pollLoopPureReads`): addresses whose repeated reads return the same value and change
  nothing more than the first, until a peripheral run or the end of the run. mdLib declares Port C data
  (`$ffffbd`): the pin follows DSP1's writes, and the MD's on-demand link releases a pending edge on the first read
  only; DSP1 runs only in DSP2's peripheral runs (the link callbacks) or between DSP2's runs. Port D is not pure (it
  follows the reading DSP's counter). Read by `movep` (pp and qq forms) or by `move x:>abs`. With them the MD's p:bb
  (three blocks, the loop machinery above) and the MM's p:195 (one block, `isIdlePollLoop`) are covered.
- Why the gain is below the estimate (about 7 % of the 15 % in cycles): after every peripheral due the loop must run
  one straight turn again before it may skip (a due may change what it reads), and the MD's dues are about 43 DSP
  cycles apart (without L3): a 17-cycle turn at p:3c leaves room to skip about one turn in two and a half. The
  Port C turn (9 cycles) loses less.

| Lever | Commit | MD stopped | MD playing | MM stopped | MM playing | Bit-exact |
|---|---|---|---|---|---|---|
| L4 poll loops over several blocks (MD p:3c) | `4c7be9f22` | −1.8 % / −1.9 % | −1.9 % / −2.4 % | −0.4 % / −0.5 % | −0.9 % / −2.0 % | yes (goldens 30/30) |
| L4 Port C declared pure (MD p:bb, MM p:195), against the row before | `5068b4e47` | −8.0 % / −5.4 % | −7.5 % / −5.1 % | −2.1 % / −0.9 % | −3.6 % / −2.4 % | yes (goldens 30/30) |
| **Both against main** | | **−9.5 % / −7.0 %** | **−9.1 % / −6.5 %** | **−2.4 % / −2.1 %** | **−4.4 % / −3.6 %** | yes |

Measured as in "Step 2 measured" (host instructions / cycles per frame, `mdmmPerfGateTest` md-busy / mm-a01, stereo,
speed-ups on, release configuration: ThinLTO, the committed PGO profile, arm64), medians of 4 paired ABBA rounds
against the build before it on a loaded Mac (load 3-20). The committed PGO profile (fresh on main) goes stale with these levers for
`source/dsp56300/source/dsp56kEmu` and `source/elektron/md/mdLib` (the build warns; the changed functions get no
profile data; the gains above were measured that way) and should be retrained before the release that ships them
(`scripts/macos/train_mdmm_pgo.sh`).

Gates, each lever: the committed goldens 30/30 (MD and MM, all scenarios, stereo and six outputs, speed-ups on and
off, and the MD's exact-ESSI keys); the firmware tests `mdIdleSelfBranchTest`, `mdIdleSchedulerFirmwareTest`,
`mdTransportScorecardTest`, `mdAudioFirmwareTest`, `mdMidiTimingTest` (+ firmware), `mdHostRxTimingTest` (+
firmware), `mdUwFirmwareTest`, `mmSineFirmwareTest`, `mmSineMidiFirmwareTest`, `mmDigiproFirmwareTest` (+ ensemble)
green; the JIT unit tests `pollCycleFastForward` (the MD's p:3c words at p:3c and at p:100, exits back to the head
and into the middle, the polled register moved by the host between runs and by a DMA transfer inside a run; the
state after every slice equal without the code, off and on) and a carried-register loop that must never skip;
`pollPortFastForward` (the MD's p:bb and the MM's p:195 words, Port C moved by the host; the same loops on Port D,
not declared, must never skip).
Broken on purpose, the test fails: without the clear on a peripheral run, and without the clear at the start of a
run.

| Gate | Pass condition |
|---|---|
| L3 audio change | Radek signs off after a 16 s listening A/B; new reference hashes recorded; `mdRuntimeTest.cpp:76` updated |
| L4, L9, L10 | keep the post-L3 hash; L4 keeps the DSP state hash at every due |
| L7 | changes the hash again: second sign-off, or leave it out |
| CPU benches | ≤ 0.65 / 0.73 (MD stopped / playing) and ≤ 0.69 / 0.77 (MM) of the step-0 baseline |
| Firmware suite | adds `mdDeskFirmwareTest plocktiming-strict`, `mdUwFirmwareTest`, `mmSineFirmwareTest`, `mmSineMidiFirmwareTest`, `mmDigiproFirmwareTest` |
| Plug-in | in-plugin journeys, pluginval and auval green |

### Step 3 — long bets (1-3 months)

Ship PGO for the arm64 slice (L6). Then decide on the codegen pass (L13b). Consider threading (L14) only if callback headroom, not average CPU, is the goal.

| Gate | Pass condition |
|---|---|
| PGO | renders bit-identically to the ThinLTO build; ≥ 5 % faster than ThinLTO on the step-2 code, else drop |
| Core capacity | `scripts/macos/check_mdmm_core_capacity.py`: p50 ≤ 90 % of a 48 kHz / 128-frame callback |
| Codegen | JIT-versus-interpreter differential harness green; ≥ 2 % on MD playing, else stop |
| Release | start tests, pluginval and auval on the shipped files |

End point if all gates pass: about 57 % / 64 % (MD) and 60 % / 68 % (MM) of today's shipped cost (§1).

## 6. Sources

Internal (repo, read only):
- Scheduler and idle skip: `source/elektron/md/mdLib/mdhardware.cpp:1007-1061` (processUC), `:1069-1074`, `:1280-1488` (schedStep), `:1404-1413` (per-instruction input loop), `:1460` (execUntilCycles), `:1543-1546` and `:1611-1615` (catch-up loops), `:522-529` (Port D), `:533-575`, `:938-953` (Port C), `:1653-1671` (RAM-packing patch); `mdhardware.h:211-224`.
- Policy and JIT config: `mdtransportpolicy.h:24-28`; `mddsp.cpp:105-122`, `:153`, `:262-375`, `:394-421`, `:452-458`; `mdLibTest/mdRuntimeTest.cpp:70-77`.
- ColdFire: `mdsim.cpp:451-484`, `:610-617`, `:636-664`; `mdmc.cpp:286-329`, `:441-475`, `:563-727`; `mdturbomidi.cpp:23-33`; `source/mc68k/memoryOps.h:49-73, 120-139`; `Musashi/m68kcpu.h:1051-1080`; `Musashi/m68kconf.h:78`.
- DSP core: `source/dsp56300/source/dsp56kEmu/jitblock.cpp:47-49, 90-94, 254, 515-526, 733-750`; `jitblockchain.cpp:272-330`; `jittrampoline.cpp:150-176, 316-357`; `jitops_jmp.cpp:49-135`; `jitops.cpp:403`; `jitregtypes.h:20-45`; `esaiclock.cpp:20-24, 101-124`; `peripherals.cpp:369-388`; `peripherals.h:117-140`; `dsp.h:210-330, 477-481`; `hdi08.cpp:144-258`; `interrupts.h:30`.
- Host: `source/synthLib/plugin.cpp:109-120`; `mdEditor.cpp:168-183`; `mdStudioLink.cpp:143-150`; `mddevice.cpp:649-652, 669-680`.
- Build: `source/elektron/md/optimization.cmake:153-196`; `scripts/macos/build_mdmm.sh:159-163`; `.github/workflows/mdmm-editors-macos.yml:81-83`; `.github/workflows/mdmm-editors-release.yml:3-10`; `base.cmake:31-46, 69-101`.
- Docs: `doc/mdmm-apple-optimization.md:115-123`; `doc/modern-ux/P4-RESULT.md` (CPU); `doc/modern-ux/MM-P3-RESULT.md`; `doc/modern-ux/DESIGN-edit-flow.md`; `doc/modern-ux/FUTURE-emulation-ideas.md` §2, §6; `scripts/mdmm-rt-check.sh`.
- Unmerged history in the local object store: PR #88 commits bcb626fb1, 19ffd1fae, 1fefca3fd, ff633dfbd, f75166c17, doc 8331469b7 (`remotes/upstream-pr/88`); PR #97 aee17bb8a.

Raw evidence: a text-only copy (harness sources, prototype patches, counts, listings; no binaries, no ROMs) is kept outside the repo at
`~/Documents/Github/audio/gearmulator-md-mm-wt/research-emulation-cpu-2026-10-08.tar.gz` (5.7 MB, 2631 files; private: it holds firmware disassembly listings, so never commit it).
The original session scratchpad (temporary) was:
`/private/tmp/claude-501/-Users-radek-Documents-Github-audio-gearmulator-md-mm/4c76e84c-c7b4-4f0d-a2b9-58b858cbff5a/scratchpad/` —
`prof/` (profiles), `measure/out2/` (exact counts, code listings), `dspjit/runs/` (DSP prototypes), `cf/run/` (ColdFire counts),
`l1verify/`, `verifier-L1/`, `l2-verifier/`, `l3-verifier/`, `l3-gainverif/`, `verifier-L4/`, `l4-verifier/`, `l5-verifier/`, `verifier-L6/`,
`l7-verifier/`, `verifier-L7/`, `verifier-L8/`, `l8-verifier/`, `l9-verifier/`, `l11-verifier/`, `l12-verifier/`, `l13-verifier/`, `l13-det-verifier/`, `l14-verifier/`.

External:
- Upstream DSP commits: https://github.com/dsp56300/dsp56300/commit/e8989e41 (poll fast-forward), https://github.com/dsp56300/dsp56300/commit/83f8f9ea, https://github.com/dsp56300/dsp56300/commit/ad63840a (inline due test, 98.87 % no-ops), https://github.com/dsp56300/dsp56300/commit/97530405 (cycles per instruction), https://github.com/dsp56300/dsp56300/commit/623c6bce, https://github.com/dsp56300/dsp56300/commit/cbb2398e, https://github.com/dsp56300/dsp56300/commit/4f15f23c (audio workgroup)
- Fork PRs: https://github.com/joelanders/gearmulator-md-mm/pull/30, /pull/44, /pull/56, /pull/68, /pull/73, /pull/88, /pull/96, /pull/97; https://github.com/joelanders/dsp56300-md-mm/pull/20
- Other forks: https://github.com/amorgan101010/gearmulator-md-mm, https://github.com/c0remusic/gearmulator-md-mm, https://github.com/c0remusic/dsp56300-md-mm
- The Usual Suspects: http://theusualsuspects.io/2024/03/01/advanced-setting-dsp-overclocking-underclocking.html; 39C3 talk https://media.ccc.de/v/39c3-from-silicon-to-darude-sand-storm-breaking-famous-synthesizer-dsps; https://github.com/dsp56300/gearmulator/issues/224
- Idle-skip precedent: Dolphin https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/DSP/DSPAnalyzer.cpp and .../PowerPC/PPCAnalyst.cpp; PCSX2 https://github.com/PCSX2/pcsx2/blob/master/pcsx2-qt/Settings/AdvancedSettingsWidget.cpp
- Dispatcher cost: https://bugs.kde.org/show_bug.cgi?id=296422; https://www.qemu.org/docs/master/devel/tcg.html
- ColdFire: https://github.com/mamedev/mame/blob/master/src/devices/cpu/m68000/mcf5206e.cpp; https://github.com/mamedev/mame/blob/master/src/mame/elektron/elektronmono.cpp; https://github.com/m-dwyer/digikit; https://github.com/irpina/digiemu
- Apple: https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon; https://bluecataudio.com/Blog/?p=5268
- Seen only as search snippets (pages blocked, figures unverified): Dolphin progress report 2512, HQEMU papers, GCC bug 94369.
