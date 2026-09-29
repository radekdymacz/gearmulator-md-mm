# Design critique: mdmm.nativekloud.com, first version (2026-09-28)

Reviewed: `c066afb5` (landing page + 3-step download flow), at 375, 1280 and 1920 px, light and dark.
Screenshots of that version: `site/review/before/`. Framework: first impression, hierarchy, consistency,
accessibility, then conversion (download and pay-what-you-want). Radek's review points are binding and
are folded in; they are marked **[R]**.

## 1. First impression (5-second test)

- **What a visitor sees first:** an eyebrow, a two-line headline, a 60-word paragraph, a strip of grey
  squares, two buttons and four tags, and only then the product. The product shot sits below the fold at
  1280 x 800. The page asks you to read before it shows you anything.
- **What they should see first:** the editor. It is the whole argument: a screen-native Machinedrum and
  Monomachine UI that nobody else has. **[R]** Hero like a normal SaaS page: headline, one sentence, one
  button, then a large framed product shot.
- **The step strip** (plain grey and white squares) reads as a generic loading bar, not as our sequencer.
  **[R]** Either our real key style (dark keys, red LED bar, accent and lock marks, the soft playhead) or
  nothing. It becomes a small accent, not the centrepiece.
- **Tone:** accurate but heavy. 1,700 words on the landing page; most of it is true and useful to someone
  who has already decided, not to someone deciding. **[R]** Aim for 30-40 % of the copy.

## 2. Hierarchy

| Issue | Where | Severity | Fix |
|---|---|---|---|
| Product shot below the fold | hero | high | shot directly under the headline, full container width |
| 12 feature cards of 30-40 words each | Features | high | 6 items: icon, label, one short line **[R]** |
| Two deep sections (HW MIDI limits, honest limits) in the main flow | Engine, Limits | high | one-line beta note; limits become a collapsible FAQ item **[R]** |
| Licence, credits and trademark as full sections | Credits | medium | move to the footer, small but complete **[R]** |
| "FREE · OPEN SOURCE · GPL-3" eyebrow | hero | medium | the page says "free"; licence detail lives in the footer **[R]** |
| 10-tab screenshot tour | Tour | medium | a strip of 4 shots with one-line captions **[R]** |
| Three CTAs with different labels ("Download — pay what you want", "Get the editors", "Download") | page | low | one label everywhere: "Download free" |

## 3. Consistency

- **Glow and blur contradict the brand** (sharp, flat): frosted sticky header (`backdrop-filter`), glowing
  primary buttons and selected cards (`--glow` in dark), blurred drop shadows on screenshot frames. **[R]**
  Remove all of them; flat edges, a 1 px rule, a hard offset shadow at most.
- Screenshot frames used a made-up bar (three grey dots and a mono title). A native macOS title bar reads
  instantly as "this is an app on your Mac".
- Mono uppercase labels were used for eyebrows, tags, tabs, captions and badges at once, so none stands
  out. Keep mono for product-flavoured details only (step labels, the plate switch, prices).
- Light default: tokens make light the default but the first version followed the OS into dark.
  **[R]** Light always by default; dark only via the toggle, remembered per visitor.

## 4. Accessibility

- Contrast: body, muted and accent-ink text pass WCAG AA in both themes (accent-ink `#92400e` on
  `#fbfbf7` is 6.8:1). The dark header "Download" button lost its text colour to `.nav a` (amber on amber):
  a contrast failure, fixed in the first version's follow-up and kept fixed.
- Keyboard: skip link, visible focus, tabs with roving tabindex all worked. The redesign keeps a skip
  link and focus rings, and uses `<details>` for the FAQ (keyboard and screen-reader native).
- Motion: the step strip respected `prefers-reduced-motion`; keep that, and pause when the tab is hidden.
- Screenshots have descriptive alt text; the decorative sequencer is `aria-hidden`.
- Mobile: no horizontal scroll after the grid fix; the overlapping second window must be hidden on
  narrow screens so the lead shot stays legible.

## 5. Conversion: download and pay what you want

- The support ask sat on its own step **before** the download, which is the moment of least value (the
  person has not used anything yet) and adds friction. **[R]** Ask at the download page, the moment of
  value, with a gentle second touch after the download starts.
- The ask gave no reason. **[R]** A short, human "why": one person builds these; money pays for
  real-hardware testing (a Monomachine), and new features.
- Amounts had no meaning. **[R]** Name what an amount buys ("€10 = one feature evening"), with €10
  preselected, and say plainly that €0 is fine.
- **Payment provider [R, decision]:** Lemon Squeezy only, as merchant of record. It is a sale, not a
  charity gift, so the UI never says "donate"; it says "Pay what you want", "Name your price (€0 is fine)",
  "Support development", "Thanks for supporting". PayPal is removed.
- A goal bar and a supporters list only if honest: both are off in the config by default, no fake numbers.
- Never a dark pattern: "No thanks, just download" stays as visible as the pay button; nothing is
  pre-ticked except the suggested amount.

## 6. Redesign plan (what changed)

1. Hero: headline, one sentence, "Download free", then the MD editor (MKI plate, the lead) in a native
   window with the MM editor offset behind it; an MKI/MKII switch in the title bar.
2. A tiny 16-step accent in the editor's real key style with the soft gliding playhead at 120 BPM.
3. Six feature items with line icons, one line each, plus a one-line HW MIDI beta note.
4. A 4-shot tour strip.
5. A short FAQ (ROM, real hardware, known limits, is it free), collapsible.
6. One CTA, then a footer with the GPL-3 note, source link, credits and the trademark notice.
7. Checkout becomes two steps: choose, then download with the pay-what-you-want panel beside the links.
