# Machinedrum Editor: release-polish UX/UI review (main @ 855902a8c)

## How this was looked at

- The **shipped page** (`skins/mdStudio/mdStudio.html` + `skins/shared/*`), not the mockup, was copied into a scratch harness and served over HTTP (a small Node static server). A fake host (`harness/host.js`) answers `ready` with the real message stream the plug-in sent on 2026-09-29 (`temp/pagereplay-md/md-ready.json`: 128 patterns, 64 kits, 32 songs, global, machine, catalogue), plus a samples doc. URL parameters patch it: `life=missing|unsupported|booting|hwConnecting|hwLost|silent`, `engine=hw`, `play=1`, `fail=1`, `slow=ms`.
- Headless Chrome was driven over CDP (`harness/cdp.mjs`) at 1440×924 (the default window), 1920×1080, 2560×1440, 1280×800 (zoom 0.889) and the 864×554 minimum (the plug-in zooms the page to 0.6 there, `mdWebPageHost.cpp:246`).
- A DOM audit counted font sizes under 10px, contrast, and hit targets under 20px.
- Screenshots are in `review/md-shots/`. The `sync-mdstudio-skin.py --check` reports the skin is in step with the mockup.
- **Harness limits:** the SEND/SYNC pending state and the undo counts come from `machine.desk.tx` / `history`, which the fake host never updates, so they were not judged. The AUDIO/MIDI panel needs the host's device list, so in the harness it only shows "Reading the devices…".

---

## Blocker (looks broken; fix before v0.2.0)

### 1. GLOBAL › ROUTING: the 16 outputs overflow the dialog
- **Where:** shot `33-global.png`; code at `mdDeskGlobal.js:44` and `mdDesk.css:1174-1176` (`.grout`).
- **What you see:** the row reads `MAINMAINMAIN…MAIN` and runs past the dialog's right border.
- **Why it matters:** this is the first thing anyone sees in GLOBAL, and it looks like a rendering bug.
- **Fix:** don't print "MAIN". Show a dim `·` for main and the letter (A–F) in cream for a direct output; the tooltip already says what each one is. Or lay the 16 outputs out as two rows of 8. Either way, add `overflow:hidden; text-overflow:clip` to the buttons.

### 2. An error moves the whole page under the pointer and is shown twice
- **Where:** shots `39-error-line-toast.png` and `52-edit-failed-500ms.png`; code at `mdDeskRender.js:76` (the `case "error": toast(m.message); showLastError(...)` line), `mdDeskApp.js:141`, and `mdOverrides.css:4-6`.
- **What happens:**
  - `#errline` is an in-flow block between the subbar and the body. A refused edit pushes the grid down by about 53px. It pushes it up again 6 s later.
  - The lock-parameter rail is squeezed and cut off while the line shows.
  - At the Sequence page's top, the line is clipped to about 12px: the text overflows the red bar and runs over the TRACK / "M/S off" header.
  - The same text also appears as a toast.
  - On a step grid, the user's next click lands one or two rows off.
- **Fix:**
  - Make `.errline` (and `.statusline`) an overlay with `position:absolute`, anchored under the header with a z-index, or give it a reserved fixed-height slot.
  - Show the message once: keep the line, drop the toast for `error`.
  - Add a × to dismiss it.

### 3. HW MIDI "connecting" and "lost" look like a working editor with an empty kit
- **Where:** shots `23-hw-connecting.png` and `24-hw-lost.png`; code at `mdDeskTop.js:30-35` (`#status` only ever says "Reading the current pattern…"), `mdDeskTop.js:38` (the boot card covers missing/unsupported/booting only), and `mdDeskTop.js:54-56`.
- **What the user sees:**
  - 16 tracks named "EMPTY" and a kit "K01 KIT 01" marked **saved**.
  - The only signal is the 6px LCD label "HW CONNECT" or "HW NO MIDI".
  - The plug-in's good explanation ("Check the MIDI cables and that its SYSEX is on") is only in a tooltip.
- **Why it matters:** a hardware user will think the editor is broken, or will edit placeholder data.
- **Fix:**
  - For `hwConnecting` and `hwLost`, show `#status` with `V.lifecycleText` and a button to open AUDIO/MIDI.
  - Dim `#body` the way the LCD dims.
  - Show "—" instead of a placeholder kit name, and hide the "saved" chip until a kit document has arrived.

---

## Should-fix

### 4. At the minimum window everything is too small to read or hit
- **Where:** shots `13-seq-min-864x554.png` and `14-sound-min-864x554.png`; code at `mdPageSpec.h:18` (`designWidth = 1440`) and `mdWebPageHost.cpp:246`.
- **Why it matters:** the page zooms uniformly. At 0.6, the 8px LCD labels (16 on Sequence) become about 5px. Hit targets that are already tiny at 1440 become 5px tall:
  - the engine menu key (88×8),
  - Copy / Clr / Paste (33×8, 24×8, 36×8),
  - the LCD line-2 fields (about 50×12),
  - the pattern ‹ › keys (12×18),
  - the Sound page's MUTATE group chips (15px tall).
- **Fix:** set the minimum window to about 1152×740 (zoom 0.8) or floor the zoom at 0.8 and let the page scroll. Independently of glyph size, pad the LCD keys to a hit box of at least 20px (an invisible `::after` hit area).

### 5. Large windows do not scale up
- **Where:** shot `12-seq-2560x1440.png`; code at `mdWebPageHost.cpp:246` (`std::min(1.0, …)`).
- **What you see:** at 2560 or 4K, the header and LCD stay 1440-sized islands. The lock-parameter keys stretch to about 45px tall, and the step cells become wide slabs.
- **Fix:** zoom by `min(width/1440, height/924)` up to about 1.5, and only let the grid absorb the remainder.

### 6. The keyboard focus ring disappears on the LCD
- **Where:** shot `55-focus-kit-crop.png`. The workspace tabs (`54-focus-tab3-crop.png`) are fine.
- **What you see:** the red focus outline on the orange LCD (Kit, Pattern, Tempo, line-2 fields) is nearly invisible.
- **Fix:** inside `.lcdpanel`, use `:focus-visible { outline: 2px solid var(--ink); outline-offset: -2px }` or an inverted field.

### 7. GLOBAL shows "NOT VERIFIED" badges
- **Where:** shot `33-global.png`; code at `mdDeskGlobal.js:31` and `:37` (`.gnv`).
- **Why it matters:** the badges on LOCAL CTRL and TRIG IN A/B read as developer notes left in a release.
- **Fix:** drop the badges. Put the reason ("stored only; needs pads on the inputs") in the card's sub-heading or the tooltip.

### 8. GLOBAL, AUDIO/MIDI and LOAD ROM are hidden in the engine dropdown
- **Where:** shot `32-engine-menu.png`; the `#engsel` markup in `mdStudio.html`.
- **What's wrong:** the only way in is the 8px "EMU OS 1.63 ▾" label in the LCD corner. The header group that is literally labelled **SETUP** contains Undo, Redo and the panel colour.
- **Why it matters:** first-run users looking for audio devices or MIDI channels will not find them.
- **Fix:** rename the right-hand group to "Edit" (Undo, Redo, panel), and add a "Setup ▾" key there that opens Global / Audio-MIDI / ROM. Keep the LCD dropdown for the engine only (or keep both entries).

### 9. A page whose host never answers spins forever
- **Where:** shot `25-no-answer-blank.png`; code at `mdDeskTop.js:38` (`lifeOf` defaults to booting) and `deskBoot.js` `tick()` (the bar parks at 95%).
- **What happens:** with no machine document, the card says "Starting the Machinedrum…" indefinitely.
- **Fix:** after about 30 s with no `machine` message, switch the card to "The editor's engine is not answering. Close and reopen the plug-in window; the log is at …". The same goes for AUDIO/MIDI's "Reading the devices…" (`34-audio-midi.png`).

### 10. The GEN chips on the Sequence rail are clipped
- **Where:** shot `07-rail-2x.png`.
- **What you see:** "E 2/16+4" renders as "E 2/16+" on tracks 2, 6, 7 and 10, and the chips also crowd the short names.
- **Fix:** `min-width:max-content` with a narrower name column, or a shorter format ("E2/16+4"), with the full spec in the title.

### 11. Track short names differ between workspaces
- **Where:** shots `01-seq-1440.png` and `02-sound-1440.png`.
- **What you see:** the Sequence rail shows "14", "06", "29" for ROM-14, ROM-06, ROM-29, right next to the track numbers 13–16, which reads as a duplicate number. "B2" appears where Sound and Mix say "TRX-B2".
- **Fix:** never show a bare number. Use "R14" (and "BD", "B2" with the family colour as the prefix cue), or the same "ROM-14" everywhere.

### 12. LCD line 2 has two "LEN" fields; the second is an action
- **Where:** shot `06-lcd-header-2x.png`; code at `mdDeskTop.js:144`.
- **What's wrong:** `L2("dbl", "LEN", "×2")` is the Double command (it doubles the pattern), but it looks like a second length readout. The first LEN also changes the total length on a click, so a readout that edits sits beside an action that looks like a readout.
- **Fix:** label it "DBL ×2", or move Double into the Copy / Clr / Paste key group, where actions live.

### 13. GEN bar copy is cryptic
- **Where:** shot `08-genbar-2x.png`; code in `mdDeskGenUi.js` and `mdDeskSeq.js:21-23`.
- **What's unclear:**
  - "FOL" (follow).
  - "⌥R all".
  - "E 4/16 · ×2 · 8 on · steps 1–32".
  - Two different "?" keys (step legend and GEN help).
  - "M/S off" on the rail head, which looks like a disabled status rather than a button.
- **Fix:** use the words "Follow", "Alt+R: all tracks" and "Unmute all", and keep a single "?" that opens the keys sheet.

### 14. Hint and help typography is inconsistent
- **Where:**
  - Sentence-case Barlow Condensed hints sit next to monospace UI: "Draw to lock · ⇧ ramp · alt erases" (Sequence), "Click adds after row 001" (Song), the library footers, the select menus ("T1 Bass drum 2", "none").
  - 8px bold all-caps notes appear in places ("LIT: USED BY THIS KIT. FAINT: AN EMPTY SLOT.", Sampler rail, `04-sampler-1440.png`).
  - The Sound page's MUTATE hint reads "+ a group's title" (`mdDeskGenUi.js:223`).
- **Fix:** one `.hint` token (font, size ≥10px, colour). The MUTATE hint should read "Click a group heading to add it".

### 15. Clearing the whole pattern gives no feedback
- **Where:** shot `37-ask-clear-pattern.png`; code at `mdDeskComforts.js:9-14`.
- **What happens:** Alt+Delete, or Alt-click on CLR, wipes every trig and lock with no confirmation and no message. The kit library, by contrast, asks before a loss.
- **Fix:** at minimum, toast "Pattern A01 cleared · ⌘Z undoes". Ideally route it through the plug-in's Ask, like other lossy commands.

### 16. The keys sheet is taller than the default window
- **Where:** shot `35-keys-help.png`.
- **What you see:** the Anywhere rows are cut off at 924px. The copy also has jargon ("the other Alt that is not 'all'") and mixes "Alt" and "⌥".
- **Fix:** `max-height: calc(100vh - 48px); overflow:auto`, or a fourth column. Use ⌥ consistently, since the rest of the sheet uses symbols.

### 17. The machine picker opens on the wrong machine
- **Where:** shot `56-machine-picker.png`.
- **What happens:** the track plays B2 (highlighted cream), but focus and the preview strip are on BD ("TRX-BD BASS DRUM").
- **Fix:** on open, focus and preview the current machine.

### 18. The pattern chooser and kit library emphasise the wrong slots
- **Where:** shots `30-pattern-chooser.png` and `31-kit-library.png`.
- **What you see:** empty slots (E–H, K17–K64) have bright outlines and look more prominent than filled ones.
- **Fix:** draw empty slots dim with no border, and filled ones as solid tiles. Also make the titles consistent ("Patterns" vs "Kit library").

---

## Nice-to-have

### 19. The Sampler opens on a lonely card
- **Where:** shots `04-sampler-1440.png` and `60-sampler-rom-slot.png`.
- **What you see:** on a RAM slot, the main area is one centred SET UP SAMPLING card in a large empty field. On a ROM slot, the 48 ROM slots are listed twice (rail and main).
- **Fix:** top-align the card. Drop the rail's ROM grid when the main view shows it.

### 20. A second, older first-run dialog still exists
- **Where:** `mdDeskRom.js:9-21`, `firstRun()`; reached from `showRomInfo` when nothing is installed.
- **What's wrong:** its copy differs from the start-up card: "8 MiB, .bin" and "Press Check again" vs "(.bin or .zip, 8 MB)".
- **Fix:** route both callers to `Boot.update({state:"missing"})` and delete the old dialog.

### 21. The Song page's selected-row controls are hard to read
- **Where:** shot `05-song-1440.png`.
- **What you see:** "TEMPO" is followed directly by a "● KEEP" toggle and then "– 120 +", so it is unclear what KEEP applies to. "● MORE · part, mutes" looks like a toggle, not a disclosure.
- **Fix:** "Tempo [– 120 +] ☐ keep", and a ▸ chevron for More.

### 22. The panel-colour key shows the state, not the action
- **Where:** shots `01-seq-1440.png` and `70-mki-plate-seq.png`.
- **What's wrong:** the key says "MKII" while the MKII plate is showing.
- **Fix:** label it "Panel" with a small MKI/MKII LED pair.
- **Also on MKI:** disabled Undo/Redo look like enabled pale keys (`73-toast-mkii.png`). Lower their opacity on that plate.

### 23. The lock lane draws hatched bars when there are no locks
- **Where:** shot `01-seq-1440.png`.
- **What you see:** with "0 / 64 locked", tall hatched bars sit on the trig steps (the base value). They read as content or a glitch.
- **Fix:** draw the base as a thin line, and keep bars for real locks only. The playhead could also continue through the lane (`53-playing.png`).

### 24. Keyboard navigation of the workspace tabs
- **What's wrong:** each `role="tab"` is a separate Tab stop, there is no roving `tabindex` or arrow keys, and `aria-selected` is set but `aria-controls` is not.
- **Why it's minor:** the 1–5 shortcuts cover keyboard users.
- **Fix:** roving `tabindex` plus ←/→.

### 25. Toasts sit over the lock lane
- **Where:** shot `73-toast-mkii.png`.
- **What happens:** a toast appears exactly where the user is drawing, and passes over it at the same z-index as the gesture area.
- **Fix:** anchor toasts at the top right, under the LCD.

---

## What is already good (keep)

- **First-run ROM card** (`20-norom-firstrun.png`, `21-rom-error.png`): clear wording, one big target, privacy line, wrong-file state handled in place.
- **Boot card** (`22-booting.png`): honest progress text.
- **Failed edit:** the optimistic step reverts cleanly (`52`).
- **Ask dialog:** focuses the safe last key (`mdDeskApp.js:221`).
- **Consistency:** visual language is consistent across Sequence, Sound and Mix. The library popovers carry their keyboard hints, and the playhead and transport feedback are clear (`53-playing.png`).
