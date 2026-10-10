#!/bin/bash
# The gate's own helpers, tried on dry stages (doc/release/LOCAL-GATE.md, "Silence"): the mute and restore of the system
# output (against a stand-in for osascript with its own volume: your real output is never touched), and the audio guard
# (a silent file played by afplay; macOS plays nothing audible). No ROM, no build needed; about 30 seconds.
#
#   scripts/local-gate/selftest.sh
# shellcheck source-path=SCRIPTDIR
set -u -o pipefail

here="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${here}/../.." && pwd)"
GATE_DIR="${here}"
work="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-gate-selftest.XXXXXX")"
trap 'rm -rf "${work}"' EXIT
OUT="${work}/out"; GATE_HOME="${work}"
mkdir -p "${OUT}/logs" "${work}/bin"
: > "${OUT}/stages.tsv"
# shellcheck source=lib.sh
. "${GATE_DIR}/lib.sh"

failures=0
check() {	# <what> <expected> <actual>
	if [ "$2" = "$3" ]; then echo "  ok    $1"; else echo "  FAIL  $1: expected '$2', got '$3'"; failures=$((failures + 1)); fi
}

# --- a stand-in for osascript: a volume and a mute in a file; STUB_NOMUTE=1 is an output with no mute control
cat > "${work}/bin/osascript" <<'EOF'
#!/bin/bash
state="${STUB_STATE}"
[ -f "${state}" ] || printf 'volume=63\nmuted=false\n' > "${state}"
volume="$(sed -n 's/^volume=//p' "${state}")"; muted="$(sed -n 's/^muted=//p' "${state}")"
[ "${STUB_NOMUTE:-0}" = 1 ] && shown="missing value" || shown="${muted}"
case "$2" in
	'get volume settings') echo "output volume:${volume}, input volume:70, alert volume:100, output muted:${shown}" ;;
	'set volume output muted true') [ "${STUB_NOMUTE:-0}" = 1 ] || printf 'volume=%s\nmuted=true\n' "${volume}" > "${state}" ;;
	'set volume output muted false') [ "${STUB_NOMUTE:-0}" = 1 ] || printf 'volume=%s\nmuted=false\n' "${volume}" > "${state}" ;;
	'set volume output volume '*) printf 'volume=%s\nmuted=%s\n' "${2##* }" "${muted}" > "${state}" ;;
esac
EOF
chmod +x "${work}/bin/osascript"
export STUB_STATE="${work}/stub-volume.txt"
export PATH="${work}/bin:${PATH}"
GATE_AUDIO_STATE="${work}/audio-state.txt"
stub() { sed -n "s/^$1=//p" "${STUB_STATE}"; }

echo "== mute and restore (stand-in osascript)"
printf 'volume=63\nmuted=false\n' > "${STUB_STATE}"
output_mute "T1" > /dev/null
check "muted during the stage" "true,63" "$(stub muted),$(stub volume)"
check "the state is on disk" 1 "$([ -f "${GATE_AUDIO_STATE}" ] && echo 1 || echo 0)"
check "the note names the stage" "output muted for stage T1 (was muted false, volume 63)" "${MUTE_NOTE}"
output_restore > /dev/null
check "unmuted again, volume untouched" "false,63" "$(stub muted),$(stub volume)"
check "the state file is gone" 0 "$([ -f "${GATE_AUDIO_STATE}" ] && echo 1 || echo 0)"
output_restore > /dev/null
check "restoring twice changes nothing" "false,63" "$(stub muted),$(stub volume)"

printf 'volume=41\nmuted=true\n' > "${STUB_STATE}"
output_mute "T2" > /dev/null
output_restore > /dev/null
check "an output that was muted stays muted" "true,41" "$(stub muted),$(stub volume)"

printf 'volume=63\nmuted=false\n' > "${STUB_STATE}"
STUB_NOMUTE=1 output_mute "T3" > /dev/null
check "no mute control: turned down to 0" "0" "$(stub volume)"
check "and says so" "yes" "$([[ "${MUTE_NOTE}" == *"no mute control"* ]] && echo yes || echo no)"
STUB_NOMUTE=1 output_restore > /dev/null
check "no mute control: the volume is back" "63" "$(stub volume)"

printf 'volume=63\nmuted=false\n' > "${STUB_STATE}"
printf 'owner=999999\nmuted=false\nvolume=63\nchanged_volume=0\n' > "${GATE_AUDIO_STATE}"
printf 'volume=63\nmuted=true\n' > "${STUB_STATE}"
output_mute "T4" > /dev/null
output_restore > /dev/null
check "a dead gate's muted output is put right first" "false,63" "$(stub muted),$(stub volume)"

printf 'volume=63\nmuted=false\n' > "${STUB_STATE}"
printf 'owner=%s\nmuted=false\nvolume=63\nchanged_volume=0\n' "$$" > "${GATE_AUDIO_STATE}"
if output_mute "T5" > /dev/null; then check "a live gate's mute is left alone" refused muted; else check "a live gate's mute is left alone" refused refused; fi
rm -f "${GATE_AUDIO_STATE}"

echo "== restore on a trap (a gate killed with TERM)"
printf 'volume=63\nmuted=false\n' > "${STUB_STATE}"
bash -c '. "$1/silence.sh"; GATE_AUDIO_STATE="$2"; trap "output_restore > /dev/null" EXIT; trap "exit 143" TERM
	output_mute T6 > /dev/null; kill -TERM $$' _ "${GATE_DIR}" "${work}/audio-state.txt" > /dev/null 2>&1
check "unmuted by the EXIT trap after TERM" "false,63" "$(stub muted),$(stub volume)"

echo "== the audio guard (afplay of a silent file)"
if [ "$(uname -s)" != Darwin ] || ! command -v afplay > /dev/null 2>&1 || ! command -v pmset > /dev/null 2>&1; then
	echo "  skipped: needs macOS (afplay, pmset)"
else
	python3 -B - "${work}/silence.wav" <<'EOF'
import sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b"\x00\x00" * 2 * 44100 * 4); w.close()
EOF
	: > "${OUT}/stages.tsv"
	stage_begin T7 "dry stage that opens an audio device" > /dev/null
	afplay "${work}/silence.wav"
	stage_end PASS "played a silent file" > /dev/null
	check "a headless stage that opens a device fails" FAIL "$(tail -n 1 "${OUT}/stages.tsv" | cut -f3)"
	check "and names the process" yes "$(tail -n 1 "${OUT}/stages.tsv" | grep -q 'afplay' && echo yes || echo no)"
	stage_begin T8 "dry stage that stays quiet" > /dev/null
	sleep 3
	stage_end PASS "did nothing" > /dev/null
	check "a stage with no device passes" PASS "$(tail -n 1 "${OUT}/stages.tsv" | cut -f3)"
	stage_begin 7d "dry device stage" > /dev/null
	afplay "${work}/silence.wav"
	stage_end PASS "played a silent file" > /dev/null
	check "the journeys' stage may hold a device" PASS "$(tail -n 1 "${OUT}/stages.tsv" | cut -f3)"
fi

[ "${failures}" = 0 ] || { echo "-- stages.tsv:"; cut -f1,3,6 "${OUT}/stages.tsv"; }
echo
if [ "${failures}" = 0 ]; then echo "selftest: PASS"; else echo "selftest: FAIL (${failures})"; fi
exit $((failures > 0))
