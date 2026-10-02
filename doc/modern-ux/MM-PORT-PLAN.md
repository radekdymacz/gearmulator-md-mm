# MM port plan: the Machinedrum Editor's 2026-10 features on the Monomachine Editor

- 2026-10-01. The owner's words: "add everything we added to the MD to the MM, GEN as well", except the
  Sampler (the MM has none). The MD side: `git log f348a57f0..e20135d7f` plus the uncommitted MD work of the
  same day; the design is [DESIGN-generators.md](DESIGN-generators.md) (its "mnemonic key map" is final).
- Phases: **1** = this plan + a) b) c) (built, see the end); **2** = d) GEN and MUTATE; **3** = e) Sound by
  function; **4** = f) library and song.

## The one difference that shapes everything

The MD page sends **edits** (`trig`, `lock`, `steps`, `rotate`, `clearPattern` ...) to a C++ core that knows the
pattern. The MM page (the mockup, played by `mmAdapter.js`) owns its view state and sends **whole documents**
(`{"op":"set","kind":"pattern","doc","g"}`) at each gesture; the core diffs, paces and undoes them (P6). So
every MD "core edit" of this list is, on the MM, **a page function on the mockup's state + `structEdited()`**:
no new core rows, no schema change, no C++ for a)–d). One gesture id per gesture keeps one undo step
(`HOST.edited("commit")` opens the next one). Risk: none new; the document path is the tested one
(`mmDeskTest`, `mmConvertTest`, firmware `p4`).

The MM's Sequence is not a 16-row trig grid: it is **one piano roll for the selected track** with ENV / SLIDE /
SWING rows and the MD's lock lane under it, six tracks a side (SYNTH / MIDI), 62 locked parameters pooled
over all twelve tracks (manual 1-58). Steps are `{n:[notes], a, f, l}` (a trig with its pitch or chord and its
envelope trigs), `{off:1}` (NOTE OFF) or empty.

## a) Page-wide basics

| Feature | Applies | How on the MM | Core | Risk |
|---|---|---|---|---|
| Placement-aware dropdowns (`placeK`) | yes | the MD's `placeK` in `60-ui.js`: fixed to the viewport, below or above, capped height that scrolls, placed again on resize / scroll | none | none |
| Menus hold renders while open | yes | `busyNow()` (which the adapter asks before it applies the core's documents) also true while `#kpop` or `#machpop` is open; `render()` no longer closes them under the person (a render while one is open keeps it) | none | a document that waits for the menu: applied at the commit after it closes (as after a drag) |
| No text selection on drag | yes | body `user-select:none` (text fields keep it), `selectstart` refused outside fields, images / canvases not draggable | none | none |
| Alt global: Alt+CLR / Alt+Delete clear the pattern | yes | `clearPattern()`: all 12 tracks' steps, slides and every lock; swing, arp, length kept | none (one `set`) | none |
| Alt + lane clear = all locks of the track | yes | `#clearLane` with Alt | none | none |
| Keys show their Alt meaning | yes | `S.alt` + `body.althold`: CLR reads ALL, the lane's clear key says "every lock of track n" | none | none |

## b) The key map (the MD's final mnemonic map)

| Key | MM |
|---|---|
| A S D F G H J K L | the selected **synth** track, real MIDI notes (note on at the key, note off at its release) on its individual track channel (`global.channels.base + t`, only while `t < span`, manual 1-89: "the first six channels directly control each of the six internal tracks"). White keys C D E F G A B C D from C-3 (MIDI 48), Z / X the octave (−3..+3), C / V the velocity (20 40 60 80 100 127, from 100: the machine uses it through ASSIGN › VEL). Each key its own note, so legato and the MM's own note priority work as on a keyboard. While LIVE RECORDING the firmware records them (the notes come in on the track's channel) |
| MIDI tracks | **not played** (said once): the MIDI sequencer's notes go to the MIDI OUT only, and an incoming note on a MIDI track's channel would play a synth track that shares it. See "Proposals" |
| Space / Alt+Space | play-stop / LIVE RECORDING (record, Alt + play); the REC key stays the toggle (stopped: GRID, playing: LIVE) |
| R / Alt+R | randomise the selected track / all: **phase 2** (GEN, MUTATE). Not bound in phase 1 (the old R = record is gone) |
| M / Alt+M | mute / unmute the selected track; Alt: all 12 (none audible: unmute all). MIDI tracks only where the engine can mute them (`midiMutes`) |
| T | tap tempo (`HOST.tempo`) |
| ↑ / ↓ | the previous / next track of the side shown (a focused value keeps them) |
| Alt+Delete | clear the whole pattern |
| Alt+← / → | Sequence: rotate the selected track (steps, slides, locks; swing stays on its steps), presses while Alt is down one undo step |
| 0 | unmute and unsolo every track (also the rail's M/S OFF key) |
| ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V | as before; no other ⇧ / ⌘ commands |
| ? | the list of keys, generated from the map (new on the MM) |
| removed | R (record), L (LEARN: the LEARN key stays; Esc leaves it), the old hard-wired handler |

The MD's `Keys` dispatcher is copied **verbatim** into the mockup (`src/56-keys.js`, a `KEYS` block that
`modal_check.py` keeps equal to `mdDeskKeys.js`, as MODAL / BOOT / SYX). The MM's own `drawKeys` and groups.
`mmKeysTest.js` (ctest `mmKeysTest`) is the MD's test on the MM's scripts: the rules, no two keys on one chord,
the approved keys bound, the removed ones gone, no hint naming them.

## c) Small comforts

| Feature | Applies | How on the MM | Risk |
|---|---|---|---|
| Lock counter in the lane header | yes | `n / 62 locked parameters` on the lane's title line, warn from 52, red at 62 (the LCD meter's thresholds) | none |
| Rotate | yes | Alt+←/→ (above) | none |
| Every-N fill | yes | ⌘-click in the roll: every 2nd step from there to the end gets a note at the clicked pitch (⌘⇧: every 4th); from a step with a note they go off (with their locks). One gesture | the roll is pitch-based: an off step that is a NOTE OFF is left alone |
| Wheel on a step edits its lock | yes, **on the lock lane** | the wheel over a lock-lane step with a trig moves its lock in the lane's parameter (from the kit value when none), 4 a notch, ⇧ 1; a run on one step is one undo step. The roll's wheel stays the pitch scroll (on the MD the grid step is the place; on the MM the roll's wheel already means pitch range) | none |
| Shift-drag ramp | yes | in the lock lane: a straight line from the press to the release over the steps with a trig, shown as it moves, one gesture | 62-lock budget: the ramp stops where `setLock` refuses (said) |
| Double pattern | yes | LCD line 2: `LEN ×2` beside LEN: length ×2 (≤ 64), the new half a copy of every track's steps, slides, swing and locks | none |
| Paste to several marked tracks | yes | ⇧-click track headers marks them (on the side shown; ⇧-click M stays "prepare mute"), ⌘V pastes the copied page into each (synth to synth, MIDI to MIDI), one gesture; Esc or a plain header click unmarks. `S.marks` (the MM's `S.multi` is MULTI TRIG) | none |
| Unmute / unsolo all | yes | `0` and the rail's M/S OFF key | MIDI mutes only where the engine has them |

## d) GEN and MUTATE (phase 2)

- **Rhythm** (as the MD): the GEN bar under the roll and its rows, merged with the PAGE control; MODE
  EUCLID / RANDOM / KEEP, R and ↺ key caps, Alt = all tracks of the side, one gesture per run (one undo step),
  STEPS default min(16, LEN) capped, the repeats summary. `mdDeskGen.js`'s pure functions are shared as a
  `GEN` block (modal_check, like MODAL). New steps get the track's last note (`lastNote`), so a rhythm on a
  bass keeps its pitch; removed steps lose their locks. Roles per machine (§4.2 analogue): DPRO-BBOX drums,
  bass (SWAVE-SAW, SID with low notes), lead, pad (ENS), FX tracks KEEP (one trig opens an FX machine), MIDI
  tracks a plain default.
- **Notes (proposal, minimal and musical):** a third GEN group NOTES on synth and MIDI tracks: ROOT (the
  track's KEY when its transpose SCALE is MAJ / MIN, else C), SCALE (MAJ, MIN, PENT, DORIAN; default the
  track's SCALE or MIN PENT), RANGE (octaves 1-2 from the root, at the roll's current octave), MOTION (STEP:
  a seeded random walk of ±1-2 scale degrees, the musical default for bass and lead; LEAP: any degree), and the
  same SEED. Applied to the **hits only**, after the rhythm: the rhythm decides when, notes decide what. BBOX
  tracks: notes pick drums from a small kit set (BD SD CH OH) instead of a scale. Pure function
  `genNotes(spec, hits, seed) -> [[s, note]]`, node-tested like the rhythm (same seed same notes; every note in
  the scale and the range; STEP never jumps more than 2 degrees). **Implement rhythm first; notes in the same
  phase only if this spec is accepted** (it is clear enough to build).
- **MUTATE:** live on Sound, one R key, Alt = kit, scopes SYN / AMP / FLT / EFX / LFO and the group chips;
  a page function on the working kit + `soundEdited()` (one `set workingKit` per change, the core diffs it into
  CCs and NRPNs). VOL, the MIDI page and FX machines' INP protected.

## e) Sound by function (phase 3)

Sections on rules (no cards), per-machine groups from the MM manual (Appendix A: e.g. SWAVE-SAW "Unison" UNIL
UNIW UNIX, "Sub" SUBX SUB1 SUB2, "Pitch" TUNE; SID "Pulse" PW PWAD PWRS, "Mod" MOD MSRC MFRQ; FM+ per operator;
VO-6 "Voice" / "Consonant"), the fixed pages as their own groups (AMP: envelope + output; FILTER: the two
filters + envelope; EFX: EQ, SRR, delay), a small plot per group where it means something (envelopes, filter,
LFO shapes, the arp), strict row alignment and equal plot heights, fitted to 1440 × 900 and 1280 × 760. A layout
table per machine, data only, checked by a node test that every SYN parameter of every machine is in one group.

## f) Library and Song (phase 4)

- **Kit auto-load on click** with the unsaved question: the MM core's `loadKit` ask exists (`mmDeskMachine.cpp`)
  but has no alternatives yet: add "Save and load" (`saveKit`, then `loadKit` with force) as the MD did
  (`deskCore::AskAlternative`, shared). The page shows the alternatives (the mockup's `ask`).
- **Garbage name → EMPTY:** measure first: read a fresh MM's unwritten kit slots on the emulator
  (`mmDeskFirmwareTest`) as the MD's K17–K64 were; port `kitNameText` only if they hold non-text bytes.
- **Song / chain:** the MD's change is the machine's own pattern **chain** (keys pressed on the panel) played at
  once. The MM has song mode (24 songs, LOAD SONG while stopped) and no MD-style chain mode: nothing to port
  unless phase 4's check of the MM manual (1-64, SONG MODE) finds an equivalent.

## Proposals for the owner

1. **Black keys on the MM: yes, later, on W E — T Y U — O P.** The MM is melodic and the home row's white keys
   cannot play a chromatic line; the black keys above A S D F G H J K L are the standard DAW layout. Cost: T is
   tap tempo (it would move, e.g. to Alt+T, which breaks "Alt is all", or to a key off the piano rows like `B`),
   and the keys test's rule "a plain letter off the piano row acts on the selected track" would name the upper
   row as piano too. R and E stay apart (no black key between E and F). Phase 1 keeps the MD's map (white keys)
   for consistency; the switch is a one-line `KEYS_BLACK` table and the test's WANT list.
2. **Playing MIDI tracks from the keys:** two candidates, both need a firmware probe: (a) SET STATUS 0x21 (MIDI
   sequencer focus) + 0x23 (focus track) and the AUTO TRACK channel, which the manual says is "distributed to the
   active track"; (b) a plug-in op that sends the note to the plug-in's own MIDI out on the MIDI track's channel
   (what the machine's keyboard does). (a) also records into the MIDI track while live recording.
3. **Wheel on the roll's notes** editing the lane's lock (as the MD's grid), with the plain wheel staying the pitch
   scroll elsewhere in the roll: only if the lane wheel is not enough.

## Phase 1: built (2026-10-01)

- a) `placeK` and `menuOpen()` in `60-ui.js` (`busyNow()` holds the core's documents while a menu is open, the
  page-follow render waits too); `selectstart` / `dragstart` guards in `130-main.js`, `user-select` in `20-mm.css`;
  Alt state, labels, `clearPattern`, `clearTrackLocks` in `75-comforts.js`.
- b) `56-keys.js` (the MD's dispatcher as the shared `KEYS` block, the ? list, `#keyspop`); the old hard-wired
  handler replaced by `Keys.bind` entries (`130-main.js`, `75-comforts.js`, the library's rows in `125-lib.js`;
  the pattern chooser's ⇧Enter went, as on the MD); the home row through the new host call
  `noteKey(t, note, vel)` (`mmAdapter.js`: note on / off on `base + t`, the off on the on's channel);
  `record(live)` for Alt+Space. R / Alt+R wait for phase 2.
- c) all eight comforts in `75-comforts.js`, hooks in `70-seq.js` (roll ⌘-click, lane ramp, header marks, M/S
  OFF, the lock budget) and `60-ui.js` (LEN ×2).
- Tests: `mmKeysTest.js` (ctest `mmKeysTest`), `modal_check.py` (the KEYS block), `sync-mmstudio-skin.py` (its
  LEARN gate needle follows the map). Files: `doc/modern-ux/mm-mockup/src/*` (`56-keys.js`, `75-comforts.js`
  new; `build.sh`), the generated `skins/mmStudio/mmMockup.js` / `mmStudio.css` / `mmStudio.html`,
  `mmAdapter.js`, `mmKeysTest.js`, `mdDeskKeys.js` (block markers only), the CMake registration.

## Phase 2: built (2026-10-01)

- **One source for the generators.** `skins/mdStudio/mdDeskGen.js` holds a `GEN BEGIN` / `GEN END` block: the
  functions without a machine in them (hash32, mulberry32, genU, genSeed, euclid, generate, genFit, genRepeats,
  genSummary, genTag, genRunFor, the comforts' step arithmetic, and the new `mutPull`, `GEN_SCALES`, `genScale`,
  `genNotes`). The MD's roles and its 24-value `mutate` sit after the block (`mutate` now calls `mutPull`: the same
  formula, mdDeskGenTest unchanged). The MM page includes the block as it is: `build.sh` cuts it out of
  `mdDeskGen.js` with `sed`, `sync-mmstudio-skin.py` does the same (its part list reads `skins/mdStudio/mdDeskGen.js`
  from build.sh), so there is no copy to keep equal; `--check` reports a skin that has not followed an MD change,
  and `mmGenTest.js` checks that the page's block is `mdDeskGen.js`'s text.
- **The MM's own (`src/52-gen.js`, pure):** the roles (`MM_GEN_ROLES`: FX machines and GND-GND keep, DPRO-BBOX
  drums, SWAVE-ENS / DPRO-DENS pad, SWAVE-SAW / PULS bass, VO-6 voice, SID / FM+ / DPRO-WAVE / DDRW lead, a lead that
  plays below C-3 a bass, the rest other, MIDI tracks their own), the defaults (bass E 5/16 STEP ×1; lead random 30 %
  STEP ×2; pad E 2/16 STEP ×1; voice E 4/16 notes off; drum box E 8/16 KIT; other random 15 % notes off; MIDI E 4/16
  rhythm only), `mmGenSteps` (a track's MM steps from a spec: hits on, other trigs off with their locks and slides,
  NOTE OFFs kept, a kept trig keeps its step, a new one takes the generated note or the last trig's notes, a chord
  stays a chord), `mmMutate` (the seven DATA pages by scope SYN AMP FLT EFX LFO, each knob in its own range).
- **NOTES as built:** MOTION OFF / STEP / LEAP (a drum box: OFF / KIT), ROOT (a note: the track's KEY when its
  transpose SCALE is MAJ / MIN, else C, the one nearest the track's lowest note, or the role's octave), SCALE (MAJ,
  MIN, PENT = minor pentatonic, DORIAN; default the track's MAJ / MIN, else PENT), RANGE 1-2 octaves, the spec's SEED
  (EUCLID shows it in NOTES). STEP starts on the root and walks ±1-2 degrees, mirrored at the range's ends. The notes
  write the hits the rhythm writes: every hit in EUCLID and REPLACE, the new ones in ADD, none in THIN. MIDI tracks:
  rhythm only (no NOTES group).
- **The page (`src/76-gen.js`):** the GEN bar in place of the old foot row (MODE, EUCLID / RANDOM + WRITE, NOTES, the
  summary with repeats, R and ↺ caps, the step legend's ? key, PAGE), the rail's spec tags (from 1380 px), the MUTATE
  bar over the Sound pages (AMOUNT, SCOPE, R). One run = one gesture: `commit()` waits while a run or trial with
  changes is live in its context (`genHeld`, as rotate's `rotHold`); another context, any other edit
  (`structEdited` / `soundEdited` not from GEN), undo or redo ends it (`genEnd`: the host's `edited("commit")`, or the
  mockup's own snapshot). While that commit runs the page counts as busy, so the adapter does not apply a core
  document under the edit that ended the run. MUTATE keeps VOL and TUNE by default, never moves INP, an LFO's PAGE /
  DEST or the MIDI page. R / Alt+R are bound (`mmKeysTest` expects them).
- Tests: `mmGenTest.js` (ctest `mmGenTest`, new), `mmKeysTest.js` (R / Alt+R), `mdDeskGenTest.js` unchanged. The
  firmware check is the document path's (`mmDeskFirmwareTest smoke`, `p4`): GEN adds no op and no core code.

## Phase 3: built (2026-10-01): e) Sound by function

- **The table, data only (`src/85-sound-groups.js`):** `MM_SYN_TAB` per machine from Appendix A (SWAVE-SAW Unison
  UNIL UNIW UNIX, Sub SUBX SUB1 SUB2, Pitch; SWAVE-PULS Unison, Sub, Pulse; SID Oscillator, Pulse, Modulation,
  Pitch; SWAVE-ENS / DPRO-DENS Chord, Wave, Chorus, Pitch; DPRO-WAVE Wave, Sync, Pitch; DPRO-DDRW Waves, Bits,
  Pitch (WID TUNE); DPRO-BBOX Drum, Retrig; FM+STAT Modulator 1, Modulator 2, Tone · pitch; FM+PAR Block 1-3,
  Tone · pitch; FM+DYN Modulator 1, Modulator 2, Pitch; VO-6 Vowel, Consonant, Pitch; GND-NOIS Noise, Pitch;
  GND-SIN Pitch; FX-THRU Input; FX-REVERB Reverb, Filter, Input · mix; FX-CHORUS Chorus, Tone · width, Input · mix;
  FX-DYNAMIX Curve, Timing, Input · mix; FX-RINGMOD Carrier, Input · mix; FX-PHASER / FLANGER Sweep, Width,
  Input · mix) and `MM_FIXED_TAB` (AMP Envelope, Drive, Level · pan, Glide; FILTER Filter, Filter env; EFFECTS
  EQ, Sample rate, Delay, Delay filter). `mmGroupCheck(m)`: every knob of every page exactly once.
- **The page (`src/90-sound.js`):** one grid; three rows (source: SYN + AMP; tone: FILTER + EFFECTS; the three
  LFOs), each row three of the grid's lines through subgrid (titles, screens, boxes), so the titles, screens and
  box rows are level and every screen is as tall (`minmax(0, --plotmax)`, shared). Two plot-less groups share a
  column (the upper one's boxes at the screens' top, the lower one's title at their foot), a lone one says what
  its knobs do. 27 small editors keyed by the group's page and knobs (`data-pg`, `data-k`), each with its help
  as a tooltip. The head line holds the machine key, the MUTATE bar and three keys to the Sequence dock (Arp,
  Transpose, Trig setup / MIDI set): the pattern's arpeggiator and transpose and the kit's trig setup left the
  Sound page (they were its fourth row and did not fit) and stay in the Sequence dock.
- **MUTATE by group:** a group's title is a scope chip (`SYN:unison`, `AMP:envelope`, `LF2:lfo`); `mmMutate`'s
  `spec.extra` adds those knobs by the same rules (VOL TUNE INP PAGE DEST stay, each knob once).
- **Measured** (headless Chrome, every machine on track 1): titles, screens, stacks and box rows level in each
  row, equal screens across rows, every knob once, no clipped box or title, no scroll: 1440 × 900 screens 137 px,
  1280 × 760 90 px, 1920 × 1080 195 px. Test: `mmSoundTest.js` (ctest `mmSoundTest`).
- Not this phase: the black keys (owner's decision pending). Pre-existing, not Sound: the top bar is 15 px wider
  than a 1280 px window (the page scrolls sideways a little on every workspace).

## Phase 4: built (2026-10-01): f) library and song

- **Kit click = load:** a click on another slot loads it (Alt / ⌘-click selects only, the current kit is only
  selected, double-click renames unless a question is open); arrows only move, Enter loads.
- **The question:** the MM core's `loadKit` ask offers "Save and load" (`saveKit` to the current slot, then
  `loadKit` with force) and says "Load without saving"; `mmAdapter.js` shows the alternatives as the MD page does;
  the schema's ask has `alternatives`. Tests: `mmDeskTest` (the alternative; a reload has none),
  `mmDeskFirmwareTest library` (on the firmware: asked, nothing done before the answer, Save and load plays the
  other kit clean and the edit is read back from the slot).
- **Never-written kit names, measured** (`mmDeskFirmwareTest smoke`): a fresh machine holds 64 named kits and 64
  unused slots (K65-K128) whose first name byte is 0xff followed by RAM bytes (e.g. `ff 00 41 56`); no slot holds
  other non-text bytes. `mmConvert.kitName` / `kitEmpty` already read the 0xff mark, so they show EMPTY: no
  `kitNameText` port (mmConvertTest checks it).
- **Song:** the MD's "More" (the row's rarer settings behind one key) ported: per-track transpose, part and mutes.
  **Chain not ported:** the MM has the MD's pattern chaining (manual 1-46: hold BANK, press TRIG keys, one bank,
  each pattern once, STOP twice or a new pattern ends it), so the MD's CHAIN mode would apply, but it needs core
  work first: the MM's BANK + TRIG panel chords in `mmDesk` (its `Key` set has TRIG 9-14 only), the chain state
  read from the MM's RAM (a firmware probe, as the MD's `machine.desk.chain`), and `capabilities.chains` over MIDI.
- **Chain ported (MM-P8).** Measured (`mmEditorProbeFirmwareTest chain`): BANK + TRIG uses the MM's own BANK
  packets (row 0x24) and the TRIG rows; the chain is at RAM 0x2bc2c4 (active, next, length, list, u32 BE),
  BANK GROUP at 0x70000b. Both gestures make a chain (TRIGs held together, or the manual's first held); a
  new chain replaces the one that plays from the pattern end; a pick (BANK + one TRIG) or STOP twice ends
  it, a SysEx LOAD PATTERN does not (it plays once, the chain goes on: the MD differs); in song mode the
  chain is kept but the song plays. Built: `chain` / `chainClear` in `MmMachine` (latest wins while keys
  are on their way, pattern mode first, BANK GROUP when needed, `breakChain` before a pick, which ends the
  chain with its own keys), `machine.desk`, `capabilities.chains` (false over HW MIDI: no message reaches
  the keys), the Song palette's ARRANGE | CHAIN with the Plays line, BACK, CLEAR and the header chip.
  Tests: `mmDeskTest` (pattern chaining), `mmDeskFirmwareTest chain`.
