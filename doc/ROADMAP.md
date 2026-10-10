# Roadmap: Machinedrum Editor + Monomachine Editor

*Decisions taken 2026-10-09 (Radek, with Claude Code). Engineering and product direction only;
marketing is in [marketing/GTM.md](../marketing/GTM.md). The CPU numbers come from
[RESEARCH-emulation-cpu.md](modern-ux/RESEARCH-emulation-cpu.md); the fork numbers from a trial
merge on 2026-10-09.*

## Summary

- We specialise: Machinedrum and Monomachine only. Everything else in the Gearmulator tree goes.
- We own the whole stack, the emulator cores included. No merges from upstream, no upstream PRs.
- We watch upstream monthly and copy single commits that help us.
- CPU is the main technical win. Evidence so far: about a third less (MD playing 44.4 → ~28.5 % of one core,
  MM 54.1 → ~36.8 %) if every researched lever lands. Anything beyond that is research, not a promise.
- 1.0 is gated on stability and hardware MIDI, not on CPU levers. Order: refresh the CPU baseline → platform
  failures and HW MIDI → MM GLOBAL and the song LOOP fix → MM step-selection parity → individually validated
  CPU improvements. Clean-up fits around them, time-boxed. An uncertain emulation change never holds 1.0 back.

## 1. Where we stand (2026-10-09)

| Layer | Lines (shipped code, no tests/docs/third-party) | Share |
|---|---|---|
| Gearmulator upstream (DSP56300 emulator 49k, framework libraries 74k, 68k wrapper 3k) | ~125,600 | 58 % |
| joelanders (mdLib hardware model, first plug-in UI, DSP and ColdFire fixes, framework edits) | ~28,200 | 13 % |
| Us (the editors, data layer, desk models, updater) | ~64,500 | 30 % |

- Upstream has moved 449 commits since the fork point (2026-07-29) and restructured its source tree.
- A full sync: ~100 conflict hunks in the framework, ~105 in the DSP JIT; 2-4 weeks. Our `source/elektron` code: no conflicts.
- We already have upstream's DSP speed-up of August (~20 %). Of the later upstream work, about 7 commits help us.
- None of joelanders' 317 commits (63 DSP, 9 ColdFire/68k) are upstream. His last activity: 2026-09-21.
- CPU today (Apple M4 Pro, % of one core, playing): Machinedrum 44, Monomachine 54. Most of it is the chips
  waiting (poll and NOP loops, silent-voice stubs) and keeping three chips in step, not sound maths.

## 2. Decisions

| # | Decision | Why |
|---|---|---|
| D1 | Specialise: Machinedrum and Monomachine only | Less code, less headache; every assumption we can make is a speed-up |
| D2 | Delete the other synths, tools, skins and build options (~61k lines plus assets) | Never shipped; slows builds, CI and every reader |
| D3 | Own the framework, the DSP56300 emulator and the ColdFire/68k core | Merging costs weeks and returns little |
| D4 | Watch upstream monthly; copy single commits, never merge | Cheap; we keep the few changes that help |
| D5 | No upstream PRs | Radek, 2026-10-09 |
| D6 | Optimise for the two firmwares (MD OS 1.63, MM OS 1.32B) | Upstream cannot; we can |
| D7 | The emulator stays at 44.1 kHz | The real machines' rate; changing it changes pitch and tempo |

## 3. Upstream watch (monthly)

1. List new upstream commits in the DSP emulator, the 68k core and the framework.
2. Claude marks each: take / skip / watch, one line why. Radek decides.
3. A taken commit is ported alone, built, and passes the firmware tests (ROMs, on Radek's Mac).

First batch (all to take):

| Commit | Area | What |
|---|---|---|
| `7d69d7a9` | DSP JIT | Checks a loop's end only while a loop runs |
| `e8989e41` | DSP JIT | Fast-forwards serial-status poll loops (measure: our hottest polls are DMA and Port C) |
| `4f15f23c`, `cfcb2d85` + `hostAudioWorkgroup` | DSP + framework | Emulation threads join the DAW's audio workgroup (macOS): fewer crackles |
| `cf23896f` | synthLib | No copy of every MIDI event on the audio thread |
| `0671f478` | synthLib | The resampler is not rebuilt on the audio thread |
| `ee238102` | synthLib | Latency is reported off the audio thread |
| `bd221511` | synthLib | The resampler's delay is part of the reported latency |

Watch: upstream's new ColdFire V2 core and MCF5206e model (`3c0de7e6`, 2026-10-07) — the MD/MM main chip.
Not used by any upstream product yet.

## 4. CPU plan

Lever names (L1-L14) and gates are those of [RESEARCH-emulation-cpu.md](modern-ux/RESEARCH-emulation-cpu.md) §3 and §5.

### 4.1 Baseline first

- L1, L11, L2b and L5 have landed in Radek's local checkout (with a legacy-emulation switch for comparison); they are
  not on `origin/main` yet. They are part of the research's step 1, so they must not be counted again.
- Before any new lever: a fresh baseline on the current code (`mdCpuBenchTest` / `mmCpuBenchTest`, host cycles per
  frame, `scripts/mdmm-rt-check.sh`, core capacity), MD and MM, stopped and playing. Every figure below is relative
  to that baseline, never to the old 44.4 / 54.1.

### 4.2 Steps

| Step | Work | Audio | Expected (from research) | Status / when |
|---|---|---|---|---|
| 1 | Clean-up (D2), time-boxed to 3 days; keep shared diagnostics, test tools and build dependencies | – | 0 % | around the 1.0 work |
| 2 | Rest of research step 1: the upstream batch (§3); evaluate (not adopt) joelanders' DSP PR #20, which saw unexplained callback overruns | bit-exact gate | step 1 total ≈ 0.87 of baseline, minus what has landed | before 1.0 if each passes its gate |
| 3 | Silent-voice skip (part of L4) on MM and on MD as a prototype; benchmark it on its own. On MD most of L4's gain depends on L3 (step 5) | bit-exact gate (DSP state hash at every due) | MD small without L3; MM ~6-15 % | measure, then decide |
| 4 | Sleep when silent (see 4.3) | behaviour change: acceptance tests required | idle instances near 0 % (unmeasured) | only if 4.3 passes; else after 1.0 |
| 5 | L3 exact MD serial-port deadlines, then full L4, L9, L10 | L3 changes MD audio once (first trigger ~24 ms later, no hardware reference): a separate correctness decision, timing check against a real MD, listening A/B, Radek signs off | MD ≈ 0.65 / 0.73 (stopped / playing), MM ≈ 0.69 / 0.77 of baseline | after 1.0 |
| 6 | L6 PGO (arm64 slice), huge pages, no virtual calls on the bus | must render bit-identically to ThinLTO | ≥ 5 % or drop | after 1.0 |

Expected end point of steps 2-6 (research): MD playing ~28.5 %, MM ~36.8 % of one core — about a third less.

### 4.3 Sleep when silent: definition before code

"Stopped, silent, no MIDI" is not enough. The emulator must keep running while any of these holds: audio input
routed to the machine or being sampled; effect tails or a decaying voice above the noise floor; MIDI clock or other
MIDI going out; a SysEx, SDS or editor transfer pending; an editor command queued; the firmware's own timers
mid-action (UI, LED, autosave). Freezing the firmware may change the next note even when the output is silent.
Acceptance tests: wake on MIDI note, play, editor edit, host transport and sampling; next-note audio and timing
compared with an instance that never slept; multiple instances in a DAW. If wake-up correctness stays uncertain,
it waits until after 1.0.

### 4.4 Experiments (no promises)

Each has a success criterion and a stop criterion before work starts; bit-exact is the acceptance requirement, not
a result.

| Experiment | Idea | Go on if | Stop if |
|---|---|---|---|
| E1 Pre-decoded ColdFire code | Decode each ROM instruction once (1-4 MB) | ≥ 3 % MD / ≥ 5 % MM, hashes equal | below that, or any hash differs |
| E2 Precompiled firmware (LLVM), dead flags | Ahead-of-time DSP code; prototype one hot routine | ≥ 1.5× on that routine, hashes equal | below that after 2 weeks |
| E3 Native hot routines, SIMD across voices | Replace a routine, proven equal by tests | ≥ 2 % per routine, equal output | not provably equal |
| E4 Precompiled ColdFire | As E2 for the UC (MM first) | ≥ 5 % MM | below that |

Rejected: a lower sample rate, GPU processing, a full native rewrite of the DSP code, chips on separate cores
(the DSP-to-DSP link talks every 96 cycles).

User-facing extras from the same work:
- Boot snapshot: restore the state after boot instead of booting; instances open almost at once.
- Render ahead (opt-in, "smooth playback"): fewer drop-outs in playback, more latency; off for live playing.

## 5. Sample rate

- The plug-in converts the host rate to 44.1 kHz and skips the conversion when the host runs at 44.1 kHz.
- To do: standalone app defaults to 44.1 kHz where the interface allows; a guide line ("44.1 kHz for the
  lowest CPU"); a hint in Setup when the host runs at another rate; `bd221511` (latency).

## 6. Features

From a feature-completeness review of main at `9674857a` (2026-10-09): both editors against the hardware
(MD OS 1.63, MM OS 1.32B), [IDEAS.md](release/IDEAS.md) and [BUGS.md](release/BUGS.md). Neither editor shows the
emulated front panel (the firmware LCD only appears at boot), so what an editor does not offer natively cannot be
reached at all.

### Where the editors stand

- **Machinedrum Editor: nearly complete.** Machines, sounds, LFOs, master FX, the kit library, sequencing (locks,
  accents, slides, copy/paste at every level, cued pattern changes), songs, chains, mutes, tap tempo, GLOBAL,
  the sampler (SDS send, RAM chops), SysEx import/export and DAW automation are native.
  [GAP-REVIEW.md](modern-ux/GAP-REVIEW.md) (2026-09-27) is mostly superseded.
- **Monomachine Editor: core complete; editing comfort and globals behind.** All 22 machines, the piano roll,
  AMP/FLT/LFO trig tracks, the arpeggiator, the 6 MIDI tracks, songs, chains, mutes, the multi map and
  import/export are native. Detail: [MM-PARITY-2026-10-09.md](modern-ux/MM-PARITY-2026-10-09.md).
- **Beyond the hardware (both):** undo everywhere, every track on one grid, drawn lock lanes, GEN and MUTATE,
  solo, SysEx import with a per-slot preview, host tempo follow, screen and audio recording.

### Gaps by priority

| # | Gap | Machine | Effort | When |
|---|---|---|---|---|
| F1 | HW MIDI proven on a real MD **and** a real MM, separately: connect/reconnect, edits both ways, mutes, transport, transfers, imported globals; Windows MIDI (B-037). FR-09 is the MD demo only | both | L | before 1.0 |
| F2 | Stability: MM crackle (B-036), Windows blank window/freeze (B-029, B-035), Linux blank window (B-033), CPU in Live (B-005), save/autosave stalls (B-034). Gate in §7 | both | L | before 1.0 |
| F3 | GLOBAL page: ~~done for 0.4.1~~ (B-051): the MD's panel, shared: the 8 slots, MIDI channels, program change, sync in and out, routing mode, Reset to defaults. Left: master tune; the global's still undecoded bytes (0x07-0x09, 0x0d-0x11, 0x30-0x35, the six CONTROL bytes 0x36-0x3b: no MIDI effect found) | MM | S | before 1.0 |
| F4 | Step selection, step menu and keyboard shortcuts, as in the MD editor (I-001) | MM | L | before 1.0 |
| F5 | Song management: names, copy, clear, save-as, a song library (I-011); the MM song LOOP target (B-038) | both | M | B-038 before 1.0, the rest 1.x |
| F6 | CTR-AL kits: the guide lists a limit no code enforces; test on the firmware, then fix or drop it | MD | S-M | before 1.0 |
| F7 | Per-step swing trigs; EDIT ALL toggles for accent, slide and swing | MD | S-M | 1.x |
| F8 | Sample manager: RAM→ROM copy, erase a slot, export a sample to WAV | MD | M | 1.x |
| F9 | Sync and control not needed by F1: the rest of CONTROL IN/OUT, clock, program change (MM); trig in A/B editable (MD); pattern change and master FX from the DAW | both | M | 1.x |
| F10 | Piano roll: a clear note-length handle (I-010), draw mode and box select (I-007) | MM | M | 1.x |
| F11 | Track copy, super copy, melody copy | MM | M | 1.x |
| F12 | Projects: save and load a whole set in the standalone (the +Drive equivalent) | both | M | 1.x |
| F13 | The Control / MIDI Learn workspace, now hidden | both | M-L | later |
| F14 | DigiPRO user waveforms (MKII) | MM | L | later |
| F15 | TurboMIDI for bulk transfers to a real machine | both | L | later |
| F16 | Small wishes: turn off the unsaved-kit warning (I-009), show incoming notes (I-002), follow the last-played track (I-003) | both | S each | later |

### To verify

- CTR-AL (F6): no code refuses it; needs a firmware test.
- HW MIDI has only run against the emulator; local control and trig in A/B are marked "not verified" in the editor.
- [mm-manual-mapping.md](modern-ux/mm-manual-mapping.md) is stale: rotate, chains and live recording are built.

## 7. 1.0 release gate

1.0 ships when all of these hold, whatever the CPU levers have reached:

| Gate | Pass condition |
|---|---|
| Stability (F2) | No callback overrun or audible drop-out while: playing, editing (lock drags, kit loads), saving/restoring a project, closing and reopening the window, and running several instances. Checked with the existing diagnostics ([md_mm_performance_diagnostics.md](md_mm_performance_diagnostics.md), `scripts/mdmm-rt-check.sh`, core capacity) on macOS (Apple silicon and Intel), Windows and Linux, at 64, 128 and 512 frames, 44.1 and 48 kHz |
| HW MIDI (F1) | The F1 checklist passes on a real MD and a real MM; anything that does not is labelled beta or hidden |
| Parity (F3, F4, F5 LOOP, F6) | Done, each with a journey test |
| Regression | Firmware suite, journeys, pluginval and auval green on the shipped files |
| Signing | Signed and notarised macOS build (LAUNCH-CHECKLIST §1) |

Lower average CPU is not evidence of crackle-free playback; the stability gate is measured on its own.

## 8. Rules

- Every optimisation passes the bit-exact gate (codec output, RAM and DSP-state hashes) unless it is marked as an
  audio or behaviour change. Marked changes (L3, L7, sleep when silent) need their own correctness decision,
  acceptance tests and Radek's sign-off.
- One change at a time, measured against the fresh baseline before and after.
- The ROM tests run on Radek's Mac; cloud sessions cannot run them.
- Machinedrum- or Monomachine-specific tricks live in our code (mdLib) and are marked as such.
