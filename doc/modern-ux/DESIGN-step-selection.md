# Design: select steps, then copy, cut, paste, duplicate (Machinedrum Editor)

- 2026-10-07. Built on branch `feat/step-selection` for the Machinedrum Editor's Sequence page. The Monomachine Editor is
  not built: §7 says what it needs.
- The ask (a customer, through the owner): "select steps by dragging, then copy and paste them, not only a whole track
  page". The owner added: duplicate is a first-class operation (⌘D), and an Alt-drag of a selection should drop a copy
  where it is let go if it fits the gesture map; pick the easiest modifier ("cmd or shift"). The tester added: the main
  case is **one step, copied and pasted on the same track**, then several; a single step must be selectable with one
  modifier-click; right-click is the editor's own menu, so it is not available; Windows uses Ctrl for ⌘.
- Read first: [FOUNDATION.md](FOUNDATION.md) ("Add a command", the MD page: `cmd`, `Held`, `Gesture`, `Keys.bind`),
  [DESIGN-generators.md](DESIGN-generators.md) §7 (paste to many), [DESIGN-edit-flow.md](DESIGN-edit-flow.md) (`g`).

## 1. Facts this leans on

- `copySteps` / `pasteSteps` / `clearSteps` were core edits over one track's range (`mdDeskEdit.cpp`); the clipboard is
  the core's (`Clipboard::Steps`), so it already survives a pattern change and works into another pattern.
- An edit with a `g` merges into the last undo step of the same `g` (`deskHistory.h`); `copySteps` changes no
  document, so a cut (copy then clear under one `g`) is one undo step.
- What a step already means: a press or drag paints trigs; ⇧-click accent; ⌥-click slide; ⌘-click / ⌘⇧-click fill
  every 2nd / 4th; the wheel moves its lock. ⌥-drag elsewhere is Control All (knobs) or erase (lock lane). ⇧-click on a
  track header marks it for paste to many. The ruler above the grid (step numbers) takes no gesture.
- The keys test (`mdDeskKeysTest.js`) allows ⌘ only on the standard edit keys.

## 2. Candidates for making a selection

| | Gesture | For | Against |
|---|---|---|---|
| A | Drag in the step ruler (no modifier) | Free: the ruler does nothing today. Works on both machines. | A small target; one step needs a precise click on a 10 px number; the main case (one step) is fiddly. |
| B | ⇧-drag / ⇧-click on steps | The usual app idiom (Shift selects). | ⇧-click is accent, used all the time. Moving accent breaks the editor's hardware-like map and every user's habit. |
| C | ⌥-drag / ⌥-click on steps | One modifier for one step and for a block. ⌥-click (slide) is the rarer mark; slide moves to ⌥⇧-click. ⌥-drag on a step does nothing today. ⌥-drag of a selection to copy it is the Finder / Photoshop idiom and needs no new modifier. | ⌥ means "all" elsewhere on the page (⌥R, ⌥M, ⌥ CLR); a selection is not "all". Slide users relearn one click. |
| D | ⌘-drag | Distinguishable from ⌘-click (fill) by movement. | No single-step select (⌘-click is the fill); ⌘-drag means "add to selection" in most apps, not "select". |
| E | A SELECT mode key | No modifier conflicts at all. | A mode: two extra clicks per copy, and a forgotten mode makes clicks select instead of paint. |

**Choice: C, with A as the no-modifier alternative.** ⌥-click selects one step (the tester's main case), ⌥-drag
selects a block (steps × tracks), a plain drag in the ruler selects steps of the selected track (moving down over the
grid takes more tracks), ⇧-click in the ruler extends the selection. Slide moves to ⌥⇧-click (the keys of the ?
list and the guide say so). B was the owner's first idea; it was rejected because accent is the MD's most used step
mark and Shift is the chord-note key on the MM page too. ⌥ beats ⌘ because ⌘-click is the fill and has no
single-step form.

Telling a click from a drag: both are the same press. The press begins a `select` gesture (`Held`); the release
decides: no other cell crossed = a click (one step), another cell crossed = a block. The click that the browser then
fires is the selection's (`clickSteps` ignores a ⌥-click without ⇧).

## 3. The selection: a value, shown, cleared

- `S.stepSel = {t, n, from, to}`: tracks `t..t+n-1`, steps `[from, to)`. UI state, a new value per change. It is the
  core's argument shape (`copySteps {p, t, n, from, to}`), so an operation is the selection plus the pattern.
- Shown: each step inside has `selx` (an outline, `mdOverrides.css`), the ruler's numbers above it too; set by
  `stepCls`, so every redraw keeps it. During an ⌥-drag of the selection, the cells it would land on have `selghost`
  (dashed).
- It **survives a pattern change** (it is the paste target there, see §4), and is clamped to the pattern's steps on use.
- Cleared by: Esc; a plain press on a step (it paints, as before) or in the ruler without ⇧ (a new one starts); a press
  in the workspace outside the grid (the lock lane, the GEN bar). A press in the top bar, the rail or the pattern
  chooser keeps it, so one can change pattern or track and paste.

## 4. Operations

| Key | With a selection | Without (as before) |
|---|---|---|
| ⌘C | `copySteps` of the block (rows × steps) | the selected track's page shown |
| ⌘X | `copySteps` + `clearSteps` of the block, one `g`: one undo step | (nothing; a toast says to select) |
| ⌘V | `pasteSteps` with the block's first step at the selection's first step and track; the selection becomes what was pasted | the page shown of the selected track (or the marked tracks) |
| ⌘D | `copyStepsTo` at = `to`: the block again right after itself, the clipboard untouched; the selection moves to the copy so ⌘D ⌘D repeats | toast |
| Delete / ⌫ | `clearSteps` of the block | the page shown of the selected track |
| ⌥-drag from inside the selection | `copyStepsTo` at the step and track it is let go on; the selection moves there | — |
| Esc | clears the selection | — |

The main case: ⌥-click a step, ⌘C, ⌥-click the target step (same track or another, this pattern or another), ⌘V.

What is carried (core, per track row): trigs, accent, slide and swing (per track, or the pattern-wide bit with EDIT
ALL: a paste then sets the shared bit on, as before), and every parameter lock **of a step with a trig** (a lock
without a trig does nothing on the machine and would spend the 64-lock budget). Locks travel by parameter index, as
before; on a track with another machine they are that machine's parameter at the same index.

Where a paste stops (one rule for paste, duplicate and drop): at the **pattern's length** (steps past it do not play;
`pasteSteps` before cut at the visible steps) and at track 16. What is left out is said in the result's note ("2
step(s) past the pattern's length (12) left out"). A paste that starts past the length is refused. Locks that need a
new row when all 64 are used are skipped and counted, as before.

Undo: every operation is one command, or two under one `Bridge.gesture()` (cut); one ⌘Z each.

## 5. The contract

- `copySteps`, `clearSteps`: a new optional `n` (1-16, tracks from `t`; 1 without it; `t + n` ≤ 16). Unchanged for
  callers that do not send it.
- `pasteSteps {p, t, from}`: unchanged; it puts the clipboard's block (all its rows) from track `t`.
- `copyStepsTo {p, t, n?, from, to, at, dt?}`: new. Copies the block within the pattern to step `at` of track `dt` (its
  own track without it), reading the block before writing (an overlapping drop is fine). The clipboard is untouched.
- The machine document's `clipboard.stepsSize`: `{tracks, length}` of the copied block, or null. The page uses it to
  select what a paste put down.
- `Clipboard::Steps` is a list of rows (`trigs, accent, slide, swing, locks`) and a length.

The page shows every operation at once (`Overlay` writes from `V`: trigs, accents and slides when not EDIT ALL, locks
by index into the target track's names) except a paste of a block the page did not copy itself, which the result
brings.

## 6. Tests

- `mdDeskTest` `testStepBlocks`: a 3 × 4 block copied and pasted into another pattern, tracks and step; the
  neighbours stay; the cut at track 16 and at the length (with their notes); a paste past the length is refused;
  duplicate (`at = to`) with the clipboard untouched; a drop down a track over its own source; a 2-track clear; marks
  carried. The schema's `$defs/command` is regenerated (`--write-schema`).
- `mdDeskPageTest.js`: the page's commands for ⌥-click + ⌘C + ⌥-click + ⌘V, ⌥-drag block, ⌘X (one `g`), ⌘D, Delete,
  the drop, Esc, and that a plain paint, ⇧-click and ⌘-click are as before.
- `mdDeskKeysTest.js`: ⌘X and ⌘D join the allowed ⌘ keys.
- Journey `journey-seq-select-copy-paste` (`mdDeskJourneys.js`): on the firmware.

## 7. The Monomachine Editor (not built)

The MM page is the generated mockup (`doc/modern-ux/mm-mockup/src/`, synced by `sync-mmstudio-skin.py`); the same
model needs, in order:

1. Core (`mmDesk/mmDeskEdit.cpp`): `Clipboard::Steps` as rows (the MM's `StepValue` list, slide bits and locks per
   track, `midi` per row); `n` on `copySteps`/`clearSteps`; `copyStepsTo`; the same length / track 12 cut; rows of
   another kind (MIDI vs synth) skipped with a note. Rows in `intent-cases.json`; `mmView.js` writes for the new ops
   and the clip mirror (`MmView.copied`) as rows.
2. Page (`70-seq.js`, `60-ui.js`, `56-keys.js`): the gesture cannot be ⌥ or ⇧ there (⇧-click is a chord note, ⌥-click
   deletes a note, ⌥-drag erases in the piano roll). The ruler drag (candidate A) works on both pages unchanged, so the
   MM gets it first; a modifier for one step there is an open question for the owner (⌘-click is the fill on both).
3. The seam (`53-seam.js`) needs no new host call: the ops are `edit(op, args)` intents.
