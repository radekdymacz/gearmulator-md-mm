# Demo storyboard: one song through the whole Machinedrum Editor

The plan for one recorded take, played by a demo journey ([DEMO-VIDEOS.md](DEMO-VIDEOS.md)), that shows every page of
the Machinedrum Editor while making one song. Reels are cut from the same take. Nothing here is recorded yet.

Sources: the page code (`skins/mdStudio/*.js`, the key map), [JOURNEYS.md](JOURNEYS.md) and
[v0.3.0](../release/v0.3.0.md). "Verify" marks a control whose behaviour has not been driven by a journey yet.

## 1. Feature inventory

**Show**: played in the song. **Flash**: on screen for a bar or less. **Skip**: not in the song, with the reason.

| Area | Feature | In the song |
|---|---|---|
| Top bar | PLAY / STOP, POSITION | show (bar 1, bar 72) |
| | Live recording: REC or Alt+Space, notes from the QWERTY keys | show (bars 7-8) |
| | Tap tempo T / B | flash (bar 13): four taps 500 ms apart; the take measured 119.1 BPM |
| | Tempo drag on the LCD | skip: the song keeps one tempo |
| | Pattern ‹ ›; a pattern picked while playing is queued | show (bar 24: A to B) |
| | LCD values LEN, SPD, SWG, ACC | flash (bar 12: SWG 50 to 58 %). Verify that SWG drags |
| | UNDO / REDO, ⌘Z ⌘⇧Z | show (bar 17, after MUTATE) |
| | Plate MK1 / MK2 | skip: looks only. A reel can show it |
| | ? list of keys | skip: covers the page. A reel can show it |
| | GLOBAL (TEMPO OUT, ROUTING), AUDIO / MIDI panel | skip: setup, not music |
| | Engine menu: HW MIDI, LOAD ROM | skip: needs hardware or replaces the ROM |
| | MIDI mapping (Control, LEARN) | skip: hidden by design |
| | SysEx import / export | skip: native file chooser |
| Sequence | Trigs by click; drag-paint steps | show (bars 5-6) |
| | Accent (⇧-click), slide (⌥-click) | show (bar 6) |
| | Every 2nd / every 4th fill (⌘-click) | show (bar 5) |
| | Copy / paste / clear steps; paste to many marked tracks | flash (bar 23) |
| | Clear the whole pattern (⌥Delete) | skip: wipes the song |
| | Rotate (⌥← →) | show (bar 11) |
| | LOCK PARAMETER lane: draw, ⇧-drag ramp, ⌥-drag erase, wheel | show (bars 9-10) |
| | Pages ALL / PAGE, [ ] | skip: patterns are 16 steps |
| | Track select ↑ ↓, M mutes, ⌥M all, 0 unmutes all | show (bars 3, 57-69) |
| | QWERTY keyboard plays the selected track | show (bars 7-8) |
| | Transpose keyboard | skip: Monomachine only |
| GEN | Per-track generator (Euclid / Random / Keep), Hits, Steps, Rotate, Accent, ×2 | show (bars 1-4) |
| | Defaults, R rolls the track, ⌥R every track; one undo step per run | show (bars 1-4); ⌥R skipped (replaces the whole beat) |
| Sound | Groups by function, values by drag, ↑ ↓ on a focused value | show (bars 13-14) |
| | Screens (curve editors): drag a handle | show (bar 14) |
| | Machine picker | flash (bar 13, the perc track) |
| | Copy / paste a sound | skip: no musical need. A reel can show it |
| | MUTATE: R, amount, scope (SYN / FX / RTG), undo | show (bars 15-18) |
| | Control All (⌥-drag), one undo step | show (bars 37-40: the riser) |
| Mix | Mute / solo, drag across M or S keys; un-solo keeps the mutes | show (bars 25, 45-48) |
| | ⇧-armed mutes, applied on ⇧ up | show (bars 37-41: the drop) |
| | Volume faders, PAN, DIST, DEL / REV sends, OUT routing | show (bars 19-20: PAN and DEL send) |
| | Master effects: rhythm echo, gate box, master EQ, Dynamix | show (bars 20-21: an echo throw) |
| | M/S off | flash (bar 72) |
| Sampler | Set up sampling card (recorder, then player, "Once, on step 1") | show (bars 26-27) |
| | Source (main mix, inputs), LEN in steps and bars, RATE | show (bar 27) |
| | Capture next loop, then Live / Freeze | show (bars 28-30) |
| | RAM view: record row, chop row (slice drag, ⌥ reverse, ⇧ retrig) | show (bars 31-34) |
| | Waveform tiles for the 48 ROM slots; audition | flash (bar 35) |
| | Load a WAV / AIFF, Rename a slot | skip: native file chooser, and it writes the owner's ROM slots |
| Song | Chain: bank, pads, Clear | show (bars 53-72) |
| | Arrange rows, row inspector (repeat, mutes), drag pads and rows | flash (bar 53) |
| Library | Kit library: a click loads at once | show (bar 0) |
| | Save as, copy / paste, rename, clear a kit slot | skip: library chores. A reel can show them |
| | Pattern chooser: switch, copy / paste a pattern slot | show (bars 22-24: A copied to B) |

## 2. The song: 72 bars at 120 BPM (2.0 s a bar, 2:24)

Implemented as `demo-md-full` (mdDeskJourneys.js). One take, 16-step patterns (one bar): two empty slots side by
side (A, B) on the kit of the machine's fullest pattern (here K01 TRX UW: 1 kick, 2 snare, 3-5 toms, 6 clap, 7 rim,
8 cowbell, 9 closed hat, 10 open hat, 11-12 cymbals); the demo finds the tracks by machine. 13 and 14 become the RAM
recorder and player. A bar clock counts the machine's steps (telemetry); each action starts in time to land on the
"1" of its bar. When the work before it overruns, the action waits for the next bar line, never lands mid-bar, and
every later bar moves by the same amount, so the bar numbers below are nominal. The take logs each real bar line
and pattern change; the run folder's `timeline.json` has them. Bar n starts at about 2(n−1) s.

| Bars | Section | Music | Page | Action | Caption |
|---|---|---|---|---|---|
| 0 | count-in | silence | Sequence | Kit library: click a kit (loads at once); pattern chooser: an empty slot A | Start from nothing. |
| 1 | intro | kick 4/16 | Sequence | PLAY; kick track, GEN Defaults: Euclid 4/16 | GEN writes the kick. |
| 2 | | + snare backbeat | Sequence | snare track, GEN Euclid 2/16, rotate 4 | |
| 3-4 | | + hats | Sequence | hat track (↓), GEN Random, R once on bar 4 | Roll the hats until they swing. |
| 5-6 | build | + perc by hand | Sequence | click 3 perc steps, ⌘-click every 4th; ⇧-click accents, ⌥-click a slide | Then play it by hand. |
| 7-8 | | tom fill, recorded live | Sequence | ⌥Space REC; play A S D F on the QWERTY keys; REC off on 9 | Record a fill from the keyboard. |
| 9-10 | | hat decay opens up | Sequence | LOCK PARAMETER DEC on the hat, ⇧-drag a ramp across the lane | Lock any parameter per step. |
| 11 | | hats shift | Sequence | ⌥→ twice: rotate the hats | |
| 12 | | swing in | Sequence | SWG 50 to 58 %; tap T four times on the beat | |
| 13-14 | | snare gets body | Sound | snare: drag the filter screen; perc: machine picker flash | Every sound, grouped by what it does. |
| 15-18 | | perc mutates | Sound | MUTATE R on perc (15), ⌘Z on 17 (back), R again on 18 and keep | MUTATE a sound. Undo if you don't like it. |
| 19-21 | | echo throw | Mix | PAN the clap; DEL send up on the snare for bar 20; rhythm echo feedback; send back on 21 | |
| 22-24 | | variation queued | Library | pattern chooser: ⌥-click A, Copy, ⌥-click B, Paste; pattern › on 24: B queued, plays from 25 | Copy the pattern. Queue the next one. |
| 25 | break | beat drops out | Mix | one drag across tracks 1-5's M keys, last key on the bar line | One drag: the beat drops out. |
| 26-27 | | perc and cymbals only | Sampler | RAM 1: Set up sampling (recorder 13, player 14, once on step 1); Main mix; LEN 1 bar | Sample the groove into RAM. |
| 28-30 | | capture | Sampler | Capture next loop on 28: records 29, frozen on 30; the take's waveform | |
| 31-34 | | chops enter | Sampler | chop row: 8 trigs, drag 4 slices, ⌥ reverse 2, ⇧ retrig the last 2 | Chop it: slices, reverse, retrig. |
| 35-36 | | ROM tiles | Sampler | waveform tiles flash; audition one ROM slot on the beat | |
| 37-40 | riser | filter sweep up | Sound, then Sequence | ⌥-drag FLTF up over 3 bars (every track); on 40 ⇧-click tracks 1-5's M keys on the rail | Sweep the whole kit. Arm the drop. |
| 41 | drop | everything back | Sequence | let ⇧ go on the bar line; ⌘Z undoes the sweep in the same bar | Drop it on the bar. |
| 42-44 | | full groove + chops | Sequence | none: the grid plays | |
| 45-48 | | snare and chops alone | Mix | drag across S keys of snare and player on 45; un-solo on 47 (mutes kept) | Solo with a drag. |
| 49-52 | | variation B | Sequence | GEN R on the open hat (49), rotate on 51 | |
| 52-56 | outro | stop, then A, B, A, B | Song | Arrange flash, Chain; STOP on the bar line; load the chain (pads A, B: one bank); PLAY starts it at A; the demo checks the machine plays A, B, A, B | Load a chain and start it. |
| 57-69 | | strip back | Sequence | M on a track every 4 bars: perc (57), chops (61), hats (65), kick last (69) | |
| 70-72 | | everything back, then silence | Sequence | ⇧-click the muted tracks, let ⇧ go on the bar line (M/S off would unmute the frozen recorder and record again); STOP on a bar line; end card over the last 3.5 s (name and line; the URL is off until launch) | |

Found while building it:

- **Chain.** A chain is one bank's: pads from two banks start the draft over, so the chain never formed. A and B
  are adjacent slots of one bank.
- **Copy a pattern slot.** ⌥-click in the pattern chooser switched the pattern; the arrows select without switching.
- **Reverse on slice 1.** ⌥-click on a chop whose slice starts at 0 does nothing (END cannot go below 0). The song
  reverses moved slices only. The editor could say why, or reverse within the slice.

## 3. Cut-downs from the same take

| Reel | Bars | Length | Content |
|---|---|---|---|
| Beat from scratch | 0-8 | 18 s | empty pattern to a full beat: GEN, hand edits, live fill |
| GEN | 1-4 (+49) | 10-15 s | kick, snare and hats rolled by GEN |
| Shape and mutate | 9-18 | 20 s | lock ramp, rotate, swing, Sound screens, MUTATE and undo |
| Sampler glitch | 26-34 | 18 s | set up sampling, capture, chop, reverse, retrig |
| Mute drop | 25 + 37-44 | 24 s | the break, the riser, the drop on the bar |
| Chain outro | 53-72 | 30 s | chain, strip back, STOP, end card |

Each reel opens on a full-groove bar (a 1-bar cold open from bars 41-42) when its own first bar is quiet.

## 4. Monomachine: the equivalent take (later)

A bass-and-lead song: an empty pattern, notes painted in the piano roll (drag to paint, ⌥-drag erases), QWERTY with
black keys W E T Y U O P played live into RECORD; GEN and MUTATE on the synth tracks; the lock lane; the arpeggiator
dock; the clickable transpose keyboard on the bar lines; Perform (POLY, MULTI TRIG and MULTI MAP, the joystick and
its assign rows, PORTAMENTO); Mix channel strips (LEVEL, PAN, DIST, DSND) with mute and solo drags and the MIDI track
mutes; the Song page's picker and Load; a chain into the outro. No sampler. HW MIDI and SYSEX RECV stay out of it,
for the same reasons as on the Machinedrum.

## 5. Second song: "Tight Sequencer" (Machinedrum techniques)

From the technique research (2026-10-07: Elektronauts threads, the Know How quick start, reviews, Sean Booth calling the
MD his "tightest sequencer"). Broken IDM electro at 120 BPM: a tuned kick, ricochet hats, metal percussion, a sine bass,
wet echo, the beat resampled and re-cut. Implemented as `demo-md-techniques`; same bar clock and rules as
`demo-md-full` (actions on bar lines; a late one waits for the next bar line, later bars move with it; `timeline.json`
has the real bars). Kit: the machine's fullest pattern's kit (K01 TRX UW: 1 kick, 2 snare, 6 clap, 7 rim, 8 cowbell, 9
closed hat, 10 open hat, 11-12 cymbals, 13-16 ROM); tracks found by machine.

What the editor and the firmware really give, checked before relying on it:

- **Retrig** is a parameter of the E12, ROM and RAM-P machines only (TRX-SD has none). The roll is an E12-SD put on
  track 15 with the machine picker, RTRG and RTIM set in its Sound page's Retrig group, played on the last beat.
- **Bass**: GND-SIN on track 12 (a cymbal) with the picker, tuned and given a long decay on its Sound page.
- **LFO**: the Sound page's LFO section on the rim: target (track and parameter selects), shape keys, SPD, DEPTH.
- **Control All**: Alt-drag, never a CTR-AL track (a kit holding CTR-AL cannot be edited yet).
- **Machine changes** are kit edits too; they survive pattern edits (probed).
- **Unsaved kit values** were lost on the machine at the next pattern edit until `b0fa65625` (found by these demos,
  `mdDeskFirmwareTest keepedits`).
- **Sampler**: the RAM-P machine keeps the values of the machine it replaces (PTCH, DEC, STRT…); the demo sets the
  player's PTCH 64, HOLD and DEC 127. Chops are heard by soloing the player first (the demo-md-sampler lesson).

| Bars | Section | You hear | Technique | Page + action | Caption |
|---|---|---|---|---|---|
| 0 | count-in | silence | kit as the start | Sequence: an empty pattern on the kit | Start from an empty pattern. |
| 1-2 | intro | kick, then a late snare | GEN Euclid, rotate | GEN Defaults on the kick; the snare's GEN, ⌥→ | Euclid kick, rotated snare. |
| 3-4 | intro | a sine bass on the 1, ricochet hats | machine choice, GEN Random | picker: GND-SIN on 12, PTCH and DEC; hats GEN Random, R | Pick the machine for the job. |
| 5-6 | build | rim and cowbell by hand, an accent, a slide | accent, slide | clicks; ⇧-click accent; ⌥-click slide | Accent and slide by hand. |
| 7-8 | build | a tom fill played live | live recording | ⌥Space; A S D F; ⌥Space | Record the fill live. |
| 9-10 | build | the hat's decay opens up | parameter locks | LOCK lane DEC on the hat, ⇧-drag ramp | Lock decay to every step. |
| 11 | build | hats shift, swing leans in | rotate, swing | ⌥→ ×2; SWG 50 → 58 | Rotate. Add swing. |
| 12-13 | build | a buzzing roll on the last beat | retrig | picker: E12-SD on 15; RTRG, RTIM; a trig on step 13 | Retrig makes the roll. |
| 14-15 | build | the rim's decay wobbles | LFO | LFO target T7 DEC, shape ramp, SPD, DEPTH | An LFO moves the decay. |
| 16-18 | build | the cowbell changes colour, back, again | MUTATE, undo | MUTATE R; ⌘Z on 17; R on 18 | Mutate it. Undo it. |
| 19-21 | dub | clap panned, one snare throw into the echo | sends, master echo | Mix: PAN; DEL up on 20, echo FB; DEL down on 21 | One hit into the echo. |
| 22-24 | variation | B queued | pattern copy, queue | chooser: copy A to B; › on 24 | Copy it. Queue the next pattern. |
| 25 | break | the beat drops dead | mute drag | Mix: one drag over M 1-5 | One drag drops the beat. |
| 26-30 | break | the full groove sampled first, then the break | resample | Sampler: Set up sampling, Main mix, LEN 1 bar, player PTCH 64 HOLD/DEC 127; Capture | Sample your own beat. |
| 31-34 | chops | the chops alone, re-cut | slicing, reverse, retrig | solo the player; chop row: trigs, slices, ⌥ reverse, ⇧ retrig | Chop it. Reverse. Retrig. |
| 35-36 | chops | ROM slots | ROM tiles | waveform tiles | 48 ROM sounds, one click. |
| 37-40 | riser | the whole kit filters up | Control All | ⌥-drag FLTF; ⇧-arm M 1-5 and un-solo | Sweep the whole kit. |
| 41 | drop | everything back on the bar | ⇧-armed mutes | ⇧ up on the bar line; ⌘Z the sweep | Release on the bar. |
| 42-48 | drop | full groove with the chops and the roll | solo drag | Mix: S snare + player on 45, off on 47 | Solo with a drag. |
| 49-52 | variation | the open hat rolled again | GEN, rotate | GEN R; ⌥→ | Roll it again. |
| 53-56 | outro | A, B, A, B | chain | Song: Chain; STOP on the bar line; pads A, B; PLAY | Chain the patterns. |
| 57-69 | outro | strip back | mutes | M every 2 bars: perc, chops, hats, kick | Strip it back. |
| 70-72 | outro | everything back, then stop | ⇧-armed return | ⇧-click, ⇧ up on the bar line; STOP; end card | End on the bar. |

Reels from this take, 30 s each, in priority order: Resample (26-34), Sweep and drop (37-41), Retrig roll (12-13 with
5-6), Lock anything (9-11), Euclid from nothing (0-4); then LFO (14-15), Mutate and undo (16-18), mute drop (25 +
45-48), chain and strip (53-72).

## Next (queued, not done)

- **Chop retrig:** lower the chop row's Shift-click retrig (RTRG 20 / RTIM 10 today) to a short roll (2-6 repeats) with RTIM on the
  grid at 120 BPM (1/16 or 1/32), on a few chosen steps only (bar-end fills); the same for the Alt-drag retrig section. The RTIM
  mapping is not known yet: `mdDeskFirmwareTest retrigmap` (WIP) heard no retrigs from kit values or locks in the test rig; an
  in-app probe (RAM-P chop soloed, RTRG/RTIM locks bar by bar, take in temp/probe) was recorded but not analysed.
- **Snare:** GEN Euclid 2/16 rotated 4 (steps 5 and 13, the backbeat) shown as the GEN move, then a ghost note by hand; check
  the Tight Sequencer snare against the same rule.
- **A producer-grade beat:** kick with syncopation (1, 7/8, 11, a pickup), snare on 5 and 13 with a ghost, hats with accent
  movement and an open hat on the off-beats, a percussion layer, a bass (GND sine) locked to the kick, swing 54-58 %. Write the
  16-step grid per track here first; prototype it in a dry run and analyse onsets and balance before recording.
- **FX throws on top:** delay throws (snare/rim at phrase ends), 2-3 reverb throws per song (a REV send lock on one hit into
  the next bar, moderate decay; caption one "Reverb throw"), distortion moves (DIST in Mix or SRR/DIST on snare or bass for a
  bar, then back); one at a time at phrase boundaries, tails and levels checked.
- **Chain proof:** make the chained patterns audibly different (E02 is a copy of E01 today), prove on the machine (read-back
  of the current pattern after each change, onsets of pattern-specific hits by audio) and check that the UI highlights the
  playing pattern; a bug if not (cause, fix, test).
- **Re-record** demo-md-full and the Tight Sequencer song (demo-md-techniques) with all of the above; three formats, the checks.
