# Reels

A reel is a JSON spec (`specs/<id>.json`, schema `mdmm.reel/1`, types in
`src/spec.ts`); Remotion draws it, ffmpeg normalises it. One command renders
a reel in 9:16 (1080x1920), 1:1 (1080x1080) and 16:9 (1920x1080).

```sh
cd marketing/reels
npm ci                                   # pinned versions (package-lock.json)
ln -s <recorder run>/raw.mov ../footage/<file named in the spec>   # gitignored
npm run validate -- specs/md-one-screen.json
npm run render -- specs/md-one-screen.json            # all three formats
npm run render -- specs/md-one-screen.json --formats 9x16
npm run studio                            # live preview of the default spec
```

Output (gitignored) in `out/<id>/`: `<id>-9x16.mp4`, `<id>-1x1.mp4`,
`<id>-16x9.mp4`, `check.txt` (measured loudness, must say ok) and `stills/`
(hook, the middle of each shot, end card, per format). Needs Node ≥ 20 and
ffmpeg on the PATH. Rendering never posts or uploads anything.

## Writing a spec

- `footage`: file in `marketing/footage/`, UI size in points (1440 x 810) and the pixel scale (2).
- `footage.timeline` + `inBar` (preferred): point at the demo recorder's `timeline.json` (symlinked next to the footage) and start shots at measured bars (`inBar`, fractions allowed; `endCard.audioInBar`). `scripts/bars.mjs` turns them into footage seconds before rendering, so cuts follow the real song even where the tempo drifts or the transport stopped. `footage.syncOffset` adds the capture's audio latency (0.025 s for the 2026-10-06 takes).
- `music`: BPM and the footage time of the first downbeat
  (`node scripts/onsets.mjs ../footage/<file> --from <s> --bpm <n>` prints onsets).
- `shots`: each starts at a beat (`inBeat`) or a time (`inSeconds`, for footage before the
  music) and lasts whole `beats`, so cuts land on the beat. Keep each jump in the source a
  whole number of bars and the groove never skips (the validator warns otherwise).
- `camera`: keyframes of a UI region (points) to frame, eased; `byFormat` gives 9:16 its own
  tighter, taller region. `spotlight` dims everything but one region.
- `subtitles`: per shot, ≤ 6 words a line, `*word*` takes the accent colour.
- `hook`: ≤ 7 words, on screen for the first 2 s over the first shot.
- `endCard`: title, "Free · pay what you want", a small honest line, and an optional `url` (off by default: no website link in the reels yet; when on, a bare domain);
  `audioInBeat` picks the bar of groove under it (faded out).
- `brief.use`: `paid-ok` or `organic-only`. `status`: idea → needs-footage → draft → approved → posted.

Loudness: two-pass ffmpeg loudnorm to -14 LUFS integrated, true peak target -1.5 dBTP;
the render fails if the measured result is off by more than 0.5 LU or peaks above -1 dBTP.

Brand values live in `src/brand.ts` (copied from the site's tokens), layouts and safe zones
too: 9:16 keeps text out of the top 250 px, bottom 420 px and the right-hand button column.
Rules for words: `../BRAND.md`, `../copy-bank.md`. Footage asks: `../FOOTAGE-REQUESTS.md`.
