# Design proposal: the controller configurator (CONTROL workspace)

- Branch `feat/tr06-profile`, 2026-09-30. A proposal and a prototype, not built. It builds on [DESIGN-tr06.md](DESIGN-tr06.md), which stays the authority for what is built today.
- **Prototype:** [mockup/controller-prototype.html](mockup/controller-prototype.html). It is one file with fake data and no host, and needs no sibling files.
  - It opens over http or from `file://`. Fonts come from the mockups' Google Fonts link; offline it falls back to system fonts.
  - The strip at the bottom (PROTOTYPE) is not part of the design. It plugs and unplugs devices, switches the machine (MM or MD) and the DAW case, and plays the hardware. To play the hardware, drag a row's meter left or right, or click a pad row's square.
- **The feedback it answers:**
  - Centre the device tiles. Show a device only while it is connected, and follow hot-plug.
  - Map through a control matrix, not a table of CC rows and not a drawing of the device (second round: "a beautiful control matrix"). The matrix gives ranges, inverse, macros, randomise, knob mode, track pinning and steady live feedback.
  - Use the same configurator for every MIDI input, with the TR-06 as a device that has a built-in profile. It replaces the old MIDI Learn matrix.

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

## 2. The configurator: one control matrix for every device

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

## 7. Open questions for Radek

1. **Connected but not enabled:** show such a device (dimmed, one click ENABLEs it in AUDIO / MIDI, as the prototype does), or hide it until it is enabled there?
2. **Where a device's mapping lives:** with the project or the saved state (MDCT, as today, so a DAW project carries its mappings), or per device in the app's settings so a controller behaves the same in every project? The proposal is the project, with a later "save as this device's default".
3. **Randomise while recording:** should a roll during live record become parameter locks on the current step, or stay a kit edit only? The proposal is a kit edit only, one undo step.

Hardware unknowns (which physical knob sends which CC, TAP, OH/CH sharing a knob) stay in DESIGN-tr06.md, On the hardware. The matrix names rows from the chart, so it does not depend on them.
