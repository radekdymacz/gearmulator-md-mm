# Machinedrum manual vs our editor: feature map

Manual: Machinedrum SPS-1 / SPS-1UW MKII user's manual, OS 1.53, rev J (2010), 122 PDF pages; page numbers are the printed ones. Date: 2026-10-10. Code state: `main` at the time of writing.

The question per row: **does our web page (the Machinedrum Editor) offer this?** The page is the only UI: the shipped editor has no emulated front panel or LCD (`mdEditorPages.h`). The firmware runs underneath, so some functions work when the page, MIDI or SysEx asks, or on a real machine's panel via the HW MIDI engine. That is only noted in the last column.

Our UI column: ✅ Yes (own control) · 🟡 Partial · ❌ No · ➖ Not applicable (hardware only, such as power or the LEVEL meter's jacks).

Method: manual read in full; claims checked against the page files (`source/elektron/md/mdJucePlugin/skins/mdStudio/*.js`, `skins/shared/`), the command table `mdDesk/mdDeskModel.cpp`, `elektronData/` and `doc/modern-ux/keymap.json`. Machines are listed per machine; all of them have a group table in `mdDeskSoundGroups.js`. The older plan `manual-mapping.md` was only a guide.

## 1. Introduction, front panel and connectors (pp.1-8)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.4 | Master volume knob | Main and headphone level | ➖ | Host / DAW or system volume | Plug-in output level belongs to the host. |
| p.4 | SOUND SELECTION LEDs and wheel | Pick the active track, show trigs | ✅ | Track rail (left); track keys 1-16 via arrows Up/Down | Activity LEDs on Mix strips. |
| p.4 | FUNCTION + TRIG selects a track | Quick track select | ✅ | Click a track in the rail | No FUNCTION needed. |
| p.4 | CLASSIC/EXTENDED key | Switch editing mode | ✅ | LCD line 2: MODE (click) | mdDeskTop.js l2step 'mode'. |
| p.4 | FUNCTION key (secondary functions) | Modifier for everything | 🟡 | Alt key, or FN key in top bar | Alt = 'all tracks'; the machine's FUNCTION combos are mapped one by one below. |
| p.4 | BANK key and A/E, B/F, C/G, D/H keys | Pick a pattern bank and pattern | ✅ | Pattern chooser (click Pattern in the LCD; keys A-H) | mdDeskLibrary.js. |
| p.4 | ENTER/YES, EXIT/NO, arrow keys | Menu navigation | ➖ | Not needed | Every menu is a native control. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.5 | LEVEL knob (per-track overall gain) | LEV, cannot be locked or LFO'd | ❌ | None | The desk has a 'level' command, but no page control uses it. Use VOL on Mix instead. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.5 | TEMPO key | Open tempo screen | ✅ | LCD: Tempo (drag), T key to tap | See §9. |
| p.5 | DATA ENTRY knobs (A-H), pressed = coarse | Eight parameter knobs | ✅ | Value boxes on Sound and Mix; drag, wheel, Shift = fine | Wheel notch 1, Shift 10. |
| p.5 | SYNTHESIS/EFFECTS/ROUTING toggle | Switch knob page | ✅ | Sound page shows all three as groups | No page toggle; all pages in one view. |
| p.5 | RECORD key (grid record on/off) | Toggle grid edit | ➖ | Grid is always editable | The step grid needs no mode. |
| p.5 | RECORD + PLAY (live recording) | Live record | ✅ | REC button, Alt+Space | LCD top bar. |
| p.5 | PLAY (second press pauses) | Start / pause | 🟡 | Play/Stop button, Space | Play and stop only; no pause. |
| p.5 | STOP; double STOP silences all voices | Stop, panic | 🟡 | Play/Stop button | No panic key. Double STOP: the firmware supports it; only a real machine's panel can do it today. |
| p.5 | STOP + PLAY restarts from the top | Restart | 🟡 | Stop then Play | Same result in two clicks. |
| p.5 | PATTERN/SONG key | Switch sequencer mode | ✅ | Song page: PATTERN / SONG switch; LCD PLAY value | seqMode command. |
| p.5 | KIT key (kit menu) / FUNCTION + KIT (song menu) | Open kit / song menus | 🟡 | Kit: library popover. Song: song stepper on Song page | No song menu (see §10). |
| p.5 | TRIG keys 1-16 | Play tracks, place notes, pick patterns | ✅ | Step grid; piano keys A-L play the selected track | Right-click a step for its menu. |
| p.5 | SCALE SETUP key (page switch) | Pick pattern page | ✅ | Page key and page LEDs above the grid, [ and ] | ALL view shows every step. |
| p.5 | <PATTERN PAGE> LEDs; MKI LEDs | Page indicator | ✅ | Page LEDs (four) in Sequence | MKI 32-step LEDs: see §13. |
| p.6 | Power switch, DC input | Power | ➖ |  |  |
| p.6 | MIDI In / Out / Thru | MIDI ports | 🟡 | AUDIO/MIDI settings (standalone); HW MIDI engine; Global page | Thru has no meaning in software. |
| p.6 | Inputs A and B (audio in) | External audio | 🟡 | INP machines on Sound; audio input in settings | Input device choice in AUDIO/MIDI settings. |
| p.7 | Individual outputs A-F, main out, headphones | Output jacks | 🟡 | Mix: OUT key per track; Global: Routing; plug-in output buses | Output buses depend on the host. Headphones: N/A. |
| p.7 | Rack mount kit, connecting, care, battery | Hardware | ➖ |  |  |

## 2. LCD user interface and quick start (pp.9-12)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.9 | Tempo display with one decimal | Show BPM | ✅ | LCD: Tempo |  |
| p.9 | Four pattern-page boxes | Playback page | ✅ | Page LEDs; LCD Position |  |
| p.9 | Transport symbols | Record/play/pause/stop | 🟡 | REC and Play LEDs | No pause state. |
| p.9 | Kit name and number | Current kit | ✅ | LCD: Kit with saved/edited chip |  |
| p.9 | Synth, machine and page text | Focus info | ✅ | Sound: machine button, family; LCD line 2 |  |
| p.9 | Level bar (overall volume of the track) | LEV meter | ❌ | None | See LEVEL knob. |
| p.9 | Eight value boxes with 'clip' between dependent values | Parameter display | 🟡 | Value boxes | Dependent-pair clip not drawn. |
| p.9 | Windows over the main screen, icon help, EXIT closes | Layer edit | ➖ | Popovers close with Esc |  |
| p.10 | Load a kit and trig tracks (quick start) | Basic play | ✅ | Kit library; step grid; piano keys | See §4. |
| p.10 | Accelerated knob editing (pressed knob) | Coarse step | ✅ | Wheel with Shift = 10; Shift-drag = fine |  |
| p.11 | Hear a machine before loading it (EDIT KIT preview) | Preview default sound | ❌ | None | Machine picker loads the machine at once; Undo takes it back. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |

## 3. Sound synthesis: kits (pp.13-21)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.14 | Kit contents (16 machines, params, track FX, routing, master FX, name) | The kit document | ✅ | Sound, Mix, kit library | Whole kit is read and written as a dump. |
| p.14 | Load kit (64 slots) | LOAD KIT | ✅ | Kit library: click a slot (Enter) | Asks first over unsaved edits. |
| p.15 | Kits not linked to a pattern marked with a star | Unused-kit marker | ✅ | Kit library slot name ends with * | EXTENDED only. |
| p.15 | Save kit and choose slot | SAVE KIT | ✅ | Kit library: Save, Save as Knn | UNDO KIT keeps the overwritten kit. |
| p.15 | Name a kit (letters, high-score picker) | Kit name | ✅ | Kit library: Rename (F2 / double-click), 16 chars | Typing replaces the wheel. |
| p.16 | UNDO KIT (kit list entry 0) | Restore lost kit | 🟡 | Editor undo (Cmd+Z); Reload | The machine's UNDO KIT is kept but not listed as a slot. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.16 | Copy kit and paste to slot | Kit copy | ✅ | Kit library: Copy, Paste, drag onto slot (Cmd+C/V) |  |
| p.17 | Clear kit | CLEAR KIT | 🟡 | Kit library: Clear | Writes an empty kit (GND-EMPTY, neutral values); the machine's own clear may leave other values. |
| p.17 | Undo paste / clear kit | Undo | ✅ | Cmd+Z |  |
| p.17 | Kit assembly: pick MD-synth and machine per track | EDIT KIT | ✅ | Sound: machine button opens the picker by family | All 10 families. |
| p.18 | Load machine and keep track FX / routing | FUNCTION + ENTER | ✅ | Picker: 'Keep effects + routing' toggle (default on) | S.keepFx. |
| p.18 | Mute relation (mute group) | Track A mutes track B | ✅ | Sound: Relations > Mute | Shown as a note in the group box. |
| p.19 | Trig relation (layered drums, no chains) | Track A trigs track B | ✅ | Sound: Relations > Trig |  |
| p.19 | Copy machine | COPY MACHINE | ✅ | Sound: Cmd+C on a track | Copies model, values, level, LFO. |
| p.20 | Paste machine (many times, to other kits) | PASTE MACHINE | ✅ | Sound: Cmd+V | Clipboard stays. |
| p.20 | Clear machine / undo | CLEAR MACHINE | ✅ | Sound: clear sound; Cmd+Z | Track shows GND-EMPTY. |
| p.21 | Machine parameter editing (up to 8 knobs) | SYNTH page | ✅ | Sound: groups with canvases and boxes | Every knob sits in one group. |
| p.21 | Kit changes lost unless saved | Dirty warning | ✅ | Saved / edited chip on LCD kit field |  |

## 4. Track effects (pp.22-24)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.22 | AMD: amplitude modulation depth | Tremolo depth | ✅ | Sound > Amp mod |  |
| p.22 | AMF: amplitude modulation frequency | Tremolo rate | ✅ | Sound > Amp mod |  |
| p.23 | EQF: EQ centre frequency | 1-band EQ | ✅ | Sound > EQ |  |
| p.23 | EQG: EQ gain | Boost/cut | ✅ | Sound > EQ | Bipolar in the lock lane. |
| p.23 | FLTF: filter base frequency | 24 dB filter | ✅ | Sound > Filter |  |
| p.23 | FLTW: filter width | Gap between HP and LP | ✅ | Sound > Filter |  |
| p.24 | FLTQ: filter Q | Resonance | ✅ | Sound > Filter |  |
| p.24 | SRR: sample rate reducer | Sample and hold | ✅ | Sound > Sample rate |  |

## 5. Routing page (pp.25-27)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.25 | DIST: output distortion | After track FX | ✅ | Sound > Drive; Mix strip |  |
| p.25 | VOL: track volume (lockable) | Separate from LEV | ✅ | Mix fader; Sound > Level |  |
| p.26 | PAN (-64 to +63) | Stereo position | ✅ | Mix strip; Sound > Level pan | No effect on A-F outputs; Mix shows it. |
| p.26 | DEL: delay send | To Rhythm Echo | ✅ | Mix strip; Sound > Sends | Greyed with a note on direct outputs. |
| p.27 | REV: reverb send | To Gate Box | ✅ | Mix strip; Sound > Sends | Same. |
| p.27 | LFOS, LFOD, LFOM on the routing page | LFO speed, depth, mix | ✅ | Sound > LFO (SPD, DEPTH, SHMIX) | Same kit parameters. |

## 6. Stereo master effects (pp.28-31)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.28 | Master FX menu (KIT > MASTER FX), only main outputs | Open effects | ✅ | Mix: four effect cards and signal-path line | Saved with the kit. |
| p.29 | Rhythm Echo: TIME, MOD, MFRQ, FB, FILTF, FILTW, MONO, LEV | Delay | ✅ | Mix > Rhythm Echo card with tap editor | TIME in 128th notes. |
| p.30 | Gate Box: DVOL, PRED, DEC, DAMP, HP, LP, GATE, LEV | Reverb | ✅ | Mix > Gate Box card |  |
| p.30 | Master EQ: LF, LG, HF, HG, PF, PG, PQ, GAIN | Shelves and parametric | ✅ | Mix > EQ card |  |
| p.31 | Dynamix: ATCK, REL, TRHD, RTIO, KNEE, HP, OUTG, MIX | Compressor | ✅ | Mix > Dynamix card |  |
| p.28 | Control master FX by CTR machines | CTR-RE, GB, EQ, DX locks | ✅ | Sound: CTR machine tables | Only way to lock master FX. |

## 7. LFOs (pp.32-34)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.32 | LFO edit window (16 LFOs, one per track) | FUNCTION + SYNTH key | ✅ | Sound: LFO row for the selected track | One LFO per track as default. |
| p.32 | TRACK target | Which track | ✅ | Sound > LFO target (first menu) |  |
| p.32 | PARAM target (synth, FX, routing, other LFO) | Which parameter | ✅ | Sound > LFO target (second menu) | LFO-on-LFO via LFOS/LFOD/LFOM; higher-track rule is the firmware's. |
| p.32 | SHP1: six shapes | Waveform 1 | ✅ | Sound > LFO shape keys |  |
| p.32 | SHP2: inverted shapes | Waveform 2 | ✅ | Sound > LFO shape keys |  |
| p.32 | UPDTE: FREE / TRIG / HOLD | Retrig behaviour | ✅ | Sound > LFO motion |  |
| p.32 | SPEED (1/128 notes) | SPD | ✅ | Sound > LFO motion |  |
| p.33 | DEPTH | Amount | ✅ | Sound > LFO motion |  |
| p.33 | SHMIX | Waveform balance | ✅ | Sound > LFO shape |  |
| p.34 | Two LFOs on one target add up | Layering | ✅ | Set same target twice | Not drawn as a total. |
| p.34 | Copy, paste, clear, undo of an LFO | LFO clipboard | 🟡 | Sound copy/paste carries the LFO with the sound | No LFO-only copy or clear. |

## 8. Pattern sequencer (pp.35-50)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.35 | CLASSIC mode | Trigs only, no kit link | ✅ | LCD MODE = CLASSIC | Lock lane greyed; locks kept. |
| p.35 | EXTENDED mode (kit link, locks) | Pattern recalls its kit | ✅ | LCD MODE = EXT; Pattern chooser shows Knn |  |
| p.36 | Tempo: integer and decimal steps | Tempo screen | ✅ | LCD tempo drag (Shift = 0.1) | Arrow keys step it when focused. |
| p.36 | FUNCTION held: tempo changes on release | Delayed change | ❌ | None |  |
| p.36 | Temporary +/-10 % tempo shift (LEFT/RIGHT) | Turntable sync | ❌ | None | The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.36 | Tap tempo | FUNCTION + TEMPO | ✅ | T or B key | keymap.json tap-tempo. |
| p.36 | External sync, EXT shown | MIDI clock follow | ✅ | Global > Sync > Tempo in; host tempo marked on LCD | DAW follows host. |
| p.37 | Pattern selection (8 banks x 16) | A01-H16 | ✅ | Pattern chooser; prev/next arrows |  |
| p.37 | Uninitialised patterns shown by LEDs | Which are empty | ✅ | Pattern chooser: EMPTY slots |  |
| p.37 | Pending pattern at the pattern end | Queue | ✅ | Pattern chooser Queue / Now; LCD blinks |  |
| p.37 | Sticky bank keys (two seconds) | One-hand operation | ➖ |  | Not needed with the mouse. |
| p.37 | Pattern chaining (one bank, each pattern once) | Chain | ✅ | Song page > Patterns > Chain | Limit 16 pads in page. |
| p.37 | STOP jumps to a queued pattern | Chain behaviour | 🟡 | Firmware behaviour | Page does not add a key; the firmware does it when a chain is set. |
| p.37 | Parameter tweaking: FUNCTION + knob on all tracks | Control All | ✅ | Alt-drag a value; MUTATE | Cmd 'tweak'. |
| p.38 | Filter sweep with FUNCTION + knob E/F in TFX | Whole-pattern sweep | ✅ | Alt-drag FLTF / FLTW | Same. |
| p.38 | FUNCTION + CLASSIC/EXTENDED reloads kit | Revert | ✅ | Kit library: Reload (kit field) |  |
| p.38 | Scale setup: number of steps | Pattern length | ✅ | LCD LEN with Alt (inner length) |  |
| p.38 | Scale setup: total length 16/32/48/64 | Pages | ✅ | LCD LEN | Doubling: LEN x2 key. |
| p.38 | Scale setup: tempo multiplier 1X, 2X, 3/4X, 3/2X | Speed | ✅ | LCD SPD |  |
| p.38 | First page copied to later pages when extending | Auto fill | 🟡 | LEN x2 copies the new half | The firmware's auto-copy on length change is not shown; not verified. |
| p.39 | Grid recording (trig toggle per step) | Compose | ✅ | Step grid click; drag paints | Undo is one step per drag. |
| p.39 | Pattern pages in grid mode | Switch page | ✅ | Page key |  |
| p.39 | No save needed; copy before editing | Work in memory | ✅ | Library copy |  |
| p.39 | FUNCTION + LEFT/RIGHT moves notes | Rotate | ✅ | Alt + arrows (Sequence) |  |
| p.39 | MKI pattern page LEDs | 32 steps | ➖ |  | See §13. |
| p.39 | Live recording, quantised | Play TRIG keys | ✅ | REC then click steps / piano keys | Quantised by the machine. |
| p.40 | Remove notes while live recording (EXIT + TRIG) | Erase live | ❌ | None |  |
| p.40 | Note copy / paste with locks | COPY NOTE | ✅ | Select a step (Cmd-click), Cmd+C, Cmd+V; copy carries locks |  |
| p.40 | Clear note parameter locks / undo | CLEAR NOTE LOCKS | ✅ | Step menu clear; lane erase (Alt-drag) |  |
| p.41 | Track copy / paste (notes, machine, LFO, locks) | COPY TRACK | 🟡 | Select track steps, copy, paste; Shift-click headers = paste to many | Machine and LFO are copied separately (Sound copy). |
| p.41 | Clear track / undo | CLEAR TRACK | ✅ | Select the track's steps, Clr (Delete) |  |
| p.42 | Copy / paste track page | COPY PAGE | ✅ | Cmd+C with no selection copies the page shown |  |
| p.42 | Clear track page | CLEAR PAGE | ✅ | Clr key |  |
| p.43 | Pattern copy / paste (with kit link) | COPY PATTERN | ✅ | Pattern chooser: Copy, Paste, drag |  |
| p.44 | Clear pattern / undo | CLEAR PATTERN | ✅ | Alt+Clr / Alt+Delete; pattern chooser Clear | Chooser clear keeps length and kit link. |
| p.44 | Mute window: toggle tracks | Track mute | ✅ | M keys in rail and Mix; M key | Read from machine memory. |
| p.44 | Held mute changes applied on release (+ and X marks) | Prepare mutes | ✅ | Shift-click M keys, apply on release |  |
| p.45 | Minimise mute window | Compact HUD | ➖ |  | Always visible. |
| p.45 | Mutes persist across patterns and modes | Behaviour | ✅ | Machine memory |  |
| p.45 | Accent amount 0-15 | ACCENT level | ✅ | LCD ACC |  |
| p.45 | Accent trigs per step | Accent pattern | ✅ | Shift-click a step | Honours EDIT ALL; no toggle for ALL vs per track. |
| p.45 | Accent pattern ALL vs per-track edit | EDIT ALL toggle | 🟡 | Marks follow the pattern's flag | No control to change the flag. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.46 | Copy / paste / clear accent pattern or track | Accent clipboard | 🟡 | Selection copy includes accents | No separate accent window. |
| p.46 | Swing amount 50-80 % | SWING level | ✅ | LCD SWG |  |
| p.46 | Swing step pattern (which steps swing) | Swing trigs | ❌ | None | Swing marks are in the document; no editor. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.46 | Per-track swing | Swing tracks | ❌ | None |  |
| p.47 | Copy / paste / clear swing | Swing clipboard | 🟡 | Selection copy includes swing marks |  |
| p.47 | Parameter locks, grid: hold TRIG + knob | Lock a value | ✅ | Lock lane: drag over a step; parameter keys | 64 budget meter on LCD. |
| p.48 | View a step's locks (hold TRIG) | Inspect | ✅ | Lock lane shows each step's value; chips show counts |  |
| p.48 | Remove all locks of a step / one lock | Unlock | ✅ | Step menu; Alt-drag lane erase; Alt+clear key |  |
| p.48 | 64 locked parameters per pattern | Budget | ✅ | LCD Locks counter, refuses past 64 |  |
| p.48 | Locks only play in EXTENDED, kept in CLASSIC | Behaviour | ✅ | Lane greyed in CLASSIC |  |
| p.48 | Live-recorded locks (move a value while recording) | Live locks | ✅ | REC and move a value | Locks next unplayed trig. |
| p.48 | Remove live locks (EXIT + knob) | Erase live locks | ❌ | None | Erase in the lane afterwards. |
| p.49 | Slide trigs (ALL and per track) | Parameter slide | 🟡 | Alt+Shift-click a step | Honours EDIT ALL; no toggle. |
| p.50 | Copy / paste / clear slide | Slide clipboard | 🟡 | Selection copy includes slides |  |

## 9. Song mode (pp.51-56)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.51 | Song capacity: 256 rows, 32 songs | Limits | ✅ | Song page: row count; song stepper |  |
| p.51 | Load song | LOAD SONG | ✅ | Song stepper (< >), LCD SONG; loads when stopped |  |
| p.52 | Play, pause, stop a song | Transport | 🟡 | Play/Stop button | No pause. |
| p.52 | Move song pointer to a row; start from a row | Navigate, transport | ❌ | None | Click on a row only selects it for editing. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.52 | Playing row shown, repeats left | Playhead | 🟡 | Playing row marked in the arrangement | Turns left not shown. |
| p.52 | STOP holds, second STOP to start | Stop behaviour | ❌ | None |  |
| p.53 | Song editor: pattern, REP, BPM, offset, length, mute | EDIT SONG | ✅ | Song > Selected row; arrangement grid |  |
| p.53 | Insert / remove step | FUNCTION + arrows | ✅ | Add by palette, Duplicate, Delete |  |
| p.53 | Copy / paste / clear / undo rows | Row clipboard | ✅ | Cmd+C/V; Delete; Cmd+Z |  |
| p.53 | END mark | Song end | ✅ | END cell |  |
| p.54 | Row offset and length (OF LEN) | Part of a pattern | ✅ | Song > More > Start, Length |  |
| p.54 | Song loop (count, infinite, nested) | LOOP | ✅ | Add loop; Times; Forever |  |
| p.55 | Song jump | JUMP | ✅ | Command: Jump |  |
| p.55 | Song halt | HALT | ✅ | Command: Halt |  |
| p.55 | Song mute per row | MUT | ✅ | Song > More > Mutes |  |
| p.56 | Save song, choose slot, name it | SAVE SONG | ❌ | None | Edits are written into the current slot at once. No name field. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.56 | Copy, paste, clear, undo of song slots | Song library | ❌ | None | Only rows have a clipboard. |
| p.52 | Song position pointer from MIDI | SPP | ➖ |  | Firmware follows host. |
| p.52 | Song bar counter | Header counter | ✅ | Song: rows, bars, time | Estimate. |

## 10. Global settings (pp.57-78)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.57 | 8 global slots | Switch set-up | ✅ | GLOBAL panel: slot buttons 1-8 |  |
| p.57 | Turbo menu (speed negotiation) | TurboMIDI | ➖ |  | Emulator and wire use their own pacing. |
| p.59 | Base channel (1-4 ... 13-16, OFF) | MIDI channels | ✅ | GLOBAL > Control |  |
| p.59 | Map editor: group TRACK | Note to track | ✅ | GLOBAL > Map editor, 16 track keys |  |
| p.59 | Map editor: group PAT A-H | Note to pattern | ✅ | GLOBAL > Map a note |  |
| p.60 | Map editor: group CTRL (start, stop) | Note to transport | ✅ | GLOBAL > Map a note |  |
| p.60 | Map editor: TRIG GATE / START / QUE | Pattern trig mode | ✅ | GLOBAL > Pattern trig |  |
| p.60 | Assign a note by playing it on a keyboard | MIDI learn of notes | ❌ | None | Choose the note with the stepper. |
| p.60 | Local control | LOCAL CTRL | ✅ | GLOBAL > Control | Marked 'not verified' in emulator. |
| p.61 | Program change OFF / IN / OUT | PRG CHANGE | ✅ | GLOBAL > Prg change in / out |  |
| p.61 | Program change channel | Channel | ✅ | GLOBAL > Prg channel |  |
| p.61 | Encoder resolution 24 / 32 (MKII) | MECH SETTINGS | ➖ |  | No knobs. |
| p.62 | Output routing, tracks to A-F or MAIN | OUTPUT | ✅ | GLOBAL > Routing; Mix OUT key |  |
| p.63 | Trig in A/B: GATE, SENS, VMIN, VMAX, DEST | External pads | 🟡 | GLOBAL > Trig in A / B (read-only table) | Shown as stored, not editable. The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.64 | Sysex send all data | Backup | 🟡 | Engine menu: Export SysEx (every document) | One file; no range. |
| p.66 | Global sysex send (all or one slot) | Send globals | 🟡 | Export SysEx | No single-slot send. |
| p.66 | Song + pattern (+ kit) sysex send, range | Send songs | 🟡 | Export SysEx | No range. |
| p.66 | Pattern (+ kit) sysex send, range | Send patterns | 🟡 | Export SysEx | No range. |
| p.66 | Kit sysex send, range | Send kits | 🟡 | Export SysEx | No range. |
| p.67 | General sysex receive (any time) | Import | ✅ | Import SysEx / drop a .syx: preview by kind and slot, then report |  |
| p.68 | Original place receive (ORIG) | Same slot | ✅ | Import SysEx | Default. |
| p.68 | Specific place receive (SPEC) | Relocate | ❌ | None | Import keeps original slots; slots can be left out only. |
| p.69 | Sysex verify (VERF) | Check without writing | 🟡 | Import report lists differences | It writes; no dry run. |
| p.75 | Tempo in INTERNAL / EXTERNAL | Sync | ✅ | GLOBAL > Sync |  |
| p.76 | Control in ON / OFF (start, stop, continue) | CTRL IN | ✅ | GLOBAL > Sync |  |
| p.77 | Tempo out (send clock) | TEMPO OUT | ✅ | GLOBAL > Sync |  |
| p.78 | Control out (send start/stop) | CTRL OUT | ✅ | GLOBAL > Sync |  |

## 11. Sample manager, UW only (pp.70-75)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.70 | Receive samples by MIDI SDS (closed / open loop) | Sample dump in | 🟡 | Sampler: Load sample..., drag WAV/AIFF into a ROM slot | Files, not SDS from another tool; converted to mono 16-bit. |
| p.72 | General sample receive (any time) | Receive | ➖ |  | Files only. |
| p.73 | Specific place receive (choose slot) | Slot | ✅ | Sampler: select ROM slot, Load... |  |
| p.74 | Original place receive (several files in order) | ORG | ✅ | Drop several files; they fill slots after the first |  |
| p.74 | Send samples to a computer (one, ALL, RAM) | SEND | ❌ | Disabled with reason | Machine ignores dump requests. |
| p.75 | Erase one / all samples | ERASE | ❌ | None |  |
| p.71 | Preview a sample | FUNCTION + ENTER | ✅ | Sampler: audition button | Plays on plug-in output. |
| p.71 | Slot list with sizes, memory left | POS/SIZE | 🟡 | Sampler: 48 ROM tiles with waveform and length; MEM on LCD | Memory is slot count, not DSP percent. |
| p.71 | Sample names / rename | Name slot | ✅ | Sampler: Rename (sent as 0x73) | Names the machine reports are shown when readable. |
| p.71 | Copy / clear / paste / undo in sample manager | Slot clipboard | ❌ | None |  |
| p.71 | RAM to ROM copy | Keep a take | ❌ | Disabled with reason | The firmware supports it; only a real machine's panel (HW MIDI engine) can do it today. |
| p.70 | Sample limits (4-48 kHz, mono, 12-bit playback) | Rules | ✅ | Plug-in converts on load |  |
| p.71 | ROM vs RAM machines | Concept | ✅ | Sampler workspace |  |

## 12. Early start-up menu (pp.79-81)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.79 | Test mode | Self test | ➖ |  |  |
| p.79 | Empty reset | Clear all | ❌ | None | Per-slot clear only. |
| p.79 | Factory reset | Presets | ❌ | None | Re-import a .syx instead. |
| p.79 | Soft reset | Reload samples | ❌ | None | Restart the editor. |
| p.79 | MIDI upgrade / send upgrade | OS update | ➖ |  | Firmware fixed to OS 1.63. |

## 13. Technical information, MKI vs MKII (pp.82-83)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.82 | Specifications (16 tracks, 128 patterns, 32 songs, 64 locks, 384 CC) | Limits | ✅ | All enforced in the page |  |
| p.83 | Pattern length 64 (MKII) / 32 (MKI) | Length | 🟡 | MKII lengths | MKI model not offered. |
| p.83 | MKI/MKII sample slots (32/48), RAM machines (2/4) | UW size | 🟡 | 48 ROM, 4 RAM | MKII only. |
| p.83 | MKI / MKII panel colour | Look | ✅ | Top bar plate key | Colour only. |

## 14. Appendix A: machine reference (pp.A-1 to A-16)

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.A-1 | TRX-BD Bass drum | PTCH DEC RAMP RDEC STRT NOIS HARM CLIP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-B2 Bass drum 2 | PTCH DEC RAMP HOLD TICK NOIS DIRT DIST | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-SD Snare | PTCH DEC BUMP BENV SNAP TONE TUNE CLIP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-XT Tom | PTCH DEC RAMP RDEC DAMP DIST DTYP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-CP Clap | CLPY TONE HARD RICH RATE ROOM RSIZ RTUN | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-RS Rim shot | PTCH DEC DIST | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-CB Cow bell | PTCH DEC ENH DAMP TONE BUMP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-CH Closed hi-hat | GAP DEC HPF LPF MTAL | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-OH Open hi-hat | GAP DEC HPF LPF MTAL | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-CY Cymbal | DEC RICH TOP TTUN SIZE PEAK | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-MA Maracas | ATT SUS REV DAMP RATL RTYP TONE HARD | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-CL Claves | PTCH DEC DUAL ENH TUNE CLIC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-1 | TRX-XC Congas | PTCH DEC RAMP RDEC DAMP DIST DTYP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-BD Bass drum | PTCH DEC RAMP RDEC MOD MFRQ MDEC MFB | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-SD Snare | PTCH DEC NOISE NDEC MOD MFRQ MDEC HPF | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-XT Tom | PTCH DEC RAMP RDEC MOD MFRQ MDEC CLIC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-CP Clap | PTCH DEC CLPS CDEC MOD MFRQ MDEC HPF | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-RS Rim shot | PTCH DEC MOD HPF SNAR SPTC SDEC SMOD | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-CB Cow bell | PTCH DEC SNAP FB MOD MFRQ MDEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-HH Hi-hat | PTCH DEC TREM TFRQ MOD MFRQ MDEC FB | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-4 | EFM-CY Cymbal | PTCH DEC FB HPF MOD MFRQ MDEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-BD Bass drum | PTCH DEC SNAP SPLEN START RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-SD Snare | PTCH DEC HP RING STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-HT High tom | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-LT Low tom | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-CP Clap | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-RS Rim shot | PTCH DEC HP RRTL STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-CB Cow bell | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-CH Closed hi-hat | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-OH Open hi-hat | PTCH DEC HP STOP STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-RC Ride | PTCH DEC HP BELL STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-CC Crash | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-BR Brushed snare | PTCH DEC HP REAL STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-TA Tambourine | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-TR Triangle | PTCH DEC HP HPQ STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-SH Shaker | PTCH DEC HP SLEW STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-6 | E12-BC Bongo conga | PTCH DEC HP BC STRT RTRG RTIM BEND | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-BD Bass drum | PTCH DEC HARD HAMR TENS DAMP | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-SD Snare | PTCH DEC HARD TENS RVOL RDEC RING | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-MT Tom (PI-XT) | PTCH DEC HARD HAMR TUNE DAMP SIZE POS | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-ML Metallica | PTCH DEC HARD TENS | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-MA Maracas | DEC GRNS GLEN SIZE HARD | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-RS Rim shot | PTCH DEC HARD RING RVOL RDEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-HH Hi-hat | PTCH DEC CLSN RING AG AU BR CLOS | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-RC Ride | PTCH DEC HARD RING AG AU BR GRAB | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-9 | P-I-CC Crash | PTCH DEC HARD RING AG AU BR GRAB | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | GND-SIN Sinus | PTCH DEC RAMP RDEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | GND-NS Noise | DEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | GND-IM Impulse | UP UVAL DOWN DVAL | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | GND-EMPTY Empty machine | none | ✅ | Machine picker | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | INP-GA/GB Input gate A/B | VOL GATE ATCK HLD DEC | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | INP-FA/FB Input filter follower A/B | ALEV GATE FATK FHLD FDEC FDPH FFRQ FQ | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-11 | INP-EA/EB Input envelope A/B | AVOL AHLD ADEC FQ FDPH FHLD FDEC FFRQ | ✅ | Sound: group screens and boxes | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-12 | MID-01 to MID-16 MIDI machines (synth, effect and routing pages) | NOTE N2 N3 LEN VEL PB MW AT; CC1-5 D/V; PCHG; LFO | ✅ | Sound: knob boxes (no picture); routing and effects pages as groups | Machine reference lists no extra gaps. |
| p.A-12 | CTR-AL Control all | 8 params + TFX + routing pages for all tracks | ✅ | Sound: knob boxes (no picture); routing and effects pages as groups | Machine reference lists no extra gaps. |
| p.A-12 | CTR-8P Control 8 parameters | P1-P8; TRK/PAR shortcuts | ✅ | Sound: knob boxes (no picture); routing and effects pages as groups | Machine reference lists no extra gaps. |
| p.A-12 | CTR-RE Control Rhythm Echo | TIME MOD MFRQ FB FILTF FILTW MONO LEV | ✅ | Sound: group screens and boxes | Machine reference lists no extra gaps. |
| p.A-12 | CTR-GB Control Gate Box | DVOL PRED DEC DAMP HP LP GATE LEV | ✅ | Sound: group screens and boxes | Machine reference lists no extra gaps. |
| p.A-12 | CTR-EQ Control Master EQ | LF LG HF HG PF PG PQ GAIN | ✅ | Sound: group screens and boxes | Machine reference lists no extra gaps. |
| p.A-12 | CTR-DX Control Dynamix | ATCK REL TRHD RTIO KNEE HP OUTG MIX | ✅ | Sound: group screens and boxes | Machine reference lists no extra gaps. |
| p.A-15 | ROM-1 to ROM-24 ROM sample player | PTCH DEC HOLD BRR STRT END RTRG RTIM | ✅ | Sound: sample screen with STRT/END; Sampler tiles | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-15 | ROM-25 to ROM-48 ROM loop player (linear STRT, END) | PTCH DEC HOLD BRR STRT END RTRG RTIM | ✅ | Sound: sample screen with STRT/END; Sampler tiles | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-15 | RAM-R1 to R4 RAM record | MLEV MBAL ILEV IBAL CUE1 CUE2 LEN RATE | ✅ | Sound + Sampler: Set up sampling, recorder editor | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-15 | RAM-P1 to P4 RAM play | as ROM-25 to 48 | ✅ | Sound: sample screen with STRT/END; Sampler tiles | Group table in mdDeskSoundGroups.js; each knob is in one group. |
| p.A-16 | RAM machines tutorial (record main out, play back) | Workflow | ✅ | Sampler: Set up sampling button, capture next loop | Page does steps 1-9. |

## 15. Appendix B: MIDI control reference

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.B-1 | Default note map (tracks C2.., patterns E4..) | Defaults | ✅ | GLOBAL > Map editor | Set in the machine's global. |
| p.B-1 | CC mapping, 4 channels from base, all parameters | Remote control | ✅ | Live edits go out as CCs; HW MIDI engine; Control (MIDI learn) | Control workspace hidden unless the plug-in enables it. |
| p.B-1 | Level and mute CCs per track | CC 8-15 | 🟡 | Mute keys use machine memory | No page control for level CCs. |

## 16. Appendix C: sysex reference

| Manual § / page | Function | What it does | Our UI | Where in our UI | Notes |
|---|---|---|---|---|---|
| p.C-1 | Kit dump 0x52 / request 0x53 | Kit transfer | ✅ | Kit library; whole page | Desk codec. |
| p.C-1 | Set kit name 0x55 | Rename | ✅ | Kit library Rename |  |
| p.C-1 | Global dump 0x50 / request 0x51 | Global | ✅ | GLOBAL panel |  |
| p.C-1 | Set active global 0x56 | Slot | ✅ | GLOBAL slot buttons |  |
| p.C-2 | Load pattern 0x57; load kit 0x58; save kit 0x59 | Select/save | ✅ | Pattern chooser; Kit library |  |
| p.C-2 | Set MIDI note mapping 0x5a | Map | ✅ | GLOBAL > Map editor | Via global dump. |
| p.C-2 | Assign machine 0x5b | Machine | ✅ | Sound machine picker |  |
| p.C-2 | Set track routing 0x5c | Outputs | ✅ | Mix OUT; GLOBAL Routing |  |
| p.C-2 | Echo / gate / EQ / dynamix params 0x5d-0x60 | Master FX | ✅ | Mix cards |  |
| p.C-3 | Set tempo 0x61; set LFO 0x62 | Tempo, LFO | ✅ | LCD tempo; Sound LFO |  |
| p.C-3 | Reset MIDI note map 0x64 | Defaults | ❌ | None | Set each note by hand. |
| p.C-3 | Trig group 0x65; mute group 0x66 | Relations | ✅ | Sound > Relations |  |
| p.C-3 | Pattern dump 0x67 / request 0x68 | Pattern | ✅ | Whole page |  |
| p.C-3 | Song dump 0x69 / request 0x6a; load song 0x6c | Song | ✅ | Song page |  |
| p.C-3 | Set receive position 0x6b | SPEC receive | ❌ | None | See §10. |
| p.C-3 | Save song 0x6d | Song save | ❌ | None | Rows are written as dumps instead. |
| p.C-3 | Status request / set / response 0x70-0x72 | Mode, slots | ✅ | PATTERN / SONG switch; MODE |  |
| p.C-4 | TurboMIDI speed messages 0x10-0x17 | Fast transfer | ➖ |  | Not needed. |

## Summary

| Status | Rows |
|---|---|
| ✅ Yes | 220 |
| 🟡 Partial | 35 |
| ❌ No | 25 |
| ➖ Not applicable | 16 |
| Total | 296 |

Of the 280 applicable rows, 220 are fully covered, 35 partly, 25 not at all.

### Top gaps (our UI), most important first

1. **Song slot management**: no save, rename, copy or clear of song slots; no way to move the song pointer or start from a row (§9).
2. **Sample manager**: no send, erase, RAM to ROM copy, or sample clipboard (§11). The first and last are blocked by the machine; erase and clipboard are not.
3. **Per-track LEVEL (LEV)**: the knob and its meter have no control; the command exists in the desk (§1, §2).
4. **Swing pattern and per-track swing**: only the amount is editable (§8).
5. **Sysex send and receive control**: export is all-or-nothing, no ranges; no specific-place receive; verify is not a dry run (§10).
6. **Machine preview before loading** in the picker (§2).
7. **Live-record erase** (EXIT + TRIG, EXIT + knob) and the tempo helpers (delayed change, +/-10 % nudge) (§8).
8. **Trig in A/B pad settings** are shown but cannot be edited (§10).
9. **Accent / slide ALL vs per-track flag** cannot be changed, only honoured (§8).
10. **Pause, panic (double STOP), reset menu** (empty / factory / soft reset) (§1, §12).

Extras the manual does not have (not counted): GEN rhythm generators, MUTATE, solo, step selection, undo/redo across edits, app modulators, MIDI learn, HW MIDI engine, waveform editors for ROM/RAM samples.
