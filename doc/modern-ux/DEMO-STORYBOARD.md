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
