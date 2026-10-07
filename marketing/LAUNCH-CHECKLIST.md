# Launch checklist

*Started 2026-10-07, after 0.3.1 went public on mdmm.dev and the first Discord beta testers
got it. Launch day (L) = the first signed release plus the posts in [calendar.md](calendar.md).
Owner: **R** = Radek, **C** = Claude. Tick items as they land.*

## 1. Signing — blocks launch

- [ ] **R** Apple Developer Program, $99 a year, as the Account Holder (developer.apple.com/programs/enroll).
- [ ] **R** Two certificates: Developer ID Application and Developer ID Installer ([doc/release/SIGNING.md](../doc/release/SIGNING.md) § 1–2).
- [ ] **R** One App Store Connect API key for notarization (SIGNING.md § 3).
- [ ] **R** The 5 GitHub secrets on `radekdymacz/gearmulator-md-mm` (SIGNING.md, table).
- [ ] **C** Tag the next release, check the build is signed, notarized and stapled (`verify_mdmm_signed.sh`).
- [ ] **C** Site and brand copy: drop the "Open Anyway" steps for macOS once signed.
- [ ] **R** Decide later: Windows signing (SmartScreen warning). Not needed for launch.

## 2. Product

- [ ] **R** Test Record in the menu bar (2 minutes): record 10 s, check the file. Then **C** commits and merges `feat/record-menu`.
- [ ] **R** Ask one Discord beta tester on Windows to try the zip; note what breaks.
- [ ] **C** Linux build (in progress on `feat/linux-windows-release`).
- [ ] **C** Fix the known midiClock bug (`-Ofast` removes the `isfinite` check).
- [ ] **C** Collect beta tester feedback into GitHub issues (R forwards Discord messages).
- [ ] **C** Release review of the signed candidate: user journeys, code review, fixes.

## 3. Payments

- [ ] **R** Lemon Squeezy approval (reply sent 2026-10-07, waiting).
- [ ] **R** Switch the store from test mode to live mode.
- [ ] **R** Buy it yourself for €1 and refund it: check checkout, email receipt and VAT.
- [ ] **C** Check terms, refunds and privacy pages match the live store.

## 4. Videos

- [ ] **R** Watch the 6 finished reels (`~/Movies/MDMM Reels/Reels`); say keep, fix or drop for each.
- [ ] **C** + **R** Finish the paused demo work: a producer-grade beat, retrig, a real chain, effect throws; re-record `demo-md-full` and "Tight Sequencer".
- [ ] **C** Monomachine reels (none yet): piano roll (FR-08) and one more.
- [ ] **C** + **R** YouTube walkthrough, 3 minutes (FR-10); Radek does the voice-over.
- [ ] **R** Film the real Machinedrum next to the editor over MIDI (FR-09).
- [ ] **C** At launch only: turn the end-card URL on (`mdmm.dev`) and re-render.

## 5. Accounts (reserve the names now, post nothing until L)

- [ ] **R** Instagram
- [ ] **R** TikTok
- [ ] **R** YouTube channel
- [ ] **R** X (Twitter)
- [ ] **R** Bluesky
- [ ] **R** Mastodon
- [ ] **R** Forums: Elektronauts, Reddit (r/Elektron), Gearspace
- [ ] **C** Bios, avatars (from the site icons) and the bio links from [UTM.md](UTM.md), ready to paste.

## 6. Launch week

- [ ] **R** Message joelanders before launch (draft in [doc/release/drafts.md](../doc/release/drafts.md)).
- [ ] **C** Launch posts for Elektronauts, r/Elektron, Gearspace and the Discord, from [copy-bank.md](copy-bank.md).
- [ ] **R** Post on day L and follow [calendar.md](calendar.md) (one reel a day in launch week).
- [ ] **R** Answer comments for the first 48 hours; **C** turns bug reports into issues.
