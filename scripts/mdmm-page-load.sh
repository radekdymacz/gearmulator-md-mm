#!/bin/sh
# B-014: what lock editing costs in the Machinedrum Editor VST3 with its page, as the pattern fills with locks.
# Loads the diagnostics VST3 in scripts/vst3EditorHost --background (an accessory app that never comes to the
# front, its window small and behind every other one, silent blocks, no audio device) with the page's self-test
# p4locks (mdDeskSelfTest.js: rounds of playing, then drawing 256 new locks), and prints per phase the CPU time
# of the host process (the audio thread, the desk, the message thread) and of the page's WebKit processes, in %
# of one core, with the locks the page shows.
#   scripts/mdmm-page-load.sh [rounds (4)]
# Environment: MDMM_VST3_DIR (default bin/plugins/Release/VST3), MDMM_VST3_HOST (default: built into
# temp/vst3EditorHost), GEARMULATOR_MD_FIRMWARE_BIN (else the ROM in the plug-in's ROM folder; only linked, never
# written). Each run has a scratch data root; the person's config is copied with the page skin set.
set -u
. "$(cd "$(dirname "$0")" && pwd)/mdmm-product.env"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROUNDS=${1:-4}
NAME=$MDMM_PRODUCT_NAME_MD
PLUGIN=${MDMM_VST3_DIR:-"$ROOT/bin/plugins/Release/VST3"}/$NAME.vst3
[ -d "$PLUGIN" ] || { echo "no diagnostics VST3 at $PLUGIN"; exit 1; }
HOST_BIN=${MDMM_VST3_HOST:-$(find "$ROOT/temp/vst3EditorHost" -path '*mdmmVst3EditorHost.app/Contents/MacOS/mdmmVst3EditorHost' -type f 2>/dev/null | head -1)}
if [ -z "$HOST_BIN" ]; then
	HOST_BIN=$("$ROOT/scripts/macos/build_vst3_host.sh" "$ROOT/temp/vst3EditorHost" 2>"$ROOT/temp/vst3EditorHost.log" | sed -n 's/^VST3_HOST=//p')
fi
[ -x "$HOST_BIN" ] || { echo "no VST3 host (temp/vst3EditorHost.log)"; exit 1; }
DATA="$HOME/Documents/Gearmulator Preview/Machinedrum"
ROM=${GEARMULATOR_MD_FIRMWARE_BIN:-$(ls "$DATA/roms/"* 2>/dev/null | head -1)}
[ -f "$ROM" ] || { echo "no Machinedrum ROM"; exit 1; }
LOGDIR="$HOME/Library/Caches/$NAME"
TMP=$(mktemp -d)
ROOTDIR="$TMP/data/"
mkdir -p "$ROOTDIR/Gearmulator Preview/Machinedrum/roms" "$ROOTDIR/Gearmulator Preview/Machinedrum/config" "$LOGDIR"
ln -s "$ROM" "$ROOTDIR/Gearmulator Preview/Machinedrum/roms/$(basename "$ROM")"
C="$DATA/config/Machinedrum Editor.xml"
[ -f "$C" ] && sed -e 's/"skinDisplayName" val="[^"]*"/"skinDisplayName" val="mdStudio"/' -e 's/"skinFile" val="[^"]*"/"skinFile" val="mdStudio.rml"/' \
	"$C" > "$ROOTDIR/Gearmulator Preview/Machinedrum/config/Machinedrum Editor.xml"
touch "$TMP/start"
before=$(pgrep -f 'com.apple.WebKit.(WebContent|GPU)' | sort)
SECS=$((60 + ROUNDS * 20))
env GEARMULATOR_DATA_ROOT="$ROOTDIR" GEARMULATOR_MDMM_BACKGROUND=1 GEARMULATOR_MDSTUDIO_SELFTEST="p4locks$ROUNDS" \
	"$HOST_BIN" "$PLUGIN" $((SECS + 60)) --background >"$TMP/host.out" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; rm -rf "$TMP"; exit 1' INT TERM
secs() { ps -o time= -p "$1" 2>/dev/null | awk -F: '{ if (NF==3) print $1*3600+$2*60+$3; else print $1*60+$2 }'; }
# the page's WebKit processes that started with the host
web=""; gpu=""; LOG=""
for i in $(seq 1 90); do
	sleep 1
	for p in $(pgrep -f com.apple.WebKit.WebContent); do echo "$before" | grep -qx "$p" || web=$p; done
	for p in $(pgrep -f com.apple.WebKit.GPU); do echo "$before" | grep -qx "$p" || gpu=$p; done
	[ -z "$LOG" ] && LOG=$(find "$LOGDIR" -name "gearmulator-mdStudio-*.log" -newer "$TMP/start" 2>/dev/null | head -1)
	[ -n "$web" ] && [ -n "$LOG" ] && break
done
[ -n "$LOG" ] || { echo "no page log"; kill $PID; exit 1; }
printf "%-8s %8s %8s | host %% | page: WebContent %% GPU %%\n" phase locks rows
n=0
while [ $n -lt $((SECS * 2)) ]; do
	grep -q "P4: cpu done" "$LOG" 2>/dev/null && break
	kill -0 $PID 2>/dev/null || break
	for ph in $(grep -o "P4: cpu [a-z0-9]* start" "$LOG" | awk '{print $3}'); do
		[ -f "$TMP/ph.$ph" ] && continue
		echo "$(secs $PID) $(secs ${web:-0}) $(secs ${gpu:-0}) $(date +%s)" > "$TMP/ph.$ph"
	done
	for ph in $(grep -o "P4: cpu [a-z0-9]* end" "$LOG" | awk '{print $3}'); do
		[ -f "$TMP/done.$ph" ] && continue
		touch "$TMP/done.$ph"
		set -- $(cat "$TMP/ph.$ph")
		a1=$(secs $PID); w1=$(secs ${web:-0}); g1=$(secs ${gpu:-0}); t1=$(date +%s)
		dt=$((t1 - $4)); [ $dt -lt 1 ] && dt=1
		locks=$(grep "P4: cpu $ph end" "$LOG" | sed -n 's/.*locks \([0-9]*\) rows \([0-9]*\).*/\1 \2/p' | head -1)
		printf "%-8s %8s %8s | %5.1f | %5.1f %5.1f\n" "$ph" $locks \
			"$(echo "($a1-$1)*100/$dt" | bc -l)" "$(echo "(${w1:-0}-$2)*100/$dt" | bc -l)" "$(echo "(${g1:-0}-$3)*100/$dt" | bc -l)"
	done
	sleep 0.5; n=$((n + 1))
done
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null; wait $PID 2>/dev/null
sysctl -n machdep.cpu.brand_string
echo "log: $LOG"
rm -rf "$TMP"
