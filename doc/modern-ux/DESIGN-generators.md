# Design: rhythm generators, sound mutation and small comforts (Machinedrum Editor)

- 2026-10-01. Design only, nothing built. Hammock style: problem, prior art, candidates, critique, choice, contracts, plan.
- Built 2026-10-01: slices 1-3 (§6): the `steps` and `params` edits, `mdDeskGen.js` (+ `mdDeskGenTest.js`), the GEN and MUTATE strips, `mdDeskFirmwareTest <ROM> gen`.
- Built 2026-10-01, slice 4 (§7) items 1-8: the lock budget over the lane, rotate (Alt + Left/Right, a core `rotate` edit: locks move with their steps, which `steps` cannot express), the every-N fill (⌘-click / ⌘⇧-click, one `steps`), the wheel on a step (`lock`, one gesture per run), the ramp (Shift-drag, `lock`s in one gesture), double (LEN ×2, a core `doublePattern` edit; no key since the mnemonic map), paste to many (Shift-click headers, ⌘V, `pasteSteps` in one `g`), unmute and unsolo all (0, M/S off). Not built: 9 (labels on history steps: a core change for a tip), 10 (there is no CLEAR menu yet), 11 (compare). The setup persistence is not built.
- The owner's words: "Add random or euclidean generator per track and full sequence? Sound mutation? Think about quality-of-life improvements we can do."
- Read first: [FOUNDATION.md](FOUNDATION.md) (layers, "Add a command"), [DESIGN-edit-flow.md](DESIGN-edit-flow.md) (gestures `g`, paced pushes), [data-contract.md](data-contract.md) §4.1-4.2 (pattern, kit), §5 (64 locks).
- Facts this design leans on, read in the code:
  - A gesture is one undo step: `deskHistory.h` merges a change into the last step when it has the same `g` (first "before", latest "after"). Any other edit in between closes it.
  - A pattern edit becomes one whole dump, paced by `PushSlot` (`minIntervalMs 200`, latest wins, one read-back at 150 ms of quiet). Over HW MIDI a dump costs its DIN time.
  - A working-kit edit is diffed by `kitDelivery` into small live edits (CCs). No kit dump is sent.
  - `trig` off clears the step's locks (`mdDeskEdit.cpp` `clearStep`). Accent with EDIT ALL is pattern-wide.
  - Unkeyed `Bridge.send` flushes at once (one iframe navigation each). Keyed sends batch per 16 ms.
  - The page model (`mdDeskModel.js`) is pure and tested in node (`mdDeskModelTest.js`).

## 1. Problem, goals, non-goals

### 1a. Rhythm generators

Making a beat on the Machinedrum is many clicks per track. Generators give a good start in one gesture.

Goals:
- **Euclid** per track: `k` hits spread as evenly as possible over `n` steps, rotated by `rot`, repeated over the pattern length. Optional accents spread evenly over the hits.
- **Random** per track: each step on with chance `density`, from a `seed`. Three modes: `replace` (the track becomes the result), `add` (only off steps may turn on), `thin` (only on steps may turn off). `add` and `thin` keep what you made.
- **Whole pattern:** one spec per track, a sensible default from the track's machine (kick, snare, hat, ...), applied as one undo step.
- **Live (2026-10-01, replaces the ghost preview and Apply):** every change of a GEN control writes to the machine at once. A run of changes on one track is one undo step; one Undo takes it back.
- **Deterministic:** the same spec and pattern give the same result, in the page and in tests.

Non-goals (v1): generated locks (velocity-like lock lanes, "humanise"); slides; per-track length (the MD has none); the Monomachine (the functions are machine-agnostic so it can follow later); live generation while recording (refused while `V.rec`).

### 1b. Sound mutation

Goals:
- Move a track's parameters, or the whole kit's, by a chosen **amount** (0-100 %).
- **Scope:** tracks (the selected one or all) and groups: `syn`, `fx`, `rt`, or one Sound-page group key (the `sndGroups` keys, e.g. a machine's "Pitch" group).
- **Seeded and reversible:** the same seed on the same base gives the same sound. The whole trial is one undo step.
- **Again** (the `R` key): a new seed from the **same base** (the sound before the trial), so the user does not drift away by accident. There is no Walk (removed with the mnemonic map, 2026-10-01): R always randomises the sound from the trial's base.
- **No Keep:** the trial simply ends when another track, workspace or kit is selected, or on any other edit, undo or redo. One Undo returns to the base.

Non-goals (v1): changing machines; track levels; master FX; the LFO's shape/target fields (the `rt` knobs LFOS/LFOD/LFOM are in, the LFO block is not); morphing between two kits.

### 1c. Quality of life

The ranked list is §7.

## 2. Prior art (from memory; check before quoting)

- **Elektron Analog Rytm MkII / Digitakt II:** from memory, the Digitakt II and the Rytm (OS 1.70+) have a per-track **Euclidean mode** in the sequencer: two pulse generators (PL1, PL2) combined by a boolean operator, with rotation. The firmware of the MD has nothing like it.
- **Mutable Instruments Grids / Topograf (its clone):** a 2D map of drum patterns (X/Y) plus per-instrument **density** (fill) and chaos. Kick, snare and hat are separate lanes. The lesson: density per role, not one global random.
- **Euclidean rhythms (Toussaint, 2005; Bjorklund's algorithm):** E(3,8) is the tresillo, E(5,8) the cinquillo. Every app's euclid is this, up to rotation.
- **Bitwig / Ableton:** from memory, Bitwig's note FX (Randomize, Ricochet) and Ableton Live 12's MIDI **Generators** (Rhythm, Euclidean, Seed) and **Transformations** work on a clip as an edit you can undo, with seed-like regenerate buttons. The lesson: a generator is an edit, previewed, then committed.
- **Synth "mutate" buttons** (e.g. Elektron Digitone/Syntakt's randomize is limited; from memory, Arturia and many soft synths have "mutate" with an amount): the useful ones keep a base and an amount, and protect volume.

## 3. Where generation lives: candidates

### A. Page-side function that sends today's commands (`trig`, `accent`, `lock`)

- Undo: one step if all carry one `g`. Good.
- Messages: a whole pattern is up to 16 x 64 `trig` commands. Unkeyed, that is 1,024 bridge navigations; each one an edit, a change, a publish. Bad.
- Pacing: `PushSlot` coalesces the dumps, but the core and the page churn.
- Testable in node. Works on every engine.
- Verdict: right place for the logic, wrong edit vocabulary.

### B. A core command `generate {spec}` (C++ in `mdDeskEdit.cpp`)

- Undo: one step, one change. Pacing: one dump. Testable in `mdDeskTest`.
- Preview needs the result before it is applied: either the algorithm is written twice (C++ and JS) or a new "dry run" round trip. Both are complexity.
- It braids a musical recipe into the vocabulary router: every new kind (random mode, roles, later Grids-like maps) is a schema change, and the MM needs its own copy.
- The stored result is the pattern anyway; the spec has no value in the core.
- Verdict: simple delivery, complected meaning.

### C. Page-side pure generator + two small general core edits (chosen)

- `generate(spec, pattern) -> rows` and `mutate(spec, kit) -> values` are pure JS functions in a new `mdDeskGen.js`, tested in node like `mdDeskModel.js`.
- Their output is plain data, handed to two **general** edits the core does not know are "generated":
  - `steps`: set tracks' trigs (and accents) in a range to exactly these step lists.
  - `params`: set many kit parameters at once.
- Undo: one command, one change, one step. A trial of several applies shares one `g`, so it stays one step.
- Determinism: a specified PRNG (§4.4), pinned by tests. The result is stored as the pattern itself; the spec is a recipe.
- Pacing: one pattern change per apply, so one dump (paced). One kit change per mutate, diffed into CCs.
- Emulator and hardware: the same documents and delivery as any edit; nothing engine-specific.
- Preview is free: the page already has the result before it sends anything.
- Cost: two schema rows and two pure edits in C++. Both are useful beyond generators (rotate, double, paste-to-many, kit-wide reset).
- Risk: the algorithm lives in JS only. Accepted: the core never needs to regenerate.

**Choice: C.** Generators are values in, values out, at the edge where the person decides. The core learns two plain verbs, not a recipe.

## 4. Data contracts

### 4.1 Generator spec (page state, never in a document)

```
GenSpec =
  { kind: "euclid", k: 0..n, n: 1..64, rot: 0..n-1,
    acc?: { k: 0..k, rot: 0..k-1 } }                       // accents spread over the hits
| { kind: "random", density: 0..100, seed: uint32,
    mode: "replace" | "add" | "thin",
    acc?: { density: 0..100 } }
| { kind: "keep" }                                         // whole pattern: leave this track alone

PatternGen = { tracks: GenSpec[16], from: 0, to: length }   // to is exclusive; default the pattern's length
```

- Euclid, exactly: step `s` (0-based, from `from`) is a hit when `((((s - rot) mod n) + n) mod n) * k mod n < k`. E(3,8) gives `x..x..x.`, E(5,8) `x.x.xx.x`. `rot` moves the hits later. The cycle repeats to `to`.
- Euclid accents: the hits, numbered 0.., get an accent by the same formula with `acc.k` over the hit count of one cycle.
- Random: step `s` is on when `u(seed, t, s) * 100 < density`; `add` ORs with the current trigs, `thin` ANDs, `replace` takes the result. Accent: `u(seed ^ 0xACC, t, s) * 100 < acc.density` on result hits.

### 4.2 Defaults per machine role (data, one table in `mdDeskGen.js`)

The role comes from the machine code (`codeOf`), one regex table, first match wins:

| Role | Codes (examples) | Default |
|---|---|---|
| kick | BD, KD, -BD* | euclid 4/16 |
| snare | SD, CP, RS, CL | euclid 2/16 rot 4 (beats 2 and 4) |
| closed hat | CH, HH | euclid 8/16 rot 0, accents 4 over 8 |
| open hat | OH | euclid 2/16 rot 2 (off-beats) |
| cymbal | CY, RC, CB | random 12 % replace |
| tom / perc | LT, MT, HT, TOM, perc | random 15 % replace |
| other (GND, E12 FX, ROM, RAM) | | random 10 % replace |
| none | MID, CTR, INP, empty, RAM-R recorders | keep |

Codes are checked against the catalogue when built; "HT" is a hi tom on TRX. Unknown codes fall to "other".

### 4.3 The edits produced (two new core rows, pattern `P` and working kit `W`)

```
{op:"steps", p, rows:[{t, on:[step...], acc?:[step...]}], from?:0, to?:length, g}
{op:"params", k, values:[[t, i, v]...], g}           // i 0-23, v 0-127
```

- `steps`: for each row, the trigs in `[from, to)` become exactly `on`. A step turned off loses its locks (the `trig` off rule). A step kept on keeps its locks. `acc` given: the accent bits in the range become exactly `acc`; with EDIT ALL on, `acc` is refused with a note (accent is pattern-wide). Slides untouched. One pattern change.
- `params`: one working-kit change, delivered by `kitDelivery` as CCs. The router checks every triple. Unchanged values send nothing.
- Both: `Owner::Core`, `Gate::Input`, a pure edit in `mdDeskEdit.cpp`. The schema is regenerated (`--write-schema`); `page_contract_check.py` sees the new ops.
- The page sends each apply **keyed** (`"gen"`, `"mut"`), so a fast Again replaces the waiting one in the frame.

### 4.4 Seeds and the PRNG

- `u(seed, a, b)`: `mulberry32(hash32(seed, a, b))` first draw, in [0, 1). `hash32` is specified in `mdDeskGen.js` (a few lines of integer mixing); tests pin values. Keying by (track, step) or (track, param) means a scope change never reshuffles the other tracks.
- Seeds live in page UI state: `S.gen = {spec per track, pattern-wide spec}`, `S.mut = {spec, base, g}`. Shown as a short number the user can type.
- Later (phase 4): kept with the project in `md-desk/setup` (data-contract §4.7), so a reopened project offers the same recipes.

### 4.5 Mutation spec and its output

```
MutSpec = { tracks: [t...] | "all", groups: ["syn"|"fx"|"rt"| <sound-group key>...],
            amount: 0..100, seed: uint32, from: "base" | "current",
            protect: ["VOL"] }                              // names never moved; default VOL
mutate(spec, kit, cat) -> values: [[t, i, v]...]
```

- For each track in scope, each **named** knob of its machine (`slots(m)`, so no meaningless slots) in the groups, not protected: `v' = round(v + amount/100 * (u(seed, t, i) * 127 - v))`. A pull toward a random target: 0 % does nothing, 100 % is fully random, never clamps.
- Skipped tracks: MID, CTR, empty machines (they have no sound to move).
- The page always mutates from the trial's base (the working kit when the trial began); `mutate()` takes any base, the page passes only that one.

### 4.6 Preview and one undo step

- **Generators (live run):** `genRunFor(run, key, base, gesture)` (mdDeskGen.js) keeps one run per context, `key = workspace:track:pattern`: one `g = Bridge.gesture()` and one `base`, every track's trigs when the run began. Every GEN change (mode, a value, REPLACE/ADD/THIN, R, Defaults) sends one `steps` with the run's `g`, generated from the base, so a value moved back gives its steps back and ADD/THIN do not pile up; Keep in a run writes the base back. The history merges the run into one step (`mdDeskTest`: four changes, one `g`, one undo step to the pattern before). The run ends on another key (track, workspace, pattern) or any other edit, undo or redo (`cmd`). Undo takes the run back in one step. No ghosts.
- **Mutation:** sound cannot be ghosted; the preview is to hear it. A trial starts with `S.mut.g = Bridge.gesture()` and `base = kit`, keyed like a run (`workspace:track:kit`). Every R (and amount or scope during the trial) sends `params` with that same `g`, so the history keeps one step whose "before" is the base. The trial ends like a run; one Undo takes it back. Cmd+Z after it ends returns to the base in one step.
- The page shows the result at once as optimistic writes (`cmd(..., optimistic)`), like any edit.

## 5. UI sketch

**Sequence page.** The GEN bar, always there: the one bar under the step grid (it replaced the foot row; its height went half to the grid, half to the lock lane). No key opens it. Left to right:
- A title with its target: `GEN · track 3 · SD`, Alt held: `all tracks` (in the LED's colour).
- Groups, each a title on a thin rule over its controls: MODE (EUCLID / RANDOM / KEEP, an LED radio row); EUCLID: HITS, STEPS, ROTATE, ACCENT as LCD windows (label inside; drag, wheel, arrows, click; STEPS defaults to min(16, pattern length), the role defaults too, and is never longer than the pattern: clamped on edit and when the pattern gets shorter, `genFit`); RANDOM: DENSITY, ACCENT, SEED, then WRITE (REPLACE / ADD / THIN).
- A muted summary: `E 3/8 · ×4 · 4 on +2 −1 · steps 1–32` (euclid: hits / steps and how often the cycle fits the pattern, `×2+4` when it does not divide; the run against the pattern before it). It shrinks to what fits; in RANDOM it goes under 1500 px.
- No Apply, Keep or Revert keys (Undo takes a run back in one step): every change writes at once, and the run ends on its own (another track, workspace or edit). Keys, small square caps with the keyboard key on them and a tiny label over them (no text buttons): RANDOM (cream, `R`: a new variation of the selected track: a new seed, or random hits and rotation in euclid; `Alt+R` or Alt-click: every track's spec, the whole pattern; the cap says `⌥R all`), DEFAULTS (no key, the cap shows `↺`: every spec from §4.2, writes this track, Alt every track).
- A divider, a small `?` key (hover: the step gestures, Trig / Accent shift-click / Slide alt-click / Has locks / Fill ⌘-click; click: the list of keys), then PAGE, its LEDs, ALL and FOL at the far right.
- The bar shows the selected track's spec (from its role until changed). The row headers show each track's spec as a tiny tag (`E 4/16`, `R 15%`, `—`).
- Range: the visible page (16 steps) by default; Alt widens to the whole length and every track's spec, like Alt+CLEAR.

**Sound page.** The MUTATE bar, always there, the rest of the Sound head's row: the title (`MUTATE · track 5 · BD`, Alt: `whole kit`), MOVE (AMOUNT, default 20 %), SCOPE (SYN / FX / RTG and each group title on the page as a chip), a muted note (seed, tracks, values), then the same square keys: RANDOM (cream, `R`: a fresh random mutation, a new seed from the trial's base; `Alt+R` or Alt-click: the whole kit). There is no WALK (removed 2026-10-01). There is no separate MUTATE key (since 2026-10-01): MUTATE, its Again and the Random key were one action in three places.

**One randomise key (2026-10-01).** `R` randomises the selected track and means what the workspace edits: on Sound a mutation (above), on Sequence a GEN variation; on every other workspace too a GEN variation of the selected track (the pattern is the one place R can always write; a toast says what changed, since that page does not show the steps), nothing when the track is set to Keep, while live recording or before the pattern is loaded (said). `Alt+R` is every track (the whole kit), matched on the physical key (`e.code` KeyR), since with Alt held macOS types another character. Record is `Alt+Space`.

**Alt as the global "all" modifier** (matches Alt+CLEAR = whole pattern, Alt+trash = all locks, Alt-drag = Control All): Alt + a GEN change = all tracks; Alt-click on RANDOM = all tracks (Sound: the whole kit). Every key is in `Keys.bind`, so the ? overlay lists it; plain keys only fire outside fields.

**The mnemonic key map (approved 2026-10-01, final).** Three rules, one modifier:
1. *Plain keys play:* A S D F G H J K L the notes of the selected track, Z / X the octave −/+, C / V the velocity −/+ (20 40 60 80 100 127, from 100; a toast and the help say it; `noteOn` uses it), Space play / stop.
2. *A plain letter off the piano row acts on the selected track:* R randomise (Sound: a fresh random sound, MUTATE; everywhere else its GEN variation), M mute / unmute it, T tap tempo (B too: the Monomachine Editor's tap key since its black keys W E T Y U O P took T, MM-PORT-PLAN.md 2026-10-05); ↑ / ↓ select the previous / next track while no value has the keys (a focused value, tempo or bar keeps ↑ / ↓).
3. *Alt is all:* Alt+R randomise every track (Sound: the whole kit), Alt+M mute all (none audible: unmute all), Alt+Delete clear the whole pattern, Alt-drag Control All, Alt-click the R cap. Two Alts are not "all", as on the machine: Alt+←/→ rotates the selected track (FUNCTION + arrows) and Alt+Space records (RECORD + PLAY: Alt + play). Alt chords are matched on `e.code` (macOS types another character with Alt).

No ⇧ or ⌘ letter commands but the standard ⌘Z / ⌘⇧Z / ⌘Y / ⌘C / ⌘V; mouse modifiers (⇧-click accent, ⇧-drag ramp, ⌘-click fill, ⇧-click an M key to prepare a mute) stay. Removed with it: Walk (W), the Alt+1..8 / Alt+Q..I mute row and its Alt+⇧ prepared mutes, ⌘R, ⇧R, ⇧D (LEN ×2), ⇧F (the Defaults cap), ⇧G (the engine menu), ⇧L (the LEARN key) and the pattern chooser's ⇧Enter (⇧-click or Now). `mdDeskKeysTest.js` (ctest `mdDeskKeysTest`) runs the page's scripts, lists every `Keys.bind` and fails on a ⇧ / ⌘ command, two entries on one chord, a missing key of this map or a hint that names a removed key.

## 6. Phased plan (smallest valuable slice first)

1. **Slice 1: Euclid on one track.**
   - Core: the `steps` row and pure edit. `mdDeskTest`: a row sets trigs in a range, keeps locks on kept steps, drops locks on removed steps, one change, one undo step; EDIT ALL refuses `acc`; schema regenerated.
   - Page: `mdDeskGen.js` with `euclid()` and `hash32`/`u()`; node tests pin E(3,8), E(5,8), E(4,16) rot 0..3, the accent split, repeat to 64 steps.
   - UI: the GEN strip, EUCLID only, selected track, ghosts, Apply, Esc. `page_contract_check.py` passes.
   - Firmware check: `mdDeskFirmwareTest <ROM> gen`: apply a `steps` row, the pattern read back equals the edit.
2. **Slice 2: Random and whole pattern.** `random()` with the three modes (node tests: same seed same steps; `add` never removes, `thin` never adds). The role table (§4.2) with a test over the catalogue (every machine has a role; MID/CTR are keep). Alt+APPLY = one `steps` with 16 rows = one dump, one undo step (`mdDeskTest`).
3. **Slice 3: Mutation.** The `params` row and pure edit (`mdDeskTest`: one working-kit change, CCs only for changed values, one undo step for a three-apply trial with one `g`). `mutate()` in node (0 % changes nothing, protect holds, unnamed slots untouched, same seed same values, from base vs current). The MUTATE strip.
4. **Slice 4: QoL items** from §7 in rank order, each on `steps`/`params` where it fits. Specs persisted in `md-desk/setup`.
5. **Later:** generated locks (respecting 64 locked (track, param), refusing beyond with a note), the MM port of `mdDeskGen.js` (shared block like MODAL), a Grids-like map.

## 7. Quality-of-life list (ranked by value / effort)

1. **Lock budget in the lane header:** "41 / 64 locked parameters", red near the limit. Data is in the pattern. Tiny.
2. **Rotate a track:** Alt+Left/Right on the selected track moves trigs, accents and locks one step (wraps at length). Page computes, `steps` + locks; one undo step per press train.
3. **Every-N fill:** Cmd+click a step fills every 2nd/4th (Cmd+Shift) step from there to the end. One `steps`.
4. **Wheel on a step edits the current lane's lock** there (Shift = fine). Reuses `lock` with a gesture.
5. **Ramp in the lock lane:** Shift-drag draws a straight line from the press to the release. One gesture of `lock`s.
6. **Double the pattern:** length x2 and copy steps and locks into the new half (EXTENDED only above 32). One pattern change.
7. **Paste a track's steps to several tracks:** Cmd+V with Shift-selected row headers. Existing `pasteSteps` per track in one `g`.
8. **Unsolo / unmute all:** one key (`0`) and a button. Today it is one click per track.
9. **Undo says what it undoes:** the undo button's tip names the step ("Undo: Euclid on track 1"). Needs a label on history steps; medium.
10. **Clear accents only / slides only** in the CLEAR menu (and Alt for the whole pattern). Today CLEAR takes trigs and their locks together.
11. **Compare to saved kit:** hold `C` on the Sound page to hear the stored slot, release to return. Medium-high effort (two `set`s, or the machine's UNDO KIT); last.
