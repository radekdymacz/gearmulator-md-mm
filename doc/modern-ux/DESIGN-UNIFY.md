# Design: one edit model and one view model for both editors

- 2026-10-03. Hammock design, no code. Answers findings 1, 2 and 4 of
  [DESIGN-REVIEW-2026-10-02.md](DESIGN-REVIEW-2026-10-02.md) ("decide the one edit model and the one view model").
- Read with [FOUNDATION.md](FOUNDATION.md), [data-contract.md](data-contract.md),
  [mm-data-contract.md](mm-data-contract.md), [MM-PORT-PLAN.md](MM-PORT-PLAN.md) and
  [DESIGN-generators.md](DESIGN-generators.md). Paths are relative to `source/elektron/md/` unless they start
  with `doc/` or `skins/` (`skins/` = `mdJucePlugin/skins/`).

## 1. The problem

The two editors build the same features in two ways.

- **MD.** The page sends small intents (`trig`, `lock`, `param`, `rowSet` ...: about 50 rows in
  `mdDesk/mdDeskModel.cpp`). The core applies them as pure edits (`mdDesk/mdDeskEdit.cpp`) to its current
  documents. The page renders `V = Overlay.over(deriveView(Docs, S))` (`skins/mdStudio/mdDeskModel.js`). An
  optimistic write belongs to a command id and leaves when that command's result comes. The result always
  comes after the documents it changed, so no clock is needed.
- **MM.** The mockup's global `S` is the live truth. A gesture changes `S`, then `mmAdapter.js` turns `S` back
  into contract documents (`mmConvert` page to firmware) and sends `{op:"set", kind, doc, g}`. Documents that
  arrive are held in a second copy (`DOCS`) and written back into `S` when the page is not busy (`synced`,
  `wantApply`, `applyPending`, `busy()`, `capturePat`/`captureKit`). Mutes and POLY echoes are told apart by
  wall-clock windows (`now() - last.muteMs > 1200`, `polyMs > 1500`).

What this costs:

- Every feature is designed twice. Undo, validation and asks differ per machine.
- The MM has a lost-update race. The page builds a whole pattern from its own copy. A step written on the
  machine meanwhile (GRID or LIVE RECORDING, the person at a HW machine) is overwritten by the next `set`.
- The time windows guess. A slow HW round trip shows a mute flicker; a fast panel press is ignored.
- The reconcile code is the most fragile part of the MM page and has no node test.

## 2. Constraints

1. Both editors ship together. Neither may be broken for more than one commit.
2. Two engines per machine: the emulator and HW MIDI (DIN pacing, SYSEX RECV, no panel). Edits must compose
   with changes the machine makes itself.
3. Undo is one step per gesture (`g`). A GEN run or a MUTATE trial is one gesture.
4. Pacing stays in the core (`PushSlot`, `DinPacer`, kit delivery as CC/NRPN from a before/after diff).
5. The existing contracts stay valid. New command rows are additions. `set` stays legal.
6. The MM mockup (`doc/modern-ux/mm-mockup/src/`, about 350 KB built) is also the design prototype. It must
   still open in a browser on its own, with no plug-in.
7. Feature parity exists today (MM-PORT-PLAN phases 1 to 4). Migration must keep every feature working.
8. Effects at the edges. The core stays pure and clock-free except where a clock is injected.

## 3. Candidates

### A. The MM grows the MD's model in one go

The MM gets `mmDesk/mmDeskEdit.cpp` and `Core::Edit` rows mirroring the MD's. The MM page is rewritten as
markup plus a pure render over `deriveView(Docs, ui)` and an overlay.

- Duplication: gone. One vocabulary, one authority (the core), one view model.
- Lost update: gone. An intent applies to the core's current document.
- Echoes: matched by command id, as on the MD.
- Undo and asks: the core's, the same on both (`review` on the adapter).
- Testability: C++ edits in `mmDeskTest`; the view in node.
- Cost and risk: high. Every renderer in the mockup reads `S.tracks`, `S.midi`, `S.locks` directly. A rewrite
  of 350 KB at once breaks constraint 1. The standalone prototype loses its engine (constraint 6).
- The page still writes optimistic view values in JS. That is a small second statement of each edit's
  effect, but only of what the screen shows, and it corrects itself at the result.

### B. Shared pure JS edits, whole documents on the wire

One JS module of pure edits over contract documents (`edit(doc, intent) -> doc`), shared by both pages.
The page computes the new document and sends `set` with `g`; the core validates and diffs.

- Duplication: removed in JS, but the MD's 1,400 lines of firmware-tested C++ edits are either kept (two
  models again) or deleted (a risky rewrite of the shipped MD path).
- Lost update: stays. It needs a base revision on every `set`, a refusal when stale, and the page re-running
  its intents on the new document. That is event sourcing on the page.
- Asks and notes: the core sees only documents, so "clear a stored slot" and "edit a stored slot" look alike.
  The page would have to ask, against the rule that asks are the core's values.
- Echoes: matched by id (a document overlay). Fine.
- Testability: very good, all in node. The standalone mockup runs the real edits. That is its best point.
- Pacing: unchanged (the core diffs).
- Verdict: simple to test, but it moves the authority to the edge that does not see the machine.

### C. Hybrid: A's model, reached by a strangler (chosen)

A's end state, reached in small phases that each leave the MM working:

- **Phase 1 removes the reconcile code before any new intent exists.** `set` itself becomes an overlay
  entry keyed to its command id, applied to the documents before the view is derived. `S`'s document members
  become derived values, written by one function. `synced`, `wantApply`, `applyPending` and the time windows
  go at once.
- Then each workspace moves its gestures from `set` to intents, one module at a time.
- Shared code goes to shared files: JS overlay, document store, intent helper and generators in
  `skins/shared/`; generic C++ edits (song rows, library slot ops, step-set moves, clipboard) in `deskCore/`.
- One set of intent cases (JSON) is run by both the C++ core and the node view test. It pins the C++ edit
  and the JS optimistic writes to the same outcome.
- The standalone mockup gets a small demo host that folds answered overlay writes into its base view. The
  prototype keeps working without a core.

Critique of C:
- Duplication: the edit logic is C++ only. JS keeps only view writes (what the screen shows at once). The
  shared intent cases catch drift between the two.
- Migration cost: spread over phases; each phase is one workspace and its tests.
- Risk: phase 1 depends on `mmConvert` round trips being exact (page to firmware to page). `mmConvertTest.js`
  already checks them against the catalogue; phase 1 adds the cases it misses.
- Echo problem: solved by ids in phase 1, for all edits. Mutes and POLY move to a core expectation.
- Undo and asks: one rule for both from the first intent onwards.
- What remains duplicated: optimistic writes for a few complex edits (rotate, double). Accepted; they are
  previews, and the intent cases test them.

**Choice: C.** It is A's model, one authority in the core, but bought one workspace at a time.

## 4. Data contracts

### 4.1 The edit: intents, one vocabulary

- An edit is a command row `{op, owner: Core, core: Edit, kind, args}` in the machine's table, as today.
- Both machines use the same op name for the same meaning. MD names win where they exist: `lock`,
  `clearLane`, `clearLocks`, `clearPattern`, `steps`, `rotate`, `doublePattern`, `length`, `speed`, `swing`,
  `patternKit`, `clearSteps`, `copySteps`, `pasteSteps`, `param`, `params`, `level`, `machine`, `kitName`,
  `copySound`, `pasteSound`, `clearSound`, `route`, `tempo`, the `row*` song ops and the library ops
  (`patCopy`, `patPaste`, `patCopyTo`, `patClear`, `kitCopy`, `kitPaste`, `kitCopyTo`, `kitClear`,
  `kitRename`).
- MM-only ops are named for the machine's own idea, never for a mockup function. First list:
  `step {p, t, s, v}` where `v` is `{n:[note...], a, f, l}`, `{off:true}` or `null` (a value, never a
  toggle); `slide {p, t, s, on}`; `swingStep {p, t, s, on}`; `transpose {p, t?, v}`; `arp {k, t, field, v}`;
  `assign {k, t, src, row, ...}`; `input {k, t, v}`; `multiMap {i, ...}`; `midiTrack {t, ch?, cc?}`;
  `routing {v}`. Final arguments are fixed when each phase lands, in the table, then generated into the
  schema (`mmDeskTest --write-schema`).
- Arguments that name a document: `p` (pattern), `k` (kit that plays, or a stored kit for library ops), `s`
  (song, for song ops). The MD's rule stands: a live kit op's `k` must be the kit that plays.
- Every edit carries `g`, the gesture id. Edits with one `g` are one undo step (the core's history merges
  them). `g` is passed explicitly: `cmd(op, args, {key, g, optimistic})`; no ambient `gesture` global
  (review finding 11).
- `set {kind, doc, g}` stays as the intent for a whole document: SysEx import, a restored project, a pasted
  document from outside. It is not used for gestures after phase 7.
- Asks: an edit that may lose something asks through the adapter's `review` (the MM adapter already has one).
  The ask names are declared in `MmModel::asks()`. The page shows them and resends with `force`. Same as MD.

### 4.2 Where the pure edits live

| Code | Where | What |
|---|---|---|
| Generic edits | `deskCore/deskEdits.h` (new, header-only templates) | Song rows (`rowSet`, `rowInsert`, `rowDelete`, `rowMove`, `copyRow`, `pasteRow`) over a row type; library slot ops over any `KindSpec` kind (copy, paste, copy-to, clear, rename); step-set moves (rotate, double, clear/copy/paste a range) over a step list; the clipboard value. Moved out of `mdDeskEdit.cpp` when the MM first needs each. |
| MD edits | `mdDesk/mdDeskEdit.cpp` | What only the MD has (trig grid, accents, MD locks, master FX, UW). Calls the generic ones. |
| MM edits | `mmDesk/mmDeskEdit.cpp` (new) | What only the MM has (note steps, arp, assign, 62 pooled locks, multimap, routing). Calls the generic ones. Same `apply(docs, command, clipboard, context) -> EditResult` shape. |
| JS view writes | the page, per workspace | Optimistic `[path, value]` writes only. No document edits in JS. |
| Generators | `skins/shared/deskGen.js` (the whole of today's `mdDeskGen.js`, review finding 3) | Pure. Their output is intent arguments (`steps` rows, `params` values), for both machines. |

### 4.3 The view

- `V = Overlay.over(deriveView(DocOverlay.over(Docs), ui))`, recomputed after every message and every `cmd`.
- `Docs`: the contract documents as published, stored by one shared `storeDoc` (`skins/shared/deskDocs.js`,
  today's `DOC_STORE` generalised over a kinds list). One copy, never written by a gesture.
- `deriveMmView(docs, ui)` (`skins/mmStudio/mmView.js`, new, pure): uses `mmConvert`'s firmware-to-page
  functions to build exactly the shapes the mockup's renderers read today: `tracks`, `midi`, `locks`, `len`,
  `mult`, `swingAmt`, `patTrn`, `routing`, `kitNames`, `patKit`, `songs`, `mmap`, `bpm`, `pat`, `kit`,
  `kitState`, `queued`, `plays`, mutes, POLY mode.
- `ui` is the part of `S` the derivation needs: solos, the song being edited (`songEdit`).

### 4.4 Overlays and how echoes are matched (no time windows)

- `Overlay` (moved as is from `mdDeskModel.js` to `skins/shared/deskOverlay.js`): entries `{path, value, id,
  doc:{kind, slot}}`, keyed by view path. A later write of a path owns it. An entry is shown only while the
  view shows its document. Values, never toggles. `Overlay.answered(id)` drops the entries of a command.
- `DocOverlay` (new, same file): entries `{kind, slot, doc, id}`. Used for `set`: the page's document is
  shown until that `set` is answered. Used by the MM until each gesture moves to intents, and later only for
  import.
- **The matching rule.** Every command gets an id from `Bridge.send`. A keyed send that replaces a waiting
  one keeps its id. The core publishes the result after every document the command changed
  (`deskCore::resultMessage`). So when the result arrives, the documents already carry the edit, and the
  entry can go. A refused command's entry goes too, and the view shows the documents. No clock in the page.
- **Machine fields that come from memory** (mutes, MIDI mutes, POLY, tempo). The result comes when the
  adapter took the command, but the RAM read may lag. The core keeps a `deskCore::Expectation` per field
  (it exists for the working kit). The machine document publishes the expected value from the moment the
  command is taken. The expectation settles when memory shows the value. It is given up when memory disagrees
  for longer than the adapter's `settleMs`, measured on the session's step clock (injected in tests). Then
  memory wins: a press on the machine's panel shows. The one clock lives in tested C++, not in the page.
- `tempoInFlight` and `modInFlight` become overlay entries owned by their command ids.

### 4.5 What `S` keeps (UI only)

Workspace and selection (`ws`, `sel`, `side`, `lane`, `lanePage`, `trigSel`, `rollLo`, `kbOct`, `asTab`,
`mmapSel`), keyboard mode (normal, multi, map; POLY itself is the machine's), solos, the song being edited,
GEN and MUTATE specs and runs, dialog drafts, drag state, the joystick position, the plate.

Rule: nothing in `S` is a document value after the last phase. Until then, `S`'s document members are
written in one place only, `applyView(V)`, and a check in `sync-mmstudio-skin.py` refuses any other
assignment to them (a list `DOC_MEMBERS`, removed when the renderers read `V` directly).

Rendering is deferred while a menu or drag is open, as now (a render would close a menu under the person).
That is a UI rule about when to draw. The view itself is always current.

### 4.6 The standalone mockup

`doc/modern-ux/mm-mockup/src/55-host.js` keeps the host seam. Without `window.MMHost`, a demo host
(`src/54-demo.js`, new, about 100 lines) runs: it starts from example contract documents (one fixture in
`doc/modern-ux/mm-mockup/demo-docs.json`), answers every command ok, and folds the command's overlay writes
into its base view. Its undo is a snapshot of that base. So every intent the page shows at once must carry
its full optimistic writes. That is also what the plug-in needs.

## 5. Migration plan for the MM page (strangler)

Each phase ends with the MM editor working, its self-test (`GEARMULATOR_MMSTUDIO_SELFTEST=1|p4`) green, and
`mmDeskFirmwareTest <ROM> p4` and `hw` green. One commit per phase, MD unaffected unless stated.

**Phase 0. Shared page modules (no behaviour change).**
- Move `Overlay`, `docOf`, `shows`, `DOC_STORE`/`storeDoc` from `mdDeskModel.js` to `skins/shared/deskOverlay.js`
  and `skins/shared/deskDocs.js`. Add `DocOverlay`. Both sync scripts concatenate them.
- `cmd` takes the gesture explicitly. One gesture value per page (`Gesture.begin()`, `Gesture.end()`).
- Tests: `mdDeskModelTest.js` passes unchanged; new `deskOverlayTest.js` (paths, Maps, Sets, DELETE,
  doc scoping, DocOverlay ordering).

**Phase 1. MM: one store, a derived view, ids instead of windows.** (First migration phase.)
- `mmAdapter.js` stores documents with `deskDocs.js`. `mmView.js` `deriveMmView(docs, ui)` builds the view.
  `applyView(V)` writes it into `S`'s document members and calls one `MMView.show`.
- Every existing `set` from a gesture is added to `DocOverlay` under its id. The gesture still mutates `S`
  and the adapter still builds the document with `mmConvert` page-to-firmware: nothing in the mockup changes.
- Delete: `synced`, `wantApply`, `applyPending`, `applyT`, the 120 ms apply interval, the `busy()`,
  `dialogOpen()` and `libBusy()` gating of documents, `last.mute`, `last.muteMs`, `last.polyMs`,
  `tempoInFlight`.
- Core: mutes, MIDI mutes and POLY published from an `Expectation` in `MmMachine` (4.4).
- Tests: `mmViewTest.js` (node): derive from catalogue fixtures equals what `applyPending` showed (golden
  values captured once from today's adapter); echo test: a `set` sent, an older document arrives, the view
  keeps the page's document, the result arrives, the view is the core's. `mmConvertTest.js`: a round trip
  page-firmware-page per document kind, exact. `mmDeskTest`: a mute expectation settles on the memory read,
  and gives up after `settleMs` with a fake clock.
- As built (2026-10-03): `MmView.derive` (mmView.js) and `MMView.show` (130-main.js, the mockup's `applyView`:
  it writes a member only when the view's value changed since the last show, as a copy, so a gesture under
  way keeps its objects); the per-document `MMView` setters left the export (the library's other slots keep
  `setPatternSlot` / `setKitSlot`). The tempo is a core expectation too (`deskCore::FieldExpectation`,
  `Profile::settleMs`). Not yet: the demo host (4.6); the standalone mockup still runs its own example engine,
  which phase 1 does not touch (the gestures still mutate `S`).

**Phase 2. Mix (`100-mix.js`, smallest, proves the path).**
- New `mmDeskEdit.cpp` with `level`, `route`, `input`. Rows in `MmModel::commands()`, schema regenerated.
- Mix gestures send `cmd(op, args, {key, g, optimistic})` instead of mutating `S`.
- Tests: `doc/modern-ux/intent-cases.json` (new): `{machine, docs, command, after}` cases. `mmDeskTest`
  applies each with `apply` and compares documents; `mmViewTest.js` checks that the optimistic writes over
  `deriveMmView(docs)` equal `deriveMmView(after)` on the written paths. `page_contract_check.py` sees the ops.

- As built (2026-10-03, with phase 3): `mmDesk/mmDeskEdit.cpp` (`mmDesk::apply`, `editOps`), Mix ops `level`,
  `route {k, t, out}` (bus bits), `input`, `param {k, t, page, i, v}` (a synth track's pages 0-6, a MIDI track's MIDI
  page 7), and the global's `routing {v}`; the trig dock's `trigPos`, `legato {env, on}`, `portamento {v}` and the
  MIDI page's `midiTrack {t, ch?, cc?}` came with phase 3. Kit ops edit the working kit of the kit that plays (`k`),
  global ops the active global (`EditContext::currentGlobal`). The mockup's gestures call `edit(op, args)`
  (60-ui.js): standalone it is the old local edit, with a host it is `MMHost.intent`; knob and lock values go in
  the page's units and the adapter turns them into the firmware's (`MmConvert.valueToFw`, keeping the raw value of
  an unchanged enumeration index), adds `p` or `k` and `g`, keys drags per target, and overlays
  `MmView.writes(view, command)` (mmView.js) under the command's id. The mockup still changes its own state at the
  gesture (it is the standalone engine; `MMView.show` writes only what changed), so a gesture's writes and its own
  change agree, and the overlay keeps the edit on screen when a document arrives before the result. A screen's
  handle (the trig dock's ATK HOLD DEC PORT, the Sound page's curves) sends `param` for every value it moved
  (`editParams`); one that moves no kit value (MULTI ENV) is still a `set`. `sync-mmstudio-skin.py` checks every
  literal `edit("op", {...})` against `$defs/command`. `doc/modern-ux/intent-cases.json` (63 cases) is run by
  `mmDeskTest` (whole documents compared) and `mmViewTest.js` (the writes over the view of the case's documents equal
  the view of the documents after; a stronger check than "on the written paths": the whole of tracks, midi,
  locks, len, mult, swing, transposes, routing).

**Phase 3. Sequence (`70-seq.js`, `75-comforts.js`, `76-gen.js`, `80-notes.js`).**
- Ops: `step`, `slide`, `swingStep`, `lock`, `clearLane`, `clearLocks`, `clearPattern`, `steps` (GEN),
  `rotate`, `doublePattern`, `length`, `speed`, `swing`, `transpose`, `clearSteps`, `copySteps`,
  `pasteSteps` (the core's clipboard, `machine.clipboard`, as on the MD).
- Move rotate, double and the step-range ops into `deskCore/deskEdits.h`; the MD calls them too
  (`mdDeskTest` must stay green: the MD's only change in this plan beyond phase 0).
- Tests: intent cases for each op; firmware `p4` gains the lost-update case: LIVE RECORDING writes a note
  while the page moves a lock on another step; both survive.

- As built (2026-10-03): ops `step {p, t, s, v}` (t 0-11: six synth tracks, then six MIDI tracks; v null, `{off:true}`
  or `{n, a, f, l, notrig?}`), `slide`, `swingStep`, `lock {p, t, page, i, s, v}`, `clearLane`, `clearLocks`,
  `clearPattern`, `steps {p, from, to, rows: [{t, steps: [[s, v]...], slide?, locks?: [[page, i, s, v]...]}]}` (the
  range becomes exactly these: the generators send the run's whole range state, so a value moved back gives its
  steps back; every-n fill sends its range), `rotate` (swing stays: the MM's rotate moves the notes, not the
  groove), `doublePattern`, `length`, `speed`, `swing`, `transpose {p, t?, v?, scale?, key?}`, `arp {p, t, field,
  v, i?}` in firmware units, `clearSteps`, `copySteps`, `pasteSteps {p, t, from, to?}` (the core's clipboard; a
  page pastes onto the same kind of track, a lock of a SYN parameter the machine lacks or past the 62 is skipped
  with a note). Ramp and the wheel are `lock` intents of one gesture; paste-to-many is one `pasteSteps` a marked
  track. The pooled things are the MM's: the 62 lock rows kept in (track, page, param) order (a row is made and
  dropped with its parameter), the chord and MIDI note pools in (step, track) order. `deskCore/deskEdits.h` has the
  step-set moves (`rotatedStep`, `rotatedBits`, `rotateRow`, `doubledBits`, `doubleRow`, `stepRange`); the MD's
  rotate and double call them (its behaviour unchanged, `mdDeskTest`). Every pattern gesture is an intent now:
  the adapter's `EDITS.struct` no longer sends the pattern, and `sendKind("pattern")` is gone (`patDoc` stays for
  the library's slot writes, phase 7). The lost update: `mmDeskFirmwareTest <ROM> record` records a note LIVE and a
  step on a TRIG key in GRID RECORDING, sends `step` and `lock` intents for other steps while the machine records,
  and reads the pattern back: the recorded steps and the page's edits are both there, undo takes the page's last
  gesture only, and, for contrast, the old whole-document `set` from the page's copy loses the recorded note.
- Not yet at the time (done in phases 4 to 8, below): the Sound page's own gestures other than knobs and screens,
  MULTI TRIG and the MULTI MAP, Song rows, the library, the demo host.

**Phase 4. Sound (`90-sound.js`, `85-sound-groups.js`).**
- Ops: `param`, `params` (MUTATE), `machine`, `arp`, `assign`, `kitName`, `copySound`, `pasteSound`,
  `clearSound`. Kit delivery is unchanged: the core diffs before and after into CC and NRPN.
- Tests: intent cases; `mmSoundTest.js` and `mmGenTest.js` drive intents, not `S`.

- As built (2026-10-03, with phases 5 to 8): `machine {k, t, model, keepFx?}` (the argument is `model`, as the MD's:
  `id` is the message id the bridge sets on every command), `clearSound {k, t}`, `copySound` / `pasteSound {k, t}`
  (the core's clipboard: the machine and its seven pages), `params {k, values: [[t, page, i, v]...]}` (MUTATE, the
  Control workspace's knob rows), `assign {k, t, src?, row?, page?, dest?, add?, mirror?, hpf?, lpf?}` (Sound's and
  Perform's ASSIGN), `multiEnv {k, i, v}` (every track's copy), `multiTrig {k, mode?, splitKey?, splitTrack?, timing?}`
  (Perform), `kitName {k, name}`. A machine's start values are the core's (`mmDeskEdit.cpp assignMachine`: the SYN
  defaults by slot name, the fixed pages' neutral values without `keepFx`, a processing machine's input and AMP DEC
  REL); the page's writes compute the same from its tables (`synDefaults`, `DEFV`), and the intent cases pin them
  together. The machine gesture also clears the track's SYN lock lanes (`clearLane`, the same `g`). The LFO selects
  and cords and a screen's handles are `param` intents of what they moved (`editTrack`, `editParams`); MULTI ENV's
  handles `multiEnv`. `mmSoundTest.js` and `mmGenTest.js` stay tests of the pure tables and generators; the intents
  MUTATE and GEN send are checked by the intent cases and the browser check.

**Phase 5. Song (`120-song.js`).**
- The `row*` ops, from `deskCore/deskEdits.h` (moved from the MD). `songEdit` stays UI.
- Tests: the MD's song cases run for both machines from the same intent cases.

- As built: `deskCore::songRows` (`deskEdits.h`) is the MD's row code, moved (`rowSet`, `rowInsert`, `rowDelete`,
  `rowMove`, `copyRow`, `pasteRow` over contract rows, the clipboard's row, "A song holds N rows"); HALT rows that
  carry a target follow it too (the MD's have none). The MD's `editSong` calls it unchanged (`mdDeskTest` green). The
  MM's `editSong` reads the rows back through its codec and keeps the bytes after END where they are (a row no longer
  used becomes zeros). The page sends its own row (`rowSet {i, row}`, the page's units); `MmView.toFw` makes the
  contract row (`MmConvert.rowToFw`, on top of the row it replaces) and the adapter adds `s`, the song the Song
  workspace edits. The page's rows retarget their loops as the core does (`songRetarget`). The MD's song cases are
  not shared as data: the MM's own row cases (intent-cases.json) cover the same ops; `mdDeskTest` keeps the MD's.

**Phase 6. Perform and the global (`110-perform.js`; Control in `115-control.js` needs nothing: `modSet` is
already a Setup row).**
- Ops: `multiMap`, `midiTrack`, `routing`; POLY, mutes and tempo are already machine ops.

- As built: `multiMap {i, hi?, pat?, ofs?, len?, trn?, tim?}` (a range's fields, firmware units; an upper key between
  its neighbours), `multiMapSplit {i}`, `multiMapDelete {i}` (the ranges past the last in use repeat its upper key and
  hold nothing, as the firmware keeps them); MULTI TRIG is the kit's `multiTrig`, ASSIGN `assign`. The Control
  workspace's knob rows move kit values: `param` intents.

**Phase 7. Library (`125-lib.js`).**
- The library ops from `deskCore/deskEdits.h`, asks from `MmMachine::review` (overwrite or clear a stored
  slot). The host call `slotWritten` goes.
- Tests: intent cases; the ask names in `MmModel::asks()` (`sameAsks`).

- As built: `deskCore::library` (`deskEdits.h`): the MD's actions (copy, paste, copy-to, clear, rename), moved and
  made a template over a model's `Shelf` (what a machine's kits and patterns differ in, as data); `mdDeskLibrary.cpp`
  keeps its shelves only (`mdDeskTest` green). The MM's shelves are in `mmDeskEdit.cpp`; its library rows carry
  `g_library`. `MmMachine::review` asks `clearSlot` (a new ask) and `overwriteSlot` (over a kit with a name, the kit
  that plays, a pattern with trigs); the page's own questions go when a host is there (the page sends
  `HOST.library(op, args)`; on its own, the example engine's library and its questions stay). A kit written into
  the kit that plays' slot is loaded too (`submit`: the dump, then LOAD KIT, as the MD's library does). A cleared
  pattern keeps what the MD's keeps (length, speed, swing, the kit link; also the arpeggiator and transposes): not
  the mockup's old empty pattern, which reset those too. `mmDeskTest` checks the asks on the scripted machine;
  `mmDeskFirmwareTest p4` the slot writes, the rename, undo and the load into the kit that plays.
- Found on the firmware with the intents (and fixed): two kit edits without a live message in quick succession (MULTI
  TRIG mode, then its timing) lost the second. Each is a dump of the kit's slot and LOAD KIT; the second dump waited
  behind the first (the push slot) while its LOAD KIT went at once, so the machine loaded the first dump again.
  `MmMachine::loadKitAfter` now sends a LOAD KIT after its own dump when that dump waits (`pumpPushes`), for the live
  kit's dumps and the library's writes into the kit that plays alike (`mmDeskFirmwareTest p4`). The same held for two
  whole working kits sent quickly before (`set`).

**Phase 8. Delete.**
- `mmAdapter.js`: `patDoc`, `kitDoc`, `songDoc`, `globDoc`, `sendDoc`, `sendKind`, `kitPageOf`, `EDITS`,
  the `edited` and `slotWritten` host calls, the separate `DOCS`/`working` copy.
- `mmConvert.js`: every page-to-firmware function (`patternToFw`, `kitToFw`, `songToFw`, `globalToFw` and the
  map's), and their tests. Only firmware-to-page remains, used by `deriveMmView`.
- The mockup: `structEdited`, `soundEdited`, `capturePat`, `captureKit`, `clearedKit`, the per-document
  `MMView` setters (`setWorkingKit`, `setPatternSlot`, `setKitSlot`, `setSong`, `setRouting`,
  `setMidiTracks`, `setMultiMap`, `setMutes`), `snap`/`restore` undo (moved into the demo host).
- `DocOverlay` stays, for `set` from import only. The `DOC_MEMBERS` check goes once renderers read `V`.
- FOUNDATION.md: "Add a command" and "Add a workspace" describe one way for both machines.

- As built: deleted from `mmAdapter.js` `patDoc`, `kitDoc`, `songDoc`, `globDoc`, `sendDoc`, `sendKind`, `EDITS`,
  the `edited` and `slotWritten` host calls, `lenOf`, the `DocOverlay` layer and the page's `KIT_OPS` / `GLOBAL_OPS`
  and `toFw` (now `MmView.kindOf` and `MmView.toFw`); host calls `commit()` (a gesture ended) and `library(op, args)`
  are new. From `mmConvert.js` every page-to-firmware function (`kitToFw`, `patternToFw`, `arpToFw`, `songToFw`,
  `globalToFw`, `mapToFw`; about 160 lines) and their round-trip tests (`mmConvertTest.js` checks the reads, the
  bands of `valueToFw` and `rowToFw(rowToPage(row), row) == row` instead). `valueToFw` and `rowToFw` stay: they are an
  intent's own values, not documents; `modToFw` is the Control setup, which is the plug-in's. From the mockup:
  `structEdited`, `soundEdited`, the snapshot undo (`H`, `snap`, `restore`, now the demo host's), the dead melody
  branch of `secAction`, and the `MMView` exports the adapter no longer reads (`captureKit`, `capturePat`,
  `clearedKit`, `emptyPat`, `kitSlot`, `patternSlot`, `patternLength`, `song`, `routing`, `midiTracks`, `multi`,
  `multiMap`, `workName`). `captureKit`, `capturePat`, `clearedKit` and `emptyPat` stay inside the mockup for the
  example engine's library. `setPatternSlot` and `setKitSlot` stay: they show the library's other slots.
  `DocOverlay` stays in `deskOverlay.js` (tested), for a page that sends an import; no MM gesture sends `set`.
  The `DOC_MEMBERS` check stays: the renderers still read `S`, written by `MMView.show` only.
- The demo host (4.6) is `src/54-demo.js` (about 45 lines): `window.MMDemoHost` when there is no `window.MMHost`,
  starting from `demo-docs.json` (the mockup's example made into contract documents once, before the page-to-firmware
  functions went) and the catalogue, which `build.sh` puts with `deskDocs.js`, `deskOverlay.js`, `mmConvert.js` and
  `mmView.js` in a second script of the standalone page. It folds every intent's writes (`MmView.toFw`, `copied`,
  `writes`) into its view and shows it; its undo is a snapshot of that view and of the example library's slots, one
  per gesture (`commit`). The example engine's pattern and kit switches and its library rebase it (`demoRebase`).
- A pattern's length change rewrites the song rows of that pattern that showed as whole (`rowLens` in the writes):
  the view's song depends on the pattern's length.
- A paste shows at once: the page keeps its own copy of what its last copy command put on the core's clipboard
  (`MmView.copied`, in the view's units) and `pasteSteps`, `pasteSound` and `pasteRow` write from it; the result
  brings the core's (a lock the machine skips shows until then). The intent cases run copy-then-paste sequences.
- Tests: `intent-cases.json` has 110 cases (the documents now by name: a song, stored kits and a stored pattern
  beside the pattern, the working kit and the global; a path `[]` is a whole document), their expected documents
  written independently of both runners. `mmViewTest.js` also drives the page's own MUTATE, GEN, machine picker,
  CLEAR and a song row through the mockup's functions and checks each is one intent and none a document. A browser
  check ran every workspace's gestures (46, the library's copy, paste and clear among them) on the standalone mockup
  (the demo host) and on the plug-in's page against the real core (`mmDesk::Desk` on a scripted machine over the
  bridge's dev transport): each one undo step, undone and redone exactly, no page error.

## 6. Risks and open points

- Phase 1 rests on exact `mmConvert` round trips. A lossy field would show as a flicker at the result.
  The round-trip test is written first.
- `deriveMmView` runs on each document message. During the background library read only the current
  pattern, kit, song and global matter; library slots keep their own cheap path (`setSlotPattern`).
- The MM's 62 pooled locks and the MD's 64 are different rules. They stay in the machine edit files; only
  the step-set moves are generic.
- Open for the owner: whether phase 3 or phase 4 comes second (Sequence has the lost-update bug; Sound has
  the most gestures in daily use).
