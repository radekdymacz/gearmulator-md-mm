# Design: one keyboard and mouse map for both editors

- 2026-10-08. Design only; nothing is built. Hammock style: the inventory (what is), principles (candidates,
  critique, a pick), the map (before → after), rebinding, the owner's decisions, slices.
- The ask (the owner): "let's rethink and do proper keyboard mapping" instead of patching shortcuts one at a time.
  What led to it: step selection shipped on ⌥-click / ⌥-drag and slide moved to ⌥⇧-click
  ([DESIGN-step-selection.md](DESIGN-step-selection.md)), but ⌥ means "all" everywhere else; ⇧-click is accent;
  ⌘-click fills every 2nd / 4th; ⌘C / ⌘V reportedly do not reach the page in real use (investigated separately).
- Scope: the Machinedrum Editor (MD) and the Monomachine Editor (MM), standalone and plug-in, macOS, Windows,
  Linux. MD first, MM in the same pass where the concept is the same (the owner's rule: settle on MD, port once).
- Read first: [FOUNDATION.md](FOUNDATION.md) (`Keys`, `Held`, `Gesture`, the modal rule), the guide's key tables
  (`site/public/guide/index.html` #keys).

Paths below are relative to `source/elektron/md/mdJucePlugin/skins/` unless they start with `source/` or `doc/`.
`mm:` lines are in the generated `mmStudio/mmMockup.js` (its sources: `doc/modern-ux/mm-mockup/src/`).

## 1. Inventory: what is bound today

Marks: **C** a conflict (one key or gesture, two meanings that can surprise), **≠** MD and MM differ for the same
concept, **H** a key that hosts or the OS commonly take (§1.6).

### 1.1 Keys, both editors

| Key | Action | Where | MD | MM | Source | Marks |
|---|---|---|---|---|---|---|
| A S D F G H J K L | play the selected track, white keys | anywhere | yes | yes | mdDeskLive.js:263,268; mm:1668,1673 | H (Live's computer keyboard is the same block) |
| W E T Y U O P | black keys | anywhere | — | yes | mm:1668,1674 | ≠ |
| Z / X | octave −/+ (MD ±2, MM ±3) | anywhere | yes | yes | mdDeskLive.js:264-269; mm:1669-1675 | ≠ range |
| C / V | velocity −/+ | anywhere | yes | yes | mdDeskLive.js:266-270; mm:1671-1676 | |
| T, B | tap tempo | anywhere | T and B | B only (T is F♯) | mdDeskLive.js:131; mm:1683 | ≠ |
| M / ⌥M | mute selected / all (none audible: unmute all) | anywhere | yes | yes | mdDeskLive.js:49-50; mm:1679-1680 | |
| 0 | unmute and unsolo all | anywhere | yes | yes | mdDeskComforts.js:126; mm:1685 | H (Logic screensets) |
| R / ⌥R | randomise selected / all (Sound: MUTATE; else GEN) | anywhere | yes | yes | mdDeskGenUi.js:263-264; mm:1881-1883 | |
| ↑ / ↓ | previous / next track (a focused value keeps them) | anywhere | 16 tracks | the side's 6 | mdDeskLive.js:53; mm:1681 | H |
| ⌥← / ⌥→ | rotate the selected track (FUNCTION + arrows) | Sequence | yes | yes | mdDeskComforts.js:125; mm:1684 | ⌥ not "all" |
| ← / → | previous / next row | Song | yes | yes | mdDeskRender.js:179; mm:3319 | H |
| ← → ↑ ↓, PgUp/PgDn | a focused value ±1 (⇧ ×10; tempo ⇧ 0.1) | a focused value | yes | yes | mdDeskGestures.js:173,269; mm:3197,3340 | ⇧ two meanings (§1.5) |
| Space | play / stop | anywhere | yes | yes | mdDeskRender.js:172; mm:3312 | H (every host) |
| ⌥Space | live record (MM: again = off) | anywhere | yes | yes | mdDeskRender.js:173; mm:3313 | ⌥ not "all"; ≠; H (Windows: the window menu) |
| 1 … 6 | workspace (6 = Control, only with mapping on) | anywhere | Seq Sound Mix **Sampler** Song Ctrl | Seq Sound Mix **Perform** Song Ctrl | mdDeskRender.js:174; mm:3314 | H (Logic screensets); guide says 1-5 |
| 1 … 8 | LEARN: controller knob for the clicked value | LEARN | — | yes | mm:3301,3310 | shadows 1-6 while learning |
| [ / ] | previous / next page of 16 | Seq (MD: also Sampler) | yes | Seq | mdDeskRender.js:175; mm:3315 | ≠ |
| Delete / ⌫ | MD: clear the selected steps, else the page shown; MM: the page shown; Song: delete row | Seq, Song | yes | yes | mdDeskRender.js:176; mm:3316 | ≠; destructive with nothing selected |
| ⌥Delete / ⌥⌫ | clear the whole pattern | Seq | yes | yes | mdDeskRender.js:177; mm:3317 | |
| ⌘Z, ⇧⌘Z, ⌘Y | undo, redo (also over panels) | anywhere | yes | yes | mdDeskRender.js:166-168; mm:3304-3306 | H (host Edit menu, host undo) |
| ⌘C / ⌘V | copy / paste (Seq: selection or page; Sound; Song row; MM Perform assign) | anywhere | yes | yes | mdDeskRender.js:169-170; mm:3307-3308 | H; reported not reaching the page |
| ⌘X / ⌘D | cut / duplicate the selected steps | Seq | yes | — | mdDeskSelect.js:117-118 | ≠; H |
| Esc | in order: dialog, keys list, LEARN, GLOBAL, unmark tracks, clear selection | anywhere | yes | yes | mdDeskRender.js:165,171; mdDeskGlobal.js:78; mdDeskComforts.js:127; mdDeskSelect.js:119; mm:3303,3309,1686 | one key, ordered by `when` |
| ? | the list of keys | anywhere | yes | yes | mdDeskKeys.js:27; mm:644 | |
| , | AUDIO / MIDI settings | anywhere | yes | yes | shared/deskAudio.js:64; mdDeskAudio.js:22; mm:3311 | |
| ⌘− ⌘+ (⌘=) ⌘0 | page zoom (capture, before the page) | anywhere | yes | yes | shared/deskZoom.js:14 | Mac: ⌘ only; Keys: ⌘ or Ctrl (§1.5) |
| Library: arrows, Enter, A–H, F2, Delete, ⌘C/⌘V/⌘Z, Esc; Enter/Space on KIT or pattern opens | kit library, pattern chooser | panel | yes | yes | mdDeskLibrary.js:178-196; mm:2895-2915 | A–H shadow the piano while open (intended) |
| Esc, arrows | machine picker, key-style dropdowns | panel | yes | yes | mdDeskPicker.js:38,81; mm:1319,2380 | |

### 1.2 Mouse with a modifier

| Gesture | Action | Where | MD | MM | Source | Marks |
|---|---|---|---|---|---|---|
| press / drag a step | paint trigs (from a trig: off) | Seq grid | yes | roll: note, drag pitch / paint | mdDeskGestures.js:190; mm:1511-1516 | |
| ⇧-click step | MD accent; MM chord note | Seq | accent | chord note | mdDeskSeq.js:73; mm:1515 | ≠ |
| ⌥-click step | MD select step; MM delete note / NOTE OFF on empty | Seq | select | delete | mdDeskGestures.js:231; mdDeskSeq.js:71; mm:1509-1510 | **C** (⌥ = all elsewhere), ≠ |
| ⌥-drag steps | MD select block; MM erase notes | Seq | select | erase | mdDeskGestures.js:228-236; mm:1509 | **C**, ≠, H (Linux: window move) |
| ⌥-drag inside a selection | drop a copy | Seq | yes | — | mdDeskGestures.js:235 | |
| ⌥⇧-click step | slide | Seq | yes | — (slide is a row) | mdDeskSeq.js:72; mdDeskRender.js:181 | moved 2026-10-07 |
| ⌘-click / ⌘⇧-click step | fill every 2nd / 4th to the end | Seq | yes | roll | mdDeskSeq.js:67; mm:1508 | blocks ⌘ for selection |
| click / drag step ruler, ⇧-click ruler | select steps of the track, extend | Seq | yes | — | mdDeskGestures.js:232-236 | |
| wheel on a step | its lock ±4 (⇧ ±1) | Seq | step | lock lane | mdDeskGestures.js:69-77; mm:1596-1599 | ⇧ = fine here |
| wheel on a MM step / roll | pitch ±1 (⇧ ±12) / scroll roll (⇧ 12) | Seq | — | yes | mm:3194-3195 | ⇧ = coarse here |
| ⇧-drag lock lane | ramp | Seq | yes | yes | mdDeskGestures.js:151; mm:3154 | |
| ⌥-drag lock lane | erase locks | Seq | yes | yes | mdDeskGestures.js:151; mm:3154 | ⌥ = erase, H (Linux) |
| ⌥-click lane clear | clear every lock of the track | Seq | yes | yes | mdDeskSeq.js:84; mm:3220 | |
| drag slide / swing / env rows | paint | Seq | — | yes | mm:3146 | |
| ⇧-click track header | mark for paste to many | Seq | yes | yes | mdDeskApp.js:318; mm:1624 | |
| ⌥-drag a value / curve handle | Control All (FUNCTION + knob) | Sound, Mix | yes | synth tracks | mdDeskLive.js:153; mm:3152-3153 | H (Linux) |
| ⇧-drag a value | fine (×0.25) | values | yes | yes | mdDeskGestures.js:159; mm:3167 | |
| wheel / ⇧-wheel on a value | ±1 / ±10 | values | yes | yes | mdDeskGestures.js:171; mm:3193 | ⇧ = coarse here |
| double-click a value | reset (MD 64, VOL 100; MM 0 or 64) | values | yes | yes | mdDeskGestures.js:172; mm:3196 | ≠ |
| tempo drag, ⇧ | ×1 / fine | top bar | yes | yes | mdDeskGestures.js:267; mm:3339 | |
| click / ⇧-click LCD value | next / previous | LCD line 2 | yes | yes | mdDeskTop.js:173; mm:3098 | ⇧ = back |
| ⌥-click / ⌥-drag LEN | inner length instead of total | LCD | yes | — | mdDeskTop.js:173; mdDeskGestures.js:117 | **C** (⌥ not all), ≠ |
| click / ⇧-click / wheel GEN value | +1 / −1 / ±1 (⇧ ±10) | GEN bar | yes | yes | mdDeskGenUi.js:250; mdDeskGestures.js:59; mm:1869,1878 | |
| ⌥ (⌘, Ctrl) click R key | randomise all | GEN bar | yes | yes | mdDeskGenUi.js:247; mm:1866 | ⌘ also = all here only |
| ⌥-click CLR | clear the whole pattern | top bar | yes | yes | mdDeskTop.js:229; mm:3272 | |
| ⇧-click M key; let ⇧ go | prepare mutes, apply on release | anywhere | yes | yes | mdDeskLive.js:35-36; mm:3105,3209 | hold-only |
| drag across M / S keys | toggle-paint mutes / solos | anywhere | yes | yes | mdDeskLive.js:81; mm:3120 | |
| ⇧-click song step buttons | ×10 | Song | yes | yes | mdDeskSong.js:135; mm:3261 | |
| ⇧-click page key | previous page | Seq | yes | yes | mdDeskSeq.js:91; mm:3251 | ⇧ = back |
| ⌥-click / ⇧-click chop step | reverse (END lock) / retrig | Sampler | yes | — | mdDeskSampler.js:333-334 | **C** (⌥ select, ⇧ accent on Seq) |
| ⌥ or ⌘-click kit slot; double-click | select without loading; rename | library | yes | yes | mdDeskLibrary.js:157,164; mm:2884,2888 | ⌥ not all |
| ⇧-click pattern slot | load at once, not queued | chooser | yes | yes | mdDeskLibrary.js:160; mm:2885 | |
| ⌥-click transpose plate | an octave down | MM Seq dock | — | yes | mm:2020-2024 | ⌥ not all |
| right-click page header | the editor's menu | top bar | yes | yes | mdDeskLive.js:179; mm:3558 | right-click **elsewhere is unused** |
| holding ⌥ | shows what Alt does (hint) | anywhere | yes | yes | mdDeskComforts.js:29-32; mm:1562-1563 | |

Dead code: `clickStep` (mm:1435-1438, ⇧ = NOTE OFF, ⌥ = trigless) has no caller and gives ⇧ and ⌥ a third set of
meanings; delete it in the MM slice.

### 1.3 Native menus

| Item | Key | Where | Source |
|---|---|---|---|
| Editor › Page Zoom › Zoom In / Out / Actual Size | labels "Cmd +" etc. (the page's deskZoom does the keys; no menu key equivalent) | both, all builds (header right-click; standalone menu bar) | source/elektron/md/mdJucePlugin/mdPageEditor.cpp:195-217 |
| Editor › Updates › Check for Updates Now, Check Daily | none | both | mdPageEditor.cpp:361-375 |
| Editor › GUI Scale, Settings… | none | both | source/jucePluginEditorLib/pluginEditorState.cpp:347-359,449 |
| Audio › Audio/MIDI Settings…, Save / Load state | none (page has `,`) | standalone | source/jucePluginEditorLib/standaloneApp.h:120-123 |
| App menu › Settings…, Audio/MIDI Settings… | none (macOS convention ⌘, missing) | macOS standalone | standaloneApp.h:62-63 |
| Record › Start / Stop Recording, Show Recordings | ⇧⌘R (a real key equivalent) | macOS standalone | source/elektron/md/mdJucePlugin/mdRecordMenu.h:3-12, mdRecordMenu.cpp:80 |
| (no Edit menu) | ⌘Z ⌘C ⌘V have no menu route in the standalone | standalone | standaloneApp.h:93-98 |

### 1.4 Conflicts and inconsistencies found

1. **⌥ has eight meanings.** "All" (⌥R, ⌥M, ⌥Delete, ⌥CLR, ⌥ R key, ⌥ GEN value, ⌥ drag value), FUNCTION pairs
   (⌥←/→ rotate, ⌥Space record), select (MD step), delete/erase (MM roll, lock lanes), reverse (chop), inner length
   (LEN), select-without-loading (library), octave down (MM transpose plate). The map's own comment
   (mdDeskKeys.js:9-12) admits two exceptions; there are six more.
2. **⇧ on values has two directions.** Fine on drag (×0.25), on lock wheel (1 instead of 4) and tempo arrows (0.1);
   coarse on value wheel and arrows (×10), GEN wheel (×10), MM pitch wheel (×12), song steps (×10).
3. **A step's ⇧ and ⌥ differ between editors** (MD accent / select; MM chord / delete), so the gestures do not
   port; DESIGN-step-selection.md §7 left "a modifier for one step on MM" open because of it.
4. **⌘ on a step is spent on fill**, so ⌘ (the desktop's selection modifier) was not available for selection.
5. **⌘ is Ctrl on a Mac too in some places.** `Keys` reads `metaKey || ctrlKey` (shared/deskKeys.js:13) and so do
   the step handlers (mdDeskSeq.js:67; mm:1508), so on a Mac Ctrl-click (which macOS also turns into a right-click)
   fills; deskZoom reads ⌘ only on a Mac (deskZoom.js:15); the modal layer lets only `metaKey` through
   (shared/deskModal.js:71), so on Windows / Linux Ctrl+Z over a panel may never reach the `modal: "panel"` entry
   (to check in slice K1). Tooltips say "Cmd" on every OS (mdStudio.html:42, undo/redo titles).
6. **The piano block is matched by `e.key` but pitched by `e.code`** (mdDeskLive.js:249,263; mm:1658-1668). On
   AZERTY or QWERTZ some keys play nothing (AZERTY's physical A types "q": no entry matches; QWERTZ's Y/Z swap breaks
   the octave key and MM's G♯).
7. **Delete with nothing selected clears the page shown** (MD and MM). Desktop habit: Delete with no selection does
   nothing. ⌘C without a selection copying the page is harmless; Delete is not.
8. **Windows and Linux take ⌥.** Alt alone focuses the standalone's menu bar on Windows / Linux (`setMenuBar`,
   standaloneApp.h:66); Alt+Space opens the window's system menu on Windows (it is our live record); Alt-drag moves
   the window on several Linux desktops (XFCE, some KDE setups), which takes ⌥-drag select, Control All and erase.
9. **No keyboard-only way to edit steps** (no step cursor; selection needs a pointer), and three hold-only
   gestures (⇧ prepare mutes, ⌥ Control All, ⌥ rotate grouping) have no latch.
10. Guide drift: the guide lists workspaces 1-5 (the map binds 6 with mapping on) and is hand-written from the map.

### 1.5 Where ⌘ / Ctrl comes from

| | macOS | Windows, Linux |
|---|---|---|
| `Keys` (dispatcher) | ⌘ or Ctrl | Ctrl (or the Windows key, `metaKey`) |
| step / roll clicks | ⌘ or Ctrl (Ctrl-click is also a right-click) | Ctrl |
| deskZoom | ⌘ only | Ctrl only |
| modal layer pass-through | ⌘ only | nothing passes (`metaKey` is the Windows key) |

### 1.6 Keys hosts and the OS commonly take

Hosts differ and change between versions; what to design for, not a list to rely on:

- **Space**: every DAW's transport. A plug-in window gets it only while it has keyboard focus, and some hosts
  never pass it.
- **⌘Z ⇧⌘Z ⌘X ⌘C ⌘V ⌘D ⌘A** (Ctrl on Windows): the host's Edit menu often wins on macOS (the menu bar's key
  equivalents run before the plug-in's view). ⌘Z that reaches the host undoes the host's edit, not ours.
- **Letters and Z X C V**: Ableton Live's computer MIDI keyboard uses the same block as ours when it is on.
- **Digits**: Logic recalls screensets with them. **Arrows**: nudge or navigation in several hosts.
- **⌥ / Alt**: see 1.4 #8. **Ctrl-click on a Mac**: a right-click.

Consequence (principle P4 below): no action may exist only behind a key.

## 2. Principles

### 2.1 Candidates

| | Philosophy | For | Against |
|---|---|---|---|
| A | **Desktop first**: ⇧-click extends, ⌘-click selects, ⌘C/X/V/D/A, Delete, arrows move a cursor, double-click edits, right-click a context menu. Marks (accent, slide) move to keys or the menu. | What every new user expects; works with any host's habits. | Accent is the MD's most used mark and is on ⇧-click today; moving it breaks every user's hands and the hardware feel. |
| B | **Elektron mnemonic**: the keyboard is the machine's panel (FUNCTION = ⌥, TRIG keys, PLAY/STOP/REC), as upstream PR #91 binds the old panel editor. | Elektron owners read it at once. | Our pages are not the panel; 16 trig keys and a FUNCTION layer eat the piano block; desktop users get nothing they know. |
| C | **One meaning per modifier** across both editors and every page, with the machine's FUNCTION as ⌥, desktop conventions where the desktop has one (selection, clipboard, undo, zoom), and every action also reachable without a key (button, menu, context menu). | Learnable rule ("⌥ = every track"), portable MD ↔ MM, DAW-safe, accessible. | Some current gestures move (selection, fill); needs a context menu and an FN latch (new UI). |
| D | **Status quo + rules on paper**: keep every gesture, document exceptions. | No relearning. | It is the eight-meaning ⌥ we have; MM cannot get selection; Linux/Windows lose ⌥ gestures; host-eaten keys stay unreachable. |

### 2.2 Pick: C

Critique of C: its cost is concentrated in one place (the step grid), its benefit spreads to every page and to MM;
A's one benefit (desktop selection) C takes too, without moving accent; B is the old editor's job (PR #91); D does
not fix the reported problems. The rules:

- **P1. Plain = play.** No modifier: keys play the selected track, the pointer paints, presses, drags.
- **P2. ⇧ = add, extend, or the other way.** A step's added mark (MD accent, MM chord note), add to a set (mark
  a track for paste, prepare a mute), extend a selection, the previous value on a click-stepper. On continuous
  values ⇧ is the value's **other speed** (finer where plain is coarse, ×10 where plain is already the smallest
  unit); each value's tooltip says both (D5).
- **P3. ⌥ = FUNCTION: every track.** ⌥ on a command, a key or a value means all tracks (R, M, Delete, CLR, R key,
  GEN value, Control All), plus the machine's own FUNCTION pairs (← / → rotate, Space record). On a **grid** ⌥
  erases (MM roll, lock lanes). Nothing else: no select, no inner length, no reverse, no select-without-loading.
- **P4. ⌘ (Ctrl off a Mac) = commands and selection.** The standard edit keys, zoom, and on steps the selection
  (click one, drag a block, ⇧ to extend). Exactly `metaKey` on macOS and `ctrlKey` elsewhere (Ctrl on a Mac
  is a right-click). Tooltips and the ? list say ⌘ or Ctrl per OS.
- **P5. Nothing only behind a key.** Every keyed action also has a pointer route: the top bar's buttons (Undo, Redo,
  Copy, Paste, Clr exist), a **context menu** on steps and on the selection (right-click, Ctrl-click on a Mac, the
  Menu key), the R and CLR keys, and an on-screen **FN** latch that gives the next action ⌥ (the hardware's
  FUNCTION key; also the answer for Linux Alt-drag and Windows Alt). In the standalone, an **Edit** menu with
  the key equivalents forwards to the page (a menu route even when the web view does not see ⌘C).
- **P6. Positional piano, mnemonic commands.** The playing block (A–L, W E T Y U O P, Z X C V) is matched by
  physical key (`e.code`), like a DAW's typing keyboard; command letters (R, M, B, ?) by their meaning (`e.key`,
  `e.code` with ⌥ on a Mac as today).
- **P7. Same concept, same gesture on MD and MM**; a machine-only concept may have its own (MD slide, MM chord).
- **P8. Discoverable.** The ? list stays generated from the map; the guide's tables are generated from it too
  (no drift); the context menu shows each item's key; holding ⌥ shows what FUNCTION does (exists).

## 3. The map, before → after

Only rows that change, plus the step grid in full. Everything in §1 not listed here stays.

### 3.1 Steps (Sequence)

| Gesture | Before (MD / MM) | After (MD / MM) | Relearn |
|---|---|---|---|
| click / drag | trig paint / note, pitch, paint | same | — |
| ⇧-click | accent / chord note | same | — |
| ⌥⇧-click | slide / — | same (D2) | — |
| ⌥-click, ⌥-drag | **select** / delete, erase | MD: nothing, a toast "select is ⌘-click now" for one release; MM: same as before (erase) | MD selection users (shipped 2026-10-07) |
| ⌘-click | fill every 2nd / same | **select the step** / same (when MM has selection) | fill users |
| ⌘-drag | — | **select a block** (steps × tracks) | — |
| ⌘⇧-click | fill every 4th | **extend the selection** to the step | fill users |
| ⌘-drag from inside the selection | (was ⌥-drag) | drop a copy where you let go | selection users |
| ruler click / drag, ⇧-click ruler | select, extend | same | — |
| right-click (Ctrl-click on a Mac, Menu key on a focused step) | nothing | **step menu**: Trig, Accent, Slide (MM: Note off, Trigless), Select, Copy, Cut, Paste here, Duplicate, Clear, Fill every 2nd, Fill every 4th, Clear its locks; each with its key | new |
| wheel on a step | lock ±4 (⇧ ±1) | same | — |

### 3.2 Keys

| Key | Before | After | Relearn |
|---|---|---|---|
| ⌘A | — | select every step of every track up to the length (then ⌘C copies the pattern as a block) | new |
| ← / → (Seq) | nothing | move the selection one step (one step selected: a cursor) | new |
| ⇧← / ⇧→ (Seq) | nothing | extend the selection | new |
| ↑ / ↓ (Seq, a selection) | select the track | move the selection a track; the selected track follows | small |
| Enter (Seq, a selection, no button focused) | nothing | trig on / off on the selected steps | new |
| Delete / ⌫ (Seq) | the selection, **or the page shown** | the selection only; with none a toast points to Clr and ⌥Delete (D3) | page-clear users use Clr |
| ⌘X ⌘D | MD only | both (MM after its selection core) | — |
| T | MD tap tempo | MD: the F♯ black key (D6); tap is B on both | MD T-tappers |
| W E T Y U O P | MM only | both; MD plays them on pitched machines (ROM, RAM-P), others at their own pitch (D6) | — |
| the piano block | `e.key` (MD) / mixed (MM) | `e.code` on both: physical positions on every layout | AZERTY/QWERTZ users gain keys |
| ⌘, (macOS standalone) | — | Settings… (the GLOBAL panel) in the app menu, the macOS convention; `,` stays | new |
| ⌘Z ⇧⌘Z ⌘X ⌘C ⌘V ⌘D ⌘A (standalone) | page only | also in a native **Edit** menu forwarding to the page | — |

### 3.3 Pointer elsewhere

| Gesture | Before | After | Relearn |
|---|---|---|---|
| ⌥-click / ⌥-drag LEN | inner length | its own LCD line 2 value, **ILEN** (click, drag, wheel like LEN) (D7) | LEN users |
| ⌥-click chop step | reverse | chop step menu (right-click): Reverse, Retrig; ⇧-click retrig stays (P2: add a retrig) | Sampler users |
| ⌥ / ⌘-click kit slot | select without loading | dropped: the arrow keys already select without loading, Enter loads (D7) | few |
| ⌥-click MM transpose plate | octave down | the plate shows two octaves, or ⇧-click (the other way, P2) | MM transpose users |
| ⌥ / ⌘ / Ctrl-click R key | randomise all | ⌥ (or FN) only; ⌘ dropped (P3) | — |
| FN key (new, top bar) | — | click: the next click, drag or key gets ⌥ (lit until used; double-click latches); shows the ⌥ hint | new |
| ⌥-drag a value on Linux | the window may move | FN, then drag | — |

### 3.4 Unchanged on purpose

R / ⌥R, M / ⌥M, 0, 1-6, [ ], ? , ↑ / ↓ tracks, ⌥← / ⌥→ rotate, Space, ⌥Space record (plus the REC key, which
exists, for Windows where Alt+Space is the window menu), ⌘Z / ⇧⌘Z / ⌘Y, ⌘− / ⌘+ / ⌘0, ⇧⌘R Record, the library's
keys, ⇧-click accent, ⇧-drag ramp, ⌥-drag erase on lanes and the MM roll, wheel on a step, mute prepare and paint.

### 3.5 What users relearn (release notes)

1. Select steps with **⌘-click / ⌘-drag** (Ctrl on Windows and Linux), extend with **⌘⇧-click**; ⌥-drag a
   selection is now ⌘-drag. (Shipped on ⌥ for one release.)
2. **Fill every 2nd / 4th** is in the step's right-click menu.
3. **Delete** clears only the selection; Clr clears the page.
4. MD: tap tempo is **B** (T plays F♯ on pitched machines).
5. LEN's inner length, the chop's reverse and the transpose plate's octave down move (§3.3).

## 4. Rebinding

**What upstream PR #91 does** (`gh pr view 91 -R joelanders/gearmulator-md-mm`, "Add keyboard rebinding with
persistence"): for the old panel editor, a fixed table of 43 keys → hardware panel controls (arrows, Enter,
Backspace = EXIT, 1-8 Q-I = trigs 1-16, Tab = RECORD, O/P = STOP/PLAY, bank and page keys), a "Key Bindings"
settings tab, persistence in the existing PropertiesFile so the standalone and the plug-in share it, conflicts
cleared automatically (the old key loses its binding), a reset, and ⇧ hard-coded (its author notes that as a gap).
Lessons: shared persistence across standalone and plug-in is right; silent conflict clearing loses bindings
without telling; one modifier left out makes the table incomplete; it rebinds keys only, not mouse gestures.

**For these editors**, if added: the map is already data (`Keys.bind` entries), so a rebinding is an override value
`{id → {code or key, mod}}` applied at bind time, stored in the editor's config (`keyMap`, beside `pageZoom`,
through a bridge op, not `localStorage`: one place for standalone and plug-in, survives updates); conflicts refused
with "already used by …, swap?" (the keys test's no-two-entries rule run live); keyboard commands only (pointer
gestures and the piano block are not rebindable; the piano gets layouts by `e.code`); the ? list and the step menu
show the bound key.

**Recommendation: not now.** Do the fixed map (P6 fixes international layouts, P5 fixes host-eaten keys, which are
the two real reasons people rebind) and give every entry a stable `id` (slice K0) so rebinding is a small slice
later if asked. A rebindable map also makes the guide, the ? screenshots and support answers per-person.

## 5. Decisions for the owner

| # | Decision | Option 1 | Option 2 | Recommend |
|---|---|---|---|---|
| D1 | Step selection modifier | keep ⌥ (shipped) | **⌘ / Ctrl** (fill to the step menu) | 2: ⌥ stays "all"; no OS takes ⌘-click; same on MM (its ⌥ is erase); desktop habit |
| D2 | Slide | ⌥⇧-click (as shipped) | back to ⌥-click (pre 2026-10-07) | 1: keeps ⌥ alone off the MD grid, no second change in two weeks |
| D3 | Delete with no selection | clears the page shown (today) | **does nothing + toast** (Clr clears the page) | 2: Delete is destructive; Clr and ⌥Delete stay |
| D4 | DAW-safe routes | buttons only (today) | **step context menu + FN latch + standalone Edit menu** | 2: P5; also the a11y answer for hold-only gestures |
| D5 | ⇧ on values | keep "the other speed" (document it per value) | make ⇧ always fine (wheel/arrows ×10 moves to PgUp/PgDn only) | 1: no relearn; tooltips state both speeds |
| D6 | MD black keys + tap on B only | yes (one piano for both) | no (MD keeps T tap, white keys only) | 1: P7, chromatic pitch on ROM/RAM-P machines |
| D7 | LEN inner length, chop reverse, library ⌥-click, MM plate ⌥ | move them (§3.3) | leave them as documented exceptions | 1, but last: low traffic, do in the MM-port pass |
| D8 | Rebinding | now (K-R slice) | **later, ids now** | 2 (§4) |

## 6. Implementation, in slices (MD first, MM in the same pass where marked)

| Slice | What | Tests |
|---|---|---|
| **K0 map as data** | `id` and `scope` on every `Keys.bind` entry (both pages); pointer gestures described as entries already are (`keys: ["step"]`); `scripts/keymap-export.js` writes both maps to JSON; the guide's key tables generated from it (site build) | keys tests: every entry has a unique `id`; a **parity test**: an `id` on both MD and MM has the same key and modifiers unless listed machine-only; guide check: generated tables = committed |
| **K1 platform modifiers** | one `Mods.of(e)` in `shared/deskKeys.js` (⌘ = `metaKey` on macOS, `ctrlKey` elsewhere) used by `Keys`, every pointer handler, deskZoom and the modal pass-through; tooltips and ? labels per OS ("Ctrl+C") | `deskKeysTest.js` with a Mac and a Windows stand-in; page test: Ctrl-click on a Mac does not fill or select |
| **K2 selection on ⌘ (MD)** | `mdDeskGestures.js` select gesture on ⌘ (click, drag, ⇧ extend, drag-copy); ⌥ on a step: toast; `mdDeskSeq.js` fill removed from ⌘; ⌘A, ← / → / ⇧← / ⇧→ / ↑ / ↓ move and extend, Enter toggles; Delete on selection only (D3); keys rules: ⌘A allowed; DESIGN-step-selection.md §2 amended | `mdDeskPageTest.js`: ⌘-click + ⌘C + ⌘-click + ⌘V, ⌘-drag block, ⌘⇧-click extend, ⌘-drag copy, arrows, Enter, Delete without selection = no command; ⇧-click accent and ⌥⇧ slide unchanged; journey `journey-seq-select-copy-paste` on ⌘ |
| **K3 step menu** | `shared/deskMenu.js`: a small HTML menu in the modal layer (`DIALOGS` row, kind `panel`), items `{label, keyId, run, enabled}`; MD step and selection items; chop menu (reverse, retrig) | `deskModalTest.js` row; page test: each item sends the same command as its key; journey `md-seq-step-menu` (right-click, Fill every 2nd) |
| **K4 FN latch** | top-bar FN key: next action gets ⌥ (`Mods.of` reads it), lit while armed, double-click latches, Esc drops it; hint shown | page test: FN then R = randomise all, FN then drag = Control All, FN then Delete = clear pattern; journey `md-fn-control-all` |
| **K5 standalone Edit menu** | `standaloneApp.h`: an Edit menu (Undo, Redo, Cut, Copy, Paste, Duplicate, Select All, Delete) with key equivalents that send a `pageKey {id}` to the page, which runs the entry by `id` (K0); ⌘, Settings in the app menu | **real-key test** (macOS, `scripts/mdmm-realkeys.sh`): post ⌘C / ⌘V / Space / ⌘Z as OS events (System Events, needs Accessibility) to the running standalone, read the result through the journey runner's documents; the same through the VST3 test host. It also pins down the ⌘C / ⌘V report |
| **K6 piano** | `e.code` dispatch for the piano block (both); MD black keys on pitched machines; tap on B (D6) | keys tests on QWERTY / AZERTY / QWERTZ stand-ins (`code` vs `key`); journey `md-keys-black-key` (PTCH moves a semitone) |
| **K7 MM port (one pass)** | with MM's selection core (DESIGN-step-selection.md §7): ⌘ selection on the roll, fill to the roll's menu, ⌘X ⌘D ⌘A, Delete (D3), step menu items (Note off, Trigless, chord), FN, `e.code`; remove dead `clickStep`; D7 moves | `mmKeysTest.js` rules as MD's; parity test green; `mmViewTest` for the new ops; journeys `mm-seq-select-copy-paste`, `mm-seq-step-menu` |
| **K8 words** | ? list groups per P1-P4, guide tables generated (K0), release notes "What you relearn" (§3.5), tooltips | guide check (K0) |

Order: K0, K1 (no visible change), then K2 + K3 together (the fill needs its new home before ⌘ is taken), K5, K6,
K4, K7, K8. K5's real-key test should run first if the ⌘C / ⌘V investigation needs it.
