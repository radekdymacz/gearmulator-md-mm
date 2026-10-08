# Monomachine Editor: manual → screen mapping

Elektron Monomachine SFX-6 / SFX-60 MKI / MKII / SFX-60+ MKII, OS 1.32 (manual rev H). This maps each feature of the manual to the control in the mockup (`index.html`) that serves it. It also gives the firmware path for each feature and says what differs from the Machinedrum Editor (MD Editor).

Sources:
- `doc/manuals/monomachine_manual_OS1.32.pdf`. Page numbers below are the manual's own (1-xx, A-x, B-x, C-x).
- `doc/modern-ux/P1-RESULT.md`, `GAP-REVIEW.md`, `data-contract.md`, `manual-mapping.md`.
- The emulator source under `source/elektron/md/`:
  - `mdLib/mdtypes.h`: `MachineModel::Monomachine` and the OS 1.32B fingerprint.
  - `mdLib/mdsysextransfer.h`: `WaitingForReceiveMode`.
  - `mdLibTest/mmSysexWorkflowTest.cpp` and `sysexPanelDriver.h` (`enterMmReceive`).
  - `mdJucePlugin/parameterDescriptions_mm.json`: 7 pages × 8.
  - `mdJucePlugin/skins/mmSfx60/*`: the panel colours.
  - `mdJucePlugin/mmLcdEditPagesFirmwareTest.cpp`: the DATA page LEDs.

Firmware path legend:
- **CC n**: a control change on the track's channel (base + track), from Appendix B.
- **NRPN**: accepted on every active channel, never sent.
- **SX 0xNN**: a SysEx command after `F0 00 20 3C 03 00` (Appendix C).
- **dump**: a pattern (0x67), kit (0x52), song (0x69) or global (0x50) dump.
- **RAM**: not reachable by MIDI, so the app would read or write emulator RAM.
- **panel**: the app presses the emulated panel keys.

---

## 0. Firmware facts that shape the whole UX

| Fact | Where it comes from | What the UI does |
|---|---|---|
| The Monomachine only takes a dump on GLOBAL › FILE › SYSEX RECV while the screen says WAITING… | Manual 1-99 note. The emulator models it: `MidiSysexTransferState::WaitingForReceiveMode`, and the test harness drives the panel there with `enterMmReceive`. A kit dump (0x52) crosses two receive-mode boundaries in `mmSysexWorkflowTest` | **The main difference from the MD.** Pattern, song and structural edits are not live dumps. EMU: the app drives SYSEX RECV itself, and the LCD PATTERN field flashes `RECV`. HW MIDI: edits queue up, the field shows a blinking `SEND n`, and a click opens the step-by-step "open SYSEX RECV, then Send" dialog. Sound edits are live CCs |
| 62 locked parameters per pattern, pooled over all tracks | Manual 1-58 | LCD `LOCKS nn/62` meter (warn from 52, blink at 62). The pool covers the 6 synth tracks and the 6 MIDI tracks |
| Every DATA page value has a CC (7 pages × 8) | Appendix B: SYN CC 48-55, AMP 56-63, FILTER 72-79, EFFECTS 80-87, LFO1 88-95, LFO2 104-111, LFO3 112-119, LEV CC 7 | Every key-style value sends its CC while you drag it. The kit shows `edited` until SAVE KIT |
| Tempo is global and not stored in patterns; songs can set it | 1-55, 1-57 | The BPM field in the LCD is global. Song rows have their own BPM ("Keep" / value) |
| Mutes are true globals, work on notes, and survive pattern changes and reboots | 1-68 | Perform › Mutes (6 + 6). The M keys in the rail are the same state. Solo (S) is app-only |
| Kits: 128 plus an UNDO KIT slot. Patterns: 128 (A01-H16). Songs: 24 × 200 rows. Globals: 8 | 1-19..21, 1-45, 1-73 | The kit dialog explains the UNDO KIT. The song grid is 200 cells. The LCD shows `SONG 01` |
| MKI vs MKII | 1-114, and panel photos on Elektronauts (see §12) | The plate is the same silver on both. MKII adds user waveforms and DPRO-DDRW / DPRO-DENS. The MKI plate disables those in the machine picker |

## 1. Workspaces (keys 1-6)

| Key | Workspace | Serves |
|---|---|---|
| 1 | **Sequence** | **The Machinedrum Editor's Sequence layout with one change.**<br>Rail: the 6 track rows (number, machine, M, S), the **SYNTH / MIDI** switch, then LOCK PARAMETER (clear key, page chips SYN AMP FLT EFX LF1 LF2 LF3, 8 parameter keys).<br>Main: the MD's PAGE / 4 LEDs / ALL / FOL at the top right and the step ruler. Where the MD has its 16-row trig grid there is **one piano roll for the selected track**, the same height (16 × 28 px + gaps = 493 px). It has keys and note names on its right edge, gate bars to the NOTE OFF, stacked chords, and an ARP strip at the bottom when the arpeggiator is on. The editing gestures are in its tooltip.<br>Directly under it: the ENV (A F L dots; VEL on MIDI), SLIDE and SWING rows, aligned to the steps. At the bottom: the MD's lock lane (title "LOCK LANE · track · PARAM", legend, bars).<br>Selecting another track, or switching SYNTH / MIDI, swaps the roll; nothing moves. MIDI tracks keep a real LEN and show velocity |
| 2 | **Sound** | One screen, no scroll. Row 1: SYN (per machine), AMP, FILTER, EFFECTS. Row 2: LFO 1-3 with drag-to-route. Row 3: Arpeggiator, Transpose, and Trig setup (MIDI page on MIDI tracks). The one-line help sits in each section title's tooltip (ⓘ). The rail has the SYNTH / MIDI switch |
| 3 | **Mix** | The 6 channel strips on top (LEV fader, VOL / PAN / DIST / DSND, OUT bus, IN), then the routing graph below, with its nodes in the same column order as the strips. The graph shows the ROUTING mode keys, mix buses AB / CD / EF, FX inputs (NEIBOR, INP A/B/AB, BUS xx), outputs and the per-bus flow line. Clicking a node selects its strip |
| 4 | **Perform** | Controls on top: MULTI ENVELOPE, ASSIGN with the joystick, 6 + 6 global MUTES. In map mode, the MULTI MAP table comes next. The keyboard is at the bottom, with the mode keys (AUTO TRACK, MULTI TRIG with its 4 modes, MULTI MAP, POLY) and OCTAVE on its title line |
| 5 | **Song** | The MD song UI with 200 rows, per-row pattern and per-track transpose (6 + 6), and 6 + 6 mutes |
| 6 | **Control** | The MD Editor's Control. A mapping matrix: rows = 8 controller CCs (21-28) + app LFO / Random; columns = T1-T6 + M1-M6. The side panel has targets with MIN / MAX, LIN / EXP / LOG, INV, and add-target over the 7 DATA pages (or the MIDI page). There are app-only LFO and Random sources, and a CC-rate readout with a 300/s ceiling on LCD line 2. LEARN binds a clicked value to knob 1-8. Targets are sent as each track's CC from Appendix B (MIDI page: NRPN 0x38-0x3F); locks are not involved |

The header is the MD Editor's, unchanged: logo | WORKSPACE | LCD (engine menu, ● ▶ square keys, TEMPO, POSITION, PATTERN ‹ ›, KIT, LOCKS, line 2, COPY / CLR / PASTE) | SETUP (UNDO, REDO, LEARN, MK).

Measured at 1280 px:
- The header is 88 px tall on every workspace.
- The workspace always starts at y = 120.
- Selecting any of the 12 tracks (synth or MIDI side), or toggling ALL, leaves the roll (top 174, 493 px), the ENV/SLIDE/SWING rows (top 676) and the lock lane (top 790) exactly where they were. The lock lane bars are 80 px, so the page fits 1280 × 900 without scrolling.
- 6 workspaces. Every one fits 1280 × 900 and 1920 × 1080 with no scroll, on synth and MIDI tracks, every Sound machine type and all 4 Perform modes. No two horizontal rules sit within 12 px of each other: section rules sit only at the bottom of a row.
- While BOOTING OS, the LCD shows a firmware-style start-up screen: model, OS 1.32B, memory and pattern counts, and a progress bar. It is drawn over the LCD, so the width does not change. The text is the editor's own, not the ROM's.
- The LCD reports the engine as NO ROM / LOADING ROM / BOOTING OS / EMU OS 1.32B / ROM ERROR / HW CONNECT / HW MIDI, with an LED that is off, blinking or on (the MD Editor's v48). Until the engine is ready, the fields dim, REC and PLAY are disabled, and editing waits. On page load it runs load → boot → ready.
- LCD fields and line 2 use fixed-width slots (MD v46), so nothing reaches COPY / CLR / PASTE and the LCD stays 597 px wide.
- In Sequence, the rail's LOCK PARAMETER block starts on the lock lane's title line (within 1 px) and ends at its bottom edge (0 px). The free space goes above it.
- No horizontal scroll on any workspace.

## 2. Front panel and LCD (1-4..1-10)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| LCD: tempo, level, 8 parameters, pattern-page boxes, play state, kit, pattern, machine (1-10) | LCD line 1: TEMPO, POSITION (page.step), PATTERN, KIT (+ saved/edited), LOCKS. Line 2 changes per workspace: LEN · SPD · SWG · PTRN · track on Sequence, ROUTING on Mix, MODE on Perform, SONG · ROWS · BARS · TIME on Song | status SX 0x70 / 0x72 (0x02 kit, 0x04 pattern, 0x08 song, 0x10 seq mode, 0x20 poly, 0x21 audio/MIDI seq, 0x22 / 0x23 focus track) | Same LCD. Line 2 carries PTRN (pattern transpose, MM only) instead of ACC and MODE (MD) |
| TRACK keys: select and mute; LED colours green / red / yellow (1-4) | Rail track headers (select, M, S) | CC 3 per track (mute, receive only). SET STATUS 0x71 / 0x22 for focus | 6 tracks, not 16. The rail shows either the synth or the MIDI tracks |
| TRIG keys as keyboard, velocity 80 or 127 with FUNCTION (1-5) | Perform keyboard (on screen, 4 octaves) | Note on the auto channel (default 9) | New: the MD has no keyboard |
| [DATA PAGE] keys, 7 pages; both keys = MULTI ENV (1-4) | Sound shows all 7 pages at once. The lock parameter picker has 7 page tabs with LEDs. The multi env lives on Perform | CC per page | MD has 3 pages (SYN, EFX, ROUTING); the MM has 7 |
| [BANK GROUP] A-D / E-H; [BANK]+[TRIG] pattern select (1-5, 1-45) | LCD ‹ A01 ›. Song palette has banks A-H | SX 0x57 LOAD PATTERN; program change on the base channel | Same |
| [SCALE] page keys, TRACK PAGE LEDs (1-5) | Rail PAGE key + 4 page LEDs, ALL, FOL (follow) | — | Same component |
| [TRIG SELECT] trig track cycling (1-5, 1-58) | Replaced by the ENV row: all three trig-track flags visible and clickable per trig (dots in the LED colours: AMP red, FILTER yellow, LFO green). In the lane, a trig with only some flags shows a small mark, and a trigless one is an outlined bar | pattern dump | New for the MM. The hardware needs 4 passes of TRIG SELECT to show what one row shows here |
| SFX-6 joystick, keyboard, [MULTI TRIG], [OCTAVE] (1-6) | Perform: on-screen joystick in ASSIGN, keyboard with an octave stepper, keyboard mode keys | Pitch bend = L/R, CC 1 up, CC 2 down (B-6) | New |

## 3. Kits (1-18..1-24)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| Kit ↔ pattern link, kit reload on a pattern change (1-18) | Switching to a pattern linked to another kit while the kit is `edited` asks: "Save kit, then switch" / "Switch (edits to UNDO KIT)" / "Cancel" | status 0x02; kit link inside the pattern dump | Same dialog, but it names the MM's UNDO KIT, which really keeps the lost edits |
| Load / save / name a kit, 128 slots (1-19) | **Kit library** (click KIT in the LCD, or Enter on it). The MD Editor's v50 panel, sized for the MM: a 16 × 8 grid of 128 slots. Each slot shows its number, its name (11 characters), its pattern links and a star when unlinked (1-19). The current kit shows saved / edited. Actions: Load, Save, Save as Kn, Copy, Paste, Clear, Rename (F2 or double-click), and drag-copy slot to slot. Arrows / Enter / Delete / Cmd+C/V / Cmd+Z / Esc. Every button's tooltip says what it is on the firmware | SX 0x58 LOAD KIT, 0x59 SAVE KIT, 0x53 request, 0x55 set kit name (11 bytes). **Kit dumps (0x52) for paste / clear / drag / renaming another slot go through SYSEX RECV** (RECV in the emulator, SEND n on hardware) | 128 kits (MD: 64), 11-character names (MD: 16). Every MM pattern recalls its kit, so there is no CLASSIC mode, and Load relinks the current pattern (as KIT › LOAD does, 1-12). A cleared kit is six GND-SIN (1-19) |
| UNDO KIT (1-20) | Named in the kit and switch dialogs | slot 0 of the kit list | New |
| Copy / clear kit (1-21) | — (P2 kit browser) | kit dump | Deferred, like the MD |
| Assign a machine to a track: SYNTH / MACHINE / MIX columns (1-22) | Sound › Synthesis › machine key: family column + machine grid + preview. "Keep track effects + LFOs" toggle. MKII-only machines are disabled on MKI | SX 0x5B assign machine (track, machine id, init pages) | Same picker pattern with MM families. A new FX machine automatically gets an input and DEC = REL = 127, the manual's rule for passing audio through |
| Copy / clear machine (1-23, 1-24) | Sound workspace: COPY / CLR / PASTE = COPY MACHINE (machine + 7 pages) / CLEAR MACHINE (back to GND-SIN) / PASTE MACHINE | kit dump | MD copies "sound"; the MM wording is used here |
| Mix bus (OUT BUS) per track (1-24) | Mix: OUT BUS keys AB / CD / EF (a track can feed two) and taps on the graph | SX 0x5C set track routing (output bits `bcd`, input `eee`) | MD has MAIN / A-F outputs |
| Kit contents list (1-18) | The kit badge scope: machines, 7 pages, LFOs, the MIDI page, routing, trig and multi-trig settings, multi env, name | kit dump 0x52 | — |

## 4. DATA pages (1-25..1-37, Appendix A)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| SYNTHESIS page, up to 8 per machine; TUNE on every synth machine (1-25) | Sound › the machine's groups (MM-PORT-PLAN e, `85-sound-groups.js`: each knob in one group, a small screen per group). SWAVE saw/pulse: oscillator stack, drag UNIW · UNIL. SWAVE-ENS / DPRO-DENS: chord keyboard from PCH2-4. SID: waveform with PW and MOD / MSRC in the title. VO-6: vowel map, drag VOC1 · VOC2, with the tutorial's O / A / I points. DPRO-WAVE / DDRW: morph (WP / MIX). FM+: modulator bars into the carrier. FX: input → machine → track FX | CC 48-55 | Different screens per engine. The MD draws drum decay curves |
| AMPLIFICATION: ATK HOLD DEC REL (AHDR, HOLD instead of sustain), DIST (headroom below 0), VOL, PAN, PORT (1-26..1-29) | Sound › Amp: AHDR screen with 4 handles and a NOTE OFF marker. DEC 127 is drawn as "holds until note off" (the VO tutorial's setting). DIST and PAN are bipolar | CC 56-63 | New page. The MD has no amp envelope |
| LEVEL knob, not lockable, not modulated (1-26) | Mix: LEV fader per strip. Its tooltip says it cannot be locked | CC 7 | — |
| FILTER: BASE / WDTH gap filter, HPQ / LPQ, env ATK / DEC, BOFS / WOFS, key tracking (1-29..1-31) | Sound › Filter: response curve. Drag BASE · HPQ and WDTH · LPQ. A dashed curve shows the envelope peak (BOFS / WOFS added) | CC 72-79 | MD: FLTF / FLTW / FLTQ with one Q; the MM has two Qs and an envelope |
| EFFECTS: EQF / EQG, SRR, DTIM (256ths, 64 = 1 beat), DSND (± = stereo kept / ping-pong), DFB (> 64 grows), DBAS / DWID (1-32..1-34) | Sound › Effects: EQ bell + one bar of delay taps. Ping-pong shows as alternating taps. Warns "feedback grows!" above 64 | CC 80-87 | The MD's delay is the master Rhythm Echo; each MM track has its own delay |
| 3 LFOs per track: PAGE, DEST (incl. PTCH with 8 ranges, the MIDI page and the other LFOs), TRIG FREE / TRIG / HOLD / ONE / HALF, 11 WAVEs, MULT, SPD, INTL, DPTH; same destination = summed (1-35..1-37) | Sound › LFO 1-3 cards: PAGE › DEST key dropdowns; the waveform over 2 bars, drawn with the real trig mode against the track's LFO trigs (ticks); 11 shape keys; TRIG seg; MULT SPD INTL DPTH. **Drag the "~n" handle onto any value to route it.** Modulated values show a `~n` badge everywhere | CC 88-95 / 104-111 / 112-119 | MD: 16 LFOs per kit with TRCK / PARAM / 2 shapes / SHMIX. MM: 18 LFOs, track-scoped, LFO-on-LFO (the SID example: LFO 2 → LF1 DPTH) |
| Machine reference: SWAVE, SID, DPRO, FM+, VO, GND, FX (A-1..A-15) | Picker families. The `about` line per machine paraphrases the manual. SysEx ids in the picker header | SX 0x5B ids: GND 0-2, SID 3, SWAVE 4 / 5 / 14, DPRO 6 / 7 / 32 / 33, FM+ 8-10, VO 11, FX 12 / 13 / 15-19 | — |
| DPRO-BBOX: the key picks one of 24 drums from C-3 (A-4) | The roll labels BBOX notes with the drum name (BD1, SD1, CH, OH…) | — | New |
| VO-6: consonant + vowel per trig, CONS table (A-8, A-9) | The roll labels VO notes with their locked CONS (M, N, SJ…). The example track is the manual's "monomachine" tutorial | — | New |

## 5. Additional kit settings (1-38..1-44)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| ASSIGN: JOY R/L (MIRR), JOY U, JOY D, VELOCITY, KEY TRACKING (+ HPF / LPF tracking), 2 × PAGE / DEST / ADD (1-38..1-40) | Perform › Assign: track keys 1-6, the 5 firmware tabs, joystick pad (springs back and shows the live ADD per destination), 2 rows of PAGE / DEST / ADD, Mirror, HPF / LPF tracking | kit dump; joystick = PB / CC 1 / CC 2 | New (the MD has no assign) |
| TRIG settings: TRIG POS (forward notes), PORTAMENTO ALWAYS / ONLY LEGATO, legato AMP / FLT / LFO (1-40, 1-41) | Sound › Trig setup: trig position select, PORT seg + PORT / HOLD / DEC values, LEGATO keys. TRIG POS is also drawn on the Mix graph as a dotted "TRIG ›Tn" cord | kit dump | MD trig groups are similar but different: the MM forwards notes and can chain |
| MULTI TRIG: ALL TRK, SPLIT KEY (ZONES: TRACK + KEY), SEQ START, SEQ TRNSP (TIMING) (1-41..1-43) | Perform › Multi trig: mode seg. SPLIT: a draggable split marker on the keyboard with coloured zones and an "Upper from Tn" stepper. SEQ modes: Timing stepper; the key transposes / restarts the pattern from C-4 | Multi trig channel (default 7) | New |
| MULTI ENV: ATK DEC SUS REL (127 = infinite) PORT (1-43) | Perform › Multi envelope: ADSR screen + 5 values + the "no effect" recipe | NRPN 0x40-0x45 | New |

## 6. The pattern sequencer (1-45..1-60)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| Pattern select, cued while playing (1-45) | LCD ‹ ›, or the **Pattern chooser** (click the pattern name): 8 banks A-H × 16, each slot showing its length and kit. Click queues (Shift = now); Copy / Paste / Clear / drag-copy; A-H jump to a bank. The queued pattern's own name blinks in the LCD, then gives one short inverted flash when it starts (MD v49). Pattern dumps for paste / clear / copy go through SYSEX RECV | SX 0x57; status 0x04 (the MD's P1 finding says it runs about 2 steps early, not re-measured on MM) | Same |
| Pattern chaining (1-46) | — | panel / RAM | Deferred (also on the MD) |
| SCALE SETUP: total steps 2-64, pages, 1X / 2X / 3/4X / 3/2X (1-46) | LCD line 2 `LEN` (click = ±1 page, scroll = ±1 step) and `SPD` | pattern dump | MD lengths are 16 / 32 / 48 / 64. The MM takes any step count |
| GRID RECORDING: trig on / off, hold a trig for its pitch, NOTE OFF with FUNCTION, chords (1-47, 1-48) | **One object per step, edited in one place.** Roll (selected track): click adds a note or moves the trig's pitch, drag vertically for pitch, shift-click adds a chord note (stacked, drawn with a dot), **drag the bar's end to move its NOTE OFF**, alt-click deletes a note, alt-click on an empty step sets or clears a NOTE OFF. Scroll moves the pitch window (no label needed). The gestures are in the roll's tooltip | pattern dump | New. The MD has no pitch per step. The hardware shows a trig's pitch only while you hold it; here every trig sits at its pitch |
| Gates: a note sounds until the next trig or a NOTE OFF; HOLD avoids NOTE OFFs (1-27, 1-49) | The roll draws the gate as a bar to its NOTE OFF or the next trig. **The light part of a bar = where amp ATK + HOLD + DEC has likely died** (an estimate, shown as such). DEC 127 = no fade | — | New |
| Rotate trigs and locks (FUNCTION + ←/→) (1-49) | — | — | Not yet |
| LIVE RECORDING (REC + PLAY, quantised, EXIT erases) (1-49) | REC key in the LCD (toast explains). Live-recorded knob moves = locks | panel | Mock only, like the MD |
| STEP RECORDING (1-50) | — | panel | Not modelled |
| NOTE COPY / CLEAR NOTE LOCKS (1-50) | Alt-drag in the lock lane erases per step; the clear-lane key clears one parameter | pattern dump | — |
| TRACK PAGE COPY / CLEAR PAGE (1-51) | Sequence COPY / CLR / PASTE = the selected track's visible page, with pitches, locks and slide | pattern dump | Same scope as the MD |
| TRACK COPY, PATTERN COPY, SUPER COPY incl. MELODY copy (1-52..1-55) | Melody copy is modelled in code but not on a key yet (see §13) | pattern dump | — |
| TEMPO, tap, ±10 % nudge, external sync (1-55, 1-56) | BPM field: drag or arrows | SX 0x61 set tempo (BPM × 24) | Same |
| PARAMETER LOCKS in grid and live mode, 62 pooled, "lock once, then every step can hold its own value" (1-57, 1-58) | Rail › Lock parameter: 7 page tabs (a green LED = locks on that page) + 8 keys with counts. Lock lane: draw bars on trig steps, alt-drag erases, a dashed line shows the kit value, the scale shows enum names | pattern dump | Same idea, with 7 pages × 8 instead of 3 × 8 |
| TRIG TRACKS: AMP / FILTER / LFO sub-tracks, split pitch from envelopes (1-58..1-60) | The **ENV row** under the lane: three dots per trig (A red, F yellow, L green), click to toggle. Removing all three makes it trigless | pattern dump | New |
| TRIGLESS trigs (FUNCTION twice → green) (1-59) | Clear all three ENV dots of a note; drawn as an outlined bar in the roll | — | New |
| PITCHLESS trigs (EXIT while holding a trig) (1-59) | Created by the ENV dots; the roll shows a dashed bar at the previous pitch | — | New |

## 7. Additional sequencer features (1-61..1-69)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| ARPEGGIATOR per track (12 per pattern): SPD (6 = 16th), MODE OFF / KEY / SID / ADD, PLAY TRUE / UP / DOWN / CYCLE / RND, RNGE, OJMP, envelope switches AMP / FLT / LFO (1-61..1-63) | Sequence dock › Arp (the Sound page's Arp key goes there): MODE and PLAY segs, TRIG keys (disabled on MIDI tracks), SPD RNGE OJMP, a state box in plain words per mode. **The ARP strip at the bottom of the Sequence roll draws what the arpeggiator plays** from the trig's chord | pattern dump | New (no arpeggiator on the MD) |
| Arp RHYTHM + OFFSET track, up to 16 steps, LEVEL = length (1-63) | A 16-cell strip: click = mute, drag = offset (±24), the strip below = length | pattern dump | New |
| TRANSPOSE: TRACK, PAT (shared), SCALE --- / FIX / MAJ / MIN, KEY (1-63, 1-64) | Sound › Transpose: TRACK, PAT, SCALE, KEY, and the sum it plays. MAJ / MIN hatch the out-of-scale rows in the lane. The Sound › Transpose card shows the sum it plays | pattern dump | New |
| SWING: amount per pattern (50-80 %), a swing track per track, default every 2nd 16th (1-64, 1-65) | LCD `SWG`, and the SWING row under the lane | pattern dump | MD: swing per pattern or per track; here one amount and a row per track |
| SLIDE: a slide track per track; locked values glide to the next lock of the same parameter; unlocked trigs don't interrupt (1-66, 1-67) | The SLIDE row under the roll. The lock lane draws a dashed glide from the slide step to the next lock, even past the page edge | pattern dump | Same concept as the MD's slide. Drawn, not implied |
| MUTE MODE window, 6 + 6, held changes with FUNCTION (1-67, 1-68) | Perform › Mutes: 6 synth + 6 MIDI keys, with a green LED = playing | CC 3 (receive only) | — |
| POLY mode: 6 voices on the track in focus, the others silent (1-69) | Perform › Poly key; the rail dims the other tracks | SET STATUS 0x71 / 0x20 | New |

## 8. The MIDI sequencer (1-70..1-72)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| 6 MIDI tracks, each with notes, chords (one length), locks, arp (no switches), swing, slide, track transpose; LFOs shared with the synth track of the same number (1-70, 1-71) | **Sequence › MIDI**: the SYNTH / MIDI key switches the rail to the MIDI tracks; the roll then shows real lengths, chords as full bars, velocity as note shade + a VEL row (instead of ENV), SLIDE / SWING, and the lock lane over the MIDI page. Arp / Transpose / MIDI page are in Sound with the same switch | pattern dump; FUNCTION + TRIG SELECT = SET STATUS 0x21 | The MD's MID machines are per track; the MM has a separate 6-track sequencer |
| Main MIDI page: LEN (127 = until NOTE OFF), VEL, PB, PCHG (only when locked), CC1-4 (1-71) | Sound › MIDI page (MIDI side): 8 values + CL1-4 CC numbers (0-127 or AFT) + channel. In the roll, **dragging a note's end locks its LEN** on that step | kit dump (page), global (channels, CL1-4), NRPN 0x38-0x3F | New |
| MIDI SEQ SETTINGS: channel and CC numbers per track (1-108) | Same panel | global dump 0x50 | — |

## 9. Song mode (1-73..1-80)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| 24 songs, 200 rows, load / save (1-73, 1-74) | Song workspace: a 200-cell grid, `SONG 01` on the LCD | SX 0x6C load, 0x6D save, 0x6A request, 0x69 dump | MD: 32 × 256 |
| Row: PAT (or END / LOOP), REP, TRN, OF LN, XTRA (1-76, 1-77) | Selected row: pattern ‹ ›, Repeat, Tempo (Keep / BPM), **Transpose**, **Per track (INT T1-T6 + MIDI M1-M6)**, Part (offset + length), Mutes (6 + 6). Cells show ×n, ±trn, T, M, B, ~ like the hardware's bold letters | song dump; edits go through SYSEX RECV (§0) | TRN and per-track transpose are MM only. 12 mute keys instead of 16 |
| Loops (nested), jumps, halt (1-78) | Command seg LOOP / JUMP / HALT, Back to / Jump to, Times / Forever | song dump | Same (MD-proven semantics from P1; MM encoding not yet decoded) |
| Song transport and song pointer (1-75) | Play from the LCD | MIDI SPP | As MD |

## 10. Routing (1-81..1-86)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| Three stereo mix buses AB / CD / EF, summed in track order (1-81, 1-84) | Mix graph: buses as lines, taps per track, and a flow line per bus in words. For example: `(T1›T2 FX-CHORUS + T3 SID-6581 + T5 DP-BBOX) › T6 FX-REVERB INSERT › OUT A/B` | SX 0x5C | New: the MD has fixed master FX |
| FX machines need a source and a trig (1-81, A-12) | FX nodes are drawn outlined, not filled. Choosing an FX sets input + DEC = REL = 127 | SX 0x5B + 0x5C input bits | New |
| Audio inputs: INP A, INP B, INP AB (1-82) | Input node on the left, dashed cord to the FX node | 0x5C `eee` = 1-3 | — |
| NEIBOR routing: the track before feeds the FX; chainable (1-83) | Solid arrow between neighbour nodes, labelled NEIBOR. The example is T1 bass → T2 chorus with T1's outputs off, as the manual advises | 0x5C `eee` = 0 | This is the MM's "+" chain: the prompt's "+ machines" are FX machines in NEIBOR mode (see §13) |
| SID MSRC / SYNC = PRCH: the previous track's pitch modulates the SID (A-3) | Dotted "PRCH" arc between nodes when active | CC (SYN page) | New |
| Bus routing: same in/out bus = insert, other = send (1-84, 1-85) | "IN AB" arrow up from the bus; the flow line says INSERT or "takes a copy" | 0x5C `eee` = 4-6 | New |
| Global routing 3xSTEREO / 3xSTEREO + AB=MIX / 6xMONO (1-85, 1-89) | Mix seg + LCD line 2 `ROUTING` (click steps). AB=MIX draws the CD / EF buses curving into A/B; 6xMONO dims the buses and wires each track to OUT A-F | global dump 0x50 | MD: MAIN / A-F per track |

## 11. Global settings and files (1-87..1-111)

| Manual feature | Control | Firmware path | vs MD Editor |
|---|---|---|---|
| 8 global slots (1-87) | — | SX 0x56 set active global, 0x50 / 0x51 dump / request | As MD (not in either mockup yet) |
| MIDI channels: base, span, multi trig (7), multi map (8), auto track (9) (1-89, 1-90) | Named on Perform (the LCD `CH` field and the mode help text) | global dump | New |
| MULTI MAP EDIT: ranges (upper key), PAT (CUR or pattern), OFS / LEN, TRN, TIM; insert / delete ranges (1-93) | Perform › Multi map: bands over the keyboard (click to select), a table with range steppers, pattern select, OFS / LEN / TRN / TIM, "Split selected range", delete | global dump | New |
| CONTROL OUT1 / OUT2 / IN, program change, clock (1-91, 1-92) | — | global dump | Not yet |
| Mechanical settings, velocity curve (1-94) | — | global | Not planned (hardware only) |
| SYSEX SEND / RECEIVE (ORIG / SPEC / VERF) (1-95..1-99) | The SYSEX RECV dialog and the `SEND n` / `RECV` state (§0) | the dumps | **MM only**; the MD has no receive screen |
| DigiPRO manager (MKII): 64 waveforms, user "u" slots (1-99..1-104) | MKII / MKI plate switch gates DDRW / DENS | SX 0x5D / 0x5E; the emulator enters its own receive screen (`enterMmReceive(…, digiPro)`) | Plays the role of the MD's UW sample manager. Deferred |
| +Drive, snapshots, Digibanks (1-105..1-108) | — | panel / RAM | Deferred (as on the MD) |
| Early startup menu, OS upgrade (1-110) | Engine menu › LOAD ROM: the first-run dialog (OS 1.32B, 8 MiB, fingerprint) | `mdromloader` fingerprint `0xe1c1b461b6d0f21b` | Same flow, MM text |

## 12. MKI vs MKII panel colours (1-114, verified)

- The manual lists only functional differences: height (MKI 77 mm vs MKII 63 mm), power supply, S/N and balanced outputs, and user waveforms.
- The front plates are the same silver. The MKII only adds "MKII" print under the screen (Elektronauts: "Monomachine MKI versus MKII front cover plate differences"). The 50-unit blue anniversary SFX-60+ is a special edition.
- The emulator's `mmSfx60` skin gives the palette:
  - silver chassis `#d4d5d0`
  - dark-grey print `#383934`
  - rust labels `#a04d39`
  - grey-green LCD `#c0c9ba` in a black bezel
  - red / green / yellow LEDs `#e65d42` / `#9fc568` / `#e4c45c`
- The MK key toggles **MKI / MKII** only, and the default is MKII.
  - Both states show the silver plate. MKII adds the small "MKII" print under the LCD.
  - On MKI, the MKII-only machines (DPRO-DDRW Doubledraw, DPRO-DENS Ensemble) and user waveforms are disabled, with the reason given ("MKII only").
- There is no dark panel. The MD Editor's black MKII plate does not apply to the Monomachine.

## 13. Not yet covered, and what I was unsure of

Deferred:
- Pattern chaining and track copy (pattern copy is in the Pattern chooser)
- Super copy (melody copy is modelled but not on a key yet)
- Rotate trigs, step recording, live recording
- Global slots
- CONTROL IN / OUT, DigiPRO manager, +Drive
- Turbo MIDI, pitch bend

Uncertain, to verify against the ROM or the hardware:
1. **The Monomachine has no MIDI machines.** The brief mentions "MIDI machines". OS 1.32's machine list (A-1..A-15, SX 0x5B ids) has none. MIDI is the separate 6-track MIDI sequencer, so it got its own workspace.
2. **"+" machines.** The only "+" in OS 1.32 is the FM+ family name. Using the previous track as an input is FX machines in NEIBOR mode, plus SID's MSRC = PRCH. That is what the graph shows as chaining.
3. **Per-step note length and accents.** Synth tracks have no LEN and no accent. Length = the next trig or a NOTE OFF, shaped by AMP HOLD / DEC, and the sequencer always plays velocity 100. Only MIDI tracks have LEN / VEL. The "light part" of a gate is an **estimate**: (ATK + HOLD + DEC) / 127 × 16 steps. The real HOLD / DEC time scale is not in the manual.
4. **Parameter slots and lists now follow the OS 1.32B screens** (read by the editor agent), not the manual text:
   - every machine's 8 SYN slots, with unused knobs left empty (for example SWAVE-SAW slot 4, DPRO-BBOX has no TUNE, GND-NOIS is ST RED STON)
   - LFO / ASSIGN PAGE order PTCH SYN AMP FLT EFX LF1-3 MID
   - WAVE TRI ITRI SAW ISAW SQR ISQR EXP IEXP RMP IRMP RND
   - MULT 1X-64X (7 steps)
   - PTCH destinations 1/12 2/12 7/12 1OCT 2OCT 4OCT 8OCT 16OCT
   - CONS order … T TH TJ V Z
   - ASSIGN has 5 tabs: JOY RL (MIRR), JOY U, JOY D, VEL, KEY (HPF / LPF)
   - The mockup keeps enum indices; the editor converts firmware 0-127 as index = floor(v × n / 128).
5. The SID page is WAVE in slot 4 after PW / PWAD / PWRS; the manual's SYNC / SFRQ text belongs to DPRO-WAVE slots 5-6.
6. **Measured (0.3.5, the knobs of FUNCTION + BANK A's arpeggiator window, `mmEditorProbeFirmwareTest lab`):** RNGE 0-8, MODE 0-3, PLAY 0-4, LEVEL (length) 1-16, SPD up to 95; OJMP's knob sets bits 3-6 of the play byte (0x78 at its end), so the contract's `flag` (bit 3) is OJMP's low bit and `ojmp` its upper three: kept as two members, not yet re-meant. **Still unverified:** the LEN unit (1 step = 8 in the roll); PCH2-4 encoding (offset from 64; OFF and the just ratios are not modelled).
7. **Which commands the MM takes outside SYSEX RECV.** The emulator proves the gate for dumps. It is unknown whether SX 0x5B (assign machine), 0x5C (routing), 0x57 (load pattern) and 0x61 (tempo) are also gated. The mockup treats them as live, like CCs. This needs a probe in `mmSysexWorkflowTest`.
8. **Kit dumps need two receive-mode passes** in `mmSysexWorkflowTest` (`expectedBoundaries` = 2 for 0x52). The reason is not known. The HW dialog assumes one pass.
9. The MD's P1 findings may or may not hold on the MM:
   - "firmware stores dumps unvalidated"
   - "status runs ahead of audio"
   - "kit dump writes the slot, not the working kit"
   They are not measured yet (P1 was MD-only).
10. **MIDI note naming:** C-4 = MIDI 60, from the multi-trig "original pitch on C-4". Notes below MIDI 12 show as LOW.
11. **Swing track default** (every 2nd 16th). The manual says so. The emulator's pattern bytes for it are not decoded.

## 14. UX ideas that differ from the MD Editor

- **One note sequencer, in the MD's frame.**
  - A step holds one trig: its pitch or chord, its NOTE OFF, its AMP / FLT / LFO trig flags and its locks.
  - The Sequence page is the MD Editor's layout, with the 16-row trig grid replaced by one piano roll for the selected track, the same height. ENV, SLIDE and SWING sit under it, then the MD lock lane.
  - One SYNTH / MIDI key switches between the synth tracks and the MIDI sequencer, so it needs no workspace of its own.
  - Arpeggiator, Transpose and Trig setup moved to Sound as pages, so Sequence is only notes and locks.
- **Honest gates.** A bar runs to the NOTE OFF or the next trig. Where the amp envelope has likely faded, the bar goes light.
- **Trig tracks without TRIG SELECT.** All three envelope flags are visible per trig, as dots in the hardware's LED colours.
- **The arpeggiator you can see.** The ARP strip draws the played notes from the trig's chord, the rhythm and offset track, SPD and RNGE.
- **Visual LFO routing.** Drag the "~n" handle onto any value on any page. Every modulated value carries a `~n` badge. The LFO screen replays the real trig mode against the track's LFO trigs.
- **Routing as a graph.** NEIBOR arrows, bus taps, insert vs send, AB=MIX and 6xMONO drawn. A per-bus flow line spells out the track-order rule.
- **Perform as a first-class workspace.** The MM is played: multi trig split zones, multi map bands, seq start / transpose, the multi envelope, joystick assign and global mutes, with the keyboard at the bottom like the SFX-6.
- **Control, as in the MD Editor**, mapped onto the MM's own CC table: 7 DATA pages per synth track plus the MIDI page, across 12 tracks.
- **SysEx honesty.** A `RECV` / `SEND n` state and a guided SYSEX RECV dialog. The MD Editor's live pattern dumps do not exist on the MM.

## Manual errata

- **TRIG SELECT NRPN bit order.** The manual gives the value as `%XXXXALF`; the machine reads it as `%XXXXFLA` (bit 0 = AMP, bit 1 = LFO, bit 2 = FILTER). Reported by a user on Discord (2026-10-01), found by ear on hardware; not yet measured on the emulator. The editor does not send this NRPN today: the trig tracks travel in the pattern dump as separate amp, filter and lfo masks (`mmPattern`). Use `%XXXXFLA` if a live TRIG SELECT is ever sent.
