# P7 design review: the Machinedrum and Monomachine Editors

- **When and what:** 2026-09-28, on `p6/simple-core` (0993cd9b).
- **How the findings were confirmed:**
  - in a browser harness of both skins (the shipped page files, run on a fake host), at 1280 × 720, 1440 × 900, 1440 × 760, 1800 × 700 and 1920 × 1080;
  - in the real standalone apps, through their self-tests;
  - on the firmware rigs.
- **Status column:** **Fixed** means fixed in P7 (see [P7-RESULT.md](P7-RESULT.md)). **Open** means proposed for later.
- **Item numbers** refer to Radek's list, where a finding comes from it. The unnumbered findings are the review's own.

## 1. Layout and scaling

The window used to open at the skin's size times the GUI scale, with a fixed aspect ratio. Below 1440 wide the page was zoomed out as a whole. The page itself was laid out for 1440 × 924: its rows, the lock lane, the editors and the faders were all fixed pixel heights.

| # | Finding | Severity | Status |
|---|---|---|---|
| 5 | **The page did not use the window.** At 1080 high the MD Sequence left 170 px empty. The MD Sound page used 558 of 788 px. At 780 high the Sequence ran 142 px past the bottom. The fixed aspect ratio meant the height could not grow on its own. | High | Fixed. The page editors now resize freely. The page is a column of fixed bars and fluid rows: the 16 track rows share what the lock lane leaves, and the lane fills to the bottom. The MD Sound editors and Mix faders grow with the height, as do the MM roll, faders and routing diagram. |
| 5 | **The lock lane overflowed the bottom** whenever the window was shorter than 924 px (MD). Its bars were drawn in pixels (168 px scale), so a taller lane could not be drawn. | High | Fixed. The lane is flexible and keeps at least the lock parameter list's height (232 px). Its bars are fractions of the lane. |
| 5 | **The window opened larger than the screen.** The MM at 140 % was 2028 × 1301 on a screen whose visible area is 1800 × 1056. macOS cut it to 1596 × 1024, and the fixed aspect ratio kept it at that. The MM Mix page then ran 27 px under the Dock, because its routing diagram's height came from its width. | High | Fixed. `windowFit` places the window inside the visible frame (menu bar and Dock excluded); it is pure and tested. The MM routing diagram now shares the height with the faders. |
| 5 | **The window forgot its size**: only a GUI scale was kept. | Medium | Fixed. A free editor keeps its width and height. A size forced by a smaller screen is not saved over the user's size. |
| — | **A `.body` rule reached the routing diagram's `rect.body`** once the layout used that class name: the MM Mix nodes were stretched 256 px tall. A name clash between layout classes and SVG classes. | Medium | Fixed (`.app>.body`). Keep layout selectors scoped. |
| — | **The stylesheets are sediment.** The MD mockup's CSS sets `.lb` height five times and `--row` three times, with more than 300 `!important`. Each design round added a layer instead of editing the rule, so each new rule has to out-shout the older ones. | Medium | Open. P7 adds one layout block and one LCD block at the end of each sheet and deletes the MM rules it replaced. A cleanup round should merge the older layers into single rules. |
| — | **The MM routing diagram's viewBox is fixed (1200 × 256)**; the diagram letterboxes in very wide windows. | Low | Open. Draw the diagram to the element's size. |
| — | **In a browser below 1440 px the mockups scroll sideways** (the header needs 1414 px). The plug-in zooms instead, so the apps are not affected. | Low | Accepted (the previews only). |
| 5 | **The MD Control and Song pages still leave space** at the bottom of tall windows. | Low | Open. They are lists and do not need it; they could use it for more rows. |

## 2. The LCD

The header's LCD is the editors' signature, but it read as a web form painted green.

| # | Finding | Status |
|---|---|---|
| 4 | **There was a stray vertical rule before the first field.** The rule was meant for fields 2 onwards (`:first-child`), but the firmware-boot overlay is the first child. | Fixed. The transport field never has a rule. |
| 4 | **The rules had four weights and opacities**: 1 px at 30 %, 35 % and 18 % inset, plus a 2 px bezel inside. | Fixed. There is one rule, `--lrule`: 1 px at 34 %. |
| 4 | **There was no pixel grid.** Labels were 9, 10 and 11 px of Silkscreen and values 12 to 16 px, depending on the window width, so the dots differed from field to field. | Fixed. Labels are 8 px (one dot = 1 px), values 16 px (2 px dots), line 2's values 12 px. At 2× these are whole device pixels: 2, 4 and 3. A faint dot matrix sits behind. |
| 4 | **The line heights were uneven.** PATTERN's label sat 2 px higher than TEMPO's because of the LED inside it; line 2 was 18 to 22 px depending on its content. | Fixed. Each text line box is its font size, and line 2 is a fixed 18 px row. |
| 4 | **The sync status had no fixed place.** The TX LED lived in PATTERN's label, and the MM's RECV state pushed the label sideways. | Fixed. A fixed-width sync slot on line 2 before COPY shows SYNC, SEND or RECV n, with the TX LED. On the MM it is also the key to the send dialog when edits wait. |
| 4 | **The MM's "MKII" print** was clipped by the header rule, with too little letter-spacing. | Fixed. It is centred under the bezel, 9 px at 0.4 em spacing. |
| — | **At a zoom of 0.6 (the smallest window), the 8 px labels are 4.8 px.** They are still legible on Retina and marginal on a 1× display. | Open. Consider a zoom floor of 0.7. |

## 3. Modals

There were eight dialogs in the two editors, each with its own placement, backdrop, Escape and outside-click code.

| # | Finding | Status |
|---|---|---|
| 3 | **Placement was inconsistent.** The libraries and settings were anchored under the LCD, the questions centred and the machine picker at its button; only the questions dimmed the page. | Fixed. One modal layer (the MODAL blocks, the same text in both editors, checked by the sync scripts) centres every dialog over a dimmed backdrop. |
| 3 | **Focus was not managed.** Tab walked out of a dialog into the page behind it, and focus was not returned on close. | Fixed. Focus moves into the dialog, Tab goes round inside it, and focus returns to where it was. |
| 3 | **The destructive key had the focus in a question** (for example "Switch and lose edits"), so Enter confirmed the loss. | Fixed. A question starts on its last key (Cancel or Close). |
| 3 | **A click outside a question closed it.** That was a silent Cancel on the MM, and on the MD it closed the library behind the question. | Fixed. The rules are data per kind: a question stays and nudges; a panel closes. The backdrop takes the click, so nothing behind it hears it. |
| 3 | **Escape was handled by several handlers at once**: the keys list, the library and the audio panel. | Fixed. The top dialog answers Escape, and nothing else does. |
| — | **A dropdown opened from a dialog** (the AUDIO / MIDI panel's device lists) now needs to sit above the modal layer. | Handled. Dropdowns stay popovers at their button, above any dialog. |

## 4. The workspace keys

| # | Finding | Status |
|---|---|---|
| 7 | **The keys' widths came from their labels**: MIX was 33 px next to SEQUENCE at 65 px, and the setup keys 37 to 47 px. The row looked broken. | Fixed. Workspace keys have a minimum of 60 px and setup keys 46 px, which fits at 1440. |
| — | **The header is full at 1440 wide**: 23 px spare between the LCD and SETUP now that the LCD is on its grid. | Watch. Any new header item needs a place in the grid, not a new field. |

## 5. The sequencer ergonomics

| # | Finding | Status |
|---|---|---|
| 1 | **Paging was the default**: a 32- or 64-step pattern showed 16 steps and hid the rest behind LEDs. | Fixed. All steps show by default (the approved "ALL" layout: columns shrink, with no horizontal scroll at 1440). PAGE, the page LEDs and FOL still page. |
| 8 | **The page keys sat above the grid at the right**, and the step legend sat in the lock lane's title line, far from the steps it explains. | Fixed. A foot row directly under the grid holds the legend in the middle and paging at the bottom right. The lock lane's title line keeps only its own words. |
| 8 | **A step was toggled per click.** Programming a run of trigs took one click per step, and each click was its own undo step. | Fixed. A drag paints: the first step decides on or off, and every step crossed takes that state, as one undo step. On the MM this applies to the SLIDE, SWING and envelope steps (its notes are the piano roll's own drag). The keyboard still toggles one step. |
| 8 | **MM mutes applied at once**, while the MD Editor already prepared mutes with Shift (the Elektron MUTE window). | Fixed. The MM prepares mutes with Shift too ("+" and "X", blinking), and they apply together when Shift comes up. It uses the same look as the MD. |
| 6 (root cause) | **A drag could stay on for ever.** A render during a drag (a pattern load, the machine's documents coming in) removed the element that held the pointer. Its pointerup then landed outside `#main`, and only `#main` listened for it. After that, every mouse move edited the dragged value, and the page held back the machine's documents because it thought a gesture was running. On the MM this showed as a kit marked edited after every pattern load, and a question on every switch. | Fixed. A gesture ends on the document's pointerup, on any move with no button down, and on leaving the window. Tested in both apps. |
| — | **No MD drag ever held the pointer.** The pointer-capture helper `capture(el, e)` was shadowed by the sampler model's later `capture(n, bins)` (in classic scripts the later function declaration wins), so every `setPointerCapture` call was a no-op. | Fixed (`grabPointer`). |
| 2 | **MM LEN could not change on the factory patterns.** A click stepped a page but stopped at 64, and every factory pattern is 64 steps. | Fixed. A click goes round the pages (16, 32, 48, 64, 16), shift-click goes back and a scroll steps one. Read back from the firmware. |
| — | **Lock drawing** still works as before, and it now fills the taller lane. | Checked (self-test p5). |
| — | **The MD's PAGE key and ALL are two ways to one state** (ALL off plus page n). | Accepted: this is the MD's own model. |

## 6. Sync (Step 3)

See [DESIGN-P7-sync.md](DESIGN-P7-sync.md). The plug-ins already sent the DAW's transport and clock to the firmware, but the machines' globals, as they boot, do not take them:
- the MD played its own tempo;
- the MM ignored all of it.

This is fixed in P7 with a model rule and a machine command, and there is no undo step. For two standalone apps the recommendation is Ableton Link, which needs approval of the SDK download. MIDI clock over IAC is the route that needs no dependency.
