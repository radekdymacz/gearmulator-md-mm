# Machinedrum Editor + Monomachine Editor — brand

*Read this before writing a hook, a caption, a reel spec or any copy that names
the product. The landing page (`site/public/index.html`, mdmm.nativekloud.com)
is the source of truth for claims; this file never claims more than the site does.*

## Names

| Context | Write |
|---|---|
| The pair, a heading, the site | **MD + MM Editor** |
| One editor | **Machinedrum Editor**, **Monomachine Editor** (capitals, no "the" in titles) |
| The hardware | Elektron Machinedrum, Elektron Monomachine — "Elektron" once per post, then Machinedrum / MD |
| URL on screen | `mdmm.nativekloud.com` (bare, no https, no UTM: it is read, not clicked) |
| URL in a caption or bio | the UTM link from `UTM.md` |

Never "Gearmulator MD" in marketing (that is the plug-in's file name in a DAW,
fine in the user guide), never "MDMM" as a product name, never "official".

## Voice

Short, plain, confident, slightly dry. Musicians who own (or miss) the boxes.
Show the screen; let the groove carry it. British English.

- Lead with the thing you see: "Mute three tracks. One drag." Not a feature list.
- Verbs and nouns, few adjectives. No "revolutionary", "insane", "game-changer".
- One idea per reel, one idea per line, six words a subtitle line at most.
- Numbers only when they are true and visible on screen (16 tracks, 64 kits, one undo step).

## The honest lines (use them, don't hide them)

Every post, caption or end card that could be someone's first contact carries
the short form: **macOS · bring your own ROM**. Longer copy carries the rest:

- **Your own ROM.** Machinedrum OS 1.63 or Monomachine OS 1.32B. No firmware is included and we cannot supply it. Never hint where to get one.
- **macOS only** for now (tested on Apple silicon). Windows/Linux: "not yet", never a date.
- **Real hardware over MIDI is beta**, so far tested only against the emulator. Say "drives your own machine over MIDI (beta)". Do not show or claim a real machine working until Radek has filmed it working (FOOTAGE-REQUESTS.md, FR-09). Over MIDI: no sample transfers; live recording and chains need the emulator.
- **Not notarised by Apple** (until signing lands): the first install needs Open Anyway. Launch waits on signing, so launch copy may drop this line only once the signed build is out.
- **Free software (GPL-3).** Free, pay what you want; €0 is fine. Never "free trial", never "Pro".
- **Alpha software:** keep backups. Fine to say in long copy and replies.

## Elektron, credits, third parties

- **Never imply Elektron endorsement**, partnership or approval. No Elektron logo, no Elektron product photos from their site, no "the official editor", no "Elektron-approved". The trademark line goes wherever there is room for it (YouTube descriptions, the site): *Elektron, Machinedrum and Monomachine are trademarks of Elektron Music Machines. This project is independent and not affiliated with or endorsed by Elektron.*
- Filming a real Machinedrum that Radek owns is fine; framing it as "made with/for Elektron" is not.
- **Credits** (long copy, YouTube descriptions, launch posts): built on joelanders' gearmulator-md-mm and The Usual Suspects' Gearmulator. Thank them; never make them the hook.
- **Third-party SysEx (Autechre's public 2008 backup):** organic posts only, never boosted or in ads, never on the landing page. Always credit (Autechre, their public 2008 Quaristice-tour backup, link to their store) and say we are not affiliated. Never imply they use, know of or endorse the editors ("Autechre's kits in the editor" is fine; "as used by Autechre", "Autechre's editor" are not). Radek decided 2026-10-06: no contact with Warp.
- Any other artist's sounds, files or name: only after Radek confirms rights in writing; until then a neutral placeholder.
- Music in reels is the editor's own output. No trending tracks, no licensed music.

## Do / don't

| Do | Don't |
|---|---|
| "Free · pay what you want" | "Free for a limited time", "Pro version coming" |
| "runs the real firmware, emulated" | "100% accurate", "identical to hardware" |
| "drives your own machine over MIDI (beta)" | "works with your Machinedrum" (unqualified) |
| "bring your own ROM" | anything that hints where ROMs are found |
| show the real UI, real audio | mock-ups, sped-up audio, fake waveforms |
| "macOS · VST3, AU and app" | "every DAW", "Windows soon" |

## Look

From `site/public/design/tokens.css` + `site/public/assets/site.css`; the reel
renderer copies them into `marketing/reels/src/brand.ts` (change the site first).

| Token | Value | Use in reels |
|---|---|---|
| `--hw` | `#161614` | ground behind footage, end card |
| `--hw-key` / `#3d3f3b` | `#30312f` | the step-key row on the end card |
| `--hw-text` | `#ecebe4` | end card title |
| `--hw-muted` | `#a3a29b` | small print |
| `--accent` (dark) | `#ff6a3d` | the accent word in a hook or subtitle, the URL plate |
| `--led` | `#ff4b2b` | the LED dot, the subtitle underline, the running step |

Flat, sharp-edged (radius 0 on brand surfaces, small radius only on hardware keys), no glows except the LED.

**Fonts.** Inter (the site's `--font-sans`) for everything big; JetBrains Mono for the eyebrow and small print (the site uses the system mono, which cannot be bundled). Both are SIL Open Font License 1.1, bundled from npm (`@fontsource/inter`, `@fontsource/jetbrains-mono`), so rendering never fetches a font and redistribution is allowed. The editor's own LCD pixel font stays inside the footage.

**End card:** "Machinedrum Editor" (or "Monomachine Editor"), "Free · pay what you want", "macOS · VST3, AU and app · bring your own ROM" small. **No website URL in reels for now** (Radek, 2026-10-06): `endCard.url` is optional and off; until launch the accent plate carries `endCard.status` ("Coming soon", Radek 2026-10-07). At launch: set `url` to `mdmm.nativekloud.com` and drop `status`.
