# Elektron Machinedrum (SPS-1/UW, OS 1.63) & Monomachine (SFX-6/60, OS 1.32) — hardware→screen UX mapping

Sources: official PDFs downloaded to `audio/gearmulator-md-mm/doc/manuals/`:
`machinedrum_manual_OS1.63.pdf` (126pp, elektron.co.jp mirror of the official manual) and
`monomachine_manual_OS1.32.pdf` (158pp, elektron.se official).

---

## 1. Machinedrum (MD) — data model

- **Kits**: 16 tracks, each with one assigned machine (drum voice). Kit = machine assignment +
  synth/effects/routing params + mute/trig relations + LFO settings. Copy/clear/undo kit and
  copy/clear machine supported.
- **Tracks**: fixed 16, each carries a TRACK EFFECT chain (see §2) and a ROUTING page.
- **Patterns**: length 16/32/48/64 steps (MKII), CLASSIC (16-step, 1 page) vs EXTENDED
  (up to 4 pages of 16 = 64 steps; p-locks only work in EXTENDED). Per-pattern **scale setup**:
  length + tempo multiplier (double-speed etc). Swing (steps 1–80%, default 50% = off, per-pattern
  or per-track), Accent (0–15 depth, per-pattern "ALL" or per-track boolean map), Slide (parameter
  slide trigs, per-pattern/per-track boolean map — slides only apply between two p-locked values).
  Trig types: normal grid trig, accent, swing, slide (each its own overlay grid, not separate step
  types).
- **P-locks**: **max 64 individually locked parameters per pattern**, shared freely across all 16
  tracks/parameters (i.e. a global pool, not per-track). Grid-record (hold TRIG + turn knob) or
  live-record. Copy/clear/undo at note, track, track-page, and pattern granularity.
- **Song mode**: up to 256 steps, 32 songs in memory; each row = pattern + start position + length
  + tempo + mute state; loop/jump/halt.
- **Globals**: 8 global slots; MIDI base channel, Map Editor (custom MIDI note→trig mapping),
  local control, program change, trig-in A/B, output routing, +Drive settings, snapshot manager.
- **MIDI/sysex**: per-track trig group / mute group sysex; pattern/kit/global/song sysex dump &
  receive (general, original-place, specific-place); sysex verify; sample manager (UW RAM/ROM
  sample receive, rename, erase, RAM→ROM copy); TurboMIDI speed negotiation protocol for fast
  bulk transfer. CC/NRPN: all SYNTH/EFFECTS/ROUTING params are MIDI-controllable per track
  (standard Elektron CC map, one bank of CCs per track via base channel + track offset).

## 1. Monomachine (MM) — data model

- **Kits**: 6 sound tracks, each with one assigned Mono-machine (synth engine) + track effects +
  routing/mix bus. Plus **6 MIDI sequencer tracks** (external gear) sharing LFOs 1:1 with their
  equally-numbered internal track. 12 tracks total for sequencing/arp purposes.
- **Patterns**: base length 16 steps ("one page"); **scale setup** extends to up to 64 steps across
  4 pages, all tracks share one scale length, plus a tempo multiplier (1x/2x/¾x/3⁄2x). Per-track
  **trig tracks**: three interlaced sub-tracks — AMP, FILTER, LFO — so a step can trig the
  amplitude envelope, filter envelope and LFO independently (ALL/AMP/FILTER/LFO trig-select
  modes), on top of the note/pitch grid. Swing and slide exist per pattern and per MIDI-seq track
  too (MIDI tracks lack trig tracks since MIDI has no envelope re-trig concept).
  **P-locks: max 62 individually locked parameters per pattern** (Monomachine's pool is 62 vs
  MD's 64), pooled across all params/tracks. Same grid/live-record mechanics as MD.
- **Songs**: song mode with per-row pattern/tempo/mute, similar structure to MD.
- **Globals**: MIDI uses up to 6 channels from base channel for internal tracks + another 6 for the
  MIDI sequencer tracks; Map Editor (multi map editor) for note-to-track/keyboard mapping;
  ASSIGN menu maps joystick / velocity / note-position tracking onto up to 2 chosen params per
  track (4–6 tabs).
- **Sysex**: same family as MD (pattern/kit/global/song dump+receive, status request/response,
  set/get individual param by sysex, set UW-style sample name doesn't apply — MM has no
  sampler — TurboMIDI speed handshake identical protocol).

## 2. Synthesis

### Machinedrum machine types & pages
TRX (analog-modelled, e.g. TRX-BD/SD/XT/CP/RS/CB/CH/OH/CY/MA/CL/XC), EFM (Enhanced
Feedback Modulation FM drums), E12 (12-bit "lo-fi classic" drums), P-I (Pi, physically-inspired),
GND (noise/tone generator, minimal params), MID (MIDI-only track, no synth params), CTR
(control machine — modulation-only, drives other tracks/LFOs, no audio output), ROM (fixed
sample playback), RAM (UW sample-loaded, user samples).

Each machine exposes **up to 8 SYNTHESIS params** (machine-specific, e.g. TRX-BD: PTCH,
DEC, RAMP, RDEC, DAMP, DIST, DTYP — 7 used of 8 slots; TRX-CH: GAP, DEC, HPF, LPF, MTAL).
Common shape: pitch/decay/damp + one or two character/distortion knobs — good graphical
targets are decay/damp as an **envelope-shape control**, DIST/DTYP as a **saturation curve**.

Every track additionally has a fixed **TRACK EFFECTS (TFX) page** (identical across all machines,
8-slot page shared with SYNTH via the same knob bank, switched by SYNTHESIS/EFFECTS/
ROUTING key):
- Amplitude modulator (tremolo): AMD (depth), AMF (freq)
- 1-band parametric EQ: EQF (center freq), EQG (gain, boost/cut)
- 24 dB resonant lo/hi/bandpass filter: FLTF (base cutoff), FLTW (gap width, hi/lo blend), FLTQ
  (resonance) — genuinely a graphical filter-response curve (freq/gap/resonance are 3 sliders
  that bend one visual curve)
- Sample-rate reducer: SRR

**ROUTING page** (per track): Distortion, Volume, Pan, Delay send, Reverb send, LFO control
(SPEED/DEPTH/SHMIX mirrored here so LFO can modulate its own destination visually).

**Master FX**: Rhythm Echo (sync delay), Gate Box reverb, Master EQ, Dynamix (dynamics/
compressor) — global, not per-track.

### Monomachine machine types & pages
SID (SID-6581 chip emulation), VO (VO-6 vocal/formant synth — spells words via consonant/
vowel trigs, CONS/CLEN/CVOL params), FM (FM+ multi-operator), SWAVE (SuperWave: Saw,
Pulse, Ensemble — analog-style unison stacks), DPRO (DigiPro: Wave, BeatBox, DoubleDraw,
Ensemble — digital/wavetable-ish), GND (ground/noise/sine — minimal generator, same role as
MD's GND), THRU (passthrough — routes external audio through the track's effects chain, no
synthesis).

Each machine has 4 fixed page groups, same knob bank reused:
- **AMP page**: Amplifier envelope (attack/decay/sustain/release-style knobs — prime envelope-
  curve UI target), Distortion, Track volume, Pan, Portamento
- **FILTER page**: basic filter cutoff/resonance/type, **Filter envelope** (separate ADSR driving
  cutoff — second envelope-curve UI target), Filter tracking (keyboard tracking amount)
- **EFFECTS page**: EQ, Sample-rate reduction, Delay (tempo-synced)
- **LFO pages** (3 of them, see §3)

Good candidates for graphical/curve editors: AMP envelope, FILTER envelope, FILTER
cutoff/resonance response curve, and all 3x LFO shapes (below).

## 3. Modulation

### Machinedrum
- **16 LFOs per kit** (one nominally per track, but re-routable). Each LFO: TRACK (destination
  track, can target another LFO's track for LFO-on-LFO chaining, only upward: LFO03 can modulate
  LFO04 but not vice versa), PARAM (any SYNTH/TFX/ROUTING param, or another LFO's
  SPEED/DEPTH/SHMIX), SHP1/SHP2 (6 waveforms: Triangle, Saw, Square, Linear decay,
  Exponential decay, Random — SHP2 = inverted set), SHMIX (crossfade between SHP1/SHP2 —
  natural **blend slider** between two curve thumbnails), UPDTE (FREE = free-running, TRIG =
  restarts on trig, HOLD = sample-and-hold on trig), SPEED (tempo-synced, 1/128-note steps),
  DEPTH. Same-destination LFOs sum. Copy/paste/clear/undo per LFO.
- No arpeggiator on MD (pure drum machine); "CTR" machines act as modulation-only sources
  driving other tracks — effectively a manual mod-matrix node.
- Mute/trig/accent "groups": SYSEX exposes explicit **trig group** and **mute group** commands
  (one track's trig/mute can be slaved to another) — a natural drag-a-cord candidate distinct from
  LFO routing.

### Monomachine
- **3 LFOs per track** ("interlaced"), each identical in capability: same TRACK/PARAM/waveform
  model as MD but per-track scoped (a track's 3 LFOs can target any param on that track, or chain
  onto each other). LFO trig can be **sequenced independently** via the track's own LFO trig-track
  (see §1), i.e. LFO restart timing is drawable on the step grid, not just global UPDTE modes.
- **Arpeggiator**: one per track — **12 total** (6 synth + 6 MIDI-seq tracks). Params: SPD (tempo-
  synced speed), MODE (OFF/KEY/SID/ADD — differing note-retention semantics, worth a small
  state-machine diagram in UI), PLAY (TRUE/UP/DOWN/CYCLE/± order), plus envelope-trig
  switches and a rhythm/offset track (its own mini step-sequencer for which arp step fires) —
  strong candidate for a **piano-roll-like arp lane** with its own trig row, separate from the main
  note grid.
- **POLY mode**: dedicates all 6 synth tracks to one polyphonic voice (chords), disabling
  individual-track sequencing — a mode toggle, not a routing concept.
- **ASSIGN**: joystick / velocity / note-position → up to 2 params per track — a lightweight
  always-on mod matrix, good fit for small drag-cord UI distinct from the LFOs.

## 4. Sequencer specifics

| | Machinedrum | Monomachine |
|---|---|---|
| Tracks | 16 (drum voices) | 6 synth + 6 MIDI-seq (12 total) |
| Max steps/pattern | 64 (16/32/48/64 selectable), 4 pages | 64 (4×16 pages), scale shared by all tracks |
| Trig granularity | 1 trig row + accent/swing/slide overlay grids | note/pitch grid + 3 trig tracks (AMP/FILTER/LFO) + swing/slide overlays |
| Retrig | via LFO UPDTE=TRIG or CTR machines; no dedicated "retrig" step type | arpeggiator provides retrig-like note repeats; no dedicated retrig step type either |
| Accent | 0–15 depth, per-pattern default ALL or per-track boolean | not present as MD-style global accent; dynamics instead via velocity/AMP-trig envelope re-fire |
| Slide | boolean overlay grid; slides only between two p-locked values, speed = tempo-relative, always resolves by next trig | same slide concept, plus portamento (AMP page) for continuous pitch glide |
| Swing | 1–80%, per-pattern or per-track, same % shared across all swing tracks | per-pattern/per-track, same mechanic |
| P-lock cap | 64/pattern (pooled) | 62/pattern (pooled) |
| Copy/paste | note, track, track-page, pattern (+kit assoc in EXTENDED) — each undoable | same granularity, same undo model |
| Chaining/Song | 32 songs, 256 steps, per-row pattern+tempo+length+mute | same structural pattern (per-row settings), song editor can also override scale/length per row |
| Tempo | global BPM + tap tempo + external sync + per-pattern multiplier via scale setup | global BPM + per-pattern 1x/2x/¾x/3⁄2x multiplier |

## 5. Performance features

- **Mutes**: both machines have a dedicated MUTE window (grid of track squares, held-key
  "prepare then release to commit" gesture) that shrinks to a small HUD when a knob is touched —
  worth replicating as a persistent corner widget, not just a modal.
- **Kit reload / snapshot**: MD +Drive Snapshot Manager and MM's kit-per-pattern link mean
  kits can be swapped live; UI should treat "kit" as a hot-swappable preset bound to the current
  pattern, with a clear "unsaved kit changes" indicator (both manuals warn changes are lost unless
  explicitly saved).
- **Sampling (MD UW only)**: ROM vs RAM machine distinction, sample manager (receive/rename/
  erase/RAM→ROM copy), sample banks — maps to an asset browser + "promote sample to ROM"
  action; not present on Monomachine (no sampler).
- **MM keyboard/Map mode**: Multi Trig mode (all 12 tracks triggerable from one key bank) and
  Multi Map mode (custom per-key mapping across tracks) — good fit for a virtual-keyboard
  workspace with a visual key→track/machine map editor.
- **MD Classic vs Extended**: Classic = 16-step, no p-locks effective; Extended = up to 64 steps
  with p-locks live. This is a real functional mode switch, not cosmetic — UI must show which mode
  a pattern is in since Classic patterns can silently carry dormant p-lock data.

## 6. Hardware control → screen-native UX mapping

| Hardware control / workflow | Proposed UX element | Constraints to respect |
|---|---|---|
| 16 (MD) / 12 (MM) hardware TRIG keys, one pattern page at a time | Always-visible multi-track step grid, all tracks stacked vertically, page tabs only for >16 steps | MD scale = 16/32/48/64; MM = up to 64 in 16-step pages — grid must scroll/paginate at 16-step boundaries, not arbitrary widths |
| Hold-TRIG-and-turn-knob p-lock gesture | Click a step, then drag/type into an inline parameter panel; locked params shown as filled dots on the step | Enforce the 64 (MD) / 62 (MM) pooled-lock ceiling per pattern with a live counter/warning, since it's a shared pool not per-track |
| SYNTH/EFFECTS/ROUTING page-select key (3-way toggle reused for both machine params and TFX) | Tabbed parameter panel per track (Synth / Amp / Filter / Effects / LFO tabs) instead of hardware paging | Each page is always exactly 8 knob-slots on hardware — keep 8 as the visual "page" quota even if the UI has room for more, so muscle memory/manual language ("page") still maps |
| Per-machine 8 knobs (PTCH/DEC/DAMP/DIST…) | Graphical envelope/curve editors where the param cluster is literally an envelope (MM AMP/FILTER envelope, MD DEC+DAMP) or a filter response (FLTF/FLTW/FLTQ) rather than 3 independent sliders | Curve editor must still resolve to the same 3 discrete knob values on save — don't invent continuous curve shapes the hardware can't encode |
| LFO SHP1/SHP2/SHMIX/SPEED/DEPTH/UPDTE knobs | Drag-and-drop mod routing: LFO node with shape-picker (6 waveform icons + blend slider) and a visible cord to its TRACK/PARAM destination | MD: 16 LFOs, only upward-chaining (LFOn→LFOn+1, never reverse) — cord UI must forbid/flag backward LFO chains; MM: 3 LFOs/track, freely chainable within/around the track |
| Trig group / mute group sysex (MD) | Drag a cord between two track headers to declare "track A triggers/mutes track B" | Sysex-level feature, not per-step — represent as a track-level relationship, not a per-cell lock |
| MM 3 trig tracks (AMP/FILTER/LFO) selected via TRIG SELECT key | 3 stacked mini-lanes under each track's main note row (like automation lanes), toggle which envelope/LFO restarts per step | Must stay visually subordinate to the note row — hardware treats them as sub-tracks of one track, not independent tracks |
| Accent/Swing/Slide windows (hold FUNCTION+key, overlay grid) | Drawn automation-style overlay lanes toggled on/off per track, always reachable without a modal (unlike hardware's transient overlay) | Same pooled ALL-vs-per-track percentage rule for swing (one % shared by whichever tracks are swung) — don't let the UI imply independent per-track swing amounts |
| Arpeggiator (MM) MODE/PLAY/rhythm-offset track | Dedicated "Arp" mini-lane per track with a state-machine diagram (OFF/KEY/SID/ADD) and its own step row for rhythm/offset | Keep as a contextual workspace panel, not merged into the main grid — it's conceptually a note post-processor, not a trig type |
| ASSIGN (joystick/velocity/note-pos → params) | Small always-on mod-matrix widget (2 slots) separate from the 3-LFO patch bay | Hardware caps this at exactly 2 destinations per track — don't let the visual matrix imply more |
| KIT load/save/snapshot, +Drive snapshot manager | Preset browser + explicit "dirty kit" badge tied to the current pattern | Kit changes are silently lost on kit switch unless saved — UI must surface this loudly, mirroring the manual's repeated warning |
| MULTI TRIG / MULTI MAP (MM keyboard mode) | A "Keyboard" workspace: virtual keys with an editable key→track/machine map overlay | Multi Trig retains each track's own arpeggiator settings — don't collapse arps into one global arp when this mode is active |
| Classic vs Extended pattern mode (MD) | Explicit per-pattern mode badge; p-locks greyed out (not deleted) when in Classic | Locks persist silently in Classic mode and reactivate in Extended — never let the UI imply data loss when toggling |
| Global workspaces (kit edit vs pattern edit vs song vs mixer) implied by hardware's FUNCTION-modifier key layers | Contextual workspace tabs: Sound Design (machine+TFX+LFO), Sequencing (step grid+trigs+arp), Mix (routing/pan/send/master FX), Song (chain editor) | Keep the same data visible across workspaces (e.g. p-lock dots) so switching workspace never hides state the user forgot about |

---

Both PDFs saved at:
`doc/manuals/machinedrum_manual_OS1.63.pdf` (local, not committed)
`doc/manuals/monomachine_manual_OS1.32.pdf` (local, not committed)
