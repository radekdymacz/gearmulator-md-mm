#!/bin/sh
# Product videos of the editors (doc/modern-ux/DEMO-VIDEOS.md): plays a demo journey in the diagnostics standalone of
# the Machinedrum Editor (or, once it has demos, the Monomachine Editor), records the app's window and its own sound
# with tools/mdmm-recorder (ScreenCaptureKit), then renders 16:9 (1920x1080), 9:16 (1080x1920) and 1:1 (1080x1080)
# with ffmpeg: loudness normalised to -14 LUFS, captions burned in, an end card over the last seconds.
#   scripts/mdmm-demo-video.sh md <demo> [captions]           demo: demo-md-groove or groove
#   scripts/mdmm-demo-video.sh render <run folder> [captions]   renders a recorded run again from its raw.mov (no app)
#   scripts/mdmm-demo-video.sh levels <run folder>              the level check alone (a bar jumping or climbing)
#   MDMM_DEMO_DRY=1 scripts/mdmm-demo-video.sh md <demo>         a dry run: the take and its level check, no videos
# The run folder's captions.txt is the copy: written from the demo's own step captions and end card on the first
# render, then kept, so it can be edited and rendered again. A captions file given replaces it; "none" burns in none.
# Its lines: "start|end|text" (seconds from the start of the video; about six words a line, two lines at most, "\n"
# breaks a line) and "card|seconds|name|line[|url]" (the end card; the URL is off until launch: add it as a fifth
# field); # starts a comment. timeline.json (written on each render) places the take for other tools (the reels):
# each bar line, pattern change and step of the demo on the video's and the raw take's clock.
# Output: temp/videos/<demo>-<date>/ (MDMM_DEMO_OUT for another folder): raw.mov (the recording), page.log,
# timeline.txt, captions.txt, <demo>-16x9.mp4, -9x16.mp4, -1x1.mp4, stills/*.png and check.txt (loudness, peak,
# silence). Exits non-zero when the demo or a check fails.
# Environment: MDMM_APP_DIR (as for mdmm-journeys.sh), MDMM_DEMO_SIZE the editor's size in points (1440x810: 16:9,
# the page's design width), MDMM_DEMO_FPS (60), MDMM_DEMO_LEAD / MDMM_DEMO_TAIL seconds kept before the demo's first
# step and after its last (0.3, 0.4), MDMM_DEMO_TIMEOUT seconds for the whole demo (300).
# The app is started from its .app path and found, fronted and recorded by its process id, never by its bundle id
# (an older download of the same app has the same id). The editor's config file and standalone settings are backed
# up first and restored byte for byte afterwards, pass or fail, as the journeys do. The ROMs are the ones in the ROM
# folders; none is copied.
# Needs: Screen Recording allowed for the app that runs this script (System Settings > Privacy & Security > Screen &
# System Audio Recording), ffmpeg (brew install ffmpeg), python3 with numpy (pip3 install numpy), the Xcode toolchain for the recorder.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/scripts/mdmm-product.env"	# the product names: the apps, their executables and caches
APPS=${MDMM_APP_DIR:-"$ROOT/bin/plugins/Release/Standalone"}
SIZE=${MDMM_DEMO_SIZE:-1440x810}
FPS=${MDMM_DEMO_FPS:-60}
LEAD=${MDMM_DEMO_LEAD:-0.3}
TAIL=${MDMM_DEMO_TAIL:-0.4}
TIMEOUT=${MDMM_DEMO_TIMEOUT:-300}
now() { python3 -c 'import time; print("%.3f" % time.time())'; }
die() { echo "FAIL $*" >&2; exit 1; }
for t in ffmpeg ffprobe python3; do command -v $t >/dev/null 2>&1 || die "$t is missing (ffmpeg: brew install ffmpeg)"; done

# ---------- timeline.json: the take's bars, patterns, sections and steps on the video's and the raw take's clock ----------
timeline() {	# $1 run folder, $2 demo, $3 offset, $4 lead, $5 duration
	DIR=$1; NAME=$2; OFFSET=$3; LEAD=$4; DURATION=$5
	python3 - "$DIR" "$NAME" "$OFFSET" "$LEAD" "$DURATION" <<'EOF' || die "no timeline"
import json, re, sys
d, name, offset, lead, dur = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
log = open(d + "/page.log", encoding="utf-8", errors="replace").read().splitlines()
start = None
for l in log:
	m = re.search(r"DEMO %s START clock (\d+)" % re.escape(name), l)
	if m: start = int(m.group(1))
vt = lambda ms: round(ms / 1000 + lead, 3)	# ms after START to the video's time
bars, pats, steps = [], [], []
for l in log:
	m = re.search(r"DEMO BAR (\d+) clock (\d+)", l)
	if m and start is not None: bars.append({"bar": int(m.group(1)), "t": vt(int(m.group(2)) - start)})
	m = re.search(r"DEMO PATTERN (\S+) clock (\d+)", l)
	if m and start is not None: pats.append({"pattern": m.group(1), "t": vt(int(m.group(2)) - start)})
	m = re.search(r"DEMO %s at (\d+) step (\d+)/\d+(?: section (\S+))?(?: caption (.*))?$" % re.escape(name), l)
	if m: steps.append({"t": vt(int(m.group(1))), "step": int(m.group(2)), "section": m.group(3), "caption": m.group(4)})
	m = re.search(r"JOURNEY %s (\d+)/\d+ ok (.*?) \(\d+ ms\)(?: · (.*))?$" % re.escape(name), l)
	if m:
		for st in steps:
			if st["step"] == int(m.group(1)): st["say"] = m.group(2); st["note"] = m.group(3)
bars = [b for b in bars if -1 <= b["t"] <= dur + 1]
sections = []
for st in steps:
	if st["section"] and (not sections or sections[-1]["section"] != st["section"]): sections.append({"section": st["section"], "t": st["t"]})
# the first bar that can sound: bar 2 when the demo starts from an empty pattern (bar 1 is PLAY on nothing)
first = next((b["t"] for b in bars if b["bar"] == 2), 0.0)
json.dump({"demo": name, "video": {"duration": dur, "lead": lead}, "raw": {"file": "raw.mov", "offset": offset, "note": "raw time = video time + offset"},
	"tempo": None, "bars": bars, "patterns": pats, "sections": sections, "steps": steps, "firstBar": first},
	open(d + "/timeline.json", "w"), indent=1, ensure_ascii=False)
EOF
}

# ---------- render: the run folder's raw.mov, page.log and captions.txt to the three videos ----------
render() {	# $1 run folder, $2 captions file (optional; "none": none)
	DIR=$1; CAPS=${2:-}
	[ -f "$DIR/raw.mov" ] && [ -f "$DIR/page.log" ] && [ -f "$DIR/timeline.txt" ] || die "$DIR has no raw.mov, page.log and timeline.txt"
	REC=$("$ROOT/tools/mdmm-recorder/build.sh") || die "the recorder did not build"
	. "$DIR/timeline.txt"	# DEMO OFFSET DURATION LEAD
	# the take must hold the whole cut, picture and sound (a recording that ended early is not rendered)
	python3 - "$DIR/raw.mov" "$OFFSET" "$DURATION" <<'EOF' || die "raw.mov is shorter than the cut"
import subprocess, sys
f, off, dur = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_type,duration", "-of", "csv=p=0", f], capture_output=True, text=True).stdout.split()
d = {k: float(v) for k, v in (x.split(",") for x in r if "," in x)}
short = [k for k in ("video", "audio") if d.get(k, 0) < off + dur - 0.3]
if short: print("raw.mov: %s ends at %s s, the cut needs %.1f s" % (" and ".join(short), {k: d.get(k) for k in short}, off + dur)); sys.exit(1)
EOF
	NAME=$DEMO
	mkdir -p "$DIR/stills" "$DIR/captions"
	rm -f "$DIR"/captions/*.png "$DIR"/stills/*.png
	if [ "$CAPS" = none ]; then printf '# no captions\n' > "$DIR/captions.txt"
	elif [ -n "$CAPS" ]; then cp "$CAPS" "$DIR/captions.txt"
	elif [ ! -f "$DIR/captions.txt" ]; then
		# the demo's own: each step caption until the next one (or the end card), and its end card
		python3 - "$DIR/page.log" "$NAME" "$LEAD" "$DURATION" > "$DIR/captions.txt" <<'EOF'
import re, sys
log, name, lead, dur = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
caps, card, CARD = [], None, 3.5
for line in open(log, encoding="utf-8", errors="replace"):
	m = re.search(r"DEMO %s at (\d+) step \S+(?: section \S+)? caption (.*)$" % re.escape(name), line)
	if m: caps.append((int(m.group(1)) / 1000 + lead, m.group(2).strip()))
	m = re.search(r"DEMO %s card (.*)$" % re.escape(name), line)
	if m: card = m.group(1).strip()
print("# %s: start|end|text, seconds from the start of the video. About six words a line, two lines at most" % name)
print("# (\\n breaks a line). Edit, then: scripts/mdmm-demo-video.sh render <this folder>")
stop = dur - CARD if card else dur - 0.3
for i, (t, text) in enumerate(caps):
	t = 0.0 if t < 0.8 else t
	end = min(caps[i + 1][0] - 0.15 if i + 1 < len(caps) else stop, stop)
	if end > t + 0.4: print("%.2f|%.2f|%s" % (t, end, text))
if card:
	print("# the end card over the last seconds: card|seconds|name|line, optionally |url (off until launch, e.g. |mdmm.dev)")
	print("card|%.1f|%s" % (CARD, card))
EOF
	fi
	timeline "$DIR" "$NAME" "$OFFSET" "$LEAD" "$DURATION"
	# the silence the demo means: before its first bar (the machine is not playing yet)
	QUIET=$(python3 -c "import json, sys; print(json.load(open(sys.argv[1] + '/timeline.json'))['firstBar'])" "$DIR")
	grep -v '^[[:space:]]*#' "$DIR/captions.txt" | grep -v '^card|' | grep '|' > "$DIR/captions/lines.txt"
	CARDLINE=$(grep '^card|' "$DIR/captions.txt" | tail -1)
	CARDSEC=0; CSTART=$DURATION
	if [ -n "$CARDLINE" ]; then
		CARDSEC=$(echo "$CARDLINE" | cut -d'|' -f2); CTITLE=$(echo "$CARDLINE" | cut -d'|' -f3)
		CLINE=$(echo "$CARDLINE" | cut -d'|' -f4); CURL=$(echo "$CARDLINE" | cut -d'|' -f5)
		CSTART=$(python3 -c "print(max(0, $DURATION - $CARDSEC))")
	fi

	# loudness, measured on the cut (loudnorm's first pass), then applied (second pass, linear) under a limiter (at 4x the rate, -4 dBFS: AAC adds up to 3 dB of true peak on dense retrigs): the
	# sound fades in at once and out under the end card
	MEAS=$(ffmpeg -hide_banner -nostats -ss "$OFFSET" -t "$DURATION" -i "$DIR/raw.mov" -vn \
		-af "loudnorm=I=-14:TP=-2:LRA=11:print_format=json" -f null - 2>&1 | python3 -c '
import json, sys
t = sys.stdin.read(); j = json.loads(t[t.rindex("{"):t.rindex("}") + 1])
print("measured_I=%s:measured_TP=%s:measured_LRA=%s:measured_thresh=%s:offset=%s" % (j["input_i"], j["input_tp"], j["input_lra"], j["input_thresh"], j["target_offset"]))') \
		|| die "no sound to measure in the recording"
	AOUT=$(python3 -c "print(max(0, $DURATION - max(2.5, $CARDSEC * 0.8)))"); AOUTD=$(python3 -c "print($DURATION - $AOUT)")
	AF="loudnorm=I=-14:TP=-2:LRA=11:$MEAS:linear=true,aresample=192000,alimiter=limit=0.63:level=false:attack=1:release=40,aresample=48000,afade=t=in:d=0.05,afade=t=out:st=$AOUT:d=$AOUTD"

	# one picture chain per format: a blurred, darkened fill behind the window, the captions, the end card
	for fmt in 16x9 9x16 1x1; do
		case $fmt in
			# a full-width band at the bottom
			16x9) W=1920; H=1080; FG="scale=1920:1080:force_original_aspect_ratio=decrease:flags=lanczos"; FY="(H-h)/2"
				CAPARGS="--band --width 1920 --size 58"; CX="0"; CY="H-h"; SAFE="160,90,1600,900" ;;
			# the window high, a band in the room under it
			1x1) W=1080; H=1080; FG="scale=1080:-2:flags=lanczos"; FY="150"
				CAPARGS="--band --width 1080 --size 56"; CX="0"; CY="H-h"; SAFE="80,80,920,920" ;;
			# Reels / Shorts: text above the bottom 20 % and clear of the buttons on the right (x 60-900)
			9x16) W=1080; H=1920; FG="scale=1080:-2:flags=lanczos"; FY="560"
				CAPARGS="--width 840 --size 60"; CX="60+(840-w)/2"; CY="1210"; SAFE="60,250,840,1250" ;;
		esac
		INPUTS=(); CHAIN=""; i=0; last="v0"
		while IFS='|' read -r s e text; do
			[ -n "$text" ] || continue
			i=$((i + 1))
			# shellcheck disable=SC2086
			"$REC" caption --text "$text" --out "$DIR/captions/$fmt-$i.png" $CAPARGS || die "caption $i"
			INPUTS+=(-loop 1 -framerate "$FPS" -t "$DURATION" -i "$DIR/captions/$fmt-$i.png")
			CHAIN="$CHAIN;[$last][$i:v]overlay=x=$CX:y=$CY:enable='between(t,$s,$e)'[c$i]"; last="c$i"
		done < "$DIR/captions/lines.txt"
		if [ -n "$CARDLINE" ]; then
			"$REC" card --out "$DIR/captions/$fmt-card.png" --width $W --height $H --safe "$SAFE" --title "$CTITLE" --line "$CLINE" --url "$CURL" || die "end card"
			i=$((i + 1)); INPUTS+=(-loop 1 -framerate "$FPS" -t "$DURATION" -i "$DIR/captions/$fmt-card.png")
			CHAIN="$CHAIN;[$i:v]format=rgba,fade=t=in:st=$CSTART:d=0.45:alpha=1[card];[$last][card]overlay=0:0:enable='gte(t,$CSTART)'[cc]"; last="cc"
		fi
		VF="[0:v]fps=$FPS,split[a][b];[a]scale=$W:$H:force_original_aspect_ratio=increase,crop=$W:$H,gblur=sigma=40,eq=brightness=-0.22:saturation=0.8[bg];[b]$FG[fg];[bg][fg]overlay=x=(W-w)/2:y=$FY,setsar=1[v0]$CHAIN"
		OUT="$DIR/$NAME-$fmt.mp4"
		# shellcheck disable=SC2086
		ffmpeg -hide_banner -loglevel error -y -ss "$OFFSET" -t "$DURATION" -i "$DIR/raw.mov" "${INPUTS[@]}" \
			-filter_complex "$VF" -map "[$last]" -map 0:a -af "$AF" \
			-c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -profile:v high -r "$FPS" \
			-c:a aac -b:a 256k -ar 48000 -movflags +faststart -t "$DURATION" "$OUT" || die "ffmpeg $fmt"
		# stills: the hook, the middle, the end card
		for p in 0.8 50% end; do
			case $p in end) at=$(python3 -c "print($DURATION - 0.5)") ;; *%) at=$(python3 -c "print($DURATION * ${p%\%} / 100)") ;; *) at=$p ;; esac
			ffmpeg -hide_banner -loglevel error -y -ss "$at" -i "$OUT" -frames:v 1 "$DIR/stills/$fmt-$(echo $p | tr -d %).png" || true
		done
		echo "   $OUT"
	done
	{ check "$DIR/$NAME-16x9.mp4" "$QUIET"; levels "$DIR"; } > "$DIR/check.txt"
	cat "$DIR/check.txt"
	! grep -q "^RESULT bad\|^LEVELS bad" "$DIR/check.txt"
}

# ---------- the level check: no bar jumps out of its neighbours, no climb bar over bar ----------
# A feedback loop (a recorder in the mix it samples, an echo at full feedback) shows as a bar several dB above the
# bars around it, or as a level that climbs bar over bar. A drop on the bar is neither: the level after it stays.
levels() {	# $1 run folder (raw.mov, timeline.json)
	python3 - "$1" <<'EOF'
import json, subprocess, sys
import numpy as np
d = sys.argv[1]
t = json.load(open(d + "/timeline.json"))
off = t["raw"]["offset"]
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", d + "/raw.mov", "-f", "f32le", "-ac", "1", "-ar", "48000", "-"], capture_output=True).stdout
x = np.frombuffer(raw, dtype=np.float32); sr = 48000
bars = [b for b in t["bars"] if b["t"] >= 0]
lv = []
for a, b in zip(bars, bars[1:]):
	seg = x[int((a["t"] + off) * sr): int((b["t"] + off) * sr)]
	if len(seg) > sr // 4 and np.abs(seg).max() > 1e-4:
		lv.append((a["bar"], 10 * np.log10((seg.astype(np.float64) ** 2).mean() + 1e-12)))
if not lv:
	print("levels: no bar lines in the log (a demo without the bar clock)"); print("LEVELS ok (not checked)"); sys.exit(0)
bad = []
# a bar above the two bars before it and the two after it (a change of the arrangement, a drop or a break, moves
# one side only)
for i, (bar, v) in enumerate(lv):
	left, right = [w for _, w in lv[max(0, i - 2):i]], [w for _, w in lv[i + 1:i + 3]]
	if left and right and v > np.mean(left) + 4.5 and v > np.mean(right) + 4.5:
		bad.append("bar %d jumps %.1f dB above the bars around it" % (bar, v - max(np.mean(left), np.mean(right))))
for i in range(3, len(lv)):
	steps = [lv[k][1] - lv[k - 1][1] for k in range(i - 2, i + 1)]
	if all(st >= 1.5 for st in steps) and sum(steps) >= 5:
		bad.append("bars %d-%d climb %.1f dB" % (lv[i - 3][0], lv[i][0], sum(steps)))
print("levels: %d bars, %.1f to %.1f dB RMS%s" % (len(lv), min(v for _, v in lv), max(v for _, v in lv), "".join("; " + b for b in bad)))
print("LEVELS " + ("ok" if not bad else "bad: " + "; ".join(bad)))
EOF
}

# ---------- the listen check: loudness, peak, silence ----------
check() {	# $1 a rendered video, $2 seconds of silence meant at the start (before the demo's first bar)
	ffmpeg -hide_banner -nostats -i "$1" -vn -af "ebur128=peak=true,silencedetect=n=-50dB:d=1.0" -f null - 2>&1 | QUIET=${2:-0} python3 -c '
import re, sys
t = sys.stdin.read()
summary = t[t.rfind("Summary:"):]
I = float(re.search(r"I:\s+(-?[\d.]+) LUFS", summary).group(1))
pk = re.search(r"Peak:\s+(-?[\d.]+) dBFS", summary)
peak = float(pk.group(1)) if pk else -99
d = re.search(r"Duration: (\d+):(\d+):([\d.]+)", t)
dur = 60 * int(d.group(2)) + float(d.group(3))
# the sound must last as long as the picture
import subprocess
ad = float(subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=duration", "-of", "csv=p=0", sys.argv[1]], capture_output=True, text=True).stdout.strip() or 0)
starts = [float(x) for x in re.findall(r"silence_start: (-?[\d.]+)", t)]
ends = [float(x) for x in re.findall(r"silence_end: ([\d.]+)", t)] + [dur]
sil = list(zip(starts, ends))
# the video opens on the machine playing; its sound fades out under the end card, so a little silence at the very end
# is the fade; any other second of silence means the sound dropped out
lead = sum(e - s for s, e in sil if s <= 0.05)
tail = sum(e - s for s, e in sil if s > 0.05 and e >= dur - 0.05)
mid = [(s, e) for s, e in sil if s > 0.05 and e < dur - 0.05]
print("loudness %.1f LUFS (target -14), true peak %.1f dBFS, silence: %.1f s at the start, %.1f s at the end, %d stretches over 1 s between (2.5 s at most)%s"
	% (I, peak, lead, tail, len(mid), "".join(" (%.1f-%.1f s)" % m for m in mid)))
bad = []
if I < -20 or I > -12: bad.append("loudness off target")
if ad < dur - 0.5: bad.append("the sound ends at %.1f s of %.1f s" % (ad, dur))
if peak > -0.5: bad.append("clipping")
# a STOP in the song (a chain loaded while stopped) is a short silence: up to 2.5 s; the start may be quiet until
# the first bar
import os
if lead > float(os.environ.get("QUIET", "0")) + 0.5 or tail > 1.5 or any(e - s > 2.5 for s, e in mid): bad.append("silence")
print("RESULT " + ("ok" if not bad else "bad: " + ", ".join(bad)))' "$1"
}

if [ "${1:-}" = levels ]; then
	[ -n "${2:-}" ] || die "usage: $0 levels <run folder>"
	levels "$2" | tee "$2/levels.txt"; ! grep -q "^LEVELS bad" "$2/levels.txt"; exit $?
fi
if [ "${1:-}" = render ]; then
	[ -n "${2:-}" ] || die "usage: $0 render <run folder> [captions]"
	render "$2" "${3:-}"; exit $?
fi


# ---------- record: the app, the demo, the recorder ----------
# ScreenCaptureKit sometimes ends a recording by itself ("Unknown stream error", seen after 13 to 78 s): the take is
# then not used, and the whole run is played again, up to MDMM_DEMO_TRIES times (4); config and settings are
# restored between tries as after any run.
if [ "${MDMM_DEMO_TRY:-}" = "" ]; then
	n=1
	while :; do
		MDMM_DEMO_TRY=$n "$0" "$@"; rc=$?
		[ $rc = 75 ] && [ $n -lt "${MDMM_DEMO_TRIES:-4}" ] || exit $rc
		echo "== the recording ended early: try $((n + 1))"
		n=$((n + 1))
		# the last try's app quits first (a new one started while it still runs ends at once)
		for i in $(seq 1 30); do pgrep -f "($MDMM_PRODUCT_NAME_MD|$MDMM_PRODUCT_NAME_MM).app/Contents/MacOS" >/dev/null 2>&1 || break; sleep 1; done
		sleep 3
	done
fi
WHICH=${1:-}; DEMO=${2:-}; CAPS=${3:-}
case "$WHICH" in md) M=MD; MACHINE=Machinedrum; SKIN=mdStudio; VAR=GEARMULATOR_MDSTUDIO_SELFTEST; NAME=$MDMM_PRODUCT_NAME_MD ;;
	mm) M=MM; MACHINE=Monomachine; SKIN=mmStudio; VAR=GEARMULATOR_MMSTUDIO_SELFTEST; NAME=$MDMM_PRODUCT_NAME_MM ;;
	*) echo "usage: $0 md|mm <demo> [captions] | $0 render <run folder> [captions]" >&2; exit 2 ;; esac
[ -n "$DEMO" ] || die "name a demo (demo-$WHICH-<name>)"
case "$DEMO" in demo-*) ;; *) DEMO="demo-$WHICH-$DEMO" ;; esac
case "$SIZE" in *x*) WW=${SIZE%x*}; WH=${SIZE#*x} ;; *) die "MDMM_DEMO_SIZE is WxH" ;; esac
APP="$APPS/$NAME.app"
EXE="$APP/Contents/MacOS/$NAME"
DATA="$HOME/Documents/Gearmulator Preview/$MACHINE"
C="$DATA/config/$MACHINE Editor.xml"	# the editor's own files since 0.3.1 (mdSettingsMigration.h), not the product name
SETTINGS="$HOME/Library/Application Support/$MACHINE Editor.settings"
LOGDIR="$HOME/Library/Caches/$NAME"	# JUCE's temp folder: ~/Library/Caches/<executable>
[ -x "$EXE" ] || die "no diagnostics build at $APP (configure with -Dgearmulator_MDMM_DIAGNOSTICS=ON, build the $(echo $M | tr A-Z a-z)JucePlugin_Standalone target)"
ls "$DATA/roms/"* >/dev/null 2>&1 || die "no $MACHINE ROM in $DATA/roms (it stays there; nothing copies it)"
pgrep -f "$NAME.app/Contents/MacOS" >/dev/null 2>&1 && die "a $NAME app is running: quit it first (the demo uses the same settings)"
REC=$("$ROOT/tools/mdmm-recorder/build.sh") || die "the recorder did not build"
OUT=${MDMM_DEMO_OUT:-"$ROOT/temp/videos"}/$DEMO-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"
echo "== $MACHINE Editor: $DEMO -> $OUT"

TMP=$(mktemp -d)
[ -f "$C" ] && cp -p "$C" "$TMP/config.xml"
[ -f "$SETTINGS" ] && cp -p "$SETTINGS" "$TMP/settings"
PID=""; RPID=""
restore() {
	[ -n "$RPID" ] && kill -INT "$RPID" 2>/dev/null
	if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
		kill -TERM "$PID" 2>/dev/null
		for i in 1 2 3 4 5; do kill -0 "$PID" 2>/dev/null || break; sleep 1; done
		kill "$PID" 2>/dev/null; sleep 1; kill -9 "$PID" 2>/dev/null
	fi
	if [ -f "$TMP/config.xml" ]; then cp -p "$TMP/config.xml" "$C"; else rm -f "$C"; fi
	if [ -f "$TMP/settings" ]; then cp -p "$TMP/settings" "$SETTINGS"; else rm -f "$SETTINGS"; fi
	rm -rf "$TMP"
}
trap 'restore; exit 1' INT TERM
# the run's config: the page skin and the window's size (the editor's own width and height, in points); the
# window's place on screen (the standalone settings), top left so it fits under the menu bar
setval() {	# $1 file, $2 name, $3 value: a <VALUE name=… val=…/> replaced or added
	if grep -q "<VALUE name=\"$2\" " "$1"; then sed -i '' "s|<VALUE name=\"$2\" val=\"[^\"]*\"/>|<VALUE name=\"$2\" val=\"$3\"/>|" "$1"
	else sed -i '' "s|</PROPERTIES>|  <VALUE name=\"$2\" val=\"$3\"/>\\
</PROPERTIES>|" "$1"; fi
}
mkdir -p "$(dirname "$C")"
[ -f "$C" ] || printf '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n</PROPERTIES>\n' > "$C"
setval "$C" skinDisplayName "$SKIN"; setval "$C" skinFile "$SKIN.rml"
setval "$C" windowWidth "$WW"; setval "$C" windowHeight "$WH"
if [ -f "$SETTINGS" ]; then setval "$SETTINGS" windowX 24; setval "$SETTINGS" windowY 40; fi

# the display awake for the whole run: a sleeping display has no window to record and draws no canvases (caffeinate:
# -u wakes it, -d keeps it on until this script ends; a locked screen still needs Radek)
caffeinate -u -t 2 >/dev/null 2>&1 &
caffeinate -d -w $$ >/dev/null 2>&1 &
mkdir -p "$LOGDIR"; touch "$TMP/start"
env "$VAR=$DEMO" "$EXE" >"$TMP/app.out" 2>&1 &
PID=$!
# Everything goes by this process id, never by the bundle id: an older download of the same app carries the same id,
# and a lookup by id would launch or pick that one (it rewrites the editor's config for its own skins).
front() { kill -0 "$PID" 2>/dev/null && "$REC" front --pid "$PID" 2>/dev/null; }
( sleep 4; front ) &
LOG=""; t0=$(date +%s); STATE=wait; TREC=""; TSTART=""; STATUS=0
while :; do
	sleep 0.1
	el=$(( $(date +%s) - t0 ))
	[ $el -lt "$TIMEOUT" ] || { echo "FAIL timeout after $TIMEOUT s"; STATUS=1; break; }
	kill -0 $PID 2>/dev/null || { echo "FAIL the app ended (see $TMP/app.out)"; STATUS=1; break; }
	if [ -z "$LOG" ]; then LOG=$(find "$LOGDIR" -name "gearmulator-$SKIN-*.log" -newer "$TMP/start" 2>/dev/null | head -1); continue; fi
	case $STATE in
	wait)	# the machine is ready and the page neutral: front the window and start recording
		grep -q "DEMOS DONE" "$LOG" && { echo "FAIL the demo ended before it was ready"; STATUS=1; break; }
		if grep -q "DEMO READY" "$LOG"; then
			front; sleep 0.3
			"$REC" record --pid "$PID" --out "$OUT/raw.mov" --content-size "$SIZE" --fps "$FPS" \
				--max-seconds "$TIMEOUT" --ready-file "$TMP/rec.ready" --info-file "$OUT/window.json" 2>"$OUT/recorder.log" &
			RPID=$!; STATE=rec
		fi ;;
	rec)	# the recorder has its first frames
		if [ -f "$TMP/rec.ready" ]; then TREC=$(now); STATE=run
		elif ! kill -0 $RPID 2>/dev/null; then RPID=""; cat "$OUT/recorder.log"; echo "FAIL the recorder stopped (Screen Recording permission? see above)"; STATUS=1; break
		elif grep -q "DEMO $DEMO START" "$LOG"; then echo "FAIL the recorder was not ready before the demo started (raise the page's preroll)"; STATUS=1; break; fi ;;
	run)
		[ -n "$TSTART" ] || { grep -q "DEMO $DEMO START" "$LOG" && TSTART=$(now); }
		if grep -q "DEMOS DONE" "$LOG"; then sleep "$TAIL"; break; fi
		if ! kill -0 "$RPID" 2>/dev/null; then wait "$RPID"; RPID=""; cat "$OUT/recorder.log"; echo "FAIL the recorder stopped before the demo ended"; STATUS=75; break; fi ;;
	esac
done
# the recorder finishes its file on SIGINT; given 15 s, then it is killed (the file is then incomplete)
if [ -n "$RPID" ]; then
	kill -INT "$RPID" 2>/dev/null
	for i in $(seq 1 150); do kill -0 "$RPID" 2>/dev/null || break; sleep 0.1; done
	if kill -0 "$RPID" 2>/dev/null; then kill -9 "$RPID" 2>/dev/null; echo "FAIL the recorder did not stop"; STATUS=1; fi
	wait "$RPID"; RS=$?; RPID=""; [ $RS = 0 ] || { echo "FAIL the recorder ended with $RS"; STATUS=1; }
fi
[ -n "$LOG" ] && cp "$LOG" "$OUT/page.log"
restore; trap - INT TERM
[ -n "$LOG" ] || die "no page log"
grep -E "JOURNEY $DEMO [^ ]+ (PASS|FAIL)|DEMO $DEMO (START|END)|DEMOS DONE" "$OUT/page.log" | sed 's/^[^JD]*\(JOURNEY\|DEMO\)/\1/'
[ $STATUS = 75 ] && exit 75
[ $STATUS = 0 ] || exit 1
grep -q "JOURNEY $DEMO PASS" "$OUT/page.log" || { echo "FAIL the demo failed: the steps are in $OUT/page.log"; grep "JOURNEY $DEMO .* FAIL" "$OUT/page.log" | head -3; exit 1; }
[ -n "$TREC" ] && [ -n "$TSTART" ] || die "no recording start or demo start seen"
END=$(sed -n "s/.*DEMO $DEMO END \([0-9]*\).*/\1/p" "$OUT/page.log" | tail -1)
# the cut: LEAD seconds before the demo's START (as this script saw it in the log) to TAIL after its END
python3 - "$TREC" "$TSTART" "$END" "$LEAD" "$TAIL" "$DEMO" > "$OUT/timeline.txt" <<'EOF'
import sys
trec, tstart, end, lead, tail, demo = float(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
offset = max(0.0, tstart - trec - lead)
lead = min(lead, tstart - trec)
print("DEMO=%s\nOFFSET=%.3f\nDURATION=%.3f\nLEAD=%.3f" % (demo, offset, end / 1000 + lead + tail, lead))
EOF
cat "$OUT/timeline.txt" | tr '\n' ' '; echo
# a dry run (MDMM_DEMO_DRY=1): the take and its level check only, no videos
if [ "${MDMM_DEMO_DRY:-0}" = 1 ]; then
	( . "$OUT/timeline.txt"; timeline "$OUT" "$DEMO" "$OFFSET" "$LEAD" "$DURATION" )
	levels "$OUT" | tee "$OUT/levels.txt"
	! grep -q "^LEVELS bad" "$OUT/levels.txt"; exit $?
fi
render "$OUT" "$CAPS"
