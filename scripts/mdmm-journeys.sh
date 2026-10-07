#!/bin/sh
# The editors' user journeys (doc/modern-ux/FOUNDATION.md, Build and check): runs the diagnostics standalone of the
# Machinedrum Editor, the Monomachine Editor or both with GEARMULATOR_MDSTUDIO_SELFTEST / GEARMULATOR_MMSTUDIO_SELFTEST
# set to the journeys asked for, waits for "JOURNEYS DONE" in the page's log, prints a line per journey and exits
# non-zero on any FAIL, a timeout or a missing build or ROM.
#   scripts/mdmm-journeys.sh [md|mm|both] [selector]      selector: journey (all, the default), journey-seq-*,
#                                                          journey-md-lib-*,mm-mix-* (comma-separated, * any)
# Environment: MDMM_APP_DIR the folder holding "Gearmulator MD.app" and "Gearmulator MM.app" (default: this tree's
# bin/plugins/Release/Standalone, where a diagnostics build puts them), MDMM_JOURNEY_TIMEOUT seconds per editor
# (default 900), MDMM_JOURNEY_VERBOSE=1 prints every step line too, MDMM_JOURNEY_FRONT=0 does not bring the app to
# the front (a covered window draws no canvases and slows its timers, so some journeys fail there).
# The ROMs are the ones already in the plug-ins' ROM folders (~/Documents/Gearmulator Preview/<machine>/roms); none is
# copied anywhere. The editor's config file and standalone settings (they hold the machine's memory, which the
# journeys write to) are backed up first and restored byte for byte afterwards, pass or fail.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WHICH=${1:-both}
SEL=${2:-journey}
APPS=${MDMM_APP_DIR:-"$ROOT/bin/plugins/Release/Standalone"}
TIMEOUT=${MDMM_JOURNEY_TIMEOUT:-900}
case "$SEL" in journey*) ;; *) SEL="journey-$SEL" ;; esac
case "$WHICH" in md|mm|both) ;; *) echo "usage: $0 [md|mm|both] [journey|journey-<names>]" >&2; exit 2 ;; esac

STATUS=0
run_one() {	# $1: MD or MM
	M=$1
	if [ "$M" = MD ]; then MACHINE=Machinedrum SKIN=mdStudio VAR=GEARMULATOR_MDSTUDIO_SELFTEST BUNDLE=com.nativekloud.machinedrum-editor; else MACHINE=Monomachine SKIN=mmStudio VAR=GEARMULATOR_MMSTUDIO_SELFTEST BUNDLE=com.nativekloud.monomachine-editor; fi
	APP="$APPS/Gearmulator $M.app"
	EXE="$APP/Contents/MacOS/Gearmulator $M"
	DATA="$HOME/Documents/Gearmulator Preview/$MACHINE"
	C="$DATA/config/$MACHINE Editor.xml"
	SETTINGS="$HOME/Library/Application Support/$MACHINE Editor.settings"
	LOGDIR="$HOME/Library/Caches/Gearmulator $M"
	echo "== $MACHINE Editor: $SEL"
	if [ ! -x "$EXE" ]; then echo "FAIL no diagnostics build at $APP (configure with -Dgearmulator_MDMM_DIAGNOSTICS=ON, build the $(echo $M | tr A-Z a-z)JucePlugin_Standalone target)"; STATUS=1; return; fi
	if ! ls "$DATA/roms/"* >/dev/null 2>&1; then echo "FAIL no $MACHINE ROM in $DATA/roms (it stays there; the journeys never copy it)"; STATUS=1; return; fi
	if pgrep -f "Gearmulator $M.app/Contents/MacOS" >/dev/null 2>&1 || pgrep -fx ".*/(Machinedrum|Monomachine) Editor.app/.*" >/dev/null 2>&1; then
		echo "FAIL a Gearmulator $M app is running: quit it first (the journeys use the same settings)"; STATUS=1; return
	fi
	TMP=$(mktemp -d)
	[ -f "$C" ] && cp -p "$C" "$TMP/config.xml"
	[ -f "$SETTINGS" ] && cp -p "$SETTINGS" "$TMP/settings"
	restore() {
		if [ -f "$TMP/config.xml" ]; then cp -p "$TMP/config.xml" "$C"; else rm -f "$C"; fi
		if [ -f "$TMP/settings" ]; then cp -p "$TMP/settings" "$SETTINGS"; else rm -f "$SETTINGS"; fi
		rm -rf "$TMP"
	}
	trap 'restore; exit 1' INT TERM
	# the page skin for the run (a configured skin is pointed at it)
	if [ -f "$TMP/config.xml" ]; then
		sed -e "s/\"skinDisplayName\" val=\"[^\"]*\"/\"skinDisplayName\" val=\"$SKIN\"/" -e "s/\"skinFile\" val=\"[^\"]*\"/\"skinFile\" val=\"$SKIN.rml\"/" "$TMP/config.xml" > "$C"
	fi
	mkdir -p "$LOGDIR"; touch "$TMP/start"
	env "$VAR=$SEL" "$EXE" >"$TMP/app.out" 2>&1 &
	PID=$!
	# The page draws its canvases on animation frames and runs its timers at full rate only while its window is
	# visible (WebKit pauses a covered page's frames): the app is brought to the front once it is up. Do not type
	# into it while it runs. MDMM_JOURNEY_FRONT=0 leaves it where it opens.
	if [ "${MDMM_JOURNEY_FRONT:-1}" = 1 ]; then
		( sleep 4; osascript -e "tell application id \"$BUNDLE\" to activate" >/dev/null 2>&1 ) &
	fi
	# the page's log is a new gearmulator-<skin>-<random>.log in the app's caches folder
	LOG=""; t=0; DONE=""
	while [ $t -lt "$TIMEOUT" ]; do
		sleep 2; t=$((t + 2))
		if [ -z "$LOG" ]; then LOG=$(find "$LOGDIR" -name "gearmulator-$SKIN-*.log" -newer "$TMP/start" 2>/dev/null | head -1); fi
		if [ -n "$LOG" ] && grep -q "JOURNEYS DONE" "$LOG" 2>/dev/null; then DONE=1; break; fi
		if ! kill -0 $PID 2>/dev/null; then break; fi
	done
	osascript -e "tell application id \"$BUNDLE\" to quit" >/dev/null 2>&1 || true
	for i in 1 2 3 4 5; do kill -0 $PID 2>/dev/null || break; sleep 1; done
	kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
	wait $PID 2>/dev/null
	restore; trap - INT TERM
	if [ -n "$LOG" ]; then
		if [ "${MDMM_JOURNEY_VERBOSE:-0}" = 1 ]; then grep -E "JOURNEY" "$LOG" | sed 's/^[^J]*JOURNEY/JOURNEY/'
		else grep -E "JOURNEY [^ ]+ (PASS|FAIL|SKIP)|JOURNEYS (start|DONE)" "$LOG" | sed 's/^[^J]*JOURNEY/JOURNEY/'; fi
		echo "   log: $LOG"
	fi
	if [ -z "$DONE" ]; then
		if kill -0 $PID 2>/dev/null; then echo "FAIL timeout after ${TIMEOUT} s"; else echo "FAIL the app ended before JOURNEYS DONE (see $TMP/app.out and the log)"; fi
		STATUS=1; return
	fi
	if grep -qE "JOURNEY [^ ]+ FAIL" "$LOG"; then STATUS=1; fi
	grep -q "JOURNEYS DONE 0/0" "$LOG" && { echo "FAIL no journey matched $SEL"; STATUS=1; }
	return 0
}
[ "$WHICH" = md ] || [ "$WHICH" = both ] && run_one MD
[ "$WHICH" = mm ] || [ "$WHICH" = both ] && run_one MM
exit $STATUS
