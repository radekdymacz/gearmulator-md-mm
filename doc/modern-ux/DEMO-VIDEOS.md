# Demo videos: the editors on camera

Product videos of the Machinedrum Editor (and, once it has demos, the Monomachine Editor) for YouTube and Instagram,
made unattended from the real page against the real firmware, with the emulator's own sound. One command plays a
demo, records the app's window and the app's sound, and renders 16:9, 9:16 and 1:1.

```sh
scripts/mdmm-demo-video.sh md groove                       # play demo-md-groove, record, render
scripts/mdmm-demo-video.sh render temp/videos/<run folder> # render again from its raw.mov (after editing captions.txt)
```

Output, under `temp/videos/<demo>-<date>/` (gitignored; `MDMM_DEMO_OUT` for elsewhere):

| File | What |
|---|---|
| `raw.mov` | the recording: the editor's window content (1440x810 points at 2x, 60 fps max) and the app's sound only |
| `page.log`, `timeline.txt` | the page's log (each demo step's time and caption) and the cut taken from it |
| `timeline.json` | the take for other tools (the reels): each bar line, pattern change, section and step on the video's clock, and the raw take's offset (raw time = video time + offset) |
| `captions.txt` | the copy: captions and end card, editable (below) |
| `<demo>-16x9.mp4`, `-9x16.mp4`, `-1x1.mp4` | 1920x1080, 1080x1920, 1080x1080; H.264 + AAC, -14 LUFS, true peak under -1 dBFS |
| `stills/` | a frame of each render: the hook (0.8 s), the middle, the end card |
| `check.txt` | the listen check: loudness, true peak, silence; `RESULT ok` or what is wrong |

## How it works

1. **The app.** The script starts the diagnostics standalone (`bin/plugins/Release/Standalone/Machinedrum Editor.app`, or
   `MDMM_APP_DIR`) from its path with `GEARMULATOR_MDSTUDIO_SELFTEST=demo-md-<name>`. Everything after that goes by
   its process id, never by the bundle id: an older download of the app has the same id, and a lookup by id would
   launch that one (it rewrites the editor's config for its own skins). Before the start the editor's config and the
   standalone settings are backed up; the run sets the skin and the editor's size (`MDMM_DEMO_SIZE`, 1440x810: 16:9 at
   the page's design width) and puts both back byte for byte afterwards, pass or fail, as `mdmm-journeys.sh` does.
2. **The demo.** A demo is a journey (`skins/shared/deskJourney.js`) played by `Journey.demo`: once the machine is
   ready it runs the demo's `setup` steps (the fullest pattern, PLAY) off camera, shows a drawn pointer, logs
   `DEMO READY`, waits four seconds for the recorder, then plays the steps at a person's pace: the pointer glides to
   each control and rings on a click, a key cap shows each key, and each step `hold`s so the machine is heard. Every
   step is still checked on the screen and on the machine, so a demo that stops working fails like a journey.
3. **The recorder** (`tools/mdmm-recorder`, Swift, ScreenCaptureKit, no third-party code; `tools/mdmm-recorder/build.sh`
   builds it into `temp/mdmm-recorder/`) records the display with that app alone on it, cut to the window's content
   (`sourceRect`), plus that app's audio only (nothing else on the machine is heard, nothing covers the window). It
   starts on `DEMO READY` and stops on SIGINT once the page logs `DEMOS DONE`.
4. **The render** (ffmpeg): the cut runs from 0.3 s before the demo's first step to 0.4 s after its last. A blurred,
   darkened copy fills each frame behind the window; captions are burned in (bottom band on 16:9 and 1:1; on 9:16 a box
   under the window, above the bottom 20 % and clear of the buttons on the right); the end card fades in over the
   last seconds while the sound fades out. Loudness: loudnorm's two passes to -14 LUFS, then a limiter. This ffmpeg
   has no `drawtext`, so `mdmm-recorder caption` and `mdmm-recorder card` draw the text as PNGs (the site's look:
   black, white, lime `#c8ff00`, sharp edges, the system sans since Inter is not installed).
5. **The check** (`check.txt`): integrated loudness near -14 LUFS, no true peak above -0.5 dBFS, no silence over a
   second, but before the demo's first bar (a song from an empty pattern starts quiet) and a STOP of at most 2.5 s. The script exits non-zero when the demo or the check fails.

## Rules for a take

- **Dry run first:** `MDMM_DEMO_DRY=1 scripts/mdmm-demo-video.sh md <demo>` plays and records the demo and runs the
  level check only (no videos). `scripts/mdmm-demo-video.sh levels <run folder>` checks a take again.
- **The level check** (`levels`, in `check.txt` too) fails a take with a bar more than 4.5 dB above the two bars before
  it and the two after it, or a level climbing three bars in a row (at least 1.5 dB a bar, 5 dB in all): the signs of a
  feedback loop. A drop or a break moves one side only and passes.
- **The sampler:** Set up sampling puts the recorder's track at VOL 0 while it samples the main mix (it would record
  itself otherwise: a feedback loop). Never retrig or Control-All the recorder; chops come after the capture froze.
- **Moves:** small and deliberate, one at a time, within musical ranges, each held a bar or more; the echo's feedback
  moderate and set back. A sweep is a section's point, not a side effect.
- **ScreenCaptureKit** sometimes ends a recording by itself ("Unknown stream error"); the recorder then exits 4, the
  run is not used, and the script plays it again (`MDMM_DEMO_TRIES`, 3).

## Changing the copy

`captions.txt` in the run folder is written from the demo's own captions on the first render and kept after that:

```
0.00|2.39|Your Machinedrum, on screen.
2.54|7.36|Roll a fresh hi-hat groove
card|3.5|Machinedrum Editor|Free · pay what you want|Coming soon
```

`start|end|text` in seconds from the start of the video; about six words a line, two lines at most (`\n` breaks a
line, a longer line is split in two). `card|seconds|name|line` is the end card over the last seconds; a fifth field
is the big lime line under them: "Coming soon" until launch, then the URL (`|mdmm.dev`). Edit it,
then `scripts/mdmm-demo-video.sh render <run folder>`; no app, no re-recording. A captions file given as the last
argument replaces it; `none` burns in no captions.

## Adding a demo

In the page's journeys file (`skins/mdStudio/mdDeskJourneys.js`; the Monomachine's `skins/mmStudio/mmJourneys.js`
then needs the same `Journey.demo(demos, …)` call), add a value to `demos` named `demo-md-<what>`:

```js
{ name: "demo-md-<what>", card: "Machinedrum Editor|Free · pay what you want|mdmm.dev",
  setup: [ /* steps before the camera: get the machine playing */ ],
  steps: [{ say, caption, act: async (u, c) => { await look(u, "#play"); }, screen, machine, hold: 1500 }, …],
  tidy }
```

- Act only through the page's controls, as a journey does; `look(u, q)` glides the pointer to the control and clicks
  it; `u.glide(q)` before `u.drag` shows the pointer travelling first.
- Open on sound (a `setup` that starts the pattern): the first two seconds are the hook. Keep a demo near 30 s and
  end with a step that just `hold`s about 4 s for the end card.
- A demo is not in `all`, so `journey` never runs it; the session accepts `demo…` for `GEARMULATOR_MDSTUDIO_SELFTEST`
  and `GEARMULATOR_MMSTUDIO_SELFTEST` (diagnostics builds only).
- Rebuild the diagnostics standalone (the page is in the binary), then run the script.

## What it needs

- **A diagnostics build**: configure with `-Dgearmulator_MDMM_DIAGNOSTICS=ON` (FOUNDATION.md, Build and check), build
  `mdJucePlugin_Standalone`. On this Mac: `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`, the Xcode SDK.
- **The ROM** in `~/Documents/Gearmulator Preview/Machinedrum/roms/` (never copied or committed).
- **Screen Recording** for the app that runs the script (Terminal, iTerm, or the agent host): System Settings >
  Privacy & Security > Screen & System Audio Recording, then restart that app. Without it the recorder exits 3 and
  says so. No Accessibility or Automation permission is needed (the window is sized through the editor's config and
  fronted by process id).
- **ffmpeg** with libx264 (`brew install ffmpeg`) and python3.
- The display must be awake (the script wakes it and keeps it on with `caffeinate`; a locked screen needs Radek).
- The window must be on screen while the demo plays (a covered page draws no canvases); do not use the Mac meanwhile.
  macOS shows its screen-recording indicator in the menu bar while the recorder runs.
