#!/bin/bash
# The editors' user journeys (doc/modern-ux/FOUNDATION.md, Build and check): runs the diagnostics build of the
# Machinedrum Editor, the Monomachine Editor or both with GEARMULATOR_MDSTUDIO_SELFTEST / GEARMULATOR_MMSTUDIO_SELFTEST
# set to the journeys asked for, waits for "JOURNEYS DONE" in the page's log, prints a line per journey and exits
# non-zero on any FAIL, a timeout or a missing build or ROM.
#   scripts/mdmm-journeys.sh [--host standalone|vst3|both] [--background] [--jobs N] [md|mm|both] [selector]
#     selector: journey (all, the default), journey-seq-*, journey-md-lib-*,mm-mix-* (comma-separated, * any)
#     --host: the standalone apps (the default), the VST3s in the minimal host scripts/vst3EditorHost, or both
#     --jobs N: N editors at once, each playing every N-th of the chosen journeys (the selector's "@k/N" shard,
#       skins/shared/deskJourney.js); one report at the end. Implies --background. About one core per editor.
#     --background (macOS): the editor never takes the focus nor comes to the front: an accessory app, its window
#       small (the page zoomed to half), in the bottom-left corner behind every other window, and the page drawn
#       while covered, and the standalone's output silent while its audio still runs (GEARMULATOR_MDMM_BACKGROUND=1,
#       mdBackgroundRun.h). The VST3 host always runs so, and plays to no device.
# Isolation: every editor runs in a sandbox of its own and never reads or writes the person's files but the ones it
# copies from: its own data root (GEARMULATOR_DATA_ROOT: the ROM linked, the person's config copied with the page
# skin set) and its own home for everything macOS keeps under ~/Library (CFFIXED_USER_HOME, which NSHomeDirectory
# and so JUCE follow; HOME does not move it): the standalone's settings (a copy of the person's, which hold the
# machine's memory the journeys write to; MDMM_JOURNEY_SETTINGS=fresh starts without), the page's log, WebKit's data.
# Nothing is backed up or restored because nothing of the person's is written: two runs at once, or a run beside the
# person's own editor, cannot touch ~/Library/Application Support/<editor>.settings or
# ~/Documents/Gearmulator Preview/*/config.
# Environment: MDMM_APP_DIR the folder holding "Machinedrum Editor.app" and "Monomachine Editor.app" (default: this tree's
# bin/plugins/Release/Standalone, where a diagnostics build puts them, scripts/mdmm-dev.sh), MDMM_VST3_DIR the folder
# holding the .vst3 bundles (default bin/plugins/Release/VST3), MDMM_VST3_HOST the host's binary (default: built into
# temp/vst3EditorHost by scripts/macos/build_vst3_host.sh when missing), MDMM_JOURNEY_TIMEOUT seconds per editor
# (default 900), MDMM_JOURNEY_OUT the report folder (default temp/journeys/<date-time>: each editor's page log,
# app output and the merged report.txt), MDMM_JOURNEY_VERBOSE=1 prints every step line too, MDMM_JOURNEY_PERF=1
# records the audio callbacks (GEARMULATOR_RT_INSTRUMENTATION=1: overruns, slow callbacks) into performance-*.jsonl in
# the report folder, MDMM_VST3_STATE=standalone starts the VST3 from the standalone's saved state (the filterState of
# the person's settings, read only) instead of none, MDMM_JOURNEY_FRONT=0 does not bring the standalone to the front
# without --background (a covered window then draws no canvases and slows its timers, so some journeys fail there).
# The ROMs: GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN, else the one in the plug-ins' ROM folder
# (~/Documents/Gearmulator Preview/<machine>/roms); never copied or written (a link in the sandbox's ROM folder).
set -u
. "$(cd "$(dirname "$0")" && pwd)/mdmm-product.env"	# the product names: the apps, their executables and caches
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOSTS=standalone
BACKGROUND=0
JOBS=1
while [ $# -gt 0 ]; do
	case "$1" in
		--host) HOSTS=${2:-}; shift 2 ;;
		--host=*) HOSTS=${1#--host=}; shift ;;
		--jobs) JOBS=${2:-}; shift 2 ;;
		--jobs=*) JOBS=${1#--jobs=}; shift ;;
		--background) BACKGROUND=1; shift ;;
		*) break ;;
	esac
done
WHICH=${1:-both}
SEL=${2:-journey}
APPS=${MDMM_APP_DIR:-"$ROOT/bin/plugins/Release/Standalone"}
VST3S=${MDMM_VST3_DIR:-"$ROOT/bin/plugins/Release/VST3"}
TIMEOUT=${MDMM_JOURNEY_TIMEOUT:-900}
OUT=${MDMM_JOURNEY_OUT:-"$ROOT/temp/journeys/$(date +%Y%m%d-%H%M%S)"}
case "$SEL" in journey*) ;; *) SEL="journey-$SEL" ;; esac
USAGE="usage: $0 [--host standalone|vst3|both] [--background] [--jobs N] [md|mm|both] [journey|journey-<names>]"
case "$WHICH" in md|mm|both) ;; *) echo "$USAGE" >&2; exit 2 ;; esac
case "$HOSTS" in standalone|vst3|both) ;; *) echo "$USAGE" >&2; exit 2 ;; esac
case "$JOBS" in ''|*[!0-9]*|0) echo "$USAGE" >&2; exit 2 ;; esac
case "$SEL" in *@*) echo "the selector takes no @shard: --jobs makes the shards" >&2; exit 2 ;; esac
# several editors at once can neither all be in front nor all play to the audio device
[ "$JOBS" -gt 1 ] && BACKGROUND=1
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

HOST_BIN=""
vst3_host() {	# the host's binary in HOST_BIN, built once when there is none
	[ -n "$HOST_BIN" ] && return 0
	HOST_BIN=${MDMM_VST3_HOST:-}
	if [ -z "$HOST_BIN" ]; then
		HOST_BIN=$(find "$ROOT/temp/vst3EditorHost" -path '*mdmmVst3EditorHost.app/Contents/MacOS/mdmmVst3EditorHost' -type f 2>/dev/null | head -1)
		if [ -z "$HOST_BIN" ]; then
			echo "   building the VST3 host into temp/vst3EditorHost"
			HOST_BIN=$("$ROOT/scripts/macos/build_vst3_host.sh" "$ROOT/temp/vst3EditorHost" 2>"$ROOT/temp/vst3EditorHost.log" | sed -n 's/^VST3_HOST=//p')
		fi
	fi
	[ -x "$HOST_BIN" ]
}

# What one editor needs, checked before anything starts: prints the problem and returns 1.
#   $1: MD or MM, $2: standalone or vst3
machine_vars() {
	if [ "$1" = MD ]; then MACHINE=Machinedrum SKIN=mdStudio VAR=GEARMULATOR_MDSTUDIO_SELFTEST NAME=$MDMM_PRODUCT_NAME_MD LEGACY="Gearmulator MD" ROMVAR=${GEARMULATOR_MD_FIRMWARE_BIN:-} ROMENV=GEARMULATOR_MD_FIRMWARE_BIN
	else MACHINE=Monomachine SKIN=mmStudio VAR=GEARMULATOR_MMSTUDIO_SELFTEST NAME=$MDMM_PRODUCT_NAME_MM LEGACY="Gearmulator MM" ROMVAR=${GEARMULATOR_MM_FIRMWARE_BIN:-} ROMENV=GEARMULATOR_MM_FIRMWARE_BIN; fi
	DATA="$HOME/Documents/Gearmulator Preview/$MACHINE"
	ROM=$ROMVAR
	[ -n "$ROM" ] || ROM=$(ls "$DATA/roms/"* 2>/dev/null | head -1)
}
check_one() {
	machine_vars "$1"
	if [ "$2" = standalone ]; then
		if [ ! -x "$APPS/$NAME.app/Contents/MacOS/$NAME" ]; then echo "FAIL no diagnostics build at $APPS/$NAME.app (scripts/mdmm-dev.sh build, or configure with -Dgearmulator_MDMM_DIAGNOSTICS=ON and build the $(echo "$1" | tr A-Z a-z)JucePlugin_Standalone target)"; return 1; fi
	else
		if [ ! -d "$VST3S/$NAME.vst3" ]; then echo "FAIL no diagnostics build at $VST3S/$NAME.vst3 (scripts/mdmm-dev.sh build)"; return 1; fi
		if ! vst3_host; then echo "FAIL the VST3 host did not build (temp/vst3EditorHost.log)"; return 1; fi
	fi
	if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then echo "FAIL no $MACHINE ROM (set $ROMENV or put it in $DATA/roms)"; return 1; fi
	return 0
}

# One editor in its sandbox, in the background of this script: writes $UNIT/result (DONE, TIMEOUT or ENDED), the page's
# lines into $UNIT/journeys.txt and keeps the page's log and the app's output in $UNIT.
#   $1: MD or MM, $2: standalone or vst3, $3: the selector, $4: the unit's folder in the report
run_unit() {
	M=$1 HOST=$2 S=$3 UNIT=$4
	machine_vars "$M"
	mkdir -p "$UNIT"
	BOX=$(mktemp -d "${TMPDIR:-/tmp}/mdmm-journey.XXXXXX")
	trap 'kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null; rm -rf "$BOX"; exit 1' INT TERM
	DROOT="$BOX/data/"
	SHOME="$BOX/home"
	mkdir -p "$DROOT/Gearmulator Preview/$MACHINE/roms" "$DROOT/Gearmulator Preview/$MACHINE/config" "$SHOME/Library/Application Support" "$SHOME/Library/Caches"
	ln -s "$ROM" "$DROOT/Gearmulator Preview/$MACHINE/roms/$(basename "$ROM")"
	C="$DATA/config/$MACHINE Editor.xml"
	if [ -f "$C" ]; then
		sed -e "s/\"skinDisplayName\" val=\"[^\"]*\"/\"skinDisplayName\" val=\"$SKIN\"/" -e "s/\"skinFile\" val=\"[^\"]*\"/\"skinFile\" val=\"$SKIN.rml\"/" \
			"$C" > "$DROOT/Gearmulator Preview/$MACHINE/config/$MACHINE Editor.xml"
	fi
	# the person's settings (read only): the editor's own, else the ones it copies them from the first time
	SAVED="$HOME/Library/Application Support/$MACHINE Editor.settings"
	[ -f "$SAVED" ] || SAVED="$HOME/Library/Application Support/$LEGACY.settings"
	if [ "$HOST" = standalone ] && [ "${MDMM_JOURNEY_SETTINGS:-copy}" = copy ] && [ -f "$SAVED" ]; then
		cp "$SAVED" "$SHOME/Library/Application Support/$MACHINE Editor.settings"
	fi
	touch "$BOX/start"
	PERF=""; [ "${MDMM_JOURNEY_PERF:-0}" = 1 ] && PERF="GEARMULATOR_RT_INSTRUMENTATION=1"
	BG=""; [ "$BACKGROUND" = 1 ] && BG="GEARMULATOR_MDMM_BACKGROUND=1"
	if [ "$HOST" = standalone ]; then
		env $PERF $BG CFFIXED_USER_HOME="$SHOME" GEARMULATOR_DATA_ROOT="$DROOT" "$VAR=$S" "$APPS/$NAME.app/Contents/MacOS/$NAME" >"$UNIT/app.out" 2>&1 &
		PID=$!
		echo "$PID" > "$BOX/pid"	# for whoever watches this box (scripts/mdmm-shots.sh: this process's window)
		# Without --background the page draws its canvases on animation frames and runs its timers at full rate only
		# while its window is visible (WebKit pauses a covered page's frames): this process (not any other copy of the
		# app, the person's own included) is brought to the front once it is up. Do not type into it while it runs.
		if [ "$BACKGROUND" = 0 ] && [ "${MDMM_JOURNEY_FRONT:-1}" = 1 ]; then
			( sleep 4; osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true" >/dev/null 2>&1 ) &
		fi
	else
		STATE=""
		if [ "${MDMM_VST3_STATE:-}" = standalone ] && [ -f "$SAVED" ]; then
			sed -n 's/.*<VALUE name="filterState" val="\([^"]*\)".*/\1/p' "$SAVED" > "$BOX/state.b64"
			[ -s "$BOX/state.b64" ] && STATE="--state $BOX/state.b64"
		fi
		env $PERF CFFIXED_USER_HOME="$SHOME" GEARMULATOR_DATA_ROOT="$DROOT" GEARMULATOR_MDMM_BACKGROUND=1 "$VAR=$S" "$HOST_BIN" "$VST3S/$NAME.vst3" $((TIMEOUT + 60)) --background $STATE >"$UNIT/app.out" 2>&1 &
		PID=$!
	fi
	# the page's log: a new gearmulator-<skin>-<random>.log in JUCE's temp folder, ~/Library/Caches/<executable>, here
	# the sandbox's (the plug-in's binary in the host)
	LOG=""; t=0; RESULT=TIMEOUT
	while [ $t -lt "$TIMEOUT" ]; do
		sleep 2; t=$((t + 2))
		[ -n "$LOG" ] || LOG=$(find "$SHOME/Library/Caches" -name "gearmulator-$SKIN-*.log" -newer "$BOX/start" 2>/dev/null | head -1)
		if [ -n "$LOG" ] && grep -q "JOURNEYS DONE" "$LOG" 2>/dev/null; then RESULT=DONE; break; fi
		if ! kill -0 $PID 2>/dev/null; then RESULT=ENDED; break; fi
	done
	# nothing to save: the sandbox goes, so the editor is simply stopped
	kill $PID 2>/dev/null
	for i in 1 2 3 4 5; do kill -0 $PID 2>/dev/null || break; sleep 1; done
	kill -9 $PID 2>/dev/null
	wait $PID 2>/dev/null
	[ -n "$LOG" ] || LOG=$(find "$SHOME/Library/Caches" -name "gearmulator-$SKIN-*.log" -newer "$BOX/start" 2>/dev/null | head -1)
	if [ -n "$LOG" ]; then
		cp "$LOG" "$UNIT/page.log"
		grep -E "JOURNEY" "$LOG" | sed 's/^[^J]*JOURNEY/JOURNEY/' > "$UNIT/journeys.txt"
	fi
	# the audio callbacks' record (MDMM_JOURNEY_PERF=1)
	for f in "$DROOT/Gearmulator Preview/$MACHINE/logs/"performance-*.jsonl; do
		[ -f "$f" ] && cp "$f" "$UNIT/"
	done
	echo "$RESULT $t" > "$UNIT/result"
	rm -rf "$BOX"
	trap - INT TERM
}

# The units: each editor and host asked for, split into JOBS shards; at most JOBS run at once.
UNITS=""
STATUS=0
for H in standalone vst3; do
	[ "$HOSTS" = "$H" ] || [ "$HOSTS" = both ] || continue
	for M in MD MM; do
		[ "$WHICH" = both ] || [ "$WHICH" = "$(echo $M | tr A-Z a-z)" ] || continue
		if ! check_one "$M" "$H"; then STATUS=1; continue; fi
		k=1
		while [ $k -le "$JOBS" ]; do
			UNITS="$UNITS $M:$H:$k"
			k=$((k + 1))
		done
	done
done
[ -n "$UNITS" ] || exit 1

echo "== journeys: $SEL, $(echo $UNITS | wc -w | tr -d ' ') editor run(s), $JOBS at once$( [ "$BACKGROUND" = 1 ] && echo ", in the background"); report: $OUT"
START=$(date +%s)
PIDS=""
for u in $UNITS; do
	M=${u%%:*}; rest=${u#*:}; H=${rest%%:*}; k=${rest#*:}
	sel=$SEL; [ "$JOBS" -gt 1 ] && sel="$SEL@$k/$JOBS"
	dir="$OUT/$(echo $M | tr A-Z a-z)-$H$( [ "$JOBS" -gt 1 ] && echo "-$k")"
	# a free slot: wait on the PIDs started (never on a process name)
	while [ "$(echo $PIDS | wc -w)" -ge "$JOBS" ]; do
		sleep 2
		alive=""
		for p in $PIDS; do kill -0 "$p" 2>/dev/null && alive="$alive $p"; done
		PIDS=$alive
	done
	run_unit "$M" "$H" "$sel" "$dir" &
	PIDS="$PIDS $!"
	echo "   started $dir ($sel)"
	sleep 3	# the editors' starts staggered: the first seconds are the heaviest
done
for p in $PIDS; do wait "$p"; done

# The merged report
REPORT="$OUT/report.txt"
: > "$REPORT"
passed=0; failed=0; skipped=0
for u in $UNITS; do
	M=${u%%:*}; rest=${u#*:}; H=${rest%%:*}; k=${rest#*:}
	dir="$OUT/$(echo $M | tr A-Z a-z)-$H$( [ "$JOBS" -gt 1 ] && echo "-$k")"
	machine_vars "$M"
	{
		echo "== $NAME ($H)$( [ "$JOBS" -gt 1 ] && echo ", shard $k/$JOBS")"
		if [ -f "$dir/journeys.txt" ]; then
			if [ "${MDMM_JOURNEY_VERBOSE:-0}" = 1 ]; then cat "$dir/journeys.txt"
			else grep -E "JOURNEY [^ ]+ (PASS|FAIL|SKIP)|JOURNEYS (start|DONE)" "$dir/journeys.txt"; fi
			echo "   log: $dir/page.log"
		fi
		read -r result secs < "$dir/result" 2>/dev/null || result=ENDED
		case "$result" in
			DONE) grep -q "JOURNEYS DONE 0/0" "$dir/journeys.txt" && [ "$JOBS" -eq 1 ] && echo "FAIL no journey matched $SEL" ;;
			TIMEOUT) echo "FAIL timeout after ${TIMEOUT} s" ;;
			*) echo "FAIL the editor ended before JOURNEYS DONE:"; tail -5 "$dir/app.out" 2>/dev/null ;;
		esac
	} >> "$REPORT"
	read -r result secs < "$dir/result" 2>/dev/null || result=ENDED
	[ "$result" = DONE ] || STATUS=1
	if [ -f "$dir/journeys.txt" ]; then
		p=$(grep -cE "JOURNEY [^ ]+ PASS" "$dir/journeys.txt"); f=$(grep -cE "JOURNEY [^ ]+ FAIL" "$dir/journeys.txt"); s=$(grep -cE "JOURNEY [^ ]+ SKIP" "$dir/journeys.txt")
		passed=$((passed + p)); failed=$((failed + f)); skipped=$((skipped + s))
	fi
done
[ "$failed" -gt 0 ] && STATUS=1
[ $((passed + failed + skipped)) -eq 0 ] && { echo "FAIL no journey matched $SEL" >> "$REPORT"; STATUS=1; }
echo "JOURNEYS: $passed passed, $failed failed, $skipped skipped in $(( $(date +%s) - START )) s ($JOBS at once)" >> "$REPORT"
cat "$REPORT"
exit $STATUS
