# Design proposal: the controller configurator (CONTROL workspace)

- Branch `feat/tr06-profile`, 2026-09-30. A proposal and a prototype, not built. It builds on [DESIGN-tr06.md](DESIGN-tr06.md), which stays the authority for what is built today.
- **Prototype:** [mockup/controller-prototype.html](mockup/controller-prototype.html). It is one file with fake data and no host, and needs no sibling files.
  - It opens over http or from `file://`. Fonts come from the mockups' Google Fonts link; offline it falls back to system fonts.
  - The strip at the bottom (PROTOTYPE) is not part of the design. It plugs and unplugs devices, switches the machine (MM or MD) and the DAW case, and plays the hardware. It also switches v2 / v3 / v4. To play the hardware in v4, drag across a row label (a pad row is hit on the first move); in v3, drag across a cell (a pad cell is hit on the first move); in v2, drag a row's meter or click a pad row's square.
- **The feedback it answers:**
  - Centre the device tiles. Show a device only while it is connected, and follow hot-plug.
  - Map through a control matrix, not a table of CC rows and not a drawing of the device (second round: "a beautiful control matrix"). The matrix gives ranges, inverse, macros, randomise, knob mode, track pinning and steady live feedback.
  - Use the same configurator for every MIDI input, with the TR-06 as a device that has a built-in profile. It replaces the old MIDI Learn matrix.

## 000. Current proposal: v5, tracks × controls × parameters, everything a pin

The prototype opens on v5. The **v2 / v3 / v4 / v5** keys in the prototype bar switch between the versions. Sections 1 and 3-7 apply to all of them.

### Layout

- **One header line:** ‹ DEVICES, the device, PROFILE, CH, KNOBS, MOVES (the page's selected track), **Find a parameter** (a search box that filters the rows by short name, long name or page), **USED ONLY** (one toggle, off by default), SELECT ON TOUCH, RESET.
- **Left: the tracks**, a vertical list.
  - **FOLLOW SELECTED** comes first ("T1 · SWAVE-SAW"): the pins that follow the page's selection, together with those pinned to that track.
  - Then T1-T6 (MD: T1-T16, compact), each with its machine and a pin count.
  - Each track also shows, read-only, which pads play it ("T6 FX-REVERB · OH CH"). This is derived from the pins; there is no separate voice editor.
  - Picking a track shows that track's routing. This replaces v4's TO tabs and the SELECTED / "T1 · SEL" confusion.
- **Across the top: MIDI IN**, the device's controls as columns under group brackets.
  - TR-06: NOTES (BD 36, SD 38 … CH 42), LEVEL (AC … CH), EFFECT (DRIVE, TIME, DEPTH), TUNE, DECAY, TONE, COMP, DELAY SEND, GLOBAL, then EDITOR (RANDOM, APP LFO). That is 50 columns.
  - Generic devices: NOTES (the pads seen), the knobs seen, "+LEARN", EDITOR.
  - Each column head has a cap (M n for a macro, R randomise, A amount), a vertical label, the CC (or the note) and a thin steady meter.
- **Down the side: the chosen track's parameters**, grouped by page with horizontal, readable names ("UNIL unison level", "BASE base frequency").
  - PLAY (MM "PLAY NOTE") or TRIG (MD) comes first. Then MM: SYN, AMP, FILT, EFX, LFO 1-3, TRACK (LEVEL, NOTE), 59 rows. MD: SYN, FX, ROUTE, TRACK, 27 rows.
  - An unused SYN slot says "unused on SWAVE-SAW" and cannot take a pin.
- **The board** is on the page's own plate, as in v4b, with the same pins and a crosshair hover.
- **Right: the settings panel** (280 px):
  - **nothing selected:** a one-line summary, three how-to lines and a compact pin key;
  - **a control:** "BD LEVEL selected — click a parameter row to connect it", its readout, type, knob mode, its pins as a list (or its randomise set with amount and ROLL);
  - **a pin:** source → destination, readout, parameter, two-handle range, INV, track, remove, knob mode, the macro's other pins;
  - **a PLAY pin:** note (MM, ‹ › and octave steps, "Follow ACC → NOTE"), track, remove.
  - Below 1180 px the panel becomes a drawer.
- **Nothing is hidden by default** (Radek: "the matrix should show all options").
  - All 50 columns and all rows of the chosen track are there, every socket visible.
  - Groups can be collapsed by hand (a click on a bracket or page tag), but everything starts expanded. USED ONLY is optional and off.

### One model: everything is a pin

- The voices are no longer a separate mapping. A note is a source like a knob: the **NOTES** columns.
- **Note × PLAY/TRIG row:** "this pad plays (MM) or triggers (MD) this track". The defaults reproduce the shipped profile: BD → T1 … CY → T5, OH and CH → T6, with OH at C5 (MD: BD → T1 … CH → T7).
- **Layering is free:** pin one pad on two tracks' PLAY rows. The track list's counts and "played by" hints show it.
- **Velocity as a source:** a note column pinned to a parameter row means the pad's velocity drives that parameter across its range. The example is BD's velocity → T1 AMP VOL 40-127 (MD: VOL). It is the same pin; the panel says "velocity".
- One gesture (a pin) now covers triggering, layering, velocity, knobs, macros and randomise. The data contract gets simpler too: a voice row `{voice, t, note}` becomes a control `{src: {note}, targets: [{t, pg: "play", note}]}` (the `PLAY` target), and the old voices table migrates into that.

### Touch to map

- Turn a TR-06 knob (with SELECT ON TOUCH) or click a column head. The column is selected and highlighted, and the panel says what to do.
- A click on a **row name** (or on any socket in that row) connects it. A second row adds a pin, making a macro. Escape ends.
- The matrix stays the overview: touching a control never hides or collapses anything.

### Measured (headless Chrome, the prototype bar included)

- **1500 x 900, MM TR-06, everything expanded** (50 columns x 59 rows):
  - the page `scrollHeight` is 900 = viewport, and the board's `scrollWidth` is 968 = its `clientWidth`;
  - the board's content is **932 px high in a 630 px board**, so the board scrolls internally (column heads stay on top, and the row names, track list, header and panel stay fixed).
  - 59 rows at a 14 px pitch plus the column heads do not fit the 630 px left under the app's own top bar and the monitor. The full board would need rows about 9 px high.
- **1500 x 900, MD** (50 x 27): fits entirely (board 630 in 630).
- **Search "dec":** 2 rows.
- **1000 x 900:** page `scrollHeight` 1009 (the header wraps, the prototype bar takes two lines). The board is 968 wide in 786 and scrolls sideways with sticky row names; the panel is a drawer.

### Self-critique of v5 (what is still confusing, what next)

- **Still confusing:**
  - FOLLOW SELECTED shows two kinds of pins, the ones that follow the selection and the ones pinned to that track (with the small square). That is right, but it is subtle. A legend line at the top of the rows would help.
  - MOVES in the header and FOLLOW SELECTED in the track list both name the selected track. One of them should go (keep FOLLOW SELECTED, let the page's own track selection drive it).
  - 50 narrow columns at a 16 px pitch are dense. The column labels are 9.5 px vertical text, readable but at the limit.
  - A pin's range is still only visible on hover or in the panel (v3's strength).
  - The dark track keys are heavy next to the light board.
- **Next:**
  - make the track list lighter (plate-coloured rows, a lit LED for the chosen one);
  - a "rows with pins first" sort as an alternative to USED ONLY;
  - a compact range tick inside a pin's socket;
  - try the MD's 16 tracks with the list folding to two columns;
  - test touch-to-map with the real TR-06 (whether one knob sends one CC per layer, as the chart suggests).

### v4b (kept on the switch as v4)

The v4 board moved to the page's own plate (light on the MM, the MD's own theme), with recessed sockets and pins in the same encoding. The popovers became a fixed settings panel on the right. The pitch is 18 px, so all 58 MM destinations fit at 1500 px without scrolling.

## 00. v4, the pin matrix (kept for comparison)

**Why a v4:** macros run out of room in v3's cells, because three stacked chips already fill one. A synth's modulation pin matrix (Reason Malström / Thor style) has unlimited connections per source and shows the whole routing at once.

- **One header line**, as in v3 (with the MOVES track chip). Under it, one line:
  - **TO: [SELECTED T1 SW-SAW n] [T1 n] … [T6 n]**, the destination track. SELECTED is the default and follows the selected track. A Tn tab shows and edits the connections pinned to that track. Each tab carries a count of its pins (red when non-zero), so pinned routings on other tracks are never invisible.
  - **USED ONLY: SOURCES / DESTINATIONS**, one filter per axis;
  - EXPAND ALL / COLLAPSE ALL.
- **Rows are sources:** the device's controls in collapsible groups.
  - TR-06: INSTRUMENT LEVEL (AC-CH), EFFECT (DRIVE, TIME, DEPTH), TUNE, DECAY, TONE, COMP, DELAY SEND, GLOBAL. Generic devices: the controls seen, with a "+ LEARN A CONTROL" row.
  - Two editor sources at the bottom: **RANDOM** (with a ROLL key; its pins are a randomise set) and **APP LFO** (static here: nothing animates).
  - A row label shows the name, the CC and the incoming value, a thin steady meter along its bottom edge, and a tag: **MACRO n** (two or more pins), **RND** or **AMT**.
  - Secondary groups start collapsed (TUNE, DECAY, TONE, COMP, DELAY SEND). A collapsed group's line still shows a small dot under every column its members connect to, so hidden routing is visible.
- **Columns are destinations:** the destination track's machine's parameters with their real short names, as vertical labels under page brackets.
  - MM: SYN (UNIL UNIW … TUNE; an unused slot is dimmed and cannot take a pin), AMP, FILT, EFX, LFO 1-3, TRACK (LEVEL, NOTE): 58 columns.
  - MD: SYN (PTCH DEC …), FX, ROUTE, TRACK: 26 columns.
  - The names follow the track: on the SELECTED tab they change with the selected track's machine.
- **The board** is a dark panel of 13 px sockets, in both plates.
  - Hovering lights a crosshair (the row, and the column including its label).
  - A pin's look encodes its settings:
    - **full red** = full range;
    - **red ring with a centre** = a limited range;
    - **a bar across the pin** = inverse;
    - **green** = in a randomise set;
    - **a small square at the corner** = pinned to a track (on a Tn tab).
  - Each pin's tooltip gives the parameter, the range, inverse and the value now.
- **Editing:**
  - A click on an empty socket connects it (full range, following the tab's track) and opens an anchored popover. It has the parameter picker, the two-handle range, INV, remove, the track (follow or pin, which moves the pin to that tab), and the row's knob-mode override.
  - A click on a lit pin opens its popover. **Shift- or alt-click** removes a pin at once.
  - On a RANDOMISE or AMOUNT row (and on RANDOM), a click adds the parameter to, or removes it from, the shared set: green pins.
  - A click on a row label opens the row's settings: type, knob mode, targets, or the randomise set with amount and ROLL.
  - Escape or a click outside closes the popover.
- **Voices** are not parameters, so they sit in one compact strip under the board (`BD 36 [T1 C4]` …). A click opens the voice popover (track pads, and the MM note steps).
- **Live:** the row that moved last stays lit (a warm label, a red meter, a thin ring round its pins) until another moves. The popover's readout and NOW marker update in place. Nothing blinks.
- **Measured** (headless Chrome, the prototype bar included):
  - 1500 x 900 on the MM TR-06 (21 rows open, 58 columns): `scrollHeight` 900 = viewport, and the board's `scrollWidth` 1468 = its `clientWidth` (no scroll either way). The same holds with a popover open, on MD (26 columns), on the BeatStep and with USED ONLY on.
  - 1000 x 900: `scrollHeight` 1027, because the header wraps RESET and the prototype bar wraps to two lines. The board scrolls sideways (1382 in 968) with the row labels sticky.

### v3 against v4, honestly

- **v4 loses the numbers at a glance.** A v3 cell shows each parameter's name, value and range bar. A v4 pin shows only that a connection exists and whether it is full, limited, inverse or randomise. The range needs a hover or a click.
- **v4 loses the TR-06-shaped layout.** v3's rows and columns mirror the TR-06's own chart (instrument x function); v4 lists the controls as rows.
- **v4 loses the whole picture of a pinned macro.** A macro that mixes SELECTED and pinned tracks spreads over TO tabs. The tab counts and the MACRO n tag say so, but you switch tabs to see it.
- **v4 gains unlimited macros:** a row takes as many pins as it has columns.
- **v4 gains the whole routing of a track on one board:** every source against every destination, collapsed groups included (their summary dots).
- **v4 gains room for many destinations:** 58 MM destinations fit at 1500 px without a scroll.
- **v4 gains one gesture for everything:** connecting, disconnecting (shift-click) and randomise membership are all a click on a dot.
- **v4 gains uniformity:** one layout for the TR-06, any controller and the editor's own sources (RANDOM, APP LFO).

## 0. v3, the instrument grid (one page), kept for comparison

The **v2 / v3 / v4** keys in the prototype bar switch between the three.

### What was wrong with v2 (self-critique)

1. **SELECTED and "T1 · SEL" were two columns for the same track**, which is redundant and confusing.
2. **The track matrix was about 90% empty.** Almost every mapping goes to the selected track, so 6-16 track columns wasted width and forced vertical scrolling. Tracks are the wrong axis for the main grid: pinning is the exception.
3. **Rows were about 60 px** (name, meter and badges), so one group filled the screen.
4. **Five chrome rows before any data:** the device bar, the track bar, the controls header, the filters, the column header. The selected-track bar also duplicated the page's own track selection.
5. **The permanent right-hand inspector duplicated the cell**, and its large LCD took space.

### v3

- **One header line:**
  - ‹ DEVICES, the device, PROFILE (or MAPPING) with its LED, CH, KNOBS;
  - **MOVES [T1 · SWAVE-SAW ▾]**, a small key-style chip for the page's selected track. It is what "selected" means, and it can be changed here without a second track bar;
  - SELECT ON TOUCH, RESET.
- **The TR-06 grid mirrors its own CC chart** instead of drawing the device.
  - **Columns** are the instruments AC, BD, SD, LT, HT, CY, OH, CH, then EFFECT (DRIVE, TIME, DEPTH) and GLOBAL (MIX IN, PROB), under the page's bracket labels (INSTRUMENT, EFFECT, GLOBAL).
  - **Rows** are the functions: VOICE (the notes to a track: the old voices matrix folded into one row, "NOTE 36·35 → T1 C4"), LEVEL (for EFFECT and GLOBAL: the knob), TUNE, DECAY, TONE (BD attack, SD snappy, LT and HT color), COMP, DELAY SEND (and MIX IN's send). Each row says how the hardware reaches it (MENU + knob, STEP LOOP + DEPTH).
  - A slot the TR-06 has no CC for is hatched.
  - **Each cell is one CC** (about 58 px high):
    - the CC number, a knob-mode tag only when it differs from the device's, and the incoming value;
    - a thin live meter along the bottom edge;
    - its parameter chips: the machine's short name, the resulting value, and a 2 px range bar (dashed = inverse, red tick = value now);
    - a dark **T2** tag only when a chip is pinned to a track other than the selected one.
    - A macro stacks 2-3 mini chips with a rust edge ("+n more" beyond three). Randomise and amount cells show the dashed dice chip ("Timbre 35%", "ROLL").
  - Seven rows, the header and the monitor fit on one page.
- **Editing is an anchored popover**, not a permanent panel. A click on a cell opens it next to the cell; Escape or a click outside closes it. It holds:
  - the name, CC and channel, and a slim IN / OUT readout;
  - TYPE (Off, Direct, Macro, Randomise, Rnd amount), and KNOB with a per-control override and DEFAULT;
  - a card per parameter: picker, two-handle range, INV, remove, track (follow or pin);
  - "+ Add a parameter (macro)";
  - for randomise: the set, its track, AMOUNT, around now or anywhere, the parameter chips, ROLL NOW and the last roll.
  - A VOICE cell's popover has the track pads and, on the MM, the note steps.
  - With SELECT ON TOUCH, moving another control while the popover is open moves the popover to it.
- **Generic devices** (BeatStep, Host MIDI in, Any MIDI input) use the same cells and popover, auto-flowed in a responsive grid: KNOBS AND FADERS, PADS AND KEYS, and a "+ LEARN" cell. A control appears the first time it sends.
- **MD:** the same grid. The voices go to TRIG n, and pins and the MOVES chip offer T1-T16.
- **Live feedback:** the cell that moved last keeps a steady warm fill and red values until another moves. The voice that played last is lit the same way. Nothing blinks.
- The MIDI monitor stays as a one-line strip ("MIDI IN") at the bottom. The device tiles, the empty state and REFRESH are unchanged.
- **Measured** (headless Chrome, 1500 x 900 viewport, the prototype bar included): the TR-06 page's `scrollHeight` is 900 on both MM and MD, with or without a popover open, so there is no vertical scroll.
  - At 1000 x 900, `scrollHeight` is 929. RESET wraps to a second header line, so the page scrolls by 29 px.
  - At 1000 px the grid scrolls sideways (at least 1200 px wide, EFFECT and GLOBAL are off to the right), and the popover places itself where there is room.

## 1. The devices (the CONTROL workspace's first view)

- The tiles are **centred** in the workspace, horizontally and vertically, with REFRESH and AUDIO / MIDI… under them.
- **Only connected devices get a tile.** Today a TR-06 tile saying NOT CONNECTED appears while the profile is on. It goes. A MIDI Learn tile saying NO MIDI INPUT FOUND also goes. The mapping of a device that is unplugged is kept, and it comes back when the device is plugged in again.
  - A device that is connected but not enabled in AUDIO / MIDI is shown dimmed, reading CONNECTED · NOT ENABLED. Its view offers ENABLE (open question 1).
  - **Any MIDI input** is a virtual tile (dashed border). It appears only while learned mappings exist (see **Migration**).
- **The empty state:** when nothing is connected, the tiles give way to a panel:
  - "No MIDI controller connected";
  - three steps: connect by USB (a TR-06 needs Roland's driver), turn it on and it appears by itself, else AUDIO / MIDI or REFRESH;
  - a line saying the learned mappings still work from any input.
- **Hot-plug.** `ControllerProfile` holds a `juce::MidiDeviceListConnection`, made in its constructor. The vendored JUCE is 7.0.10 and has it. Its callback runs on the message thread and calls `lookAtInputs()`; a change publishes the document. The connection disconnects itself when it is destroyed.
  - The one-second look stays, because enabling or disabling an input in AUDIO / MIDI changes no device list. `AudioMidiLink` already listens to the device manager and can trigger it too.
  - **REFRESH** is a new command, `ctlRefresh`, that runs the same look at once. It is the fallback for a system that misses a notification.
  - A plug or unplug shows the page's toast ("TR-06 connected", "… disconnected · its mapping is kept"). Nothing blinks.
- **DAW:** the host owns MIDI and names no device. The tiles are **Host MIDI in**, plus Any MIDI input when learned mappings exist. Host MIDI in opens the generic matrix, where a PROFILE choice ("Roland TR-06") gives it the TR-06's rows, names and defaults. There is no hot-plug in a DAW.

## 2. v2 (kept for comparison): one track matrix for every device

Every tile opens the same view, top to bottom:

- **The bar:** ‹ DEVICES, the device's name, then:
  - PROFILE (TR-06) or MAPPING (other devices) ON/OFF, with its LED. Off means the device's MIDI reaches the machine as ordinary MIDI, as today;
  - CHANNEL;
  - KNOBS (the device's default knob mode);
  - SELECT ON TOUCH;
  - RESET TO DEFAULTS (profiles only).
- **SELECTED TRACK:** the page's selected track, as pads (MM: 6, MD: 16) with each track's machine. In the product the page's own track selection drives it. The strip is there so the prototype can show names and the SELECTED column changing.
- **Left: the control matrix**, then (TR-06) the voices matrix.
- **Right: the inspector.** It is a side panel from 1180 px up. Narrower, it is a drawer from the right with CLOSE (and Escape), opened by a click in the matrix.
- **Bottom: the MIDI monitor**, reduced to one LCD strip: the last 8 messages, newest first, each as `CH10 CC 24 90 · BD LEVEL → UNIL 97`. A message that does nothing is shown dim. CLEAR empties it.

### The control matrix

- **Rows are the device's controls.**
  - **With a profile (the TR-06)**, rows are named from the chart and grouped:
    - INSTRUMENT LEVEL (ACC, BD … CH LEVEL);
    - EFFECT (DRIVE, DELAY TIME, DELAY DEPTH);
    - TUNE, DECAY, TONE (BD ATTACK, SD SNAPPY, LT/HT COLOR), COMP;
    - DELAY SEND (the sends, and MIX IN's);
    - OTHER (MIX IN LVL, MASTER PROB).
    - Controls that send no MIDI (VOLUME, TEMPO, INSTRUMENT, MODE, VALUE, MENU, STEP LOOP, the steps) are not rows; a line under the matrix says so. RUN / STOP is Start / Stop, passed on by the machine's sync settings.
  - **Without a profile**, rows are the controls seen, grouped as KNOBS AND FADERS and PADS AND KEYS. A CC or a note becomes a row the first time it arrives (a toast says so).
    - "+ LEARN A CONTROL" turns the next control moved into a row and selects it.
    - Rows can be renamed ("Knob 1").
  - **Any MIDI input** has one group: the controls learned with MIDI Learn.
- **A row head** holds:
  - the name and its CC or note;
  - a live meter: a thin bar with its value (for a pad, a square and its velocity);
  - the knob mode badge (REL / ABS / ENC; outlined dim is the device default, solid is the row's own);
  - the row type with a small colour mark: OFF, DIRECT, MACRO n (rust), RANDOMISE and RND AMOUNT (green).
  - A click on the row head opens the row's settings.
- **Columns are destinations.**
  - **SELECTED** comes first. It follows the page's selected track, and its head shows the track now ("T1 · SWAVE-SAW").
  - Then T1-T6 (MM) or T1-T16 (MD), headed with the number and the machine. The column of the selected track says "· SEL".
  - A mapping in a track column is pinned to that track.
  - The row heads and the SELECTED column stay put (sticky) while the MD's 16 columns scroll sideways.
- **Cells:**
  - An empty cell shows a quiet "+" on hover.
  - A mapped cell holds a chip per parameter:
    - the parameter's short name, which is the machine's own ("UNIL", "PTCH", "BASE"), with "—" for a slot the machine does not use, dimmed;
    - the live resulting value;
    - a thin range bar: min to max filled, dashed when inverse, with a red tick at the value now.
  - A row with several chips is a macro. Its chips share the row's rust accent on the left edge, wherever they sit: DRIVE moves SELECTED BASE and DIST and T2's SRR.
  - A randomise or amount row shows its set as a dashed dice chip ("Timbre · 6P", or "Timbre 35%") in the set's column, and nothing elsewhere.
- **Calm by construction:**
  - an aligned grid with generous rows;
  - the pages' tokens, on both plates;
  - a hover crosshair (the row and the column under the pointer are lit, softly);
  - groups that collapse (TONE, COMP, DECAY and DELAY SEND start collapsed, each saying "n of m mapped"; EXPAND ALL / COLLAPSE ALL);
  - a **MAPPED ONLY** filter.

### The voices matrix (the TR-06)

- A second, smaller matrix. Rows are the voices (BD … CH with their notes); columns are the tracks.
- There is one dot per row, like a radio button, in the track the voice plays. A click on another cell moves it.
- On the MM the dot carries the note ("C4"), and the row head has ‹ › semitone steps. The PLAYS column spells out where the voice goes (`T1 · CH 1 · C4`, MD `T1 · TRIG 1`).
- The voice that played last has its row lit steadily.

### The inspector

- **The readout**, in LCD style:
  - IN: the last value, and LAST MOVED when this was the last control to move;
  - OUT: every target's result, `T1 BASE 110 · T1 DIST 80 · T2 SRR 69`.
- **A cell** ("DRIVE → Selected track (T1 now)") holds a card per parameter in that cell:
  - **Parameter:** a key-style list grouped as the page groups them. MM: SYN (slot and the machine's own name, "SYN A · UNIL"), AMP, FLT, EFX, OTHER (LEVEL, NOTE). MD: SYN (numbered, with names), FX, ROUTE, OTHER.
  - **INV:** inverse. The bar turns dashed, and its labels read KNOB MAX → … ← KNOB MIN.
  - **×** removes the parameter.
  - **The two-handle range bar**, with the value now as a red marker, updated in place.
  - **TRACK:** move the chip to another column (Selected track, or a pinned track).
  - Under the cards, "Choose a parameter" / "+ Another parameter here" adds one. A second parameter anywhere in the row makes the row a macro.
  - ROW SETTINGS › leads to the row.
- **A row** (a click on its head):
  - **TYPE:** OFF, DIRECT, MACRO, RANDOMISE, RND AMOUNT (a pad has no RND AMOUNT);
  - **KNOB:** RELATIVE, ABSOLUTE, and for devices without a profile ENCODER (±), with "the device's default" or "own setting · DEVICE DEFAULT";
  - a direct or macro row lists its mappings ("SELECTED · BASE 20-110 ›", "T2 · SRR 60-127 INV ›"), each opening its cell;
  - a RANDOMISE or RND AMOUNT row edits its set:
    - the set (key-style list, "+ New set", RENAME) and its track;
    - AMOUNT on the page's 16-LED segment bar;
    - AROUND NOW or ANYWHERE;
    - the set's parameters as toggle chips per page, unused slots disabled;
    - ROLL NOW, and the last roll written out (`UNIL 67→36 · SUBX 15→0 …`).
  - A set is shared by every row that uses it: a TR-06 knob can set its amount while a BeatStep pad rolls it.

### Live feedback (no flashing)

- The row that moved last stays lit, with a pale warm row, a red left edge, a red meter and red values, until another row moves. There is no timer, no blink and no animation.
- Values change in place: the meters, the chips' values and ticks, the readout, the inspector's NOW markers and the monitor strip. The inspector's lists are not redrawn under an open dropdown or a drag.
- The product keeps today's rates: the page document at most about 15 times a second while watching, the knobs through `KnobPump` at most one round per 50 ms.
- **SELECT ON TOUCH** (on by default): moving a control on the device selects its row, and opens its collapsed group. Off, the inspector stays on what was clicked, and only the lit row moves.

## 3. Behaviour

- **Knob mode: per control, with a device default.**
  - The mode belongs to the physical control. A device can mix fixed-position pots (relative is right) and endless encoders (they need ENCODER).
  - A profile sets the default (the TR-06: relative). A generic device starts absolute, which is what MIDI Learn did. Nearly everyone changes only the default.
  - The rules per mode:
    - **Absolute:** `v = lo + (hi - lo) * f`, where `f = in / 127`, or `1 - f` inverted.
    - **Relative** (today's `RelativeKnobs`, per CC): the first value after a start, a track change, a mapping or mode change, or 2 s idle only sets the reference. After that, `v += (in - prev) * (hi - lo) / 127`, negated when inverted.
    - **Encoder:** `v += in - 64`, negated when inverted.
  - The result is always held inside `[lo, hi]`. A value outside the range is pulled in on the first move.
- **A macro** is a control with more than one target. Each target moves in its own range and direction. They all move in the same pump round, so a macro turn is one gesture and **one undo step**, as Control All is (`KnobPump` gestures).
- **Targets follow the selected track** (`t: "sel"`) unless pinned (`t: 0-15`). A pinned target ignores the selection, so one knob can drive track 6's AMP REL while the others follow the selection.
- **Randomise sets:**
  - AROUND NOW: each value moves by up to ±amount × 64 from where it is.
  - ANYWHERE: each value is blended towards a random point by the amount.
  - A roll is one round of edits through the same pump, and **one undo step**.
  - Triggers: a pad or note press, or a knob crossing half way upwards (RANDOMISE); a knob's position sets the amount 0-100% (RND AMOUNT); the ROLL NOW key.
- **Voices** stay as they are (DESIGN-tr06.md): notes go to tracks, and on the MM the note is kept too.
- **Per device, per editor:** the MD Editor and the MM Editor each keep their own mappings. Each device's mapping is kept by device name (see open question 2).

## 4. Data contract sketch (`desk/controller`, version 2)

The setup is kept, as now, in the MDCT chunk and written only once the user changes something. Version 2 generalises version 1 from "the TR-06 profile" to "devices".

```json
{
  "schema": "desk/controller", "version": 2, "machine": "mm",
  "devices": [
    { "match": "TR-06", "profile": "tr06", "on": true, "channel": 10, "knobMode": "relative",
      "voices": [{ "voice": "BD", "t": 0, "note": 60 }],
      "controls": [
        { "src": { "cc": 24 }, "targets": [{ "t": "sel", "pg": 0, "i": 0, "lo": 0, "hi": 127, "inv": false }] },
        { "src": { "cc": 17 }, "targets": [
            { "t": "sel", "pg": 2, "i": 0, "lo": 20, "hi": 110 },
            { "t": "sel", "pg": 1, "i": 4, "lo": 0, "hi": 80 },
            { "t": 1, "pg": 3, "i": 2, "lo": 60, "hi": 127, "inv": true }] },
        { "src": { "cc": 80 }, "targets": [{ "t": 5, "pg": 1, "i": 3 }] },
        { "src": { "cc": 113 }, "amount": "rs1" }
      ] },
    { "match": "Arturia BeatStep", "profile": null, "on": true, "channel": 1, "knobMode": "absolute",
      "names": { "cc:10": "Knob 1", "note:51": "Pad 8" },
      "controls": [
        { "src": { "cc": 74 }, "mode": "encoder", "targets": [{ "t": "sel", "pg": 2, "i": 1, "lo": 0, "hi": 127, "inv": true }] },
        { "src": { "note": 51 }, "roll": "rs1" } ] },
    { "match": "*", "legacy": true, "controls": [] }
  ],
  "sets": [{ "id": "rs1", "name": "Timbre", "t": "sel", "how": "around", "amount": 35,
             "params": [{ "pg": 0, "i": 0 }, { "pg": 0, "i": 1 }, { "pg": 2, "i": 0 }] }]
}
```

- **Targets** are the targets already in use: MM `{pg 0-6, i 0-7}`, `{pg 7, i 0}` LEVEL, `{pg 8, i 0}` NOTE; MD `{i 0-24}` (24 LEVEL), `{pg 8, i 0}` NOTE. On top of that there are `t` ("sel" or a track), `lo`, `hi` (0-127, default 0 and 127), `inv` (default false) and an optional `curve` (lin/exp/log, the mockup matrix's field).
- **A control** is `{src: {cc} | {note}, mode?, targets[]}`, `{src, roll: set}` or `{src, amount: set}`. Absent fields take their defaults, so a v1 knob is a v2 control with no extra fields.
- **Commands** (new rows in deskHost's table, schema regenerated):
  - `ctlDevice {match, on?, channel?, knobMode?, profile?}`
  - `ctlControl {match, src, mode?, targets?|roll?|amount?}`: `targets: []` or no action unmaps it.
  - `ctlName {match, src, name}`
  - `ctlSetDef {id, name?, t?, how?, amount?, params?}` and `ctlSetDel {id}`
  - `ctlRoll {id}`
  - `ctlLearnSrc {match, on}`: the next message from that device becomes a control, reported in the document.
  - `ctlRefresh`
  - `ctlVoice`, `ctlReset`, `ctlTrack`, `ctlWatch` and `ctlClear` stay.
- **The page's document** adds:
  - `devices: [{match, name, connected, enabled, profile, seen: [{src, v}], last: {src, v} | null}]`;
  - each target's `name` and `own` resolved for its own track, as today's knobs have;
  - `sets`, each with its `last` roll.
  - `activity` keeps its shape, gains a `device` per message, and is sent only while a device view watches.
- **Needed underneath: which device a message came from.** The standalone's `AudioProcessorPlayer` merges every enabled input into one stream, so the filter cannot tell a TR-06 from a BeatStep (DESIGN-tr06.md, "What arrives").
  - Per-device mappings need per-input callbacks. `AudioDeviceManager::addMidiInputDeviceCallback(identifier, cb)` on each enabled input, registered by `ControllerProfile`, would tag each message with its device before the filter. A message that comes only through the merged stream is then left alone, so nothing is taken twice.
  - In a DAW there is one device (Host MIDI in), and a channel keeps two controllers apart.

## 5. Migration: the existing MIDI Learn keeps working

- **MIDI Learn mappings** (the plug-in's learn list: `{cc, ch?, t, pg, i, inverted}`, `learnAdd`/`learnRemove`/`learnInvert`) are not converted and not moved. The configurator shows them as the virtual **Any MIDI input** device: each one is a control with one target, full range, `inv` = inverted, absolute. They listen to every input, as before.
  - Editing one in the configurator writes through the learn commands while it stays a plain one-target mapping.
  - Adding a range, a second target or a mode moves it into the MDCT setup as an `"*"` control, and removes it from the learn list in the same undo step.
  - Nothing changes for a project that never opens the configurator. The LEARN key (L) on the page keeps its flow: pick a value, move a knob. The mapping it makes appears under Any MIDI input, or under the device it came from once source tagging (above) exists.
- **The old mapping matrix** (the mockup's `S.ctl.links {src, t, pid, min, max, curve, inv}`) maps one-to-one onto targets (`lo`, `hi`, `curve`, `inv`), so its data carries over. Its UI goes, since the configurator replaces it.
  - The app-only sources (App LFO, Random) are not controllers. They would become a virtual **Editor modulators** tile with the same inspector (targets, ranges, invert). That tile is not drawn in the prototype.
- **A version 1 MDCT chunk** becomes the TR-06 device: every knob a one-target control over the full range, `knobMode` its device default, voices and channel as they are.
  - A v1 chunk **without `knobMode`** comes from before c1c411d3 and carries the old defaults (see bug b). It should load as the shipped defaults, not as saved.

## 6. Two bugs from the current build (to fix in the implementation)

### (a) "TR-06 · NOT CONNECTED · PROFILE ON" and "MIDI Learn · NO MIDI INPUT FOUND", with the TR-06 enabled in AUDIO / MIDI

- **What the code does:** the device list is **not** tied to the AUDIO / MIDI panel.
  - `DeskSession` is a `juce::Timer` (8 ms) and calls `ControllerProfile::step`. Once a second, step calls `lookAtInputs()` (`mdCtlProfile.cpp:357-361`) and publishes on any change (`:137-153`).
  - The hook `AudioMidiLink::midiInputs` (`mdAudioMidiLink.cpp:64-73`) asks the standalone holder's own device manager each time, through a fresh `juce::MidiInput::getAvailableDevices()` with `isMidiInputDeviceEnabled`. There is no cache. The AUDIO / MIDI panel builds its list the same way.
  - `inputs` is `null` only in a DAW. So `inputs: []` means **CoreMIDI listed no MIDI source at all in that process**.
- **The likely cause, from the saved settings (not a code defect):**
  - In `~/Library/Application Support/Gearmulator MD.settings`, the TR-06 is the **audio output** device (`audioOutputDeviceName="TR-06"`), and there is no `<MIDIINPUT>` entry.
  - `Gearmulator MM.settings` does have `<MIDIINPUT name="TR-06" …/>`.
  - So the MD standalone had the TR-06's audio but saw no MIDI port: the USB MIDI interface was absent (driver, cable, or the unit started before its MIDI came up).
  - The two tiles are what the current page draws for a TR-06 profile that is on with no TR-06 input found. They are a symptom, not the bug.
- **Fixes:**
  1. The new devices view (no phantom tiles, the empty state).
  2. A specific hint when an audio device is named like a TR-06 but no MIDI input is: "The TR-06 is your audio device, but its MIDI port is not visible: install Roland's TR-06 driver or reconnect it". A new `audioOnly` field would carry this.
  3. Hot-plug via `MidiDeviceListConnection`, plus REFRESH (`ctlRefresh`).
  4. In the diagnostics build, log what `getAvailableDevices()` returns.
- **Confirm it** with the TR-06 connected in the MM app, where its MIDI input is enabled. If the MM app also shows an empty list, look again at CoreMIDI's view (Audio MIDI Setup) before looking at the code.

### (b) MM: BD LEVEL (CC 24) shows AMP REL instead of SYN A · UNIL

- **The code is consistent.** Every place that numbers MM pages uses SYN = 0, AMP = 1, FLT = 2, EFX = 3, LF1-3 = 4-6, LEVEL = 7, NOTE = 8:
  - `deskController.cpp`: `targets(Mm)`, `validTarget`, `ownName`, `mmCommand`;
  - `mmMachines.cpp`: `mmFixedPage`, `mmParamName`;
  - `mmKit.h`: `pages`.
  - The shipped default `{cc 24, pg 0, i 0}` renders "SYN A · UNIL". This was checked in C++ against the built `libdeskController.a`, and on the real `mmStudio.html` in headless Chrome.
  - No commit on the branch changed the numbering.
- **The cause is saved state.** The MM standalone's settings (`Gearmulator MM.settings`, `filterState`) hold an MDCT chunk written by a build before c1c411d3. It has no `knobMode`, and in it `{cc 24, pg 1, i 3}` (AMP REL), `{cc 71, pg 0, i 3}` (ACC as SYN D, not NOTE) and BD's note is 89.
  - `ControllerProfile::restore()` (`mdCtlProfile.cpp:208-228`) reads it through `setupFromJson`. It validates, since AMP REL is a real target, so the shipped defaults never apply.
- **Fixes:**
  - Now: RESET TO DEFAULTS in the TR-06 view (`ctlReset`) writes the defaults over it.
  - In code: `setupFromJson` treats a v1 setup without `knobMode` as the old format and returns the defaults (or the version goes to 2 and old chunks fall back). A test loads such a chunk.
  - The configurator can also mark a control that differs from its profile's default (a small "changed" dot), so a stale mapping shows.

### (c) The TR-06 does not come back after unplugging and re-plugging it (installed app)

The prototype's re-plug works, so this comes from reading the code; no app was run.

- **Enabled inputs are stored by identifier.** The standalone's AUDIO / MIDI keeps its enabled MIDI inputs by JUCE device identifier (`<MIDIINPUT name="TR-06" identifier="…"/>` in the app's settings, `AudioDeviceManager::setMidiInputDeviceEnabled(identifier, …)`).
  - On macOS, a USB MIDI device that is unplugged and plugged back in can come back under a new CoreMIDI identifier. Its old identifier no longer matches, so the device manager does not re-enable it, and nothing it sends arrives.
- **No hot-plug.** The controller document looks at the inputs only once a second (`ControllerProfile::step` → `lookAtInputs`). It has no hot-plug notification, so the list refreshes but the input stays disabled.
- **The fix to build:**
  1. **Hot-plug:** a `juce::MidiDeviceListConnection` in `ControllerProfile` (JUCE 7.0.10 has it). Its callback runs on the message thread, looks again and republishes the device list.
  2. **Re-enable by name:** remember the enabled inputs by name as well as by identifier. When a device appears whose name matches an input that was enabled (and whose old identifier is gone), enable it under its new identifier and update the saved setting. This is in `AudioMidiLink`, on the standalone holder's device manager.
  3. **Republish** the device list after it, so the tile comes back ("TR-06 connected").
  4. **REFRESH** (`ctlRefresh`) runs the same look on demand.
  5. **A test** with a simulated device list change: the TR-06 disappears and returns with a new identifier. It must be enabled again and the document must list it. (`mdDeskSetupStateTest`-style, with a stand-in device list.)

## 7. Open questions for Radek

1. **Connected but not enabled:** show such a device (dimmed, one click ENABLEs it in AUDIO / MIDI, as the prototype does), or hide it until it is enabled there?
2. **Where a device's mapping lives:** with the project or the saved state (MDCT, as today, so a DAW project carries its mappings), or per device in the app's settings so a controller behaves the same in every project? The proposal is the project, with a later "save as this device's default".
3. **Randomise while recording:** should a roll during live record become parameter locks on the current step, or stay a kit edit only? The proposal is a kit edit only, one undo step.

Hardware unknowns (which physical knob sends which CC, TAP, OH/CH sharing a knob) stay in DESIGN-tr06.md, On the hardware. The matrix names rows from the chart, so it does not depend on them.
