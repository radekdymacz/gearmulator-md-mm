# Posting checklist (template)

The pipeline renders and drafts; **Radek posts**. Copy this block into the
calendar row or a note for each post, tick it on the day. Nothing in
`marketing/` posts, schedules, uploads, spends or sends.

```
Post: <reel id> · <platform> · <date, time CET>
- [ ] Gate: launch announced? (signed build live). Before launch: nothing goes out.
- [ ] Spec status is "approved" (marketing/reels/specs/<id>.json); "organic-only" reels are not boosted
- [ ] File: marketing/reels/out/<id>/<id>-<fmt>.mp4 plays to the end, sound on
        9:16 → Reels / TikTok / Shorts · 1:1 → feed, X/Bluesky/Mastodon · 16:9 → YouTube, forums
- [ ] check.txt says ok: -14 LUFS (±0.5), true peak ≤ -1 dBTP
- [ ] Watched once on a phone: hook readable in 2 s, subtitles not under the platform's buttons
- [ ] Cover / thumbnail: stills/<fmt>-hook.png (or a better frame), text readable small
- [ ] Caption from copy-bank.md (or the calendar), first line = the hook, ≤ 125 characters before "more"
- [ ] Honest line present: "macOS · bring your own ROM" (and "MIDI to real hardware: beta" if the post mentions hardware)
- [ ] Link: UTM link from UTM.md (bio link updated for the campaign; caption says "link in bio")
- [ ] Hashtags: platform set from copy-bank.md (IG 5–8, TikTok 3–5, Shorts 2–3 incl. #shorts)
- [ ] Credits/third parties: none needed, or credit + "not affiliated" exactly as BRAND.md says
- [ ] No Elektron logo, no "official", no endorsement wording
- [ ] Platform settings: comments on, TikTok duet/stitch on, YouTube "made for kids" no, Shorts linked to the long video if one exists
- [ ] After posting: spec "status": "posted", note the post URL + date in calendar.md
- [ ] Day +2: note views / profile clicks / site visits (CF logs, utm_content) in calendar.md
```
