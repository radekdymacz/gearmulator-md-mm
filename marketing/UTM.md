# UTM convention

Every link we publish carries all four tags. The site has no analytics
(no cookies, no third-party scripts), so the tags are read from the
Cloudflare request logs and from the pay-what-you-want checkout's referrer
fields. Keep them lowercase, hyphenated, and from the lists below; a new
value is added here first.

```
https://mdmm.dev/<path>?utm_source=<source>&utm_medium=<medium>&utm_campaign=<campaign>&utm_content=<content>
```

| Tag | Meaning | Values |
|---|---|---|
| `utm_source` | where the click happened | `instagram`, `tiktok`, `youtube` (Shorts and long), `reddit`, `elektronauts`, `gearspace`, `muffwiggler`, `kvr`, `newsletter`, `github`, `bandcamp`, `x`, `bluesky`, `mastodon` |
| `utm_medium` | kind of placement | `social` (organic post or bio), `video` (YouTube description), `forum`, `email`, `paid` (only with a paid campaign Radek approved) |
| `utm_campaign` | the campaign | `launch` (launch week), `evergreen` (weeks 2–5), `release-<version>` (e.g. `release-0-4-0`) |
| `utm_content` | which asset | the reel id (`md-one-screen`), or `bio`, `pinned`, `thread`, `description` |
| `utm_term` | optional variant | `a` / `b` when a reel is posted twice with different hooks |

Paths: `/` for reels and bios, `/get/` only where the post is explicitly "download"
(launch-day posts), `/guide/` for tutorial posts.

## Ready links

| Placement | Link |
|---|---|
| Instagram bio | `https://mdmm.dev/?utm_source=instagram&utm_medium=social&utm_campaign=launch&utm_content=bio` |
| TikTok bio | `https://mdmm.dev/?utm_source=tiktok&utm_medium=social&utm_campaign=launch&utm_content=bio` |
| YouTube channel link | `https://mdmm.dev/?utm_source=youtube&utm_medium=video&utm_campaign=launch&utm_content=channel` |
| Reel (pattern) | `https://mdmm.dev/?utm_source=<platform>&utm_medium=social&utm_campaign=<launch|evergreen>&utm_content=<reel-id>` |
| YouTube description | `https://mdmm.dev/?utm_source=youtube&utm_medium=video&utm_campaign=<campaign>&utm_content=<video-id>` |
| Forum launch thread | `https://mdmm.dev/get/?utm_source=<forum>&utm_medium=forum&utm_campaign=launch&utm_content=thread` |

Rules: Instagram and TikTok captions are not clickable; the caption says "link in
bio" and the bio link changes with the campaign (launch → evergreen in week 2).
On-screen URLs are always the bare domain. Never put personal data in a link.
Update the bio's `utm_campaign` on the day the campaign changes (calendar.md).
