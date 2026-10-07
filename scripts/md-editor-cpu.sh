#!/bin/sh
# P4: CPU of the Machinedrum Editor standalone, repeatable. It runs the built standalone with the
# mdStudio skin and GEARMULATOR_MDSTUDIO_SELFTEST=p4cpu (fixed 30 s phases: stopped, playing in
# Sequence, playing in Mix), and reads the CPU time of the app process (emulation, audio, JUCE) and
# of the WebKit content process that draws the page, at each phase's start and end.
# The user's MD config file and standalone settings are backed up and restored byte for byte.
#   scripts/md-editor-cpu.sh [path to "Machinedrum Editor.app"] (the names: scripts/mdmm-product.env)
set -e
. "$(cd "$(dirname "$0")" && pwd)/mdmm-product.env"
NAME=$MDMM_PRODUCT_NAME_MD
APP=${1:-"$(cd "$(dirname "$0")/.." && pwd)/bin/plugins/Release/Standalone/$NAME.app"}
C="$HOME/Documents/Gearmulator Preview/Machinedrum/config/Machinedrum Editor.xml"
SETTINGS="$HOME/Library/Application Support/Machinedrum Editor.settings"
LOG="$HOME/Library/Caches/$NAME/gearmulator-mdStudio.log"
TMP=$(mktemp -d)
cp -p "$C" "$TMP/config.xml"; cp -p "$SETTINGS" "$TMP/settings" 2>/dev/null || true
restore() { cp -p "$TMP/config.xml" "$C"; [ -f "$TMP/settings" ] && cp -p "$TMP/settings" "$SETTINGS"; }
trap restore EXIT
sed -e 's/"skinDisplayName" val="[^"]*"/"skinDisplayName" val="mdStudio"/' -e 's/"skinFile" val="[^"]*"/"skinFile" val="mdStudio.rml"/' "$TMP/config.xml" > "$C"
rm -f "$LOG"
before=$(pgrep -f 'com.apple.WebKit.(WebContent|GPU)' | sort)
GEARMULATOR_MDSTUDIO_SELFTEST=p4cpu "$APP/Contents/MacOS/$NAME" >/dev/null 2>&1 &
PID=$!
secs() { ps -o time= -p "$1" 2>/dev/null | awk -F: '{ if (NF==3) print $1*3600+$2*60+$3; else print $1*60+$2 }'; }
# The page's WebKit processes (content and GPU) that started with the app.
web=""; gpu=""
for i in $(seq 1 60); do
	sleep 1
	for p in $(pgrep -f com.apple.WebKit.WebContent); do echo "$before" | grep -qx "$p" || web=$p; done
	for p in $(pgrep -f com.apple.WebKit.GPU); do echo "$before" | grep -qx "$p" || gpu=$p; done
	[ -n "$web" ] && break
done
report() { printf "%-12s app %5.1f %% of one core, page: WebKit content %5.1f %%, WebKit GPU %5.1f %%\n" "$1" "$2" "$3" "$4"; }
for phase in stopped playing-seq playing-mix; do
	until grep -q "cpu $phase start" "$LOG" 2>/dev/null; do sleep 0.2; done
	a0=$(secs $PID); w0=$(secs $web); g0=$(secs "${gpu:-0}"); t0=$(date +%s)
	until grep -q "cpu $phase end" "$LOG" 2>/dev/null; do sleep 0.2; done
	a1=$(secs $PID); w1=$(secs $web); g1=$(secs "${gpu:-0}"); t1=$(date +%s)
	report "$phase" "$(echo "($a1-$a0)*100/($t1-$t0)" | bc -l)" "$(echo "(${w1:-0}-${w0:-0})*100/($t1-$t0)" | bc -l)" "$(echo "(${g1:-0}-${g0:-0})*100/($t1-$t0)" | bc -l)"
done
osascript -e 'tell application id "com.nativekloud.machinedrum-editor" to quit' >/dev/null 2>&1 || true
sleep 3; kill $PID 2>/dev/null || true
sysctl -n hw.model machdep.cpu.brand_string | tr '\n' ' '; echo "($(sysctl -n hw.ncpu) cores)"
grep -E " audio: | window: " "$LOG" | tail -2 || true
