# Go-to-market: from 48 testers to 1.0

*Drafted 2026-10-09. Sits on top of the existing plan: [BRAND.md](BRAND.md) (what we may claim),
[LAUNCH-CHECKLIST.md](LAUNCH-CHECKLIST.md) (gates), [calendar.md](calendar.md) (reels),
[copy-bank.md](copy-bank.md) (hooks), [UTM.md](UTM.md) (links). This file answers one question:
where do we post, in what order, with what angle, to get the right people to mdmm.dev.
Radek posts; Claude drafts. Every venue's rules change: read its rules page before the first post.*

## Summary

- The audience is narrow and findable: people who own (or owned, and kept backups from) a Machinedrum
  or Monomachine. Bring-your-own-ROM means the casual "free plug-in" crowd mostly can't use it.
  Go deep in the Elektron places first; wide channels (Hacker News, press) bring stars and links, few users.
- Two stages, so no community is spent twice on the same news:
  - **Now (0.3.x/0.4): "beta, testers wanted"** in the Elektron core only. Recruits Windows/Linux testers
    (both builds are untested by a person), finds bugs, builds a list of people to tell at 1.0.
  - **1.0 (signed, stable): the launch.** Press, Hacker News, wider Reddit, YouTubers, KVR.
- The 48 Discord testers are the best asset: quotes, beats, Windows reports, and the first replies in
  every launch thread. Never ask them to upvote (Reddit bans vote rings, and it shows).
- The single clip owners will care about most is a real Machinedrum driven from the editor (FR-09).
  Film it before 1.0, or the launch leans on emulator-only footage.
- Measure per venue with UTM links and per-venue Discord invites; drop venues that send nobody.

## Gates (from BRAND.md and LAUNCH-CHECKLIST.md)

| Gate | Blocks |
|---|---|
| Signed and notarised macOS build | anything beyond the Elektron core (press, HN, wide Reddit) |
| A person has run Windows x64 (B-022 fixed) | saying "Windows" anywhere; until then "macOS; Windows/Linux builds want testers" |
| FR-09 filmed (real MD over MIDI) | any claim or clip of real hardware |
| Lemon Squeezy live | linking pay-what-you-want in launch posts |

## Stage 1 — now: beta, testers wanted

Goal: 150–200 engaged Discord members and 5+ Windows/Linux testers before 1.0. One post per venue.

| # | Where | Post | Link |
|---|---|---|---|
| 1 | Elektronauts, Machinedrum category | "Free open-source editor for the MD: whole machine on one screen, runs the real OS 1.63 emulated. Beta; Windows/Linux testers wanted." Screenshot + 30 s clip. Stay in the thread and answer every question for 48 h | `/` with `utm_source=elektronauts&utm_campaign=beta&utm_content=thread-md` |
| 2 | Elektronauts, Monomachine category | Separate thread, MM angle: piano roll, one-screen view. Link the MD thread | `utm_content=thread-mm` |
| 3 | r/Elektron | Text post with the clip; title is what it does, not "check out my app". Flair if required. Same testers ask | `utm_source=reddit&utm_medium=forum&utm_campaign=beta` |
| 4 | r/drummachines | Short demo video (md-beat-from-scratch), testers line in a comment | as above, `utm_content=md-beat-from-scratch` |
| 5 | Upstream Gearmulator / joelanders' Discord (#gearmulator-development) | Only after asking a moderator and joelanders (draft in doc/release/drafts.md). Emulation users are early adopters and already understand "own ROM" | Discord invite only |
| 6 | Search Facebook for Machinedrum and Monomachine owner groups | Join, read the rules, one post with the clip. Owner groups skew older and own the boxes: the exact audience | `utm_source=facebook` (add to UTM.md first) |
| 7 | Old threads where people asked for an MD/MM editor or VST (Elektronauts, Gearspace, Reddit search "machinedrum editor", "machinedrum vst", "monomachine editor") | Reply only where the thread is recent enough that the forum allows it; one line + link. Collect the list first, Claude can draft each reply | per venue |

New UTM values needed before Stage 1: campaign `beta`, source `facebook`. Add them to UTM.md.

## Stage 2 — 1.0 launch

Order matters: home turf first (warm, forgiving), then wide. Day L = 1.0 release day, a Tuesday or Wednesday.

### Day L, morning (CET)

- GitHub release + site + Discord #announcements (automatic).
- Elektronauts: one launch post in each machine thread from Stage 1 (bump with "1.0 is out", don't start a third thread).
- r/Elektron: new post, "1.0" in the title, the one-screen reel.
- Email the press list below (same text, personalised first line). No embargo.

### Day L, 15:00–16:00 CET (US morning)

- Hacker News, Show HN. Title: "Show HN: Open-source editors for the Elektron Machinedrum, running its original firmware".
  First comment from Radek: why it exists, how the emulation works (DSP56300 JIT, 68k), the ROM rule up front,
  credits to joelanders and The Usual Suspects. Link the GitHub repo, not the store.
- lines (llllllll.co): post in the library/development area; the open-source, monome-adjacent crowd likes GPL tools.

### Days L+1 to L+7

- r/synthesizers, r/musicproduction: video post or the sub's self-promotion thread, whichever the rules say.
- r/emulation: technical angle (emulating the MD's DSP and 68k, running stock firmware). Not a product pitch.
- r/macapps (and r/Logic_Studio, r/ableton only as a how-to: "using a Machinedrum emulation as an AU/VST3").
- r/opensource: GPL-3 angle, pay-what-you-want.
- Gearspace, Electronic Music Instruments forum: launch thread. Check whether developers may post in the
  New Product Alerts forum; if not, the EMI forum only.
- KVR Audio: developer account, list both editors in the product database (macOS, VST3, AU, standalone, free),
  then submit a news item. KVR listings rank in search for "<product> VST" for years.
- Skip: r/WeAreTheMusicMakers and r/edmproduction (strict self-promotion rules, wrong audience), r/dawless (anti-computer).

## Press and blogs (email at 1.0)

All take tips from developers. One short email: what it is, three screenshots, one video link, the honest
lines (own ROM, macOS, free/GPL), credits. Claude drafts; Radek sends.

| Outlet | Why it fits |
|---|---|
| Synthtopia | Covers free/open-source synth software and emulations |
| CDM (Create Digital Music) | Open-source and emulation stories, Elektron-literate readers |
| Synth Anatomy | Posts new free plug-ins and emulations quickly |
| Rekkerd.org | Free plug-in news, high volume |
| Bedroom Producers Blog | Free plug-in news and roundups; caution: ROM requirement, say it in line one |
| Gearnews | Gear news, covers Elektron community tools |
| MATRIXSYNTH | Vintage/rare gear blog; Machinedrum and Monomachine are its territory |
| Sonicstate | News + Nick Batt's video channel |
| MusicRadar | Free plug-in roundups; pitch for inclusion later, not launch day |

## YouTube creators (after 1.0, after FR-09 is filmed)

Pitch: a free editor for a box they may own, a 3-minute walkthrough link (FR-10), offer a short call.
Check each channel's last 10 videos for fit before writing. No payment, no "review in exchange for".

- Elektron-heavy: Cenk Sayinli, Starsky Carr, Red Means Recording.
- Gear review/explainer: Loopop, Sonicstate, Ricky Tinez.
- Retro/vintage gear: Espen Kraft.
- Smaller Machinedrum/Monomachine channels: search YouTube for recent MD/MM videos (under 20k subscribers);
  they reply more often and their viewers own the hardware. Aim for 10 small over 1 large.

## Use the Discord (start this week)

| Action | Why |
|---|---|
| Per-venue Discord invites (Server Settings › Invites: one code per venue, e.g. Elektronauts, r/Elektron, HN) | Shows which venue brought members; Discord counts uses per invite |
| Ask 5–6 testers for a one-line quote with permission to use their handle on the site | Social proof on mdmm.dev and in launch posts |
| A #made-with channel: testers post 30–60 s beats from the editor; repost the best (with permission) | Real users' music beats our own demos |
| Pin a "Windows/Linux testers wanted" post with exactly what to report | Unblocks the Windows gate |
| At 1.0: tell testers where the launch threads are and invite honest comments | Early replies keep a thread alive; no vote asks |

## Owned channels and search

- Google Search Console and Bing Webmaster Tools: verify mdmm.dev, submit sitemap.xml.
- One guide page per search intent: "Machinedrum editor", "Machinedrum VST / AU", "Monomachine editor",
  "Import Machinedrum SysEx backups (.syx)". Each answers the question in line one.
- GitHub: repo description "Machinedrum + Monomachine editors (VST3/AU/app) running the original firmware";
  topics `machinedrum`, `monomachine`, `elektron`, `vst3`, `audio-unit`, `emulator`, `juce`.
  Point the README's "latest" line at 0.3.4 (it still says 0.3.1).
- AlternativeTo: list both editors (free, open source, macOS).
- A release email list (the site has none; Discord is the only way back to a visitor). Radek decides
  whether to add one; the privacy page would need a line.
- YouTube channel: the walkthrough (FR-10) plus Shorts from calendar.md; titles carry the search words.

## What we never do

- Mention, hint at or reply about where ROMs come from. Delete-worthy in every venue listed here.
- Imply Elektron endorsement, use their logo, or say "official".
- Cross-post the same text to many subreddits on one day (spam filters; reads as spam).
- Ask anyone to upvote or "go comment".
- Promise dates or Windows support in a public reply.

## Measure

| Signal | Source | Read it |
|---|---|---|
| Visits by venue | Cloudflare logs, `utm_source`/`utm_content` | weekly |
| Downloads | GitHub release asset counts per version | weekly |
| Discord joins by venue | per-venue invite uses | weekly |
| Paid (any amount) | Lemon Squeezy, referrer fields | monthly |
| Thread quality | replies, questions, bug reports raised | per post |

After four weeks: keep the three venues with the most downloads per post, drop the bottom half.

## Next actions

| # | Who | Action |
|---|---|---|
| 1 | R | Create per-venue Discord invites (Elektronauts, r/Elektron, r/drummachines, Facebook, HN) |
| 2 | C | Draft the Stage 1 posts (Elektronauts MD, Elektronauts MM, r/Elektron, r/drummachines) for Radek's OK |
| 3 | C | Add `beta` and `facebook` to UTM.md; update the README "latest" line |
| 4 | R | Ask joelanders / the upstream Discord mods whether a beta post is welcome |
| 5 | C | Draft a #bugs-style pinned "Windows/Linux testers wanted" post and a quote request for testers |
| 6 | C | Collect old "MD editor/VST" threads into a list with a draft reply each |
| 7 | R | Search Console + Bing for mdmm.dev |
| 8 | C | Draft the press email and the Show HN first comment, held until 1.0 |
| 9 | R | Film FR-09 (real MD over MIDI) before 1.0 |
