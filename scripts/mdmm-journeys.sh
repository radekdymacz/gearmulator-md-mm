#!/bin/sh
# The editors' user journeys (doc/modern-ux/FOUNDATION.md, Build and check): runs the diagnostics build of the
# Machinedrum Editor, the Monomachine Editor or both with GEARMULATOR_MDSTUDIO_SELFTEST / GEARMULATOR_MMSTUDIO_SELFTEST
# set to the journeys asked for, waits for "JOURNEYS DONE" in the page's log, prints a line per journey and exits
# non-zero on any FAIL, a timeout or a missing build or ROM.
#   scripts/mdmm-journeys.sh [--host standalone|vst3|both] [--background] [md|mm|both] [selector]
#     selector: journey (all, the default), journey-seq-*, journey-md-lib-*,mm-mix-* (comma-separated, * any)
#     --host: the standalone apps (the default), the VST3s in the minimal host scripts/vst3EditorHost, or both
#     --background (macOS): the editor never takes the focus nor comes to the front: an accessory app, its window
#       small (the page zoomed to half), in the bottom-left corner behind every other window, and the page drawn
#       while covered, and the standalone's output silent while its audio still runs (GEARMULATOR_MDMM_BACKGROUND=1,
#       mdBackgroundRun.h). The VST3 host always runs so, and plays to no device.
# Environment: MDMM_APP_DIR the folder holding "Machinedrum Editor.app" and "Monomachine Editor.app" (default: this tree's
# bin/plugins/Release/Standalone, where a diagnostics build puts them), MDMM_VST3_DIR the folder holding the .vst3
# bundles (default bin/plugins/Release/VST3), MDMM_VST3_HOST the host's binary (default: built into temp/vst3EditorHost
# by scripts/macos/build_vst3_host.sh when missing), MDMM_JOURNEY_TIMEOUT seconds per editor (default 900),
# MDMM_JOURNEY_VERBOSE=1 prints every step line too, MDMM_JOURNEY_PERF=1 records the audio callbacks
# (GEARMULATOR_RT_INSTRUMENTATION=1: overruns, slow callbacks) into performance-*.jsonl beside the page's log,
# MDMM_VST3_STATE=standalone starts the VST3 from the standalone's saved state (the filterState of the settings it
# starts from, read only) instead of none, MDMM_JOURNEY_FRONT=0 does not bring the standalone to the front
# without --background (a covered window then draws no canvases and slows its timers, so some journeys fail there).
# The ROMs: GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN, else the one in the plug-ins' ROM folder
# (~/Documents/Gearmulator Preview/<machine>/roms); never copied or written: each run has a scratch data root
# (GEARMULATOR_DATA_ROOT) whose ROM folder links to it, and the editor's config there is a copy of the person's with
# the page skin set. The standalone's settings (they hold the machine's memory, which the journeys write to) are
# backed up first and restored byte for byte afterwards, pass or fail.
set -u
. "$(cd "$(dirname "$0")" && pwd)/mdmm-product.env"	# the product names: the apps, their executables and caches
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOSTS=standalone
BACKGROUND=0
while [ $# -gt 0 ]; do
	case "$1" in
		--host) HOSTS=${2:-}; shift 2 ;;
		--host=*) HOSTS=${1#--host=}; shift ;;
		--background) BACKGROUND=1; shift ;;
		*) break ;;
	esac
done
WHICH=${1:-both}
SEL=${2:-journey}
APPS=${MDMM_APP_DIR:-"$ROOT/bin/plugins/Release/Standalone"}
VST3S=${MDMM_VST3_DIR:-"$ROOT/bin/plugins/Release/VST3"}
TIMEOUT=${MDMM_JOURNEY_TIMEOUT:-900}
case "$SEL" in journey*) ;; *) SEL="journey-$SEL" ;; esac
USAGE="usage: $0 [--host standalone|vst3|both] [--background] [md|mm|both] [journey|journey-<names>]"
case "$WHICH" in md|mm|both) ;; *) echo "$USAGE" >&2; exit 2 ;; esac
case "$HOSTS" in standalone|vst3|both) ;; *) echo "$USAGE" >&2; exit 2 ;; esac

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

STATUS=0
run_one() {	# $1: MD or MM, $2: standalone or vst3
	M=$1 HOST=$2
	if [ "$M" = MD ]; then MACHINE=Machinedrum SKIN=mdStudio VAR=GEARMULATOR_MDSTUDIO_SELFTEST BUNDLE=com.nativekloud.machinedrum-editor NAME=$MDMM_PRODUCT_NAME_MD ROMVAR=${GEARMULATOR_MD_FIRMWARE_BIN:-}
	else MACHINE=Monomachine SKIN=mmStudio VAR=GEARMULATOR_MMSTUDIO_SELFTEST BUNDLE=com.nativekloud.monomachine-editor NAME=$MDMM_PRODUCT_NAME_MM ROMVAR=${GEARMULATOR_MM_FIRMWARE_BIN:-}; fi
	DATA="$HOME/Documents/Gearmulator Preview/$MACHINE"
	C="$DATA/config/$MACHINE Editor.xml"
	SETTINGS="$HOME/Library/Application Support/$MACHINE Editor.settings"
	LOGDIR="$HOME/Library/Caches/$NAME"	# JUCE's temp folder: ~/Library/Caches/<executable>; in the host, the plug-in's binary
	echo "== $NAME ($HOST): $SEL"
	if [ "$HOST" = standalone ]; then
		APP="$APPS/$NAME.app"
		EXE="$APP/Contents/MacOS/$NAME"
		if [ ! -x "$EXE" ]; then echo "FAIL no diagnostics build at $APP (configure with -Dgearmulator_MDMM_DIAGNOSTICS=ON, build the $(echo $M | tr A-Z a-z)JucePlugin_Standalone target)"; STATUS=1; return; fi
		if pgrep -f "$NAME.app/Contents/MacOS" >/dev/null 2>&1 || pgrep -f "Gearmulator $M.app/Contents/MacOS" >/dev/null 2>&1; then
			echo "FAIL a $NAME app is running: quit it first (the journeys use the same settings)"; STATUS=1; return
		fi
	else
		PLUGIN="$VST3S/$NAME.vst3"
		if [ ! -d "$PLUGIN" ]; then echo "FAIL no diagnostics build at $PLUGIN (configure with -Dgearmulator_MDMM_DIAGNOSTICS=ON, build the $(echo $M | tr A-Z a-z)JucePlugin_VST3 target)"; STATUS=1; return; fi
		if ! vst3_host; then echo "FAIL the VST3 host did not build (temp/vst3EditorHost.log)"; STATUS=1; return; fi
	fi
	ROM=$ROMVAR
	[ -n "$ROM" ] || ROM=$(ls "$DATA/roms/"* 2>/dev/null | head -1)
	if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then echo "FAIL no $MACHINE ROM (set $( [ "$M" = MD ] && echo GEARMULATOR_MD_FIRMWARE_BIN || echo GEARMULATOR_MM_FIRMWARE_BIN) or put it in $DATA/roms)"; STATUS=1; return; fi

	TMP=$(mktemp -d)
	# the scratch data root: the ROM linked, the person's config copied with the page skin set
	ROOTDIR="$TMP/data/"
	mkdir -p "$ROOTDIR/Gearmulator Preview/$MACHINE/roms" "$ROOTDIR/Gearmulator Preview/$MACHINE/config"
	ln -s "$ROM" "$ROOTDIR/Gearmulator Preview/$MACHINE/roms/$(basename "$ROM")"
	if [ -f "$C" ]; then
		sed -e "s/\"skinDisplayName\" val=\"[^\"]*\"/\"skinDisplayName\" val=\"$SKIN\"/" -e "s/\"skinFile\" val=\"[^\"]*\"/\"skinFile\" val=\"$SKIN.rml\"/" \
			"$C" > "$ROOTDIR/Gearmulator Preview/$MACHINE/config/$MACHINE Editor.xml"
	fi
	[ -f "$SETTINGS" ] && cp -p "$SETTINGS" "$TMP/settings"
	restore() {
		# the audio callbacks' record (MDMM_JOURNEY_PERF=1) goes beside the page's log
		for f in "$ROOTDIR/Gearmulator Preview/$MACHINE/logs/"performance-*.jsonl; do
			[ -f "$f" ] && cp "$f" "$LOGDIR/" && echo "   audio: $LOGDIR/$(basename "$f")"
		done
		if [ "$HOST" = standalone ]; then
			if [ -f "$TMP/settings" ]; then cp -p "$TMP/settings" "$SETTINGS"; else rm -f "$SETTINGS"; fi
		fi
		rm -rf "$TMP"
	}
	trap 'kill $PID 2>/dev/null; restore; exit 1' INT TERM
	mkdir -p "$LOGDIR"; touch "$TMP/start"
	PID=""
	PERF=""; [ "${MDMM_JOURNEY_PERF:-0}" = 1 ] && PERF="GEARMULATOR_RT_INSTRUMENTATION=1"
	if [ "$HOST" = standalone ]; then
		if [ "$BACKGROUND" = 1 ]; then
			env $PERF GEARMULATOR_DATA_ROOT="$ROOTDIR" GEARMULATOR_MDMM_BACKGROUND=1 "$VAR=$SEL" "$EXE" >"$TMP/app.out" 2>&1 &
			PID=$!
		else
			env $PERF GEARMULATOR_DATA_ROOT="$ROOTDIR" "$VAR=$SEL" "$EXE" >"$TMP/app.out" 2>&1 &
			PID=$!
			# The page draws its canvases on animation frames and runs its timers at full rate only while its window
			# is visible (WebKit pauses a covered page's frames): the app is brought to the front once it is up. Do
			# not type into it while it runs. MDMM_JOURNEY_FRONT=0 leaves it where it opens; --background instead
			# keeps the page drawn behind other windows.
			if [ "${MDMM_JOURNEY_FRONT:-1}" = 1 ]; then
				( sleep 4; osascript -e "tell application id \"$BUNDLE\" to activate" >/dev/null 2>&1 ) &
			fi
		fi
	else
		STATE=""
		# the settings the standalone would start from: its own, else the ones it copies them from the first time
		SAVED="$SETTINGS"; [ -f "$SAVED" ] || SAVED="$HOME/Library/Application Support/Gearmulator $M.settings"
		if [ "${MDMM_VST3_STATE:-}" = standalone ] && [ -f "$SAVED" ]; then
			sed -n 's/.*<VALUE name="filterState" val="\([^"]*\)".*/\1/p' "$SAVED" > "$TMP/state.b64"
			[ -s "$TMP/state.b64" ] && STATE="--state $TMP/state.b64"
		fi
		env $PERF GEARMULATOR_DATA_ROOT="$ROOTDIR" GEARMULATOR_MDMM_BACKGROUND=1 "$VAR=$SEL" "$HOST_BIN" "$PLUGIN" $((TIMEOUT + 60)) --background $STATE >"$TMP/app.out" 2>&1 &
		PID=$!
	fi
	# the page's log is a new gearmulator-<skin>-<random>.log in the app's caches folder
	LOG=""; t=0; DONE=""
	while [ $t -lt "$TIMEOUT" ]; do
		sleep 2; t=$((t + 2))
		if [ -z "$LOG" ]; then LOG=$(find "$LOGDIR" -name "gearmulator-$SKIN-*.log" -newer "$TMP/start" 2>/dev/null | head -1); fi
		if [ -n "$LOG" ] && grep -q "JOURNEYS DONE" "$LOG" 2>/dev/null; then DONE=1; break; fi
		if ! kill -0 $PID 2>/dev/null; then break; fi
	done
	ALIVE=0; kill -0 $PID 2>/dev/null && ALIVE=1
	# the standalone is asked to quit (it saves its settings: restored below); the host has nothing to save
	if [ "$HOST" = standalone ]; then
		osascript -e "tell application id \"$BUNDLE\" to quit" >/dev/null 2>&1 || true
		for i in 1 2 3 4 5; do kill -0 $PID 2>/dev/null || break; sleep 1; done
	fi
	kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
	wait $PID 2>/dev/null
	if [ -n "$LOG" ]; then
		if [ "${MDMM_JOURNEY_VERBOSE:-0}" = 1 ]; then grep -E "JOURNEY" "$LOG" | sed 's/^[^J]*JOURNEY/JOURNEY/'
		else grep -E "JOURNEY [^ ]+ (PASS|FAIL|SKIP)|JOURNEYS (start|DONE)" "$LOG" | sed 's/^[^J]*JOURNEY/JOURNEY/'; fi
		echo "   log: $LOG"
	fi
	if [ -z "$DONE" ]; then
		if [ "$ALIVE" = 1 ]; then echo "FAIL timeout after ${TIMEOUT} s"; else echo "FAIL the app ended before JOURNEYS DONE:"; tail -5 "$TMP/app.out"; fi
		STATUS=1
	else
		grep -qE "JOURNEY [^ ]+ FAIL" "$LOG" && STATUS=1
		grep -q "JOURNEYS DONE 0/0" "$LOG" && { echo "FAIL no journey matched $SEL"; STATUS=1; }
	fi
	restore; trap - INT TERM
	return 0
}
for H in standalone vst3; do
	[ "$HOSTS" = "$H" ] || [ "$HOSTS" = both ] || continue
	[ "$WHICH" = md ] || [ "$WHICH" = both ] && run_one MD $H
	[ "$WHICH" = mm ] || [ "$WHICH" = both ] && run_one MM $H
done
exit $STATUS
