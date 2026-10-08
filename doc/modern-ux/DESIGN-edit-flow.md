# Design: the edit flow under fast gestures (knob drags, lock draws, all-track tweaks)

- Worktree `review/edit-flow` (from `chore/upstream-seams` @ 7b75a809), 2026-09-29. Hammock style: facts first, then candidates, critique and a recommendation. Control All (§4.3) and paced whole-document delivery (§4.4, pacing only) are built, uncommitted: see **Built** below.
- **The owner's report:** dragging a knob or drawing locks in the Machinedrum or Monomachine Editor makes the VST3 glitch in a DAW, "because it is busy updating parameters". The follow-up: "even a small parameter change seems to flood".
- **Evidence marks:**
  - **[measured]**: by the rigs in §1.1, on this Mac (Apple M4 Pro).
  - **[code]**: read in the source, cited as file:line.
  - **[inferred]**: reasoned, not measured. DAW behaviour is always this, because no DAW was run; a DAW was simulated.

## Built (2026-09-29, uncommitted)

The owner's two items; the rest of §4 (host-value dedupe, DAW gesture grouping, 30 Hz publishing, the loop guard) is not built.

**Control All (FUNCTION + a DATA ENTRY knob, "CTRL + ALL"; Alt/Option-drag on the page).**
- One intent: the `tweak` row in `MdModel::commands()` (`{k, group: syn|fx|rt, knob 0-7, d, t?}`, schema regenerated), a pure edit in `mdDeskEdit.cpp` (`tweak`, `controlAllReaches`, `controlAllLeads`): one working-kit change, one undo step per `g`.
- The firmware's own rules, measured (`mdDeskFirmwareTest <ROM> tweak`, 7 cases and a 60-move drag, all equal to the model, clamps at 0 and 127 included): MIDI and CTR machines never move; a RAM recorder (RAM-R) not on its synthesis page; every other machine moves on every knob, also one it has no name for (GND-EMPTY, GND-SIN knob 5, INP-GA knob 5). The firmware tweaks from its **selected track**: with a MIDI, CTR or RAM-R track selected only that track moves (a MIDI track's value becomes 255). So the adapter asks the machine which track is selected once a gesture, selects one that leads (the gesture's track `t` if it can) and waits for the status.
- Delivery (`MdMachine`, chosen by facts: panel keys, knob-page telemetry, not recording): the emulator gets its own gesture: `hold:function` (new in `md::panelKeySequence`), the page key if the knob page differs, one encoder packet a step for each tick's net steps (at most 16 a turn, only when ≤ 4 packets are still on their way), `release:function` 150 ms after the last turn; pending until the memory image shows every track (`Expectation`), no read-back, no CCs. Without the panel (HW MIDI, no telemetry): the changed values coalesced, one CC per (track, knob) per 50 ms.
- A multi-step encoder packet (value +5) works alone, but loses steps less than ~10 ms apart and after SET STATUS track (`editFlowBenchTest tweak`): the plug-in keeps one packet a step.
- Page: `mdDeskLive.js` (Alt on any `syn`/`fx`/`rt` box, the LFO section's SPD/DEPTH/SHMIX as `rt` knobs 5-7, the curve editors' handles through `sendEditor` → `tweakEditor`), one `tweak` per frame with its steps summed (`Bridge.send` `merge`), the view's writes from `tweakWrites` (`mdDeskModel.js`, tested in `mdDeskModelTest.js`). MM: the mockup's `controlAll` / `controlAllFrom` (`src/60-ui.js`, `src/130-main.js`, synced): Alt-drag on a Sound value or a curve editor handle moves the other synth tracks by the same delta (a machine without that SYN parameter stays, MIDI tracks never); the host gets one working-kit `set` a frame, one undo step (tested in `mmConvertTest.js`).

**Fix after 0.2.1 (the Sound page).** While FUNCTION + a knob steps the tracks, the machine reports each step as CCs, which reach the desk as host parameter changes; `MdMachine::onHostKitParam` folded them into the edit's expected values, so a Sound page drag after a Mix gesture ended part way (reproduced in the standalone with `GEARMULATOR_MDSTUDIO_SELFTEST=p7tweak`, a diagnostics build). The gesture's own knobs are now guarded until memory shows it; the expected values stay alive while the gesture waits; and when it is over, what memory does not show goes as CCs (page keys or track selections the machine does not follow give the gesture up). Tests: `mdDeskTest` (the steps' CCs, a knob page never reached, a lost FUNCTION), `mdDeskFirmwareTest tweak` (non-leading selected tracks, Mix then Sound drags), the page self-test `p7tweak` (a Mix fader, then the Sound page's synthesis, effects and LFO boxes and a curve editor: PASS 5/5).

**Paced whole-document delivery.** `deskCore::PushSlot` (`deskPush.h`) with `PushPolicy {minIntervalMs 200, quietMs 150}` in both Profiles (the MD's quiet is 750 ms since B-010, below): a dump at once or latest-wins after 200 ms, no read-back while values keep coming, one read-back request at 150 ms of quiet (or over a wire: no faster than the dump's DIN time); the timeout counts from the request. MD: `mdDataLink::Session::pushPattern/pushSong(_, askBack=false)`; the read-back of the editor's own pattern push re-reads the kit only when the pattern links another kit. MM: the same slot around SYSEX RECV (a dump still queued on RECV goes first).

**Sequencer timing (B-010, 2026-10-08).** Pacing made the edits cheap for the host but not for the firmware: the emulated MIDI UART took each pattern dump's 5,410 bytes at once and sent each read-back at once, and the firmware's UART interrupt then ran back to back (about 17 ms either way), holding up the sequencer. `md::Hardware` now paces the Elektron SysEx into UART1 (`setSysexIngressRate`, 125 KB/s, realtime bytes in between, other SysEx at once as before) and UART1's transmitter (`setMidiTransmitRate`, 125 KB/s, a holding register and a shift register like the panel UART's); the MD Profile's read-back waits for 750 ms of quiet. `mdDeskFirmwareTest <MD ROM> plocktiming` (120 BPM, TEMPO OUT, the clock and play head timed per 64-frame block; `plocktiming-strict` fails on a late tick other than a read-back's):

| 120 BPM, MIDI clock (20.83 ms) | before: sd / worst | after: sd / worst |
|---|---|---|
| idle | 0.69 / 0.94 ms | 0.69 / 0.94 ms |
| lock-lane drag (a step every 16 ms, 6 s: 31 dumps) | 3.83 / 16.9 ms | 0.75 / 3.84 ms (the read-back) |
| clicks (a lock every 250 ms, own gestures: 24 dumps) | 4.78 / 16.9 ms, 24 read-backs | 0.69 / 0.94 ms, 1 read-back |
| wheel (one step every 30 ms) | 3.95 / 16.9 ms | 0.69 / 0.94 ms |
| the page's first library read (6 s) | 8.38 / 19.4 ms | 2.31 / 8.19 ms |

Single messages at once vs paced (`PLOCK_PROBE=1`): a pattern dump in 17.4 ms of machine time, 15-17 ms off; the same bytes at 31-250 KB/s, none; a pattern read-back 15-20 ms off unpaced, 6-8 ms at 31-250 KB/s (what remains is the firmware building the dump). The emulator's CPU time a block (64 frames, 1451 us) rises from about 730 us idle to about 810 us while drawing (before: about 710 and 775 us; a busy Mac, so roughly). At 62.5 KB/s and at DIN speed the MM's sixth DigiPRO voice stays silent in `mmDigiproFirmwareTest` (31, 125 and 250 KB/s pass; timing-dependent, not understood), so 125 KB/s. `GEARMULATOR_MDMM_MIDI_PACING=0` turns the pacing off (`in` or `out`: only that side).

Borrowed from upstream Gearmulator (dsp56300/gearmulator, read, not merged): `synthLib::Plugin` no longer hands the device the raw pieces of a chunked SysEx besides the joined message (0f1aeed14), and only the Elektron SysEx the firmware acts on is paced, so a host's tuning dump or other foreign SysEx never holds up the notes behind it (the concern of 474c8e091, which drops such SysEx before the JE-8086's rate limiter). Upstream paces all MIDI input at 31.25 kbaud and thins dense CC streams (b5faead4c) and purges superseded queued SysEx (6360a2b41, 116ae3bad); here channel messages are not paced, so no CC backlog builds, and the desk's `PushSlot` already keeps one dump per document on its way (latest wins), so there is nothing to purge.

**Audio-thread load (B-014, 2026-10-08).** The emulation runs on the host's audio thread, and a whole pattern dump keeps the emulated 68k busy while the firmware reads and applies it (no idle-loop skip): about 1.5 times the idle work for 50-70 ms a dump, about 2x for a read-back. Five dumps a second (the 200 ms push interval) were heard as drop-outs on an M1 in Live. Since then every SysEx the MD desk sends goes through one stream, `mdDesk::SysexOut` with the Profile's `StreamPolicy`: a bulk message (a dump, 256 bytes or more) waits for the one before it to have had its time at MIDI cable speed (3125 B/s: a pattern 1.73 s), a newer dump of the same document replaces one still waiting, and any message after a dump waits until the machine has read it (125 KB/s) and 250 ms more, so a read-back request never reaches a firmware still applying a dump; work that must follow a dump (the working kit's live edits after a pattern dump reloads it) is queued behind it (`after`). CCs and notes go at once when nothing waits, else behind the SysEx before them (a machine change, then its values); panel keys do not wait. The push interval follows the stream (`pushPolicy`: at least the dump's stream time); timeouts add the stream's delay. `desk.tx` (the TX LED) is lit while the stream sends. `GEARMULATOR_MDMM_EDIT_RATE=<bytes/s>` raises the rate (0: no stream, as 0.3.3). Measured with `scripts/mdmm-rt-check.sh` (retired instructions per 64-frame block, independent of other load), before -> after: dumps in a 6 s drag 31 -> 5, time the blocks run hot 257 -> 45 ms a second, 128-frame buffers over 50 M instructions 21.7 -> 3.7 a second, 256-frame over 100 M 8.5 -> 1.5; clicks 202 -> 39 ms a second; `plocktiming-strict` passes (worst clock tick 6.3-6.7 ms at a gesture's read-back, as before). The first edit of a gesture is heard at once; during a drag the machine follows at most 1.73 s behind. In the VST3 with its page (`scripts/mdmm-page-load.sh`: the diagnostics VST3 in `scripts/vst3EditorHost --background`, the page's self-test `p4locks` drawing 256 new locks a round at 30 a second; M4 Pro, % of one core, before -> after): the host process while drawing 78-91 -> 62-72 %, the page's WebKit content 55-61 -> 43-51 %, WebKit GPU 129-146 -> 105-125 %; neither grows with the locks the pattern holds (0 to 700 locks, 44 locked parameters). Playing without edits the GPU process alone takes about half a core (the page's drawing; B-005).

**Every user action (B-014, 2026-10-08).** The one stream (`deskCore::Stream`, `deskCore/deskStream.h`; `mdDesk::SysexOut` is its name in the MD desk) carries everything both desks send to their machine, in lanes: SysEx (dumps no faster than a MIDI cable, 3125 B/s, the newest dump of a document replacing a waiting one, anything after a dump waiting until it is read and applied), latest (a value-setting SysEx: tempo, LFO, master effect, routing; at most every 100 ms per value, the newest wins), value (kit CCs, NRPN and mutes: a budget at cable speed with a burst of 64 CCs, the newest value of a parameter wins while it waits, never across SysEx), priority (notes and a key's held value: they pass waiting values, never SysEx) and after (work that must follow, the working kit's edits after a pattern dump). Panel keys and SDS sample packets keep their own pacing (the SYSEX RECV session, the SDS handshake). The MD's live global edits (tempo, routing) are read back once at quiet instead of at every value. `GEARMULATOR_MDMM_EDIT_RATE=<B/s>` raises the rate, `0` turns the stream off (0.3.3).

**Stopped vs playing (0.3.4, the owner's decision).** The cable's pace protects the audio while the sequencer plays; stopped there is nothing to protect. So the stream has two paces (`Stream::setPlaying`, `StreamPolicy::stopped()`): while the machine plays, the Profile's policy (MIDI cable speed for every lane); while it stands, dumps go back to back as fast as the machine reads them (125 KB/s, `settleBetweenDumps` off), values and value SysEx at once. In both, nothing but the next dump goes while the firmware reads and applies a dump (a request waits for the read and the 250 ms settle), and the newest dump of a document (and the newest value of a parameter) replaces a waiting one. The desks tell the stream every tick (MD: the telemetry's playing, a machine without telemetry counts as playing; MM: its playing flag); play starting mid-transfer sends the rest at cable speed, play stopping speeds the queue up at once. The push interval follows the pace in force. Test: `mdDeskTest` (testSysexStream: the mode switch); measured with `mdDeskFirmwareTest <MD ROM> syximport` (a full backup imported as the session does, `SYX_PLAYING=1` while playing, `SYX_KINDS=pattern` the patterns only): 128 patterns stopped 13.7 s, while playing 231 s, every pattern in the firmware as in the file (with the settle between dumps too: 46.9 s, the same result). Read-backs (B-020): the desks ask for at most `deskCore::g_maxReadBacks` (2) at a time, and a read-back's timeout starts once the stream is quiet, so an import's hundreds of read-backs are answered one after the other instead of timing out together.

Measured with `mdDeskFirmwareTest <MD ROM> actions` and `mmDeskFirmwareTest <MM ROM> actions`: each action repeated at a person's rate for 6-8 s while a 120 BPM pattern plays, the plug-in's delivery, retired instructions per 64-frame block. Heavy: a 128-frame buffer over 50 M or a 256-frame one over 100 M instructions (idle about 27 / 55). Hot: a 5-block mean over 1.3x idle. Before = 0.3.3 (`GEARMULATOR_MDMM_EDIT_RATE=0`, the global read back at every value), after = the stream. `scripts/mdmm-rt-check.sh` runs both machines against a hot-time budget (100 ms/s): before fails 6 MD actions, after passes every one.

| MD action | dumps | CCs | bytes/s | heavy 128 /s | heavy 256 /s | hot ms/s | worst 256 (M) |
|---|---|---|---|---|---|---|---|
| idle | 0 → 0 | 0 → 0 | 61 → 61 | 0.0 → 0.0 | 0.0 → 0.0 | **4 → 7** | 84.6 → 81.7 |
| lockDrag | 31 → 5 | 0 → 0 | 23985 → 3669 | 17.0 → 3.1 | 7.8 → 1.2 | **252 → 44** | 111.1 → 109.1 |
| lockClicks | 24 → 5 | 0 → 0 | 19224 → 3176 | 14.5 → 2.7 | 7.7 → 1.2 | **205 → 41** | 111.6 → 110.1 |
| genR | 15 → 5 | 0 → 0 | 12287 → 3183 | 8.7 → 3.0 | 3.9 → 1.2 | **137 → 44** | 108.9 → 108.4 |
| genAllR | 8 → 5 | 0 → 0 | 6582 → 3187 | 17.8 → 4.6 | 9.0 → 2.8 | **106 → 61** | 113.2 → 110.0 |
| genDrag | 31 → 5 | 0 → 0 | 23995 → 3176 | 18.6 → 2.2 | 8.6 → 1.0 | **256 → 45** | 108.5 → 108.5 |
| mutR | 0 → 0 | 574 → 574 | 339 → 349 | 0.0 → 0.0 | 0.0 → 0.0 | **18 → 18** | 93.7 → 96.2 |
| mutAllR | 0 → 0 | 4824 → 4824 | 2186 → 2177 | 1.0 → 0.1 | 0.3 → 0.0 | **30 → 39** | 100.1 → 98.6 |
| paramDrag | 0 → 0 | 750 → 750 | 423 → 433 | 0.0 → 0.0 | 0.0 → 0.0 | **28 → 28** | 87.9 → 87.0 |
| levelDrag | 0 → 0 | 750 → 750 | 433 → 423 | 0.0 → 0.0 | 0.0 → 0.0 | **32 → 30** | 92.1 → 89.8 |
| machine | 0 → 0 | 104 → 96 | 134 → 131 | 0.0 → 0.0 | 0.0 → 0.0 | **5 → 12** | 90.8 → 93.5 |
| controlAll | 0 → 0 | 0 → 0 | 116 → 116 | 0.0 → 0.0 | 0.0 → 0.0 | **11 → 11** | 88.9 → 87.5 |
| muteBurst | 0 → 0 | 200 → 200 | 158 → 158 | 0.0 → 0.0 | 0.0 → 0.0 | **13 → 11** | 84.8 → 88.3 |
| copyPaste | 8 → 4 | 966 → 995 | 7525 → 2449 | 6.6 → 1.8 | 3.2 → 0.8 | **83 → 35** | 112.7 → 108.9 |
| clearUndo | 9 → 5 | 1053 → 1085 | 7680 → 2573 | 6.0 → 1.9 | 3.1 → 0.7 | **90 → 35** | 108.7 → 110.0 |
| lengthDrag | 8 → 5 | 936 → 1085 | 6858 → 3788 | 15.0 → 3.6 | 8.1 → 1.6 | **105 → 61** | 113.0 → 110.0 |
| tempoDrag | 0 → 0 | 0 → 0 | 619 → 155 | 0.6 → 0.6 | 0.0 → 0.3 | **71 → 12** | 92.8 → 106.7 |
| keyboard | 0 → 0 | 0 → 0 | 97 → 87 | 0.0 → 0.0 | 0.0 → 0.0 | **4 → 4** | 81.5 → 86.7 |
| undoRedo | 0 → 0 | 0 → 0 | 98 → 78 | 0.2 → 0.0 | 0.0 → 0.0 | **9 → 6** | 91.9 → 89.4 |
| select | 0 → 0 | 0 → 0 | 105 → 105 | 4.3 → 4.5 | 1.6 → 1.8 | **61 → 55** | 110.2 → 110.2 |
| kitLoad | 0 → 0 | 0 → 0 | 153 → 153 | 5.2 → 4.6 | 2.2 → 2.2 | **54 → 58** | 110.2 → 122.0 |
| after | 0 → 0 | 0 → 0 | 60 → 60 | 0.0 → 0.0 | 0.0 → 0.0 | **6 → 1** | 84.4 → 80.1 |

| MM action | dumps | CCs | bytes/s | heavy 128 /s | heavy 256 /s | hot ms/s | worst 256 (M) |
|---|---|---|---|---|---|---|---|
| idle | 0 → 0 | 0 → 0 | 54 → 54 | 0.0 → 0.0 | 0.0 → 0.0 | **0 → 0** | 84.4 → 84.2 |
| lockDrag | 28 → 7 | 0 → 0 | 2084 → 562 | 3.8 → 0.9 | 1.0 → 0.3 | **79 → 34** | 104.6 → 102.1 |
| genR | 14 → 15 | 0 → 0 | 1144 → 1220 | 5.4 → 5.5 | 2.0 → 1.7 | **77 → 72** | 107.4 → 107.1 |
| genAllR | 8 → 8 | 0 → 0 | 682 → 682 | 3.1 → 3.0 | 1.3 → 1.2 | **54 → 51** | 106.6 → 105.1 |
| pianoPaint | 28 → 7 | 0 → 0 | 2191 → 585 | 4.2 → 1.2 | 1.3 → 0.4 | **81 → 35** | 104.7 → 102.8 |
| mutR | 0 → 0 | 462 → 462 | 208 → 208 | 0.0 → 0.0 | 0.0 → 0.0 | **8 → 19** | 98.0 → 98.5 |
| mutAllR | 0 → 0 | 1523 → 1523 | 561 → 561 | 0.0 → 0.0 | 0.0 → 0.0 | **6 → 8** | 95.7 → 95.1 |
| paramDrag | 0 → 0 | 375 → 375 | 179 → 179 | 0.0 → 0.0 | 0.0 → 0.0 | **18 → 16** | 94.4 → 94.1 |
| levelDrag | 0 → 0 | 375 → 375 | 179 → 179 | 0.0 → 0.0 | 0.0 → 0.0 | **18 → 22** | 94.7 → 95.7 |
| machine | 0 → 0 | 96 → 96 | 101 → 101 | 0.0 → 0.1 | 0.0 → 0.0 | **1 → 3** | 92.5 → 95.0 |
| muteBurst | 0 → 0 | 200 → 200 | 121 → 121 | 0.0 → 0.0 | 0.0 → 0.0 | **0 → 0** | 88.3 → 89.8 |
| copyPaste | 3 → 2 | 0 → 0 | 288 → 210 | 0.9 → 0.8 | 0.4 → 0.2 | **22 → 20** | 104.0 → 104.5 |
| clearUndo | 4 → 4 | 0 → 0 | 491 → 491 | 2.8 → 2.6 | 1.0 → 1.1 | **48 → 46** | 105.4 → 104.4 |
| lengthDrag | 0 → 0 | 0 → 0 | 714 → 220 | 1.8 → 0.9 | 0.7 → 0.1 | **62 → 24** | 101.9 → 103.8 |
| transpose | 0 → 0 | 0 → 0 | 727 → 223 | 3.3 → 0.9 | 0.2 → 0.2 | **60 → 16** | 103.2 → 103.2 |
| tempoDrag | 0 → 0 | 0 → 0 | 256 → 121 | 0.0 → 0.0 | 0.0 → 0.0 | **5 → 6** | 97.7 → 98.5 |
| keyboard | 0 → 0 | 0 → 0 | 71 → 71 | 0.0 → 0.0 | 0.0 → 0.0 | **0 → 0** | 84.6 → 92.7 |
| undoRedo | 0 → 0 | 0 → 0 | 350 → 325 | 3.2 → 2.3 | 1.0 → 0.7 | **48 → 50** | 104.2 → 103.8 |
| select | 0 → 0 | 0 → 0 | 61 → 61 | 0.0 → 0.0 | 0.0 → 0.0 | **3 → 2** | 97.6 → 98.2 |
| loadKit | 0 → 0 | 0 → 0 | 72 → 72 | 0.9 → 1.0 | 0.5 → 0.4 | **14 → 13** | 101.7 → 102.4 |
| after | 0 → 0 | 0 → 0 | 54 → 54 | 0.0 → 0.0 | 0.0 → 0.0 | **0 → 0** | 86.8 → 85.3 |

Not changed by the stream: pattern select and kit load (the machine sends the documents the desk reads: 55-60 ms/s hot), a sample load (SDS, paced by the machine's handshake: about 160 ms/s of mild extra work while it runs, no heavy buffers), MUTATE of the whole kit (a kit of CCs, spread over about 0.6 s; the firmware's cost is small). A SysEx import goes at cable speed too, each document's dump then its read-back.

**The page (WebKit, B-014).** `scripts/mdmm-page-load.sh`: the diagnostics VST3 in `scripts/vst3EditorHost --background`, the page's self-test `p4locks`; `MDMM_PAGE_PROBE=1` switches parts of the styling off for a phase to attribute the cost. Playing with no edits, WebKit's GPU process took about half a core: the playhead marked a column of 16 step cells every step (`.st.ph`, which draws nothing since the soft playhead), and WebKit repainted the cells with their box-shadow glows. The cells are no longer marked: GPU 42-58 % -> 6-10 % of a core while playing, WebContent 6-13 % -> 6-8 %, stopped 1-4 %. A lock-lane drag (the page's full renders held, Held "lane") costs about 17-20 % WebContent and 20-25 % GPU. Edits not held by a gesture (a wheel run, a GEN or MUTATE value drag at 30 a second) still re-render the grid at every document: about 50 % WebContent and more than a core of GPU, almost all of it the step cells' box-shadows (every shadow off: 28 %). Next: hold the renders for those gestures as the lane does, or draw the cells' bevel and glow without box-shadow. The Monomachine page marks its step cells the same way (`.mst.ph`, visible there): not measured yet.

**The proof in a real host** (`mdVst3EditFlowHost`, our target: the built VST3 bundle through JUCE's VST3 hosting, `processBlock` on a real-time time-constraint thread, 48 kHz; the page's messages replayed by the bundle's env-gated edit-flow driver `mdEditFlowDriver.cpp`, built only with `-Dgearmulator_MDMM_EDITFLOW_DRIVER=ON`; 4 s at 60 moves/s, two interleaved runs before/after, the same driver in both bundles; before = this worktree's base with the page's old 16-param tweak):

| | MD 128 before → after | MD 512 before → after | MM 128 before → after | MM 512 before → after |
|---|---|---|---|---|
| Control All: CCs / host notifications / gestures (4 s) | 3,840 / 3,840 / 3,840 → **0 / 0 / 0** (≈230 panel packets) | same | 1,440 → 1,440 (6 a move, 1 per track per frame) | same |
| Control All: session time per move | 2.19 ms → **0.18-0.20 ms** | 2.0 → 0.16-0.18 ms | 0.31 → 0.30 ms | 0.28 → 0.28-0.30 ms |
| Control All: load vs idle | +1.1/-1.0 → +0.0/-0.5 pts | +1.4/+2.2 → +2.9/+2.7 | +1.6/+1.9 → +1.1/+2.3 | +2.5/+3.4 → +2.9/+3.8 |
| Control All: to the page (4 s) | 21.6 MB → 1.4 MB | 20.9 → 1.3 MB | 0.9 → 0.9 MB | 0.8 → 0.8 MB |
| Lock draw: dumps / read-backs (5.5 s) | 70 / 70 → **21 / 1** | 64-66 / same → **21 / 1** | 67-69 / same → **18 / 1** | 50-52 / same → **18 / 1** |
| Lock draw: load vs idle | **+22.7 / +19.5 → +0.9 / +0.4 pts** | **+21.9 / +24.9 → +4.3 / +4.2** | +7.0 / +7.1 → +1.3 / +2.7 | +7.6 / +9.6 → +2.1 / +2.8 |
| Lock draw: worst 512-frame block | | 9.0-9.3 ms → 9.0-9.3 ms (< 10.67) | | 8.8-9.2 → 8.5-8.7 ms |
| Undo steps per gesture | 1 → 1 | | | |

At 128 frames this Mac already misses 2-6 % of the 2.67 ms deadlines with no edits in this host (both bundles, within noise); the draws add none beyond that after the change.

**pluginval 1.x (JUCE 8.0.3), strictness 10, 48 kHz, blocks 128 and 512, seed 0x1234, scratch HOME with the ROMs:** MD and MM VST3, before (the base's bundles) and after (driver inert): all 24 test groups pass, no crash, no hang; per-group times equal within 0.6 s (MD 123.0 s → 122.6 s in all, MM 46.5 → 47.2 s; `Plugin state restoration` dominates: 104 s MD, 30 s MM). AU not built in this worktree, not run.

**Other rigs:** `mdEditFlowPluginTest` (in-process, not real-time; `tweak60` now sends `tweak`, `lock60` added, MM `tweak60` added): MD tweak60 0 CCs, 0 host notifications, 0.25 ms a move (was 3.1 ms); lock60 1 read-back, load +1.4 (128) / +2.6 (512) points; two 512-frame blocks over (12.7 ms) in that rig, none in the real-time host. `editFlowBenchTest md`: lock 60/s 15 dumps in 3 s, 0 requests while drawing, load 52.3 % vs idle 48.0 %, worst 512-window 82 % (was 93-163 %). The bench's MM lock asks mid-draw (6 in 3 s) because its rig sends each dump synchronously and emulated time jumps past the 150 ms quiet; the plug-in paths ask once.

**Found on the way (not fixed):** a kit with CTR-AL is refused whole: the firmware sets that track's LFO track to 16 and `elektronData::validate` allows 0-15, so every edit of such a kit fails and its doc message is off the contract.

**Tests:** `ctest -E "Plugin|_AU|VST|FirmwareTest"` 76 of 78 (the two known `synthLib` failures), the VST/Plugin ctests 7/7, `mdDeskTest` (paced pushes, a 2 s draw, Control All emulator, wire and skips), `mdDeskModelTest.js`, `mmConvertTest`, `mdDeskFirmwareTest` default, `p4`, `hw`, `hostclock`, `playload`, `tweak`; `mmDeskFirmwareTest` default, `p4`, `hw`, `hostclock`; `mdSessionFirmwareTest`; both sync scripts' checks; `scripts/mdmm-upstream-footprint.sh` 17 files, 0 deleted (no upstream file changed by this work). Standalone MD and MM apps (this worktree, scratch HOME): launch, run 45 s, quit, no crash report.

**Not done or unverified:** the Control All drag and the lock draw by hand (or by a page self-test) in the standalone apps: the page's gesture code is checked in node only (`tweakWrites`, `controlAll`), the session paths through the replayed messages; a DAW; the HW MIDI engines on a real machine (coalesced CCs are unit-tested only); the page's commit as a read-back trigger (the MD page has no commit message; quiet only).

## 0. The short answer

**One small kit change does not flood the emulator [measured].** Through the real processor (the code the VST3 wraps) it is:
- 1 CC (3 bytes) to the firmware;
- 1 host parameter notification inside its own begin/end gesture;
- 2-3 documents to the page (MD 17-18 KB of JSON, MM 7.6 KB).

The firmware echoes nothing back, and nothing is read back or re-pushed. Emulator load does not move: 60.2 → 60.7 % of a 128-frame block, 52.1 → 53.4 % at 512.

**The floods are elsewhere.** Each one multiplies a change, or repeats it every audio block:

| # | Flood | 1 user action → | Measured cost | Root cause |
|---|---|---|---|---|
| 1 | **DAW automation read / host echo.** Every value the host writes becomes a CC, even when unchanged | **1 CC per automated parameter per audio block**: 375 CC/s per parameter at 128 frames; 16 lanes = 6,000 CC/s, 54 KB/s into the firmware (17× DIN). A drag with the host echoing its values sends **2 CCs per change** | MD +7 points (1 lane) and **+17** (16 lanes) at 128 frames; MM +6 and **+12** | `mdController.cpp:27` (`shouldSendRepeatedHostValues` true) → `parameter.cpp:285-298` (every host value → `sendParameterChangeNow`, no dedupe) → `mdController.cpp:397-414,441-492` (every publication is a new revision, so it is transmitted) |
| 2 | **Lock-lane draws.** A whole pattern goes out, plus a read-back, per edit | ~20 dumps + 20 read-backs/s, 107 KB/s each way (MD) | MD **+31 to +35 points**, 512-frame windows at 163 % | `mdDataLink.cpp:66-73`, `mdDeskMachine.cpp:312-327,937-957`, `mmDeskMachine.cpp:415-443`; `PushSlot` is paced only by the emulator's ~50 ms round trip |
| 3 | **MD all-track tweak (Alt-drag = FUNCTION + knob).** The page sends one `param` per track | **16 messages per mouse move**: 960 CCs/s, 960 host notifications and 960 gestures/s, 91 KB of JSON to the page per move, 3.1 ms of message-thread time per move | emulator +0 points (CCs are cheap); message thread ~19 % at 60 moves/s; the DAW gets 960 automation touches/s | `mdDeskLive.js:80-97` (a `sendParam` per track, line 95) |
| 4 | **One DAW gesture per value.** Every UI value is wrapped in its own begin/end | 60 begin/end pairs/s during a drag (960 with a tweak) | inferred: the DAW's UI and undo churn, with a write pass per touch | `parameter.cpp:195-207` + `ScopedChangeGesture` `parameter.cpp:433-441` (Origin::Ui), called from `mdStudioLink.cpp:92`, `mmStudioLink.cpp:114` |
| 5 | **Whole documents to the page** on every flush | MD drag: 10.5 KB/change (the 5.3 KB working kit twice, plus a 2.2 KB machine document); MM 3.6 KB/change | message thread 0.25-0.4 ms/change (MD), 0.45-0.6 ms (MM); page JS ~1 ms per re-render | `deskCore.h:437-458`, `deskDesk.h:76-123` |
| 6 | **Device MIDI out looped back** (only if the user routes the device's output to the host, and the DAW routes it back in) | The MD's note output echoes back and grows **62 → 1,084 → 3,998** notes per 3-4 s | MD +7 to +9 points | `processor.cpp:911-919`. Device→Host is **off** by default (`midiRoutingMatrix.cpp:37-58`), so this needs a user routing change |

**Baseline, not caused by edits:** at 48 kHz and 128 frames the emulator already needs 60 % (MD) and 68-74 % (MM) of each block. Blocks go over their 2.67 ms deadline 0-27 times per 3 s **with no edits** (max 3.2-18 ms) [measured]. At 512 frames, idle blocks never went over. **Any extra audio-thread work at 128 frames glitches**, and flood 1 is extra audio-thread work at the rate of the audio blocks themselves.

**Order of the fixes by value:**
1. dedupe host values (flood 1);
2. pace whole-document delivery (flood 2);
3. the tweak as one intent (flood 3);
4. one DAW gesture per page gesture (flood 4);
5. publish on the page's clock (flood 5);
6. a loop guard (flood 6).

Section 4 has the details; the total is about 7.5-8 days.

## 1. The facts

### 1.1 The rigs (all uncommitted)

- **`source/elektron/md/mdJucePlugin/mdEditFlowPluginTest.cpp`** (a target in our `mdmmPlugins.cmake`) runs **the real plug-in path**:
  - `AudioPluginAudioProcessor`, with its desk session and controller;
  - an audio thread calling `processBlock` at real time (48 kHz; 128 or 512 frames), like a DAW;
  - the page's exact messages replayed on the message thread (the CFRunLoop, so the session's and controller's timers run).

  It counts every hop:
  - messages and bytes, session → page;
  - host parameter notifications, gestures and `updateHostDisplay` (an `AudioProcessorListener`, which is what the VST3 wrapper is);
  - the controller's CCs and sync requests;
  - bytes the firmware consumed (`midiRxConsumedCount`);
  - what the device sends: SysEx via `evDeviceSysex`; CCs and notes through a tap that turns Device → Host routing on;
  - parameter changes by origin;
  - `processBlock` time.

  It also has DAW modes:
  - **echo**: the host plays back each notified value on the audio thread, as a VST3 host feeds edits to the processor;
  - **read**: automation lanes, the value every block;
  - **loop**: MIDI out → MIDI in.

  Run: `mdEditFlowPluginTest md|mm <ROM> 48000 128|512`; optionally `EDITFLOW_RECORD=<file>` records what the session publishes on ready.
- **`source/elektron/md/mdLibTest/editFlowBenchTest.cpp`** (a target in `mdmmTests.cmake`) runs the firmware and the desk headless, at the adapter level: dumps, read-backs, 64-frame block times. Its `tweak` mode probes FUNCTION + encoder on the MD firmware.
- **The page replay** (`temp/pagereplay/`, gitignored): the shipped MD skin files plus a fake host (`replayHost.js`), fed the real session's recorded documents, run in a browser. It counts what the page sends for a real gesture and times the page's own work.
- Build notes:
  - `temp/cmake_ef` is headless; `temp/cmake_efp` has `-Dgearmulator_BUILD_JUCEPLUGIN=ON` and VST3.
  - Both need `SDKROOT=<Xcode MacOSX26.2.sdk>`: the CommandLineTools 27.0 SDK does not link.
  - The submodules were initialised from `../upstream-seams` (no network).

### 1.2 One change through the real plug-in, hop by hop [measured, MD at 48 kHz / 128 frames unless noted]

| Hop | One click or arrow step on a kit value | Knob drag, 60 values/s (per value) |
|---|---|---|
| Page → session | **1** `param` message (the real page: a click with no movement sends **0**; a drag sends 1 per pointer move, 53 for 60 moves) | 1 |
| Session work (message thread) | 0.29-0.39 ms (MD), 0.5-0.6 ms (MM) | 0.25-0.29 ms (MD), 0.45-0.52 ms (MM: the whole kit parsed and diffed) |
| Host (DAW) | 1 parameter notification + **1 begin/end gesture** | 1 notification + 1 gesture **per value** |
| Controller → firmware | **1 CC** (`getTransmittedAutomationChangeCount` +1) | 1 CC |
| Firmware MIDI in | 3 B above the idle status polls (the desk polls status about 7/s, ~190 B per 3 s) | 5 B per change |
| Firmware → out | **nothing new**: no CC or SysEx echo; its note output (MD) and clock (MM) unchanged | nothing new |
| Controller sync | nothing per change: the 5 s status poll and a global refresh (`0x50`) stay | nothing |
| Read-back or kit re-push | **none**: the working kit comes from the memory image (`mdDeskMachine.cpp:387-390`, `Expectation`) | none |
| Session → page | 2 `workingKit` docs (pending, then from memory) + 1-2 machine documents: MD 17-18 KB, MM 7.6 KB | MD 10.5 KB/change at 128 frames (6.2 KB at 512), MM 3.6 KB |
| Page's own work | ~1 ms of JS per full re-render (Chrome, the shipped MD page) | a partial render while the gesture runs |
| `processBlock` | 60.7 % average, as idle (60.2 %) | 58-61 % (MD); MM 68-71 % against 71 % idle |

**With the DAW in the loop [measured, simulated host]:**

| Scenario | CCs to the firmware | Load (128 frames) | Note |
|---|---|---|---|
| drag + host echo | 240 per 120 changes (**2×**) | MD 62 %, MM 69 % | the echo equals the value, and is sent again |
| automation read, 1 parameter | **1,121 in 3 s = 375/s** | MD 67.6 % (+7), MM 74.8 % (+6) | nothing changed; the host re-asserted the lane every block |
| automation read, 16 parameters | **18,000 in 3 s = 6,000/s**, 54 KB/s | MD **77.3 %** (+17), MM **80.3 %** (+12) | at 512 frames: 4,500 in 3 s, +6 points |
| page tweak (MD Alt-drag), 60 moves/s | 960 per 60 moves (16 a move) | MD 60 % (CCs are cheap) | 960 host notifications + 960 gestures; 5.5 MB of JSON in 3.2 s; 3.1 ms of session time per move |
| loop, 1 change (the Device → Host tap on) | 1 | MD 67-69 % | device notes 62 → 1,084 in 3 s, then 3,998: feedback |

Other findings:
- **`updateHostDisplay`** fires only when the controller applies a kit dump it asked for (program change, sync), never per edit [measured: 0 in every edit scenario].
- **The MM page does not drift:** the working kit round-trips through `MmConvert.kitToPage` → `kitToFw` with 0 differences, so an MM change stays a CC and never becomes a kit dump plus LOAD KIT [measured, node over the kit the session published].
- **Rare 20-25 ms `processBlock` outliers** appeared at 512 frames in 4 of 22 edit scenarios and in none of 8 idle ones. The rig's audio thread is not real-time priority, so they are not attributed [measured; cause inferred: scheduling].

### 1.3 Lock draws and the background read (the desk bench) [measured]

`editFlowBenchTest` timed each 64-frame block (deadline 1,451 µs) and summed 512-frame windows (11.6 ms). Each scenario lasted 3 s while the pattern played. The MD ran twice; compare within one run.

In the table:
- **load** is emulator wall time over audio time;
- **p99** is the 99th percentile of block time in µs;
- **over** counts blocks past their deadline;
- **worst 512** is the worst window, as a percentage of its deadline;
- **win > 100 %** counts the windows past theirs.

| Run | Scenario | Load | p99 | Over | Worst 512 | Win > 100 % | SysEx to the machine | To the page |
|---|---|---|---|---|---|---|---|---|
| MD #1 | idle (playing) | 58.6 % | 1502 | 33 | 87 % | 0 | 7/s status polls | 728 B/s |
| | knob 60/s | 59.4 % | 1517 | 35 | 94 % | 0 | CCs only | **767 KB/s**, 273 msg/s |
| | **lock 60/s** | **93.4 %** | **1976** | **267** | **163 %** | **14** | 20 dumps/s + 20 requests/s, 107 KB/s | 215 KB/s |
| | background library read | 89.8 % | 3316 | 2217 | **436 %** (one block 24.5 ms) | 236 | 36 requests/s, 98 KB/s back | 144 KB/s |
| MD #2 | idle | 47.5 % | 1141 | 1 | 65 % | 0 | | |
| | knob 60/s | 49.3 % | 1147 | 0 | 58 % | 0 | | 767 KB/s |
| | **lock 60/s** | **78.9 %** | 1325 | 5 | 96 % | 0 | 20 dumps/s, 107 KB/s | 215 KB/s |
| | lock 10/s | 69.3 % | 1322 | 1 | 83 % | 0 | 10 dumps + 10 pattern requests + 10 kit requests /s, 54 KB/s | 164 KB/s |
| | lock 4/s | 55.4 % | 1260 | 1 | 83 % | 0 | 4 + 4 + 4 /s, 22 KB/s | 65 KB/s |
| | dumps only 20/s, no read-back | 65.2 % | 1297 | 6 | 87 % | 0 | 108 KB/s | |
| | dumps only 4/s, no read-back | 51.0 % | 1226 | 0 | 81 % | 0 | 22 KB/s | |
| | background library read | 63.4 % | 1455 | 56 | 235 % (one block 13.2 ms) | 7 | | |
| MM | idle | 63.8 % | 1672 | 36 | (one 13 ms outlier) | 2 | | |
| | knob 60/s | 65.9 % | 1467 | 23 | 112 % | 1 | CCs only | 178 KB/s |
| | **lock 60/s** | **72.5 %** | 1539 | 45 | 124 % | 2 | 4.7 dumps/s via SYSEX RECV, 10 KB/s | 209 KB/s |
| | background library read | 81.6 % | 1994 | 793 | 193 % | 31 | 34 requests/s | 162 KB/s |

- **The cost is linear in SysEx through the firmware.** On the MD, a 5.4 KB dump costs about 0.9 points of one core per dump a second; its read-back (request, reply and the kit re-read) about as much again. So a lock draw at 60 edits/s adds +31 to +35 points, at 4 edits/s +8, and 4 dumps/s with no read-back +3.5.
- **Why SysEx costs CPU:** the firmware's idle fast-forward is off while MIDI arrives (`mdhardware.cpp:1366-1394`), and parsing and building dumps is real emulated work [code].
- **The background library read** runs on every first ready of a page, and after an engine change or reset, with single blocks of 13-24 ms. Its long blocks are not isolated yet.
- **The MM** is gentler only because SYSEX RECV throttles it (~210 ms a round trip). Its desk parses, validates and re-encodes an 11 KB pattern JSON per lock move: 0.9 ms an edit.

### 1.4 Who does what, on which thread [code]

- **Page → session.** A `gmbridge://` navigation → `json::parse` → `SessionOf::onPageMessage` → `Desk::onPageMessage` → `flush` → the `WebPageHost` outbox → `javascript:gm.recv(...)`. All on the message thread (`mdWebPageHost.cpp:190-217`, `deskDesk.h:46-78`).
- **Kit value.** `StudioLink::setKitParam` → `Parameter::setUnnormalizedValueNotifyingHost(Ui)`:
  1. `ScopedChangeGesture` begins the host gesture;
  2. `setUnnormalizedValue` → `sendToSynth` → `Controller::sendParameterChange` → `publishAutomationIntent` + drain → `transmitParameterChange` → `Processor::addMidiEvent` (Editor → Device) → the `Plugin` ring;
  3. the device takes it inside `processBlock`, on the audio thread;
  4. `notifyHost`, and the gesture ends.

  Code: `mdStudioLink.cpp:86-94`, `parameter.cpp:195-207,303-315`, `mdController.cpp:397-414,538-553`, `processor.cpp:68-98`, `plugin.cpp:37-47,109-155`.
- **Host values.** The host's `setValue` on the audio thread → `AutomationParameter` (`shouldSendRepeatedHostValues` true) → `sendParameterChangeNow(HostAutomation)` on **every** call, equal or not → a new publication revision → a CC in the block's drain (`processBlock` → `processRealtimeParameterChanges`). Code: `parameter.cpp:276-301`, `mdController.cpp:21-28,441-492,715-718`, `processor.cpp:869`.
- **Device output.** It is fed to `addMidiEvent` (Device → Editor: the controller, `evDeviceSysex` → the desk). It goes to the host only if Device → Host is enabled (`processor.cpp:911-919`).
- **The firmware** runs in `Plugin::process` under `Plugin::m_lock` on the audio thread. The message thread takes that lock only for the probe (~10/s), panel keys and knob turns, never per kit edit (`mdStudioLink.cpp:122-171,247-268`).
- **Per flush:** the desk rebuilds and compares the machine document on every flush, 5-10 per 8 ms step, and sends each dirty document whole (`deskCore.h:437-458`).

## 2. The MD all-track tweak (FUNCTION + a DATA ENTRY knob, manual p.37), and the same on the MM

### 2.1 What the MD page does today [measured, code]

- `mdDeskLive.js:80-97` wraps `setV`: with Alt held, a move of one value sends the same delta for every other track as its own `param` (line 95). RAM, MIDI and CTR machines are skipped.
- **One move = 16 page messages** (replay: 30 moves → 480 messages, 25.9 KB) → 16 core edits, 16 CCs, 16 host notifications and 16 gestures, ~16 working-kit publishes (91 KB). Session time is 3.1 ms per move.
- Undo is already **one step**: the same `g` merges per document (`deskHistory.h:31-55`).
- **Coverage on the Sound page** (the real page's controls, listed by the replay):

  | Controls | Covered? |
  |---|---|
  | value boxes of groups `syn` (8), `fx` (8), `rt` (8) | yes |
  | the LFO section's boxes (`lfo`: SPD, DEPTH, SHMIX; the same kit parameters as routing's LFOS/LFOD/LFOM) | **no** |
  | the four curve editors (`canvas.ed` synth, fx, route, lfo: `sendEditor`, never `setV`) | **no** |
  | the LFO fields (TRCK, PARAM, SHP, UPDTE) | no (they are not DATA ENTRY knobs on the machine either) |
  | the Mix page's level faders | no; the machine's tweak is the DATA ENTRY knobs, not LEVEL [inferred from the manual; not probed] |

### 2.2 What the MD firmware does itself [measured]

`editFlowBenchTest tweak <MD ROM>` held FUNCTION (a panel row state), turned DATA ENTRY B by +5 (5 encoder packets) and released:

```
param 2, before         74   7  15  24  27 106   0  48   8  61   0  35  64  64   0  64
B +5, plain             79   7  15  24  27 106   0  48   8  61   0  35  64  64   0  64
FUNCTION + B +5         84  12  20  29  32 111   5  53  13  66   5  40  69  69   5  69
tracks moved by the one panel gesture: 16 of 16 (7 panel packets)
```

- The firmware applies the relative change to every track, with its own skip and clamp rules.
- **7 panel packets replace 5 × 16 = 80 CCs.**
- The working-kit memory image then shows all 16 tracks at once, so `Expectation` settles the edit with no read-back.

### 2.3 The tweak as one intent

- **The intent (a new core row):** `{"op":"tweak","k":kit,"group":"syn"|"fx"|"rt","knob":0-7,"d":delta,"g":gesture}`.
- **The pure `apply`** builds the new working kit:
  - every track, the same knob of that page;
  - `clamp(v + d)`;
  - the firmware's skips: RAM, MIDI and CTR machines, and a synthesis knob that machine lacks.

  It makes one `Change` of the working kit, and the history merges it per `g`, so a gesture is one undo step.
- **The page** sends `tweak` once per frame, keyed; the frame's deltas are summed, so the latest wins. It replaces the 16 `param` messages. The optimistic overlay shows every track.
- **Delivery:** the adapter chooses by the engine's capabilities (a Profile fact, never a branch on the engine id).
  - **Emulator with panel keys and knob-page telemetry:**
    1. make the machine's DATA ENTRY page the group's (the `page` key until telemetry's `knobPage` says so);
    2. hold FUNCTION;
    3. `turnKnob(knob, d)` for each tick's net delta;
    4. release FUNCTION at quiet.

    This needs a hold/release key pair in `md::panelKeySequence` (`hold:function`, `release:function`). The edit is pending until the memory image reflects it. No read-back, no CCs. The panel-keys rule stays: no dump requests while keys are pending (`panelPending`).
  - **HW MIDI (no panel):** the changed tracks' CCs, **coalesced to one CC per track per tick**, within the DIN budget (16 × 3 B per tick; at 15 Hz that is 720 B/s, 23 % of DIN).
  - **Fallback when FUNCTION + encoder is not possible** (another OS, no telemetry): the same coalesced CCs.
- **Wider Sound-page coverage:**
  - the LFO section's SPD/DEPTH/SHMIX map to `group:"rt", knob:5-7` (the same kit parameters);
  - the curve editors' handle drags go through `tweak` when Alt is held (each handle moves values of one group);
  - level and the LFO fields stay out, because the machine has no tweak for them.

### 2.4 The same on the MM Sound workspace (an editor feature; the MM firmware has none)

- **The rule:** Alt-drag on a Sound knob applies the same delta to all six synth tracks, on the same page and knob index.
  - It skips a track whose machine has no parameter there (the catalogue's empty names).
  - It never touches the MIDI tracks (7-12).
  - The shared pages (AMP, FLT, EFX, LFO) apply to every synth track.
- **Delivery:** the MM's intent is already the whole working kit, so the page sends **one** `set` per frame, keyed per document, which is latest-wins. `deliverKitLive` turns it into at most 6 CCs or NRPNs per tick. It is one undo step (`g`), and no firmware path is needed.
- **The page change:** `mmAdapter.js` handles Alt on the mockup's Sound knobs through a `HOST.tweak(page, index, delta)` host call that edits the view's six tracks. It needs a line in the mockup's host seam list (`src/55-host.js`) and a sync.
- **The joystick and MULTI TRIG ALL TRK** are the MM's own "all tracks" features. They are unchanged, and not the same thing.

## 3. Candidate designs

The contracts to keep:
- `Expectation` (a live edit is pending until memory shows it);
- one undo step per gesture (`g`);
- results after their documents;
- pending/observed;
- the HW engine at 31.25 kbaud;
- the host automation contract: DAW automation must still reach the machine, and must re-assert after the machine changed.

| Candidate | Verdict |
|---|---|
| **A.** Throttle in the pages | rejected as the fix: it cannot bound the host path (flood 1) or the adapter's dumps, and every page would do it differently. The 16 ms per-key bridge batch stays |
| **B.** Live messages during a gesture, one SysEx at the end | kept for kits (already so). Impossible for locks: no live lock message on either machine |
| **C.** Pace whole-document delivery in the adapter (a budgeted `PushSlot`: at most 1 dump per interval, one read-back at quiet, a byte budget shared with the `LoadQueue`) | **adopted** for flood 2 and the background read |
| **D.** Write locks into emulated RAM | rejected: a second delivery path with no HW twin, and firmware internals |
| **E.** Move the desk off the message thread | rejected: the glitch is the audio thread; flood 3's message-thread cost goes with the tweak intent |
| **F.** Publish on the page's clock (F1), later diffs (F2) | **F1 adopted** (flood 5; the tweak's 16 publishes also collapse to 1 per frame); F2 later, if measured necessary |
| **G.** Dedupe host values at the device boundary: send a CC only when the host's value differs from the last one the device was given, or when the firmware's value is known to have moved since | **adopted** for flood 1. A blanket rate limit (the alternative) would drop real automation ramps |
| **H.** One DAW gesture per page gesture (begin at the first value of `g`, end at quiet or at the page's `commit`) instead of one per value | **adopted** for flood 4 |
| **I.** The tweak as one intent, delivered by the engine (FUNCTION + encoder on the emulator, coalesced CCs on HW) | **adopted** for flood 3 (§2.3, §2.4) |

## 4. Recommendation

### 4.1 Host values: dedupe at the device boundary (flood 1)

**The rule:** a host write for a slot is transmitted when its value differs from the slot's *last delivered* value, or when the firmware's value is known to have moved since (a kit dump, a panel turn, a CC from the device, a kit or program switch). Equal host values are dropped **before** they become a publication.

- **Data:**
  - `AutomationSlot` gains `lastDelivered` (value + epoch, one atomic `uint64`).
  - The controller bumps a per-slot `firmwareEpoch` wherever it already observes firmware values: `publishFirmwareValue`, `applyKitParameters`, a kit status change (`mdController.cpp:510-536,720-742,821-856`).
  - `AutomationParameter::setValue` compares against it on the audio thread, lock-free.
- **Why the repeated path existed:** it re-asserts DAW automation after the machine changed. The epoch keeps that; the flood (the same value in the same epoch) is gone.
- **Budget:**
  - automation read with 16 lanes and constant values sends **0 CCs/s**, not 6,000;
  - a ramp sends at most 1 CC per slot per block, and only on a change;
  - a host echo of a UI value adds 0 CCs.
- **Tests:**
  - a unit test for `AutomationParameter`: the same value ×N gives 1 CC; the same value after a kit-dump epoch gives 1 more;
  - `mdEditFlowPluginTest read16`: firmware MIDI in ≤ idle + 1 KB per 3 s, and load within 1 point of idle;
  - `drag+echo`: CCs = changes;
  - `mdAutomationSoakTest` and `mdAutomationRobustnessTest` stay green (the realtime contract).
- **Where:** `mdController.cpp` (ours). `jucePluginLib/parameter.cpp` is upstream's, so it gets a hook only: a virtual `shouldSendHostValue(value)` beside `shouldSendRepeatedHostValues` (`UPSTREAM.md`).

### 4.2 One DAW gesture per page gesture (flood 4)

- `StudioLink` and `MmStudioLink` hold a gesture per parameter and page gesture: `pushChangeGesture()` at the first value of a new `g`, `popChangeGesture()` at quiet (150 ms) or at the page's `commit`. The existing API is at `parameter.h:111-112`.
- The per-value `ScopedChangeGesture` then nests and adds no host calls.
- The session passes `g` down; the desk already has it.
- **Test:** `mdEditFlowPluginTest drag60` shows gestures **1/1** (not 120/120), with notifications = changes.

### 4.3 The tweak intent (flood 3)

As specified in §2.3 (MD) and §2.4 (MM).

**Tests:**
- `mdDeskTest`:
  - a `tweak` of +5 on `syn:1` gives one working-kit change with 16 tracks, the skips honoured, and one undo step;
  - the emulator profile delivers the FUNCTION hold, 5 encoder steps and the release, and **0** CCs;
  - the wire profile delivers at most 16 CCs per tick.
- `mdDeskFirmwareTest tweak`: after the gesture, the firmware's memory image equals `apply`'s kit, on a kit with RAM and CTR machines too.
- `mmConvertTest` and `mmDeskTest`: the page tweak over the catalogue's machines leaves skipped tracks and the MIDI tracks unchanged; one `set` → at most 6 CCs, one pending working kit, one undo step.
- `mdEditFlowPluginTest tweak60`, emulator:
  - at most 1 host notification per track per tick;
  - at most 1 working-kit doc per frame;
  - session time ≤ 0.5 ms per move.
- The page self-tests: an Alt-drag on SPD in the LFO section, and on a curve editor, tweaks all tracks.

### 4.4 Paced whole-document delivery (flood 2) and publishing on the page's clock (flood 5)

```cpp
namespace deskCore {
  struct PushPolicy { double minIntervalMs; double quietMs; };   // per engine, in the Profile
  template<typename T> class PushSlot {                           // pure: the caller gives the time
  public:
    bool want(const T& _value, double _nowMs);                    // send now, or keep as next (latest wins)
    enum class Due { Nothing, Send, ReadBack };
    Due due(double _nowMs) const;                                 // the adapter's tick asks
    const std::optional<T>& next() const;
    void sent(double _nowMs);
    void askedBack(double _nowMs);
    ReadBack onReadBack(const T& _firmware);                      // Confirmed only for the LAST value sent
    void abandon();
  };
  class ByteBudget { public: bool spend(size_t _bytes, double _nowMs); double bytesPerSecond; };
}
```

- **`Profile`** gains `push {minIntervalMs, quietMs}` and `sysexBytesPerSecond`:
  - emulator: `{250, 150}` and 32,000 B/s;
  - HW MIDI: `{DinPacer::wireMs(dump), 150}` and 3,125 B/s.
- **`mdDataLink::Session::pushPattern`, `pushSong` and `pushKit`** send the dump only; the read-back request is made on `Due::ReadBack`.
- **`MdMachine::onPattern`** re-reads the kit only when the pattern's `kit` field changed.
- **`MmMachine::pushDump`**: the same rules through `RecvSession`.
- **The `LoadQueue`** spends only what pushes leave.
- **Publishing:** `Desk::flush()` becomes `pump()`. The session's page transport publishes after each bridge batch and at 30 Hz (the `WebPageHost` timer): each dirty document once, then the machine document if it changed, then results, asks and errors in order.

**Budgets:**
- at most 4 dumps/s per document, and exactly 1 read-back per gesture;
- a lock draw costs at most idle + 8 points (today +31 to +35);
- the background read: no block over 3 ms;
- at most 30 docs/s per document;
- an MD knob drag at most 200 KB/s to the page (today 767 KB/s at 60 edits/s);
- a tweak at most 30 working-kit docs/s.

**Tests:**
- `deskCoreTest`: pure `PushSlot` cases;
- `mdDeskTest`/`mmDeskTest`: dumps, requests and publishes counted per fake second;
- `editFlowBenchTest`: the budgets.

### 4.5 Guard the device-out loop (flood 6)

- While Device → Host is on, the processor keeps a short ring of the device's own outgoing channel messages. The same bytes coming back from the host within 50 ms are dropped.
- The routing panel warns that Device → Host plus a DAW MIDI loop feeds back.
- **Test:** `mdEditFlowPluginTest one+loop` keeps the device's note count at idle's.
- **Priority: low.** It needs a user routing change.

### 4.6 The baseline at small buffers

This is not an edit bug, but it is the stage the floods land on. At 128 frames the MD needs ~60 % and the MM ~70 % of each block, with overruns even at idle.
- The AUDIO/MIDI panel should recommend 256 frames or more.
- The diagnostics log should record the DSP load (`realtimeInstrumentation`).
- `mdEditFlowPluginTest idle` at 128 and at 512 is the reference.

### 4.7 Effort

| Step | Effort |
|---|---|
| 4.1 host-value dedupe (controller + one upstream hook) + unit and soak tests | 1 day |
| 4.2 one DAW gesture per page gesture | 0.5 day |
| 4.3 the tweak intent: model row + `apply`; the MD page (Alt-drag → `tweak`, the LFO section, the curve editors); the adapter's FUNCTION + encoder path (a hold-key sequence) and the coalesced-CC fallback; the MM Sound tweak; tests | 2 days |
| 4.4 paced `PushSlot` + `ByteBudget` + publishing on the page's clock | 2.5-3 days |
| 4.5 the loop guard | 0.5 day |
| The rigs as manual firmware tests with pass/fail budgets; one DAW check (Live or Reaper, 128 frames, automation in latch) | 1 day |
| **Total** | **about 7.5-8 days** |

## 5. Open questions

- **Which DAWs re-send constant automation every block?** This was simulated (every block), not observed. Flood 1 costs the same for a moving ramp, which every DAW sends. Confirm in one DAW with a lane in read mode.
- **The MD tweak's skip and clamp rules** were measured on one kit only. `mdDeskFirmwareTest tweak` should cover RAM and CTR machines before `apply` claims to match.
- **The 20-25 ms outliers at 512 frames:** repeat on a real-time audio thread (in a DAW) before attributing them.
- **The background read's 13-24 ms single blocks:** bisect by request kind before tuning the budget.
- **4 Hz or 6 Hz audible feedback for lock draws:** Radek's ear decides.
