# shellcheck shell=bash
# shellcheck source-path=SCRIPTDIR
# Helpers of scripts/mdmm-local-gate.sh (doc/release/LOCAL-GATE.md): stage bookkeeping, sandboxes, logged runs.
# Sourced, never run. bash 3.2 (macOS /bin/bash): no associative arrays, no mapfile, no wait -n.
# The caller sets ROOT, OUT, GATE_DIR, MD_ROM and MM_ROM first.

GATE_TOOLS="${GATE_DIR}/gate_tools.py"
# shellcheck source=silence.sh
. "${GATE_DIR}/silence.sh"

gate_py() { python3 -B "${GATE_TOOLS}" "$@"; }

# Every long command runs in the background and is waited for: bash runs a trap (Ctrl-C, kill) only when a foreground
# command ends, but at once when it is in `wait`. kill_descendants then ends what the gate started.
gate_run() {	# gate_tools.py run <args>: a time limit, a log, NAME=value words for the environment
	python3 -B "${GATE_TOOLS}" run "$@" &
	wait $!
}

kill_descendants() {	# <pid>: its children, their children, ... (TERM; the runner ends its command's whole group)
	local pid
	for pid in $(pgrep -P "$1" 2> /dev/null); do
		kill_descendants "${pid}"
		kill -TERM "${pid}" 2> /dev/null
	done
	return 0
}

# --- stage bookkeeping ---------------------------------------------------------------------------------
# stages.tsv: id, name, result (PASS, FAIL, SKIP, PENDING), seconds, numbers, notes. One line each on stdout.
STAGE_ID=""; STAGE_NAME=""; STAGE_START=0; STAGE_RESULT=""

# The audio guard: below the gate, only the stages in DEVICE_STAGES may hold an audio stream (and those run silenced, see
# silence.sh). Every other stage is watched by a poller of coreaudiod's assertions (pmset -g assertions names the process
# an open stream was made for), and fails if a process it started holds one.
DEVICE_STAGES=" 7d "
GUARD_PID=""; GUARD_FILE=""; GUARD_OPENED=""

guard_start() {
	[ -z "${GUARD_PID}" ] || return 0
	GUARD_FILE="${OUT}/logs/audio-guard-${STAGE_ID}.txt"
	: > "${GUARD_FILE}"
	python3 -B "${GATE_TOOLS}" audio-watch --root-pid $$ --interval 2 --out "${GUARD_FILE}" > /dev/null 2>&1 &
	GUARD_PID=$!
	disown "${GUARD_PID}" 2> /dev/null	# a bare `wait` of the gate must not wait for it
}

guard_stop() {	# GUARD_OPENED: what the stage held open, if anything (a global: no command substitution, it must stop the poller here)
	GUARD_OPENED=""
	[ -n "${GUARD_PID}" ] || return 0
	kill -TERM "${GUARD_PID}" 2> /dev/null
	local waited=0
	while kill -0 "${GUARD_PID}" 2> /dev/null && [ "${waited}" -lt 50 ]; do sleep 0.1; waited=$((waited + 1)); done
	GUARD_PID=""
	if [ -s "${GUARD_FILE}" ]; then GUARD_OPENED="$(tr '\n' ';' < "${GUARD_FILE}" | sed 's/;$//')"; else rm -f "${GUARD_FILE}"; fi
	return 0
}

scrub() { printf '%s' "$1" | tr '\t\n' '  '; }

stage_begin() {	# <id> <name>
	STAGE_ID="$1"; STAGE_NAME="$2"; STAGE_START=${SECONDS}
	printf '[%s] %s\n' "$1" "$2"
	[[ "${DEVICE_STAGES}" == *" $1 "* ]] || guard_start
}

# something when the Mac is busy: the timing checks (and the firmware tests that time) fail from load alone
busy_note() {
	local load cores
	load="$(sysctl -n vm.loadavg 2> /dev/null | awk '{print $2}')"
	cores="$(sysctl -n hw.ncpu 2> /dev/null)"
	awk -v l="${load:-0}" -v c="${cores:-1}" '
		BEGIN {
			if (l > c / 2)
				printf "the Mac is busy (load %.0f on %d cores): the timing checks and some firmware tests fail " \
					"from load alone, run again when it is quiet", l, c
		}'
}

stage_end() {	# <PASS|FAIL|SKIP|PENDING|MANUAL> <numbers> [notes]
	local result="$1" numbers="${2:-}" notes="${3:-}" took=$((SECONDS - STAGE_START)) busy opened
	guard_stop
	opened="${GUARD_OPENED}"
	if [ -n "${opened}" ]; then
		# a headless stage opened an audio device: it could have made a sound
		result=FAIL
		notes="${notes:+${notes}; }OPENED AN AUDIO DEVICE (${opened})"
	fi
	if [ "${result}" = FAIL ]; then
		busy="$(busy_note)"
		[ -n "${busy}" ] && notes="${notes:+${notes}; }${busy}"
	fi
	# shellcheck disable=SC2034  # read by scripts/mdmm-local-gate.sh
	STAGE_RESULT="${result}"
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${STAGE_ID}" "${STAGE_NAME}" "${result}" "${took}" \
		"$(scrub "${numbers}")" "$(scrub "${notes}")" >> "${OUT}/stages.tsv"
	printf '    %-7s %s%s%s (%s s)\n' "${result}" "${numbers}" "${numbers:+${notes:+; }}" "${notes}" "${took}"
}

stage_skip() {	# <id> <name> <reason>: a stage left out on purpose, with the reason
	stage_begin "$1" "$2"
	stage_end SKIP "" "$3"
}

cannot_run() {	# <test> <reason>: named in the summary, not counted as skipped
	printf '%s\t%s\n' "$1" "$(scrub "$2")" >> "${OUT}/cannot-run.txt"
}

show_tail() {	# <log> [lines]: the end of a log, indented, to see why a stage failed
	[ -f "$1" ] && tail -n "${2:-12}" "$1" | sed 's/^/        | /'
	return 0
}

key() { sed -n "s/^$1=//p" | head -n 1; }	# a `numbers=` or `notes=` line out of a gate_tools answer

# --- inputs --------------------------------------------------------------------------------------------
first_file() {	# <dir>: the first file in it, or nothing
	local f
	for f in "$1"/*; do
		[ -f "${f}" ] && { printf '%s' "${f}"; return 0; }
	done
	return 0
}

label() { if [ "$1" = md ]; then printf MD; else printf MM; fi; }

rom_for() {	# md|mm -> the ROM path
	if [ "$1" = md ]; then printf '%s' "${MD_ROM}"; else printf '%s' "${MM_ROM}"; fi
}

have_roms() {	# md|mm|both: succeeds when the ROMs asked for are files; says which is not
	local ok=0
	MISSING_WHAT=""
	if [ "$1" != mm ] && [ ! -f "${MD_ROM}" ]; then
		MISSING_WHAT="no Machinedrum ROM (GEARMULATOR_MD_FIRMWARE_BIN, or a file in ${PREVIEW}/Machinedrum/roms)"
		ok=1
	fi
	if [ "$1" != md ] && [ ! -f "${MM_ROM}" ]; then
		MISSING_WHAT="${MISSING_WHAT:+${MISSING_WHAT}; }no Monomachine ROM (GEARMULATOR_MM_FIRMWARE_BIN,"
		MISSING_WHAT+=" or a file in ${PREVIEW}/Monomachine/roms)"
		ok=1
	fi
	return "${ok}"
}

# --- tools of the build --------------------------------------------------------------------------------
find_tool() {	# <name>: the built executable of that name, or nothing
	find "${BUILD}" -type f -name "$1" -perm -u+x ! -path '*/CMakeFiles/*' 2>/dev/null | head -n 1
}

# --- sandboxes -----------------------------------------------------------------------------------------
# A firmware run never sees the owner's folders: HOME, the macOS user home (CFFIXED_USER_HOME: JUCE follows that
# one, HOME it does not) and GEARMULATOR_DATA_ROOT are under the run folder; the ROMs are links to the owner's
# files (read only). The working folder is inside too: the loader also scans the current folder for .bin files.
SB_HOME=""; SB_DATA=""; SB_WORK=""

sandbox_new() {	# <name> [norom]: with "norom" the ROM folders stay empty
	SB_HOME="${OUT}/sandbox/$1"
	SB_DATA="${SB_HOME}/Documents"
	SB_WORK="${SB_HOME}/work"
	rm -rf "${SB_HOME}"
	mkdir -p "${SB_DATA}/Gearmulator Preview/Machinedrum/roms" "${SB_DATA}/Gearmulator Preview/Monomachine/roms" \
		"${SB_WORK}"
	if [ "${2:-}" != norom ]; then
		[ -f "${MD_ROM}" ] \
			&& ln -s "${MD_ROM}" "${SB_DATA}/Gearmulator Preview/Machinedrum/roms/$(basename "${MD_ROM}")"
		[ -f "${MM_ROM}" ] \
			&& ln -s "${MM_ROM}" "${SB_DATA}/Gearmulator Preview/Monomachine/roms/$(basename "${MM_ROM}")"
	fi
	return 0
}

# run_box <seconds> <log> <command...>: in the current sandbox, with every firmware test made strict (a skip is
# a failure), output to <log>. Leading NAME=value words of the command are environment settings. Status as the
# command's; 124 when the time limit killed it.
run_box() {
	local limit="$1" log="$2" fixtures=()
	shift 2
	# syxImportFileTest reads one backup of each machine from MD_SYX / MM_SYX, and skips without them.
	[ -n "${FIXTURE_MD:-}" ] && fixtures+=(MD_SYX="${FIXTURE_MD}")
	[ -n "${FIXTURE_MM:-}" ] && fixtures+=(MM_SYX="${FIXTURE_MM}")
	env HOME="${SB_HOME}" CFFIXED_USER_HOME="${SB_HOME}" GEARMULATOR_DATA_ROOT="${SB_DATA}" \
		GEARMULATOR_MD_FIRMWARE_BIN="${MD_ROM}" GEARMULATOR_MM_FIRMWARE_BIN="${MM_ROM}" \
		GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1 MD_AUTOMATION_REQUIRE_FIRMWARE=1 ${fixtures[@]+"${fixtures[@]}"} \
		python3 -B "${GATE_TOOLS}" run --timeout "${limit}" --log "${log}" --cwd "${SB_WORK}" \
		--unset GEARMULATOR_MDMM_SPEEDUPS --unset GEARMULATOR_MDMM_SIM_DEFERRAL --unset GEARMULATOR_MDMM_EXACT_ESSI -- "$@" &
	wait $!
}

# run_plain <seconds> <log> <command...>: the owner's own environment (scripts that sandbox themselves).
run_plain() {
	local limit="$1" log="$2"
	shift 2
	env GEARMULATOR_MD_FIRMWARE_BIN="${MD_ROM}" GEARMULATOR_MM_FIRMWARE_BIN="${MM_ROM}" \
		python3 -B "${GATE_TOOLS}" run --timeout "${limit}" --log "${log}" -- "$@" &
	wait $!
}

last_line() { grep -v '^[[:space:]]*$' "$1" 2>/dev/null | tail -n 1; }
