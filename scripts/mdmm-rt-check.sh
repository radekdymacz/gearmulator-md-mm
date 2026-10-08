#!/bin/sh
# B-014: the audio thread's cost per block while the editor edits parameter locks, measured headless
# with the firmware (no window, no audio device, no sound). Runs mdDeskFirmwareTest plocktiming (the
# emulator, the desk and the page's edits as the plug-in runs them, one audio block of 64 frames at a
# time on this thread) and prints, per phase (idle, drag, click, click1s, wheel, after, fill/play
# rounds), the retired instructions per block and per host buffer of 128 and 256 frames (independent
# of what else the computer runs), the CPU time, and how many host buffers a second go over a work
# level. Compare runs: GEARMULATOR_MDMM_MIDI_PACING has no effect here; use the PLOCK_* variables.
#
#   scripts/mdmm-rt-check.sh <MD ROM> [build dir (temp/mac_perf)] [VAR=value ...]
#     PLOCK_INGRESS=<B/s>   SysEx into the firmware (0: at once, as 0.3.2; default 125000, as 0.3.3)
#     PLOCK_TRANSMIT=<B/s>  the firmware's MIDI out (0: at once; default 125000)
#     PLOCK_LONG=<rounds>   rounds of 96 new locks, each followed by 4 s of playing
#     PLOCK_PHASES=a,b      only these phases; PLOCK_CLICK_MS=<ms> between the click1s clicks (1000)
#
# The ROM is only read. Output and per-block traces go to a temporary folder (printed at the end).
set -e
ROM=${1:?usage: mdmm-rt-check.sh <MD ROM> [build dir] [VAR=value ...]}
shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/temp/mac_perf
if [ -n "$1" ] && [ -d "$1" ]; then BUILD=$1; shift; fi
BIN=$(find "$BUILD" -name mdDeskFirmwareTest -type f -perm +111 | head -1)
[ -x "$BIN" ] || { echo "no mdDeskFirmwareTest in $BUILD (cmake --build $BUILD --target mdDeskFirmwareTest)"; exit 1; }
OUT=$(mktemp -d)
mkdir -p "$OUT/trace" "$OUT/data"
env GEARMULATOR_DATA_ROOT="$OUT/data" PLOCK_TRACE="$OUT/trace" "$@" "$BIN" "$ROM" plocktiming > "$OUT/run.txt" 2>&1 || true
grep -E "^== |^  [a-z]+ |Minstr/|cpu  us|idle-skipped|to the page|locks the" "$OUT/run.txt" | cut -c1-200
python3 - "$OUT/trace" <<'EOF'
import csv, os, sys
d = sys.argv[1]
print('host buffers a second over a work level (Minstr): 128 frames > 45 / 50 / 55, 256 frames > 90 / 100 / 105')
for f in sorted(os.listdir(d), key=lambda n: os.path.getmtime(os.path.join(d, n))):
    v = [float(r['minstr']) for r in csv.DictReader(open(os.path.join(d, f)))]
    if not v:
        continue
    secs = len(v) * 64 / 44100
    w2 = [v[i] + v[i + 1] for i in range(0, len(v) - 1, 2)]
    w4 = [sum(v[i:i + 4]) for i in range(0, len(v) - 3, 4)]
    a = ' '.join('%5.1f' % (sum(x > t for x in w2) / secs) for t in (45, 50, 55))
    b = ' '.join('%5.1f' % (sum(x > t for x in w4) / secs) for t in (90, 100, 105))
    print('  %-8s %s | %s' % (f[:-4], a, b))
EOF
sysctl -n machdep.cpu.brand_string
echo "output: $OUT"
