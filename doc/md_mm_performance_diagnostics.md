# MD/MM performance diagnostics

Performance capture helps distinguish sustained CPU overload from lock waits,
resampling overhead, JIT events, and work advancing a replacement machine during
project restore. It is off by default and never uploads a report.

## Collect a report

1. Right-click the instrument background in the Machinedrum Editor or the Monomachine Editor and open
   **Developer** (the Performance diagnostics entries moved there in 0.3.5). Knobs may have their own parameter menu.
2. Click **Start Performance Capture**. Reopen the submenu to check its status.
3. Reproduce the crackling or slow playback using your normal host settings.
4. Click **Stop Performance Capture**, then **Open Log Folder**.
5. Share the `performance-<timestamp>-<unique-id>.jsonl` file and whether you heard
   the problem. Panel actions are recorded automatically. If there were several
   incidents, approximate audible-problem times still help identify which one.

The folder is `logs/` beside the product's `roms/` and `config/` folders, beneath
its public data directory. This also respects `GEARMULATOR_DATA_ROOT`.
Each plugin instance and each capture gets a separate filename. Existing reports
are preserved. Capture automatically stops after ten minutes or 8 MiB. Start a
new capture if you need another recording. The submenu displays file/directory errors
and automatic stops. The worker flushes approximately every two seconds, so a
crash can lose the most recent interval. Stopping or closing the plugin flushes
the report; a callback still in flight at stop may be absent from the final data.

Capture is session-only: reopening the plugin does not restore the capture setting.
For hosts without an open editor, set `GEARMULATOR_RT_INSTRUMENTATION=1` (also
accepts `true`/`TRUE`) **before starting the host**. MD/MM then automatically starts
an exported capture for each instance. Remove the environment variable to prevent
this on later launches. The context menu can stop an environment-started capture.

## Switches for testers

The editors run the emulated Machinedrum and Monomachine faster than they used to (step 1 of the emulation
CPU work: four speed-ups, listed below). One switch turns all of them off, so you can find out in minutes
whether a CPU problem, a click or a timing difference comes from them. **Both positions produce the same
audio, bit for bit**, checked on both firmwares (audio, RAM, SRAM, loader RAM, patch RAM and MIDI out are
compared); only the host CPU differs. With the speed-ups off the emulation runs the code that the
speed-ups replaced, and costs about what it did before step 1: a few per cent less, because the one thing
that stays on is the `processUC` gating (part B of L5; it only skips work that cannot change anything, so
it has no switch).

- **Menu: Developer > Speed-ups off (legacy emulation, slower).** Ticked is the old
  emulation. The choice applies at once, without restarting, and is kept in the plug-in's settings (key
  `legacyEmulation`) and across project loads.
- **`GEARMULATOR_MDMM_SPEEDUPS=0`** starts every new session with the speed-ups off. Set it as an
  environment variable **before starting the host**; remove it to go back to the default. It only sets the
  starting position: a ticked menu item also starts a session with the speed-ups off, and the menu can
  change either at run time.

The speed-ups the switch controls:

- **L1** the idle skip of the ColdFire's wait loop tests its inputs once per batch instead of at every
  skipped instruction.
- **L11** the ColdFire memory fast lane: plain RAM is read and written directly and 32-bit accesses take
  one step. Off, every access goes through the complete memory map, as two 16-bit steps for a long word.
- **L2b** the Machinedrum DSP catch-ups run under one cycle-bounded entry instead of block by block.
- **L5** the ColdFire's timers and UART transmitters are stepped when they reach an event, not after
  every instruction.

Two finer environment variables exist for narrowing a problem down further. Both are environment
variables only:

- **`GEARMULATOR_MDMM_SIM_DEFERRAL=0`** turns off L5 alone (timers and UART transmitters stepped after
  every instruction). It also holds when the menu leaves the speed-ups on.
- **`GEARMULATOR_MDMM_BOUNDED_JIT=0`** is older than the speed-ups and unrelated to them: it runs the DSP
  background slices instruction by instruction instead of in cycle-bounded entries. The speed-ups switch
  does not change it, and it does not change L2b.

If a problem goes away with the speed-ups off, send the performance report from a capture with them off
and one with them on.

## Contents and interpretation

The file is JSON Lines: every complete line is independently parseable JSON.
Schema 2 contains a `session` header, cumulative `summary` records, selected
`callback` records, panel/transport `event` records, and an `end` record (`stopped`
or `capture_limit`). Older schema-1 reports have no action timeline. Durations
are nanoseconds. Summary elapsed time and callback start time are relative to
capture start; they use a monotonic clock.

The session identifies the product, version/build revision, host/plugin format,
OS, CPU model/vendor, logical CPU count, reported clock speed, and the process
and machine architecture (whether the editor ran translated, see [The session record](#the-session-record)). Callback records include actual block size and sample rate,
device sample rate, resampler mode, DSP clock percentage, active output layout,
transport/bypass/offline state, and incoming MIDI event/byte counts. The action
timeline records panel button states and encoder movements. No MIDI payload,
audio, presets, project contents, or firmware data are recorded. Panel actions
can reveal which trigger keys a person pressed.

The realtime load histogram counts duration divided by the nominal block budget
in six buckets: `<25%`, `25–50%`, `50–75%`, `75–100%`, `100–150%`, `≥150%`.
Offline callbacks are counted separately and excluded from both this histogram
and estimated deadline overruns. Deadline estimates are **not host/driver xrun
notifications**. Host scheduling and buffering can differ from the nominal budget.
Inter-callback spacing is context, not proof that the host scheduled us late.

The callback trace records the first callback, periodic context samples (every
1024 callbacks), realtime callbacks consuming at least 75% of their budget,
lock waits of at least 100 microseconds, callbacks with JIT compilation, and
callbacks advancing both live and restore machines. Each trace entry correlates
those events with the same callback's timings. A fixed 512-entry queue drops new
trace records when full; `slowCallbacksDropped` reports this. Summaries continue
to count all callbacks. A trace is a selected sample, not an exhaustive profiler.

| Observation | Next investigation or comparison |
| --- | --- |
| Most realtime callbacks are near/exceeding budget and device time dominates | Sustained emulation cost. Compare a larger host block, fewer active outputs, and the same project on another CPU. |
| Slow callbacks spend a large share waiting for the synth lock | Inspect concurrent state/settings/controller operations and how long they hold that lock. |
| Resampler time substantially exceeds device time | Compare resampler modes and host/device sample-rate combinations. |
| Spikes coincide with `dualMachine` and substantial `deferredNanoseconds` | Investigate scheduling or moving restore preparation off the audio thread. |
| Spikes coincide with live/deferred JIT counts | Investigate warmup/precompilation for that machine; confirm compilation cost with a profiler. |
| `translated` is `true` | The editor ran as an Intel build on an Arm machine: expect about twice the CPU. Compare with a capture of the same project in a host opened as an Apple silicon app (Windows on Arm: there is no native build yet). |
| Callback timing is comfortably within budget despite audible trouble | Investigate host/driver scheduling and other processing; the capture does not prove the entire audio system met its deadline. |

Timings are **inclusive**: synth time includes its lock wait and resampler work;
resampler time includes device processing; device time includes deferred-machine
advancement. Do not add these totals together. Resampler minus device time is an
approximation of resampling/dispatch overhead. JIT counts measure code-generation
entries; this feature does not measure compilation duration or split device cost
into MCU/DSP/peripheral phases. Those remain targeted profiling follow-ups.

Snapshots use relaxed atomics and are approximate while recording/resetting.
Individual trace entries have synchronized ownership and are not torn. Measuring
adds some overhead, so compare captures with the same instrumentation settings.
Reported CPU MHz is descriptive and does not track dynamic frequency or throttling.
Timings measure elapsed wall time, which can include OS preemption and waits;
device-dominated time alone does not prove continuous CPU execution in emulation.

## The session record

Every field is a string. The first line of a report looks like this (shortened):

```json
{"type":"session","schema":2,"duration_unit":"ns","product":"Machinedrum Editor","version":"0.4.0",
 "host":"Ableton Live","format":"VST3","os":"macOS 15.1","cpu":"VirtualApple @ 2.50GHz",
 "architecture":"x86","process_arch":"x86_64","machine_arch":"arm64","translated":"true", ...}
```

| Field | Meaning |
| --- | --- |
| `type`, `schema`, `duration_unit` | `session`, `2`, `ns` (all durations are nanoseconds). |
| `product`, `version`, `revision` | The editor, its version, and the source revision it was built from (`-modified` when the tree had uncommitted changes). |
| `started` | When the capture started (ISO 8601). |
| `host`, `format` | The host application and the plug-in format (VST3, AU, Standalone). |
| `os` | The operating system's name. |
| `cpu`, `cpu_vendor`, `logical_cpus`, `cpu_mhz` | What the system reports. `cpu_mhz` is descriptive only. Under Rosetta, `cpu` reads `VirtualApple @ 2.50GHz`, whatever the real chip is. |
| `architecture` | `arm` or `x86`: what the build was compiled for (the slice of a universal binary that the host loaded). |
| `pointer_bits` | `64` or `32`. |
| `process_arch` | The architecture of the running build: `arm64`, `x86_64`, `x86` (or `unknown`). |
| `machine_arch` | The architecture of the hardware under it: macOS names Apple silicon (`arm64`) when the process is translated, and otherwise gives the build's own; Windows asks the system (`IsWow64Process2`). |
| `translated` | `true` when an Intel build runs on an Arm machine: Rosetta 2 on an Apple silicon Mac (`sysctl.proc_translated` is 1; where that cannot be read, a `VirtualApple` CPU stands in), or the x64 emulation of Windows on Arm. Otherwise `false`. A 32-bit build on 64-bit Windows is not translated in this sense. |
| `resampler_modes` | The key to the `resamplerMode` numbers in callback records. |
| `notes` | A reminder of how to read the timings. |

`translated` settles one common cause of a Mac using about twice the CPU of another: a
DAW opened as an Intel app (**Get Info > Open using Rosetta**, or a DAW without an Apple
silicon build) loads the editor's Intel code, and the whole emulator then runs translated.
The same fields are in the first line of the start-up log (**Open Log Folder**,
`editor-*.log`): `... CPU VirtualApple @ 2.50GHz, process x86_64 on arm64, translated`. A
translated editor also says so on screen at start, once a session, with a **Don't show
again** button (config key `rosettaNoticeDismissed`).

A developer build (`gearmulator_MDMM_DIAGNOSTICS=ON`) can pretend, to see the notice and the
fields on a Mac without Rosetta: start the host or the standalone with
`GEARMULATOR_MDMM_FAKE_ROSETTA=1`. A release build ignores it.

## Panel and transport timeline

Event `timeNanoseconds` uses the same capture-relative monotonic clock as callback
`startNanoseconds`. Sort by timestamp when correlating actions and slow callbacks:
the writer drains separate queues, so JSONL line order is not chronological.
`sequence` identifies an event recording attempt; concurrent producers can enqueue
attempts out of order.

Panel events include the raw `model`, `command`, and `argument`, plus readable
labels. Button packets describe a complete row state: `buttonsDown` and `buttonsUp`
list controls separated by `|`. They are not individual button-edge claims.
Encoder packets include `encoder` and signed `steps` (a decimal string).

| Panel phase | Meaning |
| --- | --- |
| `submitted` | The editor is about to send a panel packet, before acquiring the synth lock. |
| `result` | The send returned; the same `inputId` pairs it with submission. `accepted` is the device queue API result. A false result can involve queue overflow/recovery, not only a missing device. |
| `delivered` | An audio callback handed the packet to the emulated MCU's UART2 RX queue. `callbackIndex` and `deferred` identify that callback and live/replacement machine. This does not establish when firmware processed it. |

Delivery has `inputId:0`: the functional panel queue is unchanged. Recovery,
coalescing and inputs from other sources prevent a guaranteed one-to-one match
with editor submissions. Match packet values and timestamps with that limitation;
do not infer exact per-input firmware latency from an ambiguous match.

The common editor send path covers panel presses, releases, modifier chords,
latches, generated releases and encoder detents. It records inputs, not confirmed
firmware actions or a full replay. Untouched buttons held before capture are not
reconstructed. Preset/settings operations and LCD/LED changes are not a separate
semantic timeline.

`host_transport` records the first observed state (`initial:true`) and subsequent
changes to playing, offline, bypassed or transport availability. With `known:false`,
the host did not provide transport information; ignore `playing`. Internal panel
Play and the host's Play state are distinct. Host seek/loop positions, tempo and
automation values are not recorded. There is no automatic audible-glitch marker.

A separate preallocated 1024-entry queue serves panel and transport events. Each
producer makes one nonblocking attempt; contention or a full queue drops only the
diagnostic event. Summaries expose `timelineEvents` (attempts) and
`timelineEventsDropped`. Slow-callback volume cannot fill the action queue. Capture
limits, stopping and event drops can truncate the timeline, so absence of an event
does not prove an action never occurred.

## Implementation and validation

The audio path records into preallocated storage, with no report formatting,
filesystem calls, worker notification, or waiting for queue capacity. A dedicated
`PerformanceReport` worker is the sole trace consumer and handles control labels, formatting,
file I/O, periodic flushes and limits. The processor owns the writer and destroys
it before destroying the synth. Capture stops on open/write/flush failures.

`synthLibPerformanceReportTest` covers correlated phase/context capture, offline
accounting, queue overflow and concurrent draining, actual synth processing at
44.1/48/96 kHz, JSON escaping, output, file errors, automatic limits and rapid
start/stop. Timeline tests cover disabled capture, input/result pairing, callback
delivery, initial and changing host state, session boundaries, bounded overflow,
immediate return under contention, concurrent producers/draining, and off-thread
label formatting. `synthLibAudioInstrumentationTest` runs the prepared-audio
allocation regression with capture scopes, transport and delivery events enabled.
These are included in the focused CI gate.

The session's architecture fields come from the editor, not from `synthLib` (the report
takes any list of name/value pairs): `mdProcessArchTest` covers the translated decision over
injected macOS and Windows cases and the three fields, and `mdRosettaNoticeTest` the on-screen
notice, its once-a-session rule and **Don't show again** kept in a real config file.

## Gate tool: bit-exact audio and host cost (mdmmPerfGateTest)

`mdmmPerfGateTest` (source/elektron/md/mdLibTest, built with `BUILD_TESTING=ON`, manual: it needs a ROM) is the
check behind "both positions produce the same audio, bit for bit" above and behind the local release gate
(doc/release/LOCAL-GATE.md). It boots MD OS 1.63 or MM OS 1.32B headless, sets up a scenario, renders a stopped
phase (with a fixed burst of host notes in its first 1.5 s), presses PLAY and renders a playing phase, and prints
hashes of everything the emulation produced next to its host cost per audio frame.

```
mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8] [--scenario <name>] [--outputs stereo|all]
                 [--golden <goldens.json> [--record]]
```

**Scenarios.** Without `--scenario` the tool does what it always did (`md-busy` for md, `mm-a01` for mm), with
the same hashes.

| Scenario | What plays |
|---|---|
| `md-busy` | the MD's current pattern with every track on every other 16th |
| `md-factory` | the pattern the MD boots with, untouched |
| `md-song` | song mode (set by SysEx 0x10, then LOAD SONG): `md-busy`'s pattern twice, a sparser copy in the next slot with tracks 5-8 muted at 150 BPM, a loop to the start; rows of 16 steps |
| `mm-a01` | the pattern the MM boots with (the factory A01) |
| `mm-busy` | that pattern with a trig in ALL on every other 16th of the six synth tracks (sent on SYSEX RECV as the editor sends it, then LOAD PATTERN) |
| `mm-song` | song mode (SysEx 0x6C and 0x10): `mm-busy`'s pattern twice, a sparser copy with tracks 1-3 muted, a loop to the start; rows of 16 steps |

**Output.** One line per phase (`MD stopped:`, `MD playing:`, or `MM ...`) and a `combined` line, `key=value`
fields in a fixed order; newer fields come after the older ones.

- Per phase: `cpu_pct`, `instr_per_frame`, `cycles_per_frame`, `uc_cycles_per_frame`, `audio_hash` (left then
  right samples), then `frames`, `nonsilent_frames`, `playhead_moves` (the sequencer's step, read once per
  block; the run fails if it never moves while playing), `instr_median_per_frame`. With `--outputs all` also
  `outputs_hash` and `out0_hash` to `out5_hash`: each of the six outputs, and the hash of the six.
- `combined`: `audio_hash` (both phases chained), `ram_hash`, `sram_hash`, `loader_hash`, `patch_hash`,
  `midi_out_hash` and `midi_out_events` (everything the machine sent, with its frame), then the PLAY press:
  `press_frame` (counted from the start of the stopped phase), `press_frames` (PLAY held 40 ms, 40 ms after) and
  `press_hash`; and `stream_frames` and `stream_hash`, every frame from the start of the stopped phase to the end
  of the playing phase with no gap. With `--outputs all` also `stream_outputs_hash`.

The phases cover the same frames as before the press was hashed, so `audio_hash`, the memory hashes and the MIDI
hash of `md-busy` and `mm-a01` are the values the step 1 work was checked against.

**Goldens.** `--golden <file>` compares the run with the file's entry for its key
`<ROM fingerprint>/<scenario>/<stereo|all>/speedups-<on|off>/<seconds>s` and exits 1, with one
`mdmmPerfGateTest golden: differs <field> golden=... run=...` line per difference, when any compared field
differs or is missing on either side, or when the file has no entry for the key. The speed-ups position is the
machine's own (`GEARMULATOR_MDMM_SPEEDUPS=0` gives `speedups-off`); both positions have their own entries, which
hold the same hashes. The last line is `mdmmPerfGateTest golden: PASS (...)` or `... FAIL (...)`.
`--golden <file> --record` writes or replaces that one entry and keeps the others. An entry's `compare` object holds
every hash and count (`frames`, `nonsilent_frames`, `playhead_moves`, `midi_out_events`, `press_frame`,
`press_frames`, `stream_frames`); its `info` object holds the recording run's instruction figures and guest cycles
per frame, which a compare prints beside the run's (with the change in per cent) and never judges. The goldens
contain nothing from the firmware.

The committed goldens are `source/elektron/md/mdLibTest/goldens/mdmm-goldens.json`: every scenario, both
`--outputs` modes, both speed-ups positions, 8 s per phase. Record them again only for a change that is meant to
change what the emulation produces (a fix to the emulation, a new scenario or field), from the build that has that
change, and say in the commit why the values changed. `GEARMULATOR_MDMM_MIDI_PACING` changes MIDI timing, and so
the hashes; the tool says so when it is set.

**Benches.** `mdCpuBenchTest` and `mmCpuBenchTest` (`<ROM> [instances] [seconds]`) measure the CPU per
instance. A playing figure counts only if the machine played: each instance's playhead (MD) or step counter (MM),
read every 50 ms, must move at least twice after PLAY, or the bench prints `FAIL instance n did not play` and
exits 1.
