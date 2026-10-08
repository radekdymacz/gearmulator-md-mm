#!/bin/sh
# B-014: the audio thread's work while the editors edit, measured headless with the firmware (no window, no audio
# device, no sound), as a pass/fail check. The emulator, the desk and the page's commands run as the plug-in runs
# them, one audio block at a time on this thread; the cost is counted in retired instructions, so what else the
# computer runs does not change it.
#   scripts/mdmm-rt-check.sh [--md <MD ROM>] [--mm <MM ROM>] [--build <dir>] [--budget <hot ms/s>] [--plock] [VAR=value ...]
#     --md / --mm   the ROMs (default: the plug-ins' ROM folders); only read
#     --budget      fail any user action whose blocks run hot (a 5-block mean over 1.3x idle) longer than this
#                   many ms a second (default 100; the stream keeps every action under about 75)
#     --plock       also mdDeskFirmwareTest plocktiming-strict (B-010's sequencer timing, B-014's per-block tables)
#     VAR=value     passed on: GEARMULATOR_MDMM_EDIT_RATE=0 measures without the stream (0.3.3), ACTIONS=a,b
#                   only some actions, PLOCK_* for plocktiming (mdDeskFirmwareTest.cpp)
# Per action (mdDeskFirmwareTest / mmDeskFirmwareTest actions): what went to the machine, the 128- and 256-frame host
# buffers a second whose work is over 50 / 100 M instructions (idle: about 27 / 55), the hot time a second and the
# worst 256-frame buffer. Output and traces go to a temporary folder (printed at the end).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MD=$(ls "$HOME/Documents/Gearmulator Preview/Machinedrum/roms/"* 2>/dev/null | head -1)
MM=$(ls "$HOME/Documents/Gearmulator Preview/Monomachine/roms/"* 2>/dev/null | head -1)
BUILD=$ROOT/temp/mac_perf
BUDGET=100
PLOCK=0
while [ $# -gt 0 ]; do
	case "$1" in
		--md) MD=$2; shift 2 ;;
		--mm) MM=$2; shift 2 ;;
		--build) BUILD=$2; shift 2 ;;
		--budget) BUDGET=$2; shift 2 ;;
		--plock) PLOCK=1; shift ;;
		*) break ;;
	esac
done
find_bin() { find "$BUILD" -name "$1" -type f -perm +111 | head -1; }
MDBIN=$(find_bin mdDeskFirmwareTest)
MMBIN=$(find_bin mmDeskFirmwareTest)
OUT=$(mktemp -d)
mkdir -p "$OUT/data"
STATUS=0
if [ -n "$MD" ] && [ -x "$MDBIN" ]; then
	env GEARMULATOR_DATA_ROOT="$OUT/data" ACTIONS_BUDGET="$BUDGET" "$@" "$MDBIN" "$MD" actions > "$OUT/md-actions.txt" 2>&1 || STATUS=1
	grep -E "^== B-014|^  [a-zA-Z]+ +[0-9]+ \||FAIL|actions:" "$OUT/md-actions.txt"
	if [ "$PLOCK" = 1 ]; then
		mkdir -p "$OUT/trace"
		env GEARMULATOR_DATA_ROOT="$OUT/data" PLOCK_TRACE="$OUT/trace" "$@" "$MDBIN" "$MD" plocktiming-strict > "$OUT/md-plock.txt" 2>&1 || STATUS=1
		grep -E "^== |^  [a-z0-9]+ |clock:|Minstr/|FAIL|plocktiming" "$OUT/md-plock.txt" | cut -c1-200
	fi
else
	echo "MD: no ROM or no mdDeskFirmwareTest in $BUILD (cmake --build $BUILD --target mdDeskFirmwareTest)"; STATUS=1
fi
if [ -n "$MM" ] && [ -x "$MMBIN" ]; then
	env GEARMULATOR_DATA_ROOT="$OUT/data" ACTIONS_BUDGET="$BUDGET" "$@" "$MMBIN" "$MM" actions > "$OUT/mm-actions.txt" 2>&1 || STATUS=1
	grep -E "^== B-014|^  [a-zA-Z]+ +[0-9]+ \||FAIL|^PASS" "$OUT/mm-actions.txt"
else
	echo "MM: no ROM or no mmDeskFirmwareTest in $BUILD (cmake --build $BUILD --target mmDeskFirmwareTest)"; STATUS=1
fi
sysctl -n machdep.cpu.brand_string
echo "output: $OUT"
[ "$STATUS" = 0 ] && echo "rt-check: PASS (budget $BUDGET ms/s)" || echo "rt-check: FAIL"
exit $STATUS
