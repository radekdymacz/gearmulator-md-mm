# MIDI Learn: thread ownership

Developer note. The user guide is [midi_learn.md](midi_learn.md).

The MIDI Learn translator (`source/jucePluginLib/midiLearnTranslator.*`) is not thread-safe. This note says where
the threads meet, what the Machinedrum and Monomachine Editors do about it today, and the redesign that has to land
before MIDI mapping is switched on again in those editors.

## The problem

`pluginLib::Processor::addMidiEvent` hands every event that does not come from the device to
`MidiLearnTranslator::processMidiInput`. That call runs on whichever thread delivered the event: the audio thread
(host MIDI in `processBlock`), the JUCE MIDI-input thread (physical ports), and the message thread (editor). On those
threads the translator:

- reads `m_preset` and its index map `m_midiToMappingIndex` (`findMapping`) while the message thread, or the host's
  state thread through `loadChunkData` and `loadDefaultMidiLearnPreset`, can be replacing both in `setPreset`.
  A replace during a read is a use after free.
- reads `m_isLearning` and `m_learnInputSources`, plain fields written on the message thread.
- writes `m_learningValues` in `handleLearning`; two MIDI threads can run it at once.
- calls `onLearningProgress` and `onMappingLearned` in place. The callbacks set a new preset, write the default
  preset file (`saveDefaultMidiLearnPreset`) and publish to the editor, all on a realtime thread. The Machinedrum
  callback (`mdMidiLearnCommands.cpp`, LearnStart) also assigns `onMappingLearned = nullptr` from inside itself,
  which destroys the running closure, and then calls `publish()` through the freed capture. The upstream editor
  (`jucePluginEditorLib/pluginEditor.cpp`, `setMidiLearnMode`) has the same shape.
- applies a mapping with `Parameter::setUnnormalizedValueNotifyingHost` (`applyMappingToParam`), so a host change
  gesture starts on the audio thread.

## Today (MD and MM)

MIDI mapping is off: `MDMM_MIDI_MAPPING` is 0 (`source/elektron/md/deskHost/deskHost.h`). The learn commands are
refused and the pages hide the CONTROL workspace. Since the fix of 2026-10, the editors also make no translator at
all: `AudioPluginAudioProcessor::usesMidiLearn()` returns the switch, and `Processor::getController` creates the
translator only when it is true. So no learn code runs on any thread, no default preset is loaded, and no `MDLN`
chunk is written to or read from the project (an old project's chunk is skipped). Every user of
`getMidiLearnTranslator()` already handles null. `mdProcessorHooksTest` (no firmware) and `mdSessionFirmwareTest`
assert that the translator follows the switch.

The other synths keep the upstream behaviour (`usesMidiLearn()` defaults to true).

## The redesign (prerequisite for turning mapping on)

One owner per piece of state; the realtime threads read immutable values and post messages, never call back.

1. **The message thread owns the preset and the learn state.** `setPreset`, `startLearning`, `cancelLearning`,
   `setLearnInputSources`, chunk load and the default preset file all run there. A call from another thread (the
   host's `setStateInformation` thread) posts to the message thread with `callAsync`, guarded by the static
   instance-set pattern (CLAUDE.md, "callAsync safety").

2. **The realtime side reads one immutable table.** Build a `MappingTable` (the mappings plus the
   `(channel << 8) | controller` index, resolved to parameter indices, no strings) on the message thread whenever the
   preset changes, and publish it as `std::shared_ptr<const MappingTable>` with `std::atomic_store`
   (C++17; `std::atomic<std::shared_ptr>` once C++20 is allowed). `processMidiInput` takes one `std::atomic_load`
   per event and never sees a half-built table. The last reference to an old table must not drop on the audio
   thread: the message thread keeps the previous table alive for a generation (or retires tables through a
   lock-free queue that it drains), so no free happens in the callback.

3. **Learn flags are atomics.** `m_isLearning` and `m_learnInputSources` become `std::atomic<bool>` and
   `std::atomic<uint8_t>`; the realtime side only reads them.

4. **While learning, the realtime side only enqueues.** An event that would be learned goes into a bounded
   single-producer FIFO per MIDI thread (audio, MIDI input), or one MPSC FIFO, as a plain `(source, a, b, c)`
   record, and is consumed. The message thread drains it on a timer, runs `handleLearning` (the value collection,
   mode detection), fires `onLearningProgress` and `onMappingLearned` there, sets the preset, saves the default
   preset file and publishes. Nothing of that runs on a realtime thread.

5. **Mapped values reach parameters from the message thread too,** or through a parameter API that is explicitly
   realtime-safe. The realtime side resolves the mapping in the table and posts `(parameter index, value)`; the
   message thread applies it with the change gesture. (If a host needs sample accuracy for learned controllers,
   that is a separate design: the value would have to go to the device directly, not through `Parameter`.)

6. **Callbacks are cleared after they return.** `mdMidiLearnCommands.cpp` LearnStart: copy the work out, clear
   `onMappingLearned` after the call completes (for example the translator clears the slot itself before invoking a
   moved-out copy), then publish. Same fix in `jucePluginEditorLib/pluginEditor.cpp`.

### Tests to add with it

- Learning completion runs on the message thread: record the thread id in `onMappingLearned` and compare it with
  `MessageManager::getInstance()->isThisTheMessageThread()`.
- No file I/O on the audio thread: a learn completed from `processBlock` does not call `saveDefaultMidiLearnPreset`
  there (a counting hook on the processor).
- A ThreadSanitizer stress test: one thread calls `processMidiInput` in a loop with mapped and unmapped CCs while the
  main thread swaps presets and starts and cancels learning; no reports, no crash.
- Extend the switch check in `mdSessionFirmwareTest` (and `mdProcessorHooksTest`) for the "on" position: a
  translator exists and a learned CC moves the parameter.

### Turning MIDI mapping on again

Land the redesign in `jucePluginLib` (upstream code: keep the change narrow and offer it upstream), add the tests
above, then set `MDMM_MIDI_MAPPING` to 1. `usesMidiLearn()` follows the switch, so nothing else changes.
