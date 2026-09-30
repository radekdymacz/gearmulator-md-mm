# Design proposal: the controller configurator (CONTROL workspace)

- Branch `feat/tr06-profile`, 2026-09-30. A proposal and a prototype, not built. It builds on [DESIGN-tr06.md](DESIGN-tr06.md), which stays the authority for what is built today.
- **Prototype:** [mockup/controller-prototype.html](mockup/controller-prototype.html). It is one self-contained file with fake data and no host. Open it from `file://` or any static server. The strip at the bottom (PROTOTYPE) is not part of the design: it plugs and unplugs devices, switches the machine (MM or MD) and the DAW case, and plays the hardware. To play the hardware, drag a knob on the drawing up or down.
- **The feedback it answers:** centre the device tiles. Show a device only while it is connected. Follow hot-plug. Make mapping richer than a table of CC rows (ranges, inverse, macros, randomise, knob mode, track pinning, steady live feedback). Use the same configurator for every MIDI input, with the TR-06 as a device that has a built-in profile. Replace the old MIDI Learn matrix.

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
- **DAW:** the host owns MIDI and names no device. The tiles are **Host MIDI in**, plus Any MIDI input when learned mappings exist. Host MIDI in opens the generic configurator, where a PROFILE choice ("Roland TR-06") gives it the TR-06's drawing, names and defaults. There is no hot-plug in a DAW.

## 2. The configurator: one view for every device

Every tile opens the same view, top to bottom:

- **The bar:** ‹ DEVICES, the device's name, then:
  - PROFILE (TR-06) or MAPPING (other devices) ON/OFF, with its LED. Off means the device's MIDI reaches the machine as ordinary MIDI, as today;
  - CHANNEL;
  - KNOBS (the device's default knob mode);
  - SELECT ON TOUCH;
  - RESET TO DEFAULTS (profiles only).
- **SELECTED TRACK:** the page's selected track, as pads (MM: 6, MD: 16) with each track's machine. Targets follow it unless they are pinned. In the product the page's own track selection drives it. The strip is there so the prototype can show the names changing.
- **Left: the device.**
  - **With a profile (the TR-06):** a stylised front-panel drawing (inline SVG, the page's tokens, so it follows both plates).
  - **Without a profile:** **Controls seen**. A CC becomes a knob cell the first time it arrives, and a note becomes a pad cell. LEARN turns "the next control you move" into a cell and selects it. Cells can be renamed ("Knob 1").
- **Right: the inspector** for the selected control (or voice).
- **Bottom: the MIDI monitor**, reduced to one LCD strip: the last 8 messages, newest first, each as `CH10 CC 24 90 · BD LEVEL → UNIL 97`. A message that does nothing is shown dim. CLEAR empties it.

### The TR-06 drawing

- **The instrument row:** ACC, BD, SD, LT, HT, CY, OH and CH, each with its own LEVEL CC from the chart. The TR-06 sends a separate CC per function, so a **KNOB LAYER** switch redraws the row for LEVEL, TUNE, DECAY, TONE (BD ATTACK, SD SNAPPY, LT/HT COLOR), COMP (BD, SD) and DELAY SEND. Each layer's caption says how the hardware reaches it (MENU + turn; STEP LOOP + DEPTH).
  - A slot with no CC in a layer (ACC TUNE, for example) is drawn grey and says why.
- **EFFECT:** DRIVE 17, TIME 18, DEPTH 19.
- **ALSO SENDS:** CCs that are in the chart but have no known place on the panel (MIX IN LVL 12, MIX IN DELAY SEND 111, MASTER PROB 113). They are chips that are mapped like any knob.
- **Sends no MIDI** (grey, dashed; a click says why): VOLUME, TEMPO (clock only), INSTRUMENT, MODE, VALUE, MENU, STEP LOOP, the 16 step keys.
  - RUN / STOP is Start / Stop: the machine's own sync settings decide, and it is not mappable.
  - TAP is marked UNKNOWN (DESIGN-tr06.md, On the hardware 3).
  - The drawing says **STYLISED · CHECK ON THE HARDWARE**. Its positions come from the chart and the manual, not from a measured panel.
- **On each control:**
  - a mapped LED (lit when it does something);
  - a badge: M3 (a macro of three), RND (rolls a set), AMT (sets a set's amount);
  - an arc showing the last value it sent;
  - the CC under its name.
  - The selected control has a dashed ring and a cream label.
- **Voices:** the instrument row as seven keys under the drawing. Each shows its notes and where it plays (LCD chip "T1 C4"). A click opens the voice in the inspector: the track as pads, and the MM note with semitone and octave steps.

### The inspector

- **The readout**, in LCD style:
  - IN: the last value, and LAST MOVED when this was the last control to move;
  - OUT: every target's result, `T1 BASE 110 · T1 DIST 80 · T1 SRR 60`.
- **DOES:** OFF, DIRECT, MACRO, RANDOMISE, RND AMOUNT (a pad has no RND AMOUNT).
  - DIRECT and MACRO are the same thing with one target or several. "+ ADD TARGET" on a direct control makes it a macro, and removing targets goes back.
- **KNOB:** RELATIVE, ABSOLUTE, and for devices without a profile ENCODER (±). It shows "device default" or "own setting · USE DEVICE DEFAULT".
- **Target cards**, one per target:
  - **Track:** SEL T1 (follows the selection) or a pinned track (cream key, "T6"). A key-style list offers "Selected track" and T1-T6 / T1-T16 with their machines.
  - **Parameter:** a key-style list grouped as the page groups them. MM: SYN (slot and the machine's own name, "SYN A · UNIL"), AMP, FLT, EFX, OTHER (LEVEL, NOTE). MD: SYN (numbered, with names), FX, ROUTE, OTHER.
    - A slot the machine does not use says so: "SYN D · unused on SWAVE-SAW: this target does nothing on T1".
  - **INV:** inverse. The range bar turns dashed, and its labels read KNOB MAX → 60 … 127 ← KNOB MIN.
  - **×** removes the target.
  - **The range bar:** two handles (min, max) and a red marker at the parameter's current value, updated in place. The numbers read `KNOB MIN → 20 · NOW 110 · 110 ← KNOB MAX`.
- **RANDOMISE / RND AMOUNT:**
  - a set (key-style list, "+ New set", RENAME) and its track (selected or pinned);
  - AMOUNT, drawn as the page's 16-LED segment bar;
  - AROUND NOW or ANYWHERE;
  - the set's parameters as toggle chips per page (the machine's own names; unused slots disabled);
  - ROLL NOW, and the last roll written out (`UNIL 67→33 · SUBX 15→26 …`).
  - A set is shared by every control that uses it: a TR-06 knob can set its amount while a BeatStep pad rolls it.

### Live feedback (no flashing)

- The control that moved last keeps a **steady** red ring, and its value is shown next to it, until another control moves. There is no timer, no blink and no animation.
- Values change in place: the arc, the readout, the NOW markers and the monitor strip. The rest of the view is not redrawn, so an open list or a drag is not disturbed.
- The product keeps today's rates: the page document at most about 15 times a second while watching, the knobs through `KnobPump` at most one round per 50 ms.
- **SELECT ON TOUCH** (on by default): moving a control on the device selects it in the inspector, the quickest way to map ("touch it, then choose"). Off, the inspector stays on what was clicked, and only the ring moves.
- A voice that plays gets a steady ring round its chip, until another voice plays.

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
            { "t": "sel", "pg": 3, "i": 2, "lo": 60, "hi": 127, "inv": true }] },
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

Hardware unknowns (which physical knob sends which layer, TAP, OH/CH sharing a knob) stay in DESIGN-tr06.md, On the hardware. The drawing is marked stylised until they are checked.
