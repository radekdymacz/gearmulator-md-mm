# Pre-release C++ review: the MD + MM editors (main @ 855902a8c)

Scope: `source/elektron/md/{deskCore,deskHost,deskWire,elektronData,mdDesk,mmDesk,mdJucePlugin}` plus the
fork's `mdLib/mddeskdevice.*` where the editors reach the audio thread. Items fixed in
DESIGN-REVIEW-2026-10-02 (Status) were not re-reported. Read-only: nothing in the repo was edited.

Confidence: **H** = verified in the code (and, where marked, by a test program); **M** = strong evidence, but the
trigger depends on the host or on the input; **L** = plausible, not proven.

## Tests run

- Built the existing targets incrementally in `temp/cmake_cifix` (Ninja, Release, `BUILD_TESTING=ON`):
  deskCoreTest, mdDeskTest, mmDeskTest, deskWireTest, deskWirePortTest, elektronDataTest, mmDataTest,
  elektronDataCorpusTest, mmDataCorpusTest, syxImportTest, mdPageBridgeTest, mdSessionNoRomTest (md and mm),
  mdAutomationRobustnessTest, mdProjectStateRestoreTest, mdSysexLifecycleTest. Build time 67 s; no errors (5 warnings).
- `ctest`: **14 passed, 0 failed, 2 skipped**. mdProjectStateRestoreTest and mdSysexLifecycleTest skip because the
  pinned MD/MM firmware is not on this machine. No firmware test, and no test that needs a ROM, ran.
- The data-layer findings marked "test program" were proved with small programs in the scratchpad
  (`loc.cpp`, `fm.cpp`, `kit.cpp`, `conv.cpp`, `c2.cpp`), never in the repo.

---

## Blocker

None that I would hold the release for on its own. The closest are A1, A2 and S1; "Ranking at a glance" at the end
gives the order to fix things in.

## Should-fix

### S1. A negative run index writes before a heap buffer (Intel Macs). H
`elektronData/mmJson.cpp:267` (`runsInto`; the MM pattern's `lockRows` and the kit pools reach it too, lines 438 and 657).
- **Defect.** `at = static_cast<size_t>(r[0].asNumber())` checks only that the value is a number. On x86_64,
  `-8.0` converts to 2^64-8, `at + b.size()` wraps to a small number and passes the `> _bytes.size()` check, and
  `std::copy` writes 8 bytes before the vector. On arm64 the conversion saturates to 0, so nothing happens there.
  The release is universal (`arm64;x86_64`).
- **Scenario.** On an Intel Mac, an MM song document arrives with hidden `"rowsAfterEnd":[[-8,"0000…"]]`: from a
  `set` (an import, or the page) or from a library/project file. The result is heap corruption and a later crash.
- **Fix.** Read the index with the existing integer helper (`toInteger(..., 0, _bytes.size())`), then check
  `b.size() <= _bytes.size() - at`.

### S2. Release builds use `-Ofast`, which removes the JSON layer's non-finite checks. H (test program)
`base.cmake:70,116` (`-Ofast` for every target); `elektronData/json.cpp` (parse and `writeNumber`); `mdSamples.cpp:454,458`.
- **Defect.** `-Ofast` implies `-ffinite-math-only`, so `std::isfinite` and `isnan` fold to "finite".
  `mdLib` opts out with `-fno-fast-math`; `elektronJson` and `elektronData` do not.
  - At -O2, `1e400`, `nan`, `-inf` and `[1e999]` are refused.
  - At -Ofast, all four are accepted, and `write()` emits bare `inf` / `nan`.
- **Scenario.**
  - A value such as `1e999` in a document reaches the writer, and the outbox carries `inf`. The page's
    `JSON.parse` of that whole `gm.recv` batch then fails, so every message in the batch is lost: documents,
    results, the machine document.
  - A float WAV with NaN samples passes the NaN guard.
- **Fix.**
  - `target_compile_options(elektronJson elektronData PRIVATE -fno-fast-math)`, as `mdLib` already does.
  - `writeNumber` writes `null` for any non-finite value.

### S3. The JSON layer depends on the C locale. H mechanism (test program); M for real-world reach
`elektronData/json.cpp:238` (`strtod`) and `:94` (`snprintf "%.17g"`).
- **Defect.** Under a comma-decimal `LC_NUMERIC` (for example `pl_PL.UTF-8`), `{"tempo":120.5}` does not parse
  ("expected , or }"), and 120.5 is written as `120,5`, which is invalid JSON.
- **Scenario.**
  - Inside a host that calls `setlocale(LC_ALL, "")`, every message with a fractional number breaks: song and
    global tempos (tempo/24), audio settings and sample rates.
  - A page batch with a fractional number is dropped whole ("bad page message").
  - A plug-in batch with one makes the page's `JSON.parse` throw.
  - The repo's own comment at `juceRmlComponent.cpp:1238` names Carla as such a host.
- **Fix.** Parse with `std::from_chars` (or `strtod_l` with a C locale), and write with `std::to_chars` / `snprintf_l`.

### S4. Notices go to the wrong window, or nowhere, once two editors have been open. H
`juceUiLib/messageRoute.h` (`setSink`, a process-wide sink) used by `mdPageEditor.cpp:70` (create) and `:40` (destroy).
- **Defect.** There is one global sink. Each editor sets it when it opens, and each editor clears it (`setSink({})`)
  when it closes, even if the sink is now another editor's.
- **Scenario.** In a DAW, two MD instances have their windows open (A, then B), and the user closes A. A clears B's
  sink. From then on, every plug-in warning or question (for example "state restore failed", or any
  `MessageBox::ask*`) is queued silently: `offer` returns true, so no native box is shown either. After 16 queued
  notices, the rest are dropped. A question's callback never runs, so the flow that asked waits forever.
  While both windows are open, instance A's notices also show in B's window.
- **Fix.**
  - `setSink` returns a token, and closing clears the sink only if the token still owns it.
  - Better: route notices per processor. The `DeskHost` owns its notices, and its editor drains them.
  - Never drop a notice silently: past the cap, fall back to the native box.

### S5. The last batch of messages is delivered to the page again when the web view is shown again. H in JUCE; M for trigger frequency
`mdWebPageHost.cpp:238` (`m_web->goToURL("javascript:…gm.recv([...])")`), with JUCE 7
`juce_WebBrowserComponent_mac.mm:635,680-710`.
- **Defect.** `WebBrowserComponent::goToURL` stores every URL as `lastURL`, including `javascript:` ones.
  `checkWindowAssociation()` (called from `parentHierarchyChanged` / `visibilityChanged` whenever the browser is
  showing) runs `reloadLastURL()`, which evaluates that last `gm.recv` again. The page has no sequence check.
- **Scenario.** A host that hides and re-shows the plug-in window, or re-parents the editor (window or tab
  switches), replays the newest batch. Replayed messages are not all idempotent:
  - an `ask` or `notice` opens its dialog a second time, and answering both acts twice (`noticeAnswer` for an id
    already erased does nothing, but an `ask` resends its command with `force`);
  - `syxPreview` reopens;
  - `error` and `romInstall` toasts repeat;
  - an old `reset` without its documents after it wipes the page's documents until the next publish.
- **Fix.** Number the batches: `gm.recv(seq, [...])`, and have the page drop any `seq` it has already seen.
  Alternatively, after each flush call `goToURL("javascript:void 0")` so `lastURL` is harmless.

### S6. SysEx import reports success when items were refused or are waiting on a question. H
`mdDeskSession.h:306-311` with `mdSyxSession.h:130-157`; the reviews in `mdDeskMachine.cpp:347-391` and the MM equivalents.
- **Defect.** The session feeds each import item to `m_desk->onPageMessage(set …)` and counts it as done whatever
  the outcome. `review()` can refuse an item or ask about it:
  - refuse: "Live recording owns this pattern";
  - ask: `overwriteKit` for the kit that plays while it has unsaved edits, or `relinkKit`.
  The final progress text is always "Imported: the machine takes the documents in…".
  The MM page suppresses error toasts for `set` results (`mmAdapter.js:602`). The resulting `result` messages carry
  no `id`, so no pending page handler is tied to them.
- **Scenario.** The user imports a full-backup `.syx` while the playing kit has unsaved edits. That kit's item
  raises an ask, and the dialog's command is the import's `set`. Everything else goes through and the panel says
  "Imported". If the user dismisses the dialog, the kit is silently not imported. On the MM page the failure is
  invisible.
- **Fix.**
  - Have `SyxJob` collect each item's outcome. The session can read the core's result for its own command (give it
    an `id`) or call a desk API that returns an `Outcome`.
  - Report "n imported, m refused, k waiting for your answer" and list the refused items.
  - For an import, either force the asks once, after one up-front question that lists them, or stop and ask.

### S7. The UW sample scan does heavy, allocating work on the audio thread. M
`mdLib/mddeskdevice.cpp:26-32,57-108` (`processAudio` → `scanSamples`) → `elektronData/mdSamples.cpp:194-224`
(`readMdRomSample`).
- **Defect.** Once the page asks (`readSampleBank` every 250 ms after a page has been seen) and the sample memory
  changes, `scanSamples` reads one whole slot per audio block, inside the audio callback:
  - it allocates a 64 KB sector vector and a vector of the sample's full length (up to ~2.7 M int16);
  - it copies every sector out of flash and computes the peaks;
  - it calls `make_shared` for the PCM and for the published bank.
  `indexMdSamples` (signature, 48 names, a 4096-entry expander table) also runs in the callback.
- **Scenario.** On an MD with a few long UW samples, opening the editor for the first time, or loading a sample
  (the signature changes), makes several consecutive blocks each copy up to megabytes and allocate. At small buffer
  sizes (64 to 128 frames) this is a burst of xruns or dropouts in the host.
- **Fix.** Do the scan off the audio thread. The audio thread publishes only the signature and a "changed" flag, and
  a worker (or the session's step, under the device lock) reads flash through `copyFlashDataRange`. If it must stay
  in the callback, cap the bytes copied per block (one sector), and pre-reserve or recycle the buffers.

### S8. App modulators notify the DAW as if the user moved the parameter. M
`mdDesk/mdDeskMachine.cpp:1399-1405` (`sendModulation` → `m_port.sendKitParam`) → `mdStudioLink.cpp:110-116`
(`setUnnormalizedValueNotifyingHost`, `Origin::Ui`).
- **Defect.** Each LFO or random step (up to 300 CC/s) goes out as a host-notified parameter change, without
  begin/end gesture. The value is also fed back as a kit edit (`onHostKitParam`), and the parameter listener reports
  the same change again on the next drain.
- **Scenario.**
  - With track automation in write, touch or latch mode, the DAW records the app LFO as automation.
  - In read mode, the DAW's lane and the LFO fight.
  - The project is marked modified continuously while the machine plays.
  - The working kit reads "edited" whenever a modulator runs.
- **Fix.** Send modulation the way the held PTCH is sent (`sendHeldParam`): as the machine's CC through
  `sendChannel`, past the plug-in's parameters. Keep it as a transient layer, not as a kit edit (the
  `HeldOverrides` pattern), or at least never call `…NotifyingHost` for it.

### S9. Lock-row helpers have no upper bound. H for the bug; M for an input that reaches it
`elektronData/mdPattern.cpp:255` (`lockRowIndex`), `:268` (`lockValue` read), `:290/:308` (`withLock`,
`withoutLockRow` writes); `elektronData/mmPattern.cpp:118` (`mmLockRow`); direct `lockRows[row]` indexing in
`mdDeskEdit.cpp` 465, 503, 614, 656 and `mmDeskEdit.cpp` 201, 664, 851.
- **Defect.** A row index of 64 to 511 (more than 64 mask bits set) reads and writes past `lockRows`. Patterns
  decoded from a dump (`decodeMdPattern`, `mdDataLink.cpp:250`) are not validated before they reach the editor.
- **Scenario.** A corrupt or hostile pattern dump, from HW MIDI or from a `.syx` the import does not validate (MM
  patterns of an older format are only validated), with every lock mask set. The next lock edit writes out of
  bounds.
- **Fix.** Return nullopt / -1 for a row at or above `g_lockRows`, and validate what `decode*` returns before it is
  observed.

### S10. Non-ASCII text is mangled in both directions. H (test program)
`elektronData/json.cpp:75` (writer) and `:281` (parser).
- **Defect.**
  - The writer escapes each UTF-8 byte at or above 0x80 as `\u00XX`, so "é" reaches the page as "Ã©".
  - The parser stores `é` as one Latin-1 byte and refuses any `\u` above 0xFF, which drops the whole batch.
- **Scenario.**
  - `romInfo.folder`, the ROM install path and `syxPreview.file` show mojibake for a user name or file name with
    Polish letters (for example "Radosław", or a `.syx` named "zestaw-łódź.syx").
  - A page batch with a character above U+00FF (an emoji in a name field) is rejected whole.
- **Fix.** Write bytes at or above 0x80 unescaped. Decode `\uXXXX`, including surrogate pairs, to UTF-8.

### S11. The MD import never validates songs. M
`mdJucePlugin/mdSessionMd.cpp:175` (`fits`) and `elektronData/mdSong.cpp:35`.
- **Defect.** Patterns and kits are validated; songs are only version-checked. `decodeMdSong` accepts about 1360
  rows against `MdSong::g_maxRows = 256`.
- **Scenario.** A `.syx` with an over-long or malformed song (no END row, targets past the end) is sent to OS 1.63
  as it is.
- **Fix.** Add `validate(song)` to `fits`, as is already done for patterns and kits.

### S12. A hidden lock pool can make the parser allocate gigabytes. H
`elektronData/mdJson.cpp:557-566`.
- **Defect.** Each `lockPoolHidden` pair can add up to 4096 bytes, there is no limit on the number of pairs, and the
  size check comes only after the loop.
- **Scenario.** A 10 MB document of `[4096,0]` pairs (a hostile library or project file) allocates about 4.6 GB on
  the message thread.
- **Fix.** Stop as soon as `bytes.size()` exceeds the expected `hiddenLockBytes(p).size()`.

## Nice-to-have

### N1. Notes and the held PTCH are not released when the page goes away. M (raised to Should-fix as A4 below)
`mdDeskSession.h:336` (`onDetach` → `m_desk->detachPage()` only); `setEngine` (`:410-418`) drops the old adapter;
`mdDeskMachine.cpp:862-920`; `mmDeskNotes.cpp`.
- **Defect.** The pages release held keys on `blur` (`mdDeskLive.js:255`, the MM page's `75-comforts.js:113`), but
  nothing on the plug-in side does when the page is gone.
  - If the window is closed or the page crashes while a key is down, the MM note keeps sounding.
  - On the MD, the held PTCH stays in the working kit until the next note or a save. `cmdSaveKit` releases it, but a
    save from the machine's own panel does not.
  - An engine switch with a note down leaves the note on the old engine.
- **Fix.** On `onDetach`, on a page `ready`, and before `setEngine`, call an adapter `releaseAll()`: note-offs for
  `m_notes`, then `restore(held.releaseAll())`.

### N2. StudioLink's destructor races the drain thread. L
`mdStudioLink.cpp:64-67` and `mmStudioLink.cpp` (same shape).
- **Defect.** Members are destroyed in reverse order, so `m_alive` goes before `m_sysexListener`. If the controller's
  MIDI is drained on another thread, `onDeviceSysex` can run in that window and copy a destroyed `shared_ptr`.
  `Controller::processOfflineControllerWork` drains on the render thread in non-realtime mode. `baseLib::Event` has
  no lock at all.
- **Scenario.** An engine switch during an offline bounce. This is rare.
- **Fix.** Reset `m_sysexListener` first in the destructor (make it a `unique_ptr`).

### N3. ROM replace and remove delete files permanently, recursively. M
`mdRomInstall.cpp:88-151`.
- **Defect.** `romsInFolder` searches the ROM folder recursively, and `deleteAllBut` calls `File::deleteFile`, which
  is a permanent delete.
- **Scenario.** Installing a ROM through the BOOT card permanently deletes every other valid image of that model
  anywhere under the ROM folder, including the very file the user picked if it sat in a subfolder.
- **Fix.** Search only the top level, and use `moveToTrash()`.

### N4. Two smaller defects in `json.cpp` (both proved with test programs).
- **Duplicate keys** (`json.cpp:354` → `set` asserts). `{"a":1,"a":2}` aborts a Debug build; Release keeps the last
  value. Fix: use `put`.
- **A truncated `\u` escape** (`json.cpp:276`). `"a\u00zzb"` is accepted as `a\0b`, and a short `\u` fails with an
  empty error.

### N5. Unchecked number-to-integer casts. M
- **The gesture id `g`** (`deskCore/deskCore.h:372`). It is cast to `uint64_t` with no range check, which is
  undefined behaviour for values above 2^64. In practice the page sends small integers. Check it as `Integer 0..2^53`.
- **Knob CCs** (`mdDeskSetup.cpp:54`). They are cast with `static_cast<int>(double)`, so 64.7 is accepted as 64.

### N6. A WAV or AIFF is decoded whole before it is cut to size. M
`mdSamples.cpp:467-487` (`decodeAudioFile`) and `mdSessionMd.cpp:226`.
- **Defect.** The decoder turns up to 512 MB of file into float channels, then a mono copy, then a resampled buffer,
  all on the message thread, before cutting to the few megabytes the machine can take.
- **Scenario.** Choosing a long recording costs gigabytes of memory and freezes the UI.
- **Fix.** Decode only `maxSamples × (fileRate / targetRate)` frames, and cap the file size well below 512 MB.

### N7. Parsing an object is quadratic in its key count. L
`json.cpp:41` (`set` does a linear search for every key).

---

### N8. Kit names go into the ask dialogs' HTML unescaped. H for the mechanism; L for harm
`mdDesk/mdDeskMachine.cpp:114-122,375-386` (`kitLabel` inside `<b>…</b>`), `mmDesk/mmDeskMachineParts.h:71`; the
pages render `m.message` with `innerHTML` (`mdDeskApp.js:221`, the MM page's `V().ask`).
- **Defect.** A kit name may be any printable 7-bit text: the rename path allows 0x20-0x7e and upper-cases it;
  `.syx` and device names allow any 7-bit byte (`mdValidate.cpp:52`). The adapter concatenates it into HTML.
- **Scenario.** A kit named `A<B` or `R&B<1>` breaks the dialog's markup, and the text after `<` disappears.
  Markup can be injected within 16 upper-case characters (for example `<SVG ONLOAD=X>`), but I found no harmful
  payload that fits. JSON escaping itself is sound: control bytes are escaped by the writer. Non-ASCII is the
  separate S10. Sample file names (`sampleLoad.file`) and `syxPreview` names are sent as JSON strings, not HTML;
  how the page renders them is the page review's concern.
- **Fix.** HTML-escape `&`, `<`, `>` in every user- or device-supplied string the C++ wraps in markup (one
  `htmlText()` helper used by `kitLabel` and the MM's labels), or send the names as separate fields and let the page
  build the markup.

---

## Adapters and queues (mdDesk/, mmDesk/, deskCore queues)

From a read-only pass over the adapters. I re-checked the file:line references; the queue mechanics and their
consequences are from reading the code, not from a run.

### A1. MM (emulator): a dump parked for SYSEX RECV never times out or fails. H in the code; M for frequency
`deskCore/deskPush.h:193` (`Pushes::pump` skips parked pushes, so neither the timeout nor `TimedOut` runs for them);
`mmDesk/mmRecv.cpp:38,45-50,79-108`; `mmDesk/mmDeskDelivery.cpp:204-217,284-304`.
- **Defect.** When the panel never reaches the SYSEX RECV screen, `RecvSession` goes Failed, waits 5 s, goes back
  to Idle and retries, forever. It never drops its queue, and nothing unparks or fails the push. It does nothing at
  all while telemetry is invalid.
- **Scenario.**
  - The edit stays pending in the core and never settles; the page shows "RECV n" as failed.
  - Every cycle sends EXIT ×6 plus the GLOBAL macro to the panel.
  - While the session is in ToMain, Entering or Leaving, PLAY, STOP and RECORD are refused as "panel busy"
    (`mmDeskCommands.cpp:107-111`).
  - Chains wait too (`mmDeskChain.cpp:33`).
- **Fix.** After N failed attempts, drop the RecvSession queue and return the tags it gave up. The adapter then
  unparks those pushes, calls `slot.abandon()` and `fail(ref)`, and loads the document again. A park deadline in
  `Pushes::pump` would also do it.

### A2. A load with no reply is dropped silently and never asked for again. H
`deskCore/deskLoadQueue.h:62-67` (after its retries, `next()` resets `m_loading` and tells nobody). Re-requests happen
only on a change: `mdDeskMachine.cpp:1312-1326` and `mmDeskLoad.cpp:146-178`. The background sweep runs once
(`m_backgroundQueued`), and nothing re-reads on HwLost → Ready.
- **Scenario.** On HW MIDI, a slow wire or a 30-second cable drop gives up several slots for good. If one is the
  current pattern or kit, every edit is refused ("not loaded yet") until the user switches. The HW MD's working kit
  is never seeded.
- **The MM progress counter.** `done` is `knownCount()`, which also counts the `{WorkingKit,0}` ref
  (`deskAdapter.h:39`), while `total` counts only loadable slots (`mmDeskMachine.cpp:257-262`). So the progress
  `done/total`:
  - reads complete one slot early;
  - stays stuck after two drops;
  - while shown, hides the RECV and Host indicators (`mmAdapter.js:523-525`).
- **Fix.**
  - `next()` returns what it gave up, and the adapter requeues it with a backoff.
  - Every status poll re-requests the current pattern and kit if they are unknown.
  - Re-run the sweep on HwLost → Ready.
  - Leave the working kit out of `done`.

### A3. MM over HW MIDI: LOAD KIT messages pile up behind a waiting kit dump. H
`mmDesk/mmDeskDelivery.cpp:177-186,249-253`.
- **Defect.** A newer kit dump replaces the waiting one in place, but `loadKitAfter` → `afterDumps` appends another
  LOAD KIT each time.
- **Scenario.** A drag on a field with no live path (ASSIGN, MULTI TRIG, trig position, legato) queues one dump plus
  N LOAD KITs. The page shows "SEND 51", and `hwSend` sends all of them.
- **Fix.** Add no LOAD KIT when the dump was replaced in place, or keep a load-after flag on the waiting entry.

### A4. Held notes and the held PTCH are not released when the page or the engine goes. M
This merges with N1 above and raises it to Should-fix: `startOver` clears `m_notes` without sending note-offs
(`mmDeskMachine.cpp:96`, `mdDeskMachine.cpp:259`), on top of detach, page ready and `setEngine` releasing nothing
(`deskDesk.h:82-91,117`, `deskCore.h:467-476`).
- **Scenario.** On the MD, the images are masked by the held layer, so the editor shows the document's PTCH while
  the machine plays the shifted pitch, and a SAVE KIT on the machine's panel stores the shifted value.
- **Fix.** One `releaseAll()` on detach, on ready, on engine switch and in `startOver`, before `m_notes` is cleared.

### A5. MD live recording: `KnobRecorder` has no limit. M
`mdDesk/mdDeskRecord.cpp:21-70`; sent from `mdDeskMachine.cpp:1559-1586`.
- **Defect.** Targets never expire, and page presses and turns are not capped. `pumpTweak` has
  `g_tweakMaxPageKeys` for this; the recorder has no equivalent. With `knobPage == -1` it presses PAGE every 160 ms.
- **Scenario.** The RAM page byte is above 2 (a menu is open), or a parameter does not move with its knob. PAGE
  presses or knob turns repeat until REC stops, and the live edit stays pending all that time (`:1442`).
- **Fix.** Cap the page keys and turns per target, and drop a target after a timeout or after N turns with no change
  in memory.

### A6. `(m_curGlobal & 7)` reads 7 while the active global is unknown. H; rare
`mmDesk/mmDeskLoad.cpp:86,105`.
- **Defect.** With `m_curGlobal == -1` (before the first status, or after `startOver`), the expression gives 7.
- **Scenario.** A dump of global 7 then sets the base channel, and a settled push of global 7 queues SET ACTIVE
  GLOBAL 7.
- **Fix.** Guard with `m_curGlobal >= 0`.

### A7. MM `startOver` keeps `m_activateGlobal` and `m_recvRefs`. M
`mmDesk/mmDeskMachine.cpp:78-98,163-167`.
- **Scenario.** After a project restore, an activation still pending from before switches the restored machine's
  active global.
- **Fix.** Reset both in `startOver`.

### A8. `load` does not check the slot against the kind. H; minor
`mdDeskModel.cpp:288`, `mmDeskModel.cpp:377` (the argument range is 0-127, the largest kind's);
`mdDeskMachine.cpp:714`, `mmDeskCommands.cpp:158`.
- **Scenario.** A request for kit 100 or song 100 goes out on the wire and costs 2 to 3 load timeouts.
- **Fix.** Check the slot with `kindSpec(kind)->slots`.

### A9. A queued pattern pick never expires. M
MD `m_audibleQueue` (`mdDeskMachine.cpp:1507-1520`) and MM `m_queuedPattern` (`mmDeskLoad.cpp:153`).
- **Scenario.** The machine ignores the pick (song mode, or the message was dropped). "Queued" then shows forever,
  and the status is polled every 250 ms.
- **Fix.** A timeout, or clear the queue when a stopped machine's status reports another pattern.

Checked and found sound by the adapter pass:
- step, rotate, double, paste and range edits, and song-row moves (`deskEdits.h`, both machines);
- the MULTI MAP edits, the lock-row and note pools;
- the router's argument ranges against what the edits assume;
- history (redo is cleared on a new edit; only what reached the machine is recorded);
- push pacing and read-back timeouts, and the chain mailbox.

Checked and found sound by the data pass:
- 7-bit pack and unpack, and the dump trailer (checksum and length);
- the MD and MM decoders' size checks and the MM run-length decoder;
- `.syx` splitting, the JSON nesting limit (64) and the schema `$ref` depth guard;
- WAV and AIFF chunk walking, and SDS framing and handshake.

## Ranking at a glance (what to fix first)

1. A1: MM push parked for SYSEX RECV never fails (stuck pending, panel-key spam, transport refused).
2. A2: loads given up silently, so the current pattern or kit can stay unknown and every edit is refused (HW MIDI).
3. S1: negative run index → heap write before the buffer on Intel Macs (`mmJson.cpp:267`).
4. S2 + S3 + S10: JSON robustness. `-Ofast` lets `inf`/`nan` through, the locale breaks fractional numbers, and
   UTF-8 is mangled. Any of them can lose a whole bridge batch.
5. S5 + S4: page messaging. The last `gm.recv` is replayed when the view is re-shown, and the global notice sink is
   cleared by another window.
6. S6 (import says "Imported" though items were refused), S7 (sample scan on the audio thread), A3, A4, S8.
