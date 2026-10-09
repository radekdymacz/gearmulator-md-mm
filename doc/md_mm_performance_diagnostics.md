# MD/MM performance diagnostics

Performance capture helps distinguish sustained CPU overload from lock waits,
resampling overhead, JIT events, and work advancing a replacement machine during
project restore. It is off by default and never uploads a report.

## Collect a report

1. Right-click the instrument background in the Machinedrum Editor or the Monomachine Editor and open
   **Developer** (the Performance diagnostics entries moved there in 0.3.5). Knobs may have their own parameter menu.
2. Click **Start performance capture**. Reopen the submenu to check its status.
3. Reproduce the crackling or slow playback using your normal host settings.
4. Click **Stop performance capture**, then **Open logs folder**.
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
