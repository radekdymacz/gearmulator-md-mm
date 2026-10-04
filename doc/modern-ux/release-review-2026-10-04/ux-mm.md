# Monomachine Editor: release-polish UX/UI review (v0.2.0)

- Repo: `audio/gearmulator-md-mm`, branch `main`, HEAD `855902a8c`. Read-only: no repo file was edited.
- Screenshots: `scratchpad/review/mm-shots/` (MD comparison shots in `mm-shots/md/`).
- **What was rendered.** The mockup `doc/modern-ux/mm-mockup/index.html`, rebuilt into the scratchpad with the
  repo's own `build.sh` lists. The result is byte-identical to the committed `index.html`, so the committed
  mockup is current. The plug-in page (`skins/mmStudio/mmStudio.html`) is that same mockup, synced by
  `sync-mmstudio-skin.py` and played by `mmAdapter.js`. Served bare over http it does not run, because
  `deskBridge.js` and the other shared files are inlined only by the editor. So the plug-in-only states (start-up
  card, NO ROM, ROM error, HW MIDI connect, lost and pending, unavailable controls, empty machine) were driven
  through the page's own view API inside the mockup: `Boot.update`, `Boot.showInstalled`, `setEng`, `disable`,
  `startEmpty`, `renderPst`.
- Sizes: 1440 × 924 (the plug-in's design width and default window), 1920 × 1080, and 1280 × 760. Below 1440 the
  plug-in zooms the whole page out, so 1280 findings apply to the browser mockup only.
- Headless Chrome was driven over CDP: clicks, keys, Alt held, ⌘Z.

## Ranked findings

### Blocker

**1. Perform › MULTI MAP: the joystick covers the Multi map table**
- Where: Perform › MULTI MAP. Shots `c04-perform-multimap.png` (1440), `c08-perform-multimap-1920.png`,
  `c09-…-rerender.png`.
- What: the Assign card's joystick square runs about 200 px into the Multi map card. The joystick sits on top of
  the PATTERN column header and the four pattern selects. The ranges' pattern can't be seen or clicked. This
  happens at both sizes.
- Why: `doc/modern-ux/mm-mockup/src/25-mm.css:234` sizes the stick as
  `--joy: clamp(200px, calc(100vh - 500px), 440px)`. That height budget is for AUTO TRACK / POLY, where the
  keyboard is the only thing below. MULTI MAP inserts a roughly 250 px card (`.perf.map`, `110-perform.js`), and
  the `.perf3` row then overflows into it.
- Fix:
  - Shrink the stick in map mode, e.g. `.perf.map .perf3 .asgcard{--joy:clamp(140px,calc(100vh - 760px),240px)}`.
    Alternatively, give the `.perf3` row a bounded height with `min-height:0; overflow:hidden` on the card.
  - Re-sync `mmStudio.css`.
  - Add a layout assertion to `mmSelfTest.js`: no `.perf3` card's rect may intersect `.maprow`.

### Should-fix

**2. The Monomachine Editor names the Machinedrum**
- Where:
  - `doc/modern-ux/mm-mockup/src/130-main.js:282`. The keys list (? key, `m02-keys.png`) says "⌥drag a value:
    Control All … (FUNCTION + knob on the **Machinedrum**)".
  - `40-data.js:33`. The FX-REVERB tooltip says "The **Machinedrum's** gated reverb".
- Fix: replace the first with "… every synth track (an editor feature)". Replace the second with "A gated reverb.
  GATE 127 turns the gate off."

**3. The AUDIO / MIDI footer starts with a stray comma**
- Where: `skins/shared/deskAudio.js:48`, shared with the MD. Shot `b10-audio.png`.
- What: the footer reads ", or the engine menu opens it · Esc closes …". The comma is the shortcut key (`,`), but
  it reads as a typo.
- Fix: `<kbd>,</kbd> (comma) or the engine menu opens it`, the way the MD's Global footer says "G or the engine
  menu…".

**4. The GEN summary collapses to "E…" at the plug-in's default width**
- Where: Sequence GEN bar, `25-mm.css:76`. Shots `seq-1440.png` (reads "E…") and `seq-1920.png` (the full
  "E 5/16 · ×2 · 10 on · steps 1–32").
- Why: one ellipsised letter is noise. It looks broken, and the repeats information is lost at 1440, the size most
  people will see.
- Fix, either of:
  - Below roughly 1700 px, hide `.gsum` (as `25-mm.css:98` already does when WRITE is shown) and put the summary in
    the R key's tooltip.
  - Or keep a short form ("5/16 ×2") that fits without an ellipsis.

**5. The rail's GEN spec tags are truncated**
- Where: Sequence rail, `25-mm.css:110-113`. Shots `seq-1440.png`, `m10-seq-midi.png`.
- What: the tags show from 1380 px, so at 1440 they read "E…", "R", "E 4/…". On the MIDI side every row reads
  "E 4/…".
- Fix: raise the breakpoint to about 1600 px, or render a fixed short tag (`E5`, `R30`, `—`) that never ellipsises.

**6. Holding Alt shifts the GEN bar and clips its labels**
- Where: GEN bar while Alt is held. Shots `m06-althold.png` and `c03-cleared-pattern.png` against `seq-1440.png`.
- What: the head changes from "T1 · SW-SAW" to "all synth tracks". The column widens, every group moves about
  23 px right, and "DEFAULTS" clips to "DEFAUL'". The bar shifts on every Alt press, which is a common modifier
  here.
- Fix: give the GEN head column a fixed `min-width` that fits the longer label (about 14ch), or show "ALL" in the
  same width.

**7. MUTATE on a MIDI track looks live but does nothing**
- Where: Sound with a MIDI track selected. Shot `m12-sound-midi.png`. Code: `76-gen.js:165`.
- What: the SCOPE chips (SYN AMP FLT EFX LFO) and R look enabled. The only explanation is cut off: "A MIDI track
  has no sound to move: Alt+R moves …".
- Fix: on MIDI tracks, render the chips and R as disabled (dim, `aria-disabled`). Put the full sentence where the
  chips are, without an ellipsis.

**8. Kit names are clipped in the library**
- Where: kit library. Shot `m03-kitlib.png`.
- What: names are clipped to "MONOMACHIN" and "DIGI CHOIF". The slot's pattern list ("A01 A02 A…") is clipped
  too.
- Why: a kit name is the main thing you look for here.
- Fix: in the 16-column grid at 1440, use `letter-spacing:0` and a font 1 px smaller for the name line, or let the
  name wrap to two lines.

**9. The pattern chooser claims facts about the user's machine**
- Where: `125-lib.js:77`, the header note. Shot `m04-patchooser.png`.
- What: the note says "Factory presets sit in A-D, E-H start empty." The editor can't know that about the
  person's machine, and the demo itself contradicts it (B-D are EMPTY).
- Fix: delete the sentence. Also add "Cmd+Z undoes a paste or clear" to the footer, as the MD has (if MM pattern
  clears are undoable).

**10. Transpose dock: the help line is truncated and starts with a literal "---"**
- Where: Sequence › Transpose dock. Code: `80-notes.js:49` (`TRNHELP`). Shot `m08-dock-trn.png`.
- What: the line begins "---: no transpose scale…", which is a raw placeholder, and it is truncated at 1440.
- Fix: one short line, e.g. "No scale: track, pattern and song transpose add up; every note can sound." Move the
  rest to `title`.

**11. Perform: the Multi envelope's subtitle is truncated**
- Where: Perform, Multi envelope card. Code: `110-perform.js:31`. Shot `perform-1440.png`.
- What: the subtitle reads "both DATA PAGE keys · multi t…" at 1440.
- Fix: shorten it to "multi trig only". Put the DATA PAGE detail in the tooltip.

**12. HW MIDI with no machine answering gives no guidance**
- Where: HW engine, lifecycle `hwLost`. Shots `h01-hw-nomidi.png`, `h02-hw-nomidi-sound.png`.
- What: the whole page dims. The only clue is "HW NO MIDI" in 8 px LCD caps, and nothing says what to do.
- Fix: while `S.eng==="hwnone"`, show a one-line strip under the header: "No Monomachine answers on MIDI. Check the
  cable and the machine's channel, then AUDIO / MIDI… (,)", with a button that opens the panel. The MD has the same
  label scheme (`mdDeskTop.js:60`), so do both.

**13. HW MIDI: unsent edits are easy to miss**
- Where: LCD sync slot, `60-ui.js:41`. Shot `b08-hw-send.png`.
- What: "SEND 3" (unsent pattern and song edits) uses the same dim 9 px LCD text as "SYNC". Missing it means the
  machine never gets the edits.
- Fix: make it a bordered LCD key with a blinking red LED while `pend > 0`, and toast once when the first edit
  starts waiting.

**14. Start-up card: the link row runs together**
- Where: `skins/shared/deskBoot.js:31`, shared with the MD. Shots `b01-norom.png`, `b04-installed.png`.
- What:
  - On the first run, "Check again" wraps alone onto a third line.
  - With a firmware installed, "Remove the ROM Close" reads as one phrase.
  - Close is an underlined link, not a button.
- Fix: separate the links with " · ". Make Close a real button in a `.btnrow`, and make Remove a danger-styled
  key.

**15. Low-contrast help text**
- Measured (`audit.js`):
  - Hint text (`--print3` on the panel) is 3.15:1 at 9-13 px, e.g. "Draw to lock · ⇧ ramp", "Keys play the track in
    focus…", "Tracks sum into a bus…".
  - The Song selector "S01 DEMO SONG · on the machine" is 2.43:1. It looks disabled, but it is the active control.
- Fix: darken `--print3` to at least 4.5:1, and give `#songsel`'s button normal key styling.

**16. Toasts are hard to read**
- Where: shots `b08-hw-send.png`, `c05-perform-poly.png`.
- What: long sentences are set in the LCD pixel caps font, e.g. "HARDWARE MODE: SOUND EDITS GO OUT AS CCS;
  PATTERN AND SONG EDITS WAIT FOR SYSEX RECV". The toast also sits over the lock lane or the keyboard for 3 s.
- Fix: use the sans face in sentence case when a toast is longer than about 40 characters. Keep the LCD style for
  short status like "Undo".

**17. POLY: the Mutes card still lights T2-T6**
- Where: Perform. Shot `c05-perform-poly.png`.
- What: after "POLY: T1 now plays six voices. The other five tracks are off", the Mutes card still lights T2-T6
  as playing.
- Fix: dim T2-T6 with the title "off in POLY" while `S.mode==="poly"`.

### Nice-to-have

**18. Mix: the IN control on synth tracks looks like a dropdown**
- Where: `100-mix.js:52`. Shot `mix-1440.png`.
- What: on synth tracks, IN is a disabled `kselbtn` ("synth · none") styled exactly like the live FX selects.
- Fix: plain dim text "—" with the existing title.

**19. Mix: T1 looks unrouted when it feeds its neighbour**
- Where: Mix, routing 3×STEREO+AB=MIX. Shot `mix-1440.png`.
- What: T1 feeds T2 through NEIBOR, and its OUT row shows AB / CD / EF all unlit with no reason.
- Fix: show "→ T2 (NEIBOR)" in place of the three keys.

**20. Mix at 1920 × 1080: dead space**
- Where: shot `mix-1920.png`.
- What: the strips don't grow, which leaves about 150 px of empty band above the routing diagram.
- Fix: let the fader rows take `1fr`.

**21. Naming on MIDI tracks is inconsistent**
- Where: shots `m12-sound-midi.png`, `m10-seq-midi.png`.
- What:
  - The Sound page's chip says "M1 · CH 10", but the rail says "CH10".
  - The Sound dock key is "MIDI SET", but the Sequence dock key is "MIDI PAGE".
  - Two thirds of the Sound page is empty.
- Fix: one spelling ("CH10") and one key name ("MIDI page").

**22. Sound, FX-REVERB envelope: two handles float off the curve**
- Where: shot `s01-sound-fxreverb.png`.
- What: with HOLD and DEC at 127, two handles float mid-plot, not on the drawn curve.
- Fix: check `ED.amp` (`90-sound.js:138`) for the hold-until-note-off case and clamp the handles to the drawn
  path.

**23. Perform: the envelope line strikes through the plot title**
- Where: Multi envelope plot. Shot `perform-1440.png`.
- What: with SUS at 127 the line runs through "ADSR OVER ALL TRACKS · REL ∞".
- Fix: start the curve below the label band (T 18 → 26 in `ED.menv`).

**24. Leftover duplicate first-run flow and dead code**
- Legacy first-run ask:
  - Where: `mmAdapter.js:506-514` `firstRun()`, reached from `romManage()` when the lifecycle is `missing`
    (`:498`), and `130-main.js:36`. Shot `b05-firstrun-ask.png`.
  - What: the old ask still exists beside the shared P7 start-up card. Its copy and layout differ ("Choose ROM
    file…", "Close preview" in the mockup).
  - Fix: route `romManage` on `missing` to `Boot.update({state:"missing"})` and delete both `firstRun` asks.
- Dead code in `130-main.js:482` `setLcd`:
  - What: `if(bits&&bits.length>=1024){Boot.lcd(bits);return}` is followed by an identical-condition block that
    can never run.
  - Fix: delete the unreachable block.

**25. Mockup-only issues (the standalone prototype)**
- Unavailable controls look enabled:
  - What: there is no `[data-na]` style in the mockup CSS, so controls that are not available look enabled. The
    plug-in has it (`mmStudio.css:2060`), so this affects only the standalone mockup (`e04-hw-disabled-perform.png`).
  - Fix: move the rule into `25-mm.css`.
- The keys list offers a Control workspace that has no tab:
  - What: with `setMapping(!window.MMHost)`, the list shows "6 Control" although the mockup has no Control tab.
- Undo looks unavailable after a GEN run:
  - What: in the demo host, Undo stays disabled after a GEN run (R) until another action, because `genHeld`
    defers `commit`. This was measured as `history().undo === 0` after R. ⌘Z still works.
  - Plug-in: likely fine, since the core counts history per gesture id. Verify once in the plug-in.
- At 1280 (browser only), the DELAY FILTER plot's title and handles clip (`sound-1280.png`).

**26. The MKII key is unclear**
- What: it sits in SETUP. Its toast says "The plate looks the same", and its only effect in the plug-in is to grey
  DPRO-DDRW / DPRO-DENS in the machine picker.
- Fix: title it "Your machine: MKI or MKII (MKI cannot load DPRO-DDRW or DPRO-DENS)", or move it into the engine
  menu.

### Checked and fine
- Every visible control has an accessible name: 0 unnamed buttons, selects or sliders on all five workspaces.
- Focus rings are present (`:focus-visible` in base; GEN caps ring the `kbd`).
- `prefers-reduced-motion` is honoured.
- No `console.log`, TODO or FIXME leftovers in the MM sources: the only `console.log` is the bridge's own logger.
- The ask before loading a kit focuses Cancel. Its wording is clear ("Save and load / Load without saving /
  Cancel").
- The SysEx import preview, the SYSEX RECV instructions, the NO ROM / bad file / installed cards, the empty machine
  (`e01`-`e03`) and Song › Chain all read well.

## Parity: Machinedrum Editor vs Monomachine Editor

| Feature | MD | MM | Gap |
|---|---|---|---|
| Keyboard map: mnemonic keys, ? list, Alt = all | shipped (`mdDeskKeys.js` + bindings) | Same bindings, group for group. Diffed: every MD `Keys.bind` has an MM twin; MM uses "roll" where the MD has "step" | none. The MM list wrongly says "Machinedrum" (#2) |
| Home-row playing (A-L, Z/X, C/V) | yes | yes, synth tracks only. MIDI tracks are not played (said once) | known: MIDI tracks from keys need a firmware probe (MM-PORT-PLAN proposal 2) |
| Black keys (W E T Y U O P) | no | no | owner's decision pending, same on both |
| GEN rhythm (EUCLID / RANDOM / KEEP, R, ↺, Alt = all, one undo step) | yes | yes, plus NOTES (MOTION / ROOT / SCALE / RANGE) | MM ahead (melodic notes) |
| MUTATE (amount, scopes, group chips) | yes | yes | MM: looks live on MIDI tracks (#7) |
| Rail spec tags | yes | yes | MM: truncated at 1440 (#5) |
| Comforts: lock budget, LEN ×2, M/S OFF / 0, rotate Alt+←/→, ⌘-click fill, ⇧ ramp, paste to marked tracks, drag M/S | yes | yes. Every-N fill is on the roll; wheel-lock is on the lock lane only | MM wheel on roll notes not done (proposal 3); fine for release |
| Alt hold labels (CLR→ALL, lane clear) | yes | yes | MM: layout shift / clipped "DEFAULTS" (#6) |
| Placement-aware menus, no text selection on drag | yes | yes | none |
| Sound by function (groups, plots, aligned rows) | yes | yes, 27 group editors, every knob once (`mmSoundTest`) | MIDI-track Sound page sparse (#21) |
| Library: click loads, Save-and-load ask, F2 rename, drag copy, Import / Export SysEx | yes | yes | MM: names clipped (#8); pattern note claims factory layout (#9) |
| Song: arrangement, row editor, MORE (per-track transpose, part, mutes) | yes (part and mutes inline) | yes (behind MORE) | layout difference only |
| Song: CHAIN (BANK + TRIGs, Plays line, BACK / CLEAR) | yes | yes (MM-P8) | none; HW gating present on both |
| Start-up card (boot LCD, NO ROM, ROM error, LOAD ROM manage) | yes (shared `deskBoot.js`) | yes, same card | both: link row (#14). MM: leftover legacy first-run ask (#24) |
| HW MIDI engine (connect, lost, unavailable controls) | yes | yes, plus the SYSEX RECV / SEND n flow (MM only) | both: no actionable no-MIDI message (#12); MM SEND n too subtle (#13) |
| AUDIO / MIDI panel (`,`) | yes (shared) | yes (shared) | both: footer copy (#3) |
| **GLOBAL settings panel** (MIDI base channel, sync in / out, program change, map) | yes (`mdDeskGlobal.js`, engine menu GLOBAL…) | **no**. Only the multi map (Perform) and channel hints. MM globals exist as documents | MM missing; post-release candidate unless channel and sync setup is needed for HW users |
| Mix | 16 strips + master FX chain | 6 strips + routing diagram (3 modes) | machine-specific; MM: IN / OUT affordances (#18, #19) |
| Perform (multi trig / map, POLY, assign, joystick, mutes) | n/a (MD has no equivalent) | yes | MM: multi map overlap (#1, Blocker); POLY mute lamps (#17) |
| Sampler | yes | n/a (the MM has none) | — |
| Control / LEARN (MIDI mapping) | hidden in the plug-in | hidden in the plug-in | same; mockup lists "6 Control" without a tab (#25) |
| MKI / MKII key | plate recolour | picker gating + "MKII" print | MM purpose unclear (#26) |

**Port verdict:** the 2026-10 port (MM-PORT-PLAN phases 1-4 + P8 chain) is complete and consistent at the
level of keys and features. Two gaps remain:
- The GLOBAL panel.
- MIDI-track keyboard play, a known proposal.

Everything else is polish of the port's presentation at the plug-in's 1440 width: truncation, Alt shift, MIDI-track
states.
