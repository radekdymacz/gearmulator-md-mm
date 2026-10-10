# Plan: 0.5 (written 2026-10-10)

The big item is the roadmap's clean-up: we stop being a fork, delete everything that is not the Machinedrum or
the Monomachine, prove nothing changed, then bring in upstream fixes one commit at a time. User fixes run beside
it. Decisions behind it: [ROADMAP.md](../ROADMAP.md) D1-D6. Way of working: [CI.md](CI.md).

## Why now

- We build, test and read about 125k lines of upstream code and ~120k lines of other synths we never ship.
- Three of our submodules live in joelanders' account (`dsp56300-md-mm`, `mc68k-md-mm`, `JUCE-md-mm`). He has
  been inactive since 2026-09-21. If he deletes or rewrites them, we cannot build any version, old or new.
- GitHub still shows the repository as a fork, so pull requests and search point at other people's repos.

## Phase 0: freeze the baseline (half a day)

- Tag `pre-cleanup-0.5` on main.
- Record what "unchanged" means: the bit-exact goldens (`mdmm-goldens.json`, MD and MM, speed-ups on and off),
  the CPU baseline (`mdCpuBenchTest`, `mmCpuBenchTest`), the list of tests that pass, and the shipped bundle
  names, IDs and AU codes (so a DAW still finds the plug-ins and old projects still load).

## Phase 1: detach (1-2 days)

1. **Own the code we build from.** Move `source/dsp56300` and `source/mc68k` into this repository as plain
   folders (their full history stays in the archive tag). Move JUCE to a copy under our own account, or into
   the repository too. Keep the true third-party submodules (RmlUi, freetype, lunasvg) pointed at a copy we own.
2. **Drop the upstream plumbing:** the `upstream` and `gearmulator` remotes, `scripts/mdmm-sync-upstream.sh`,
   the integration-policy workflow, the `upstream-main` branch, the Jenkins files.
3. **GitHub:** Radek asks GitHub to detach the repository from the fork network (Settings or GitHub Support,
   "detach fork"). Only the owner can do this.
4. **Check:** a fresh clone with no access to joelanders' or dsp56300's repos builds and passes.

## Phase 2: delete what is not MD or MM (3 days, time-boxed)

In small slices, one commit each, each checked before the next (build, unit tests, goldens equal):

1. The other synths: `virus*`, `osirus*`, `osTIrus*`, `mq*`, `xt*`, `nord`, `ronaldo`, `wLib`, their skins,
   data and consoles (`*TestConsole`, `*ConsoleLib`, `*PerformanceTest`, `virusIntegrationTest`).
2. Formats and tools we do not ship: VST2 (`vstsdk2.4.2`, `mqVst2`, `fst`), CLAP (`clap-juce-extensions`),
   LV2, `cpp-terminal`, `Android`, `portaudio` / `portmidi` if only the consoles use them,
   `changelogGenerator`, upstream's deploy and pack scripts.
3. Upstream's workflows (`cmake.yml`, `elektron-*.yml`, `nightly.yml`, `release.yml`) and docs.
4. The build options: one product list, no per-synth switches.
5. Shared code stays: the DSP and ColdFire cores, `synthLib`, `baseLib`, `hardwareLib`, the plug-in libraries,
   the bridge and network code only if the editors still use them (the MCP server goes: item 7 below).

## Phase 3: prove it (1 day)

- Full local gate (`scripts/mdmm-local-gate.sh`) and full CI on `release/0.5.0`: same goldens, same CPU or
  better, same bundle names and IDs, an old project opens, the installers upgrade 0.4.0 in place.
- Record the result: lines, build time, CI time, before and after.

## Phase 4: upstream fixes, one commit at a time (ongoing)

- A script lists new upstream commits in the DSP core, the 68k core and the framework. Claude marks each
  take / skip / watch with one line why; Radek decides; each taken commit is ported alone and gated.
- First batch: the seven commits in ROADMAP §3 (DSP loop-end check, serial-poll fast-forward, audio workgroup,
  three synthLib audio-thread fixes, the resampler latency).
- Watch: upstream's new ColdFire V2 / MCF5206e model (the MD/MM main chip).

## Monomachine at the Machinedrum's level (decided 2026-10-10: 0.5 = clean-up + MM parity)

The gap list is [MM-PARITY-2026-10-09.md](../modern-ux/MM-PARITY-2026-10-09.md); the port order is
[MM-PORT-PLAN.md](../modern-ux/MM-PORT-PLAN.md). On top of the clean-up, in this order:

| # | Slice | Size |
|---|---|---|
| 1 | GLOBAL: the top-bar key and panel; MIDI channels first (B-051: an imported global left tracks without a channel, so sound edits were lost), then control in/out, master tune | M |
| 2 | Step selection, ⌘C/V/X/D/A, move and extend, ⌘-drag copy, the step menu, fill moved to the menu, Delete and the LCD COPY/CLR/PASTE on the selection | M-L |
| 3 | Piano roll: note length like Ableton/Digitakt (I-010), draw toggle and box select (I-007) | M |
| 4 | The live-recording lock marker, the keyboard view's missing tips, the MD journeys the MM lacks | S-M |
| 5 | The Sound page layout and GEN/MUTATE as on the MD, where the parity list marks them "a" (built on `feat/mm-slice5`: one source for both, MM-PARITY-2026-10-09.md §2b) | M |

## Emulation CPU step 2 (in 0.5, Radek 2026-10-10)

On top of the clean-up, each lever alone, behind the speed-ups switch, gated by the goldens
([RESEARCH-emulation-cpu.md](../modern-ux/RESEARCH-emulation-cpu.md) §3, §5):

| # | Lever | Gain (estimate) | Gate |
|---|---|---|---|
| 1 | Upstream batch: DSP loop-end check, serial-poll fast-forward (`7d69d7a9`, `e8989e41`) | ~5 % | bit-exact |
| 2 | CMPM JIT fix (helica1 review: `cmpm a,b` turns A into \|A\|) | correctness | goldens; if they change, Radek listens |
| 3 | L3: exact MD ESSI deadlines | ~10 % MD | changes MD audio once: Radek's listening sign-off |
| 4 | L4: DSP idle-loop fast-forward (after L3; the NOP stubs first). Finished on perf/l4-sync-waits: the MD's two waits (p:3c over five blocks, the Port C sync at p:bb) estimated 14.6-14.9 % if skipped perfectly (go); measured, bit-exact, both levers against main: MD −7.0 % / −6.5 % host cycles (stopped / playing), MM −2.1 % / −3.6 %; retrain the PGO profile before the release (RESEARCH "L4 finished") | ~10-15 % | bit-exact against the L3 goldens |
| 5 | L7, L9, L10: scheduler glue | ~5 % | bit-exact |
| 6 | PGO in the release build: committed profile, both macOS slices, done on perf/pgo (RESEARCH "L6 measured") | MD −7.5 %, MM −5.5 % (measured) | goldens 24 + 6 equal; retrain when stale (CI.md "PGO") |
| 7 | Try: render ahead (helica1), opt-in, with real-time priority and the audio workgroup | DAW thread lighter | delayed output equal |

Around the core (branch `perf/audio-thread-spikes`, RESEARCH "Around the core"): of six suspected audio-thread
costs outside the chips, two were real and are fixed bit-exact: the editor's sample list (built on the audio thread
with 143 allocations, a slot up to 128 us: now a raw copy, at most 13 us a block, built by the reader) and the
resampler's filter at host rates other than 44.1 kHz (8 copies per instance: now one per process, -40 % of the
resampler's time). Measured and left: the save's lock hold (0.14 ms), the second machine during a first-run restore
(once per installation; a worker thread is the fix if testers report crackle on the first project), the link queues
and the patch-RAM lock (under 0.5 %).

## User fixes beside it

From the Discord cross-check of 2026-10-10:

| # | Item | Size |
|---|---|---|
| 1 | AU in Ableton opens the old Gearmulator screen (new report) | S-M |
| 2 | Monomachine: amp envelopes wrong; a piano-roll note resets another track; song LOOP row (B-038) | M |
| 3 | Windows and Linux: get logs, fix the start freeze (B-035), the crackle (B-036), the blank window (B-033) | M-L |
| 4 | Hardware MIDI on a real MD and MM with the two owners who offered (roadmap F1) | M |
| 5 | Drag and drop on Windows and Linux (I-017) | S-M |
| 6 | Remove the MCP server from the editors | S |
| 7 | In-app update signing (I-005) | M |

Later, after the clean-up: emulation CPU step 2, port MD features to MM, the custom-firmware spike.

## Order

Phase 0 → 1 → 2 → 3 first, on a branch, while user fixes 1, 2 and 6 go on main. Phase 4 and the rest after
Phase 3 is green.
