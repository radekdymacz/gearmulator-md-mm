#!/bin/sh
# Screenshots of the Machinedrum Editor's own window (macOS, diagnostics build): runs the journey md-shots
# (skins/mdStudio/mdDeskJourneys.js) through scripts/mdmm-journeys.sh in the background (the window behind every other
# one, no focus, no sound, a scratch data root, the person's settings restored), and on each "SHOT <name>" line of the
# page's log captures the window by its id (screencapture -l, which works behind other windows) into <out>/<name>.png.
#   scripts/mdmm-shots.sh <out folder> [width x height in points, default 1440x900]
# MDMM_SHOTS_JOURNEY names another screenshot journey (default md-shots; md-shots-song: the Song page, 0.3.5).
# Needs the Screen Recording permission for the terminal that runs it (else the images are blank).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: $0 <out folder> [1440x900]}
mkdir -p "$OUT"
export GEARMULATOR_MDMM_BACKGROUND_SIZE=${2:-1440x900}
LOGDIR="$HOME/Library/Caches/Machinedrum Editor"
swiftc -O -o "$OUT/.window-id" "$ROOT/scripts/macos/window-id.swift" || exit 1
STAMP="$OUT/.start"; : > "$STAMP"
JOURNEY=${MDMM_SHOTS_JOURNEY:-md-shots}
"$ROOT/scripts/mdmm-journeys.sh" --host standalone --background md "journey-$JOURNEY" > "$OUT/journeys.log" 2>&1 &
RUN=$!
LOG=""; n=0
while [ -z "$LOG" ] && [ $n -lt 300 ] && kill -0 $RUN 2>/dev/null; do
	# the journeys run the editor in a sandbox of its own (mdmm-journeys.sh: its home under $TMPDIR/mdmm-journey.*)
	LOG=$(find "$LOGDIR" "${TMPDIR:-/tmp}"/mdmm-journey.*/home/Library/Caches -name 'gearmulator-mdStudio-*.log' -newer "$STAMP" 2>/dev/null | head -1); sleep 1; n=$((n + 1))
done
[ -n "$LOG" ] || { echo "no page log"; wait $RUN; cat "$OUT/journeys.log"; exit 1; }
WID=""
tail -n +1 -f "$LOG" | while read -r line; do
	case "$line" in
		*"SHOT done"*|*"JOURNEYS DONE"*) echo "$line"; pkill -P $$ tail 2>/dev/null; break ;;
		*"SHOT "*)
			name=$(printf '%s' "${line##*SHOT }" | tr -d '\r')	# the page's log ends its lines with CR LF
			[ -n "$WID" ] || WID=$("$OUT/.window-id" "Machinedrum Editor")
			screencapture -x -o -l "$WID" "$OUT/$name.png" && echo "captured $name ($WID)" ;;
		*"JOURNEY $JOURNEY "*FAIL*|*"JOURNEY $JOURNEY "*SKIP*) echo "$line" ;;
	esac
done
wait $RUN
grep -E "JOURNEY|DONE" "$OUT/journeys.log"
rm -f "$STAMP"
