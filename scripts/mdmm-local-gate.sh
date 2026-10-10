#!/bin/bash
# The local release gate (doc/release/LOCAL-GATE.md): one command that says red or green for a release candidate on
# this Mac, with the firmware that GitHub CI never has. Build as shipped, every registered test, ROM loading,
# SysEx round trips, the playing goldens, timing, then the plug-in checks and the user journeys. It builds on the
# repo's own scripts (mdmm-rt-check.sh, mdmm-pluginval.sh, mdmm-journeys.sh, mdmm-dev.sh,
# macos/check_mdmm_core_capacity.py) and adds only the glue: sandboxes, a time limit per run, the summary.
#
#   scripts/mdmm-local-gate.sh [--quick] [--skip-plugin] [--soak] [--skip-journeys] [--record-goldens]
#                              [--build <dir>] [--out <dir>]
#
#   --quick            no plug-ins: configure with JUCE off and run what needs no window or bundle (stages 1 to 6a).
#                      Not a release gate: the verdict says so
#   --skip-plugin      build everything, but leave out the core-capacity run, the soak and stage 7 (the plug-in checks)
#   --soak             also run the soak (ten minutes of busy play per machine, 6c: 21 minutes). Off by default since
#                      0.4.0: run it when a report points at drop-outs over time (doc/release/CI.md). --skip-soak: the default
#   --skip-journeys    leave out only the user journeys (8 to 16 minutes, in the background of the Mac)
#   --record-goldens   stage 5 records the goldens (mdmmPerfGateTest --record) instead of comparing; the new numbers
#                      are then compared once more, and the summary says the goldens changed and need sign-off
#   --build <dir>      the build tree (default temp/local-gate/build; --quick: temp/local-gate/build-quick)
#   --out <dir>        the run folder: logs, sandboxes, summary.md (default temp/local-gate/<date-time>)
#
# The Mac stays silent: no stage but 7d (the standalones of the journeys) opens an audio device, an audio guard fails
# any other that does, and 7d runs with the standalone's output zeroed (--background) and the system output muted,
# restored afterwards and on any exit (doc/release/LOCAL-GATE.md, Silence).
#
# Inputs, all overridable: GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN (default: the first file in
# <preview>/<machine>/roms), MDMM_GATE_PREVIEW (default ~/Documents/Gearmulator Preview), MDMM_GATE_MD_CACHE (the
# Machinedrum factory cache), MDMM_GATE_MM_PATCH (the Monomachine factory patch RAM), MDMM_GATE_FIXTURES (the SysEx
# fixtures, default <preview>/fixtures/sysex), MDMM_GATE_JOBS (build jobs), MDMM_GATE_GOLDEN_JOBS (golden runs at
# once), MDMM_GATE_JOURNEY_ARGS (options for mdmm-journeys.sh, default --host both --background),
# MDMM_GATE_COMPONENTS_DIR (where the AU bundles are installed), MDMM_GATE_SOAK_SECONDS (6c, default 600),
# MDMM_GATE_SOAK_WARMUP (seconds not judged, default 30), MDMM_GATE_SOAK_LOCK_US (a synth lock wait that fails it,
# default 1000), MDMM_GATE_ALLOW_UNMUTED=1 (run 7d although the output cannot be muted), MDMM_GATE_SCENARIOS (extra
# golden scenarios, for --record-goldens). For working on the gate itself: MDMM_GATE_SKIP_BUILD=1 (use the build tree
# as it is), MDMM_GATE_ONLY="3 5" (only those stages) and MDMM_GATE_GOLDENS (another goldens file); the summary calls
# such a run partial.
# Exit status: 0 green (also partial green and recorded goldens), 1 red, 2 bad usage.

# shellcheck source-path=SCRIPTDIR
set -u -o pipefail

here="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${here}/.." && pwd)"
GATE_DIR="${here}/local-gate"
# shellcheck source=mdmm-product.env
. "${here}/mdmm-product.env"

usage() {
	awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "${here}/mdmm-local-gate.sh" >&2
	exit "${1:-2}"
}

QUICK=0; SKIP_PLUGIN=0; SKIP_SOAK=1; SKIP_JOURNEYS=0; RECORD=0; BUILD=""; OUT=""
while [ $# -gt 0 ]; do
	case "$1" in
		--quick) QUICK=1; shift ;;
		--skip-plugin) SKIP_PLUGIN=1; shift ;;
		--skip-soak) SKIP_SOAK=1; shift ;;
		--soak) SKIP_SOAK=0; shift ;;
		--skip-journeys) SKIP_JOURNEYS=1; shift ;;
		--record-goldens) RECORD=1; shift ;;
		--build) [ $# -ge 2 ] || usage; BUILD="$2"; shift 2 ;;
		--out) [ $# -ge 2 ] || usage; OUT="$2"; shift 2 ;;
		-h|--help) usage 0 ;;
		*) echo "unknown option: $1" >&2; usage ;;
	esac
done

absolute() { mkdir -p "$1" && (cd "$1" && pwd); }
STARTED="$(date '+%Y-%m-%d %H:%M:%S %z')"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
GATE_HOME="${ROOT}/temp/local-gate"
[ -n "${OUT}" ] || OUT="${GATE_HOME}/${TIMESTAMP}"
OUT="$(absolute "${OUT}")"
if [ -z "${BUILD}" ]; then
	if [ "${QUICK}" = 1 ]; then BUILD="${GATE_HOME}/build-quick"; else BUILD="${GATE_HOME}/build"; fi
fi
BUILD="$(absolute "${BUILD}")"
DIAG_BUILD="${GATE_HOME}/build-diag"
mkdir -p "${GATE_HOME}" "${OUT}/logs"
: > "${OUT}/stages.tsv"; : > "${OUT}/info.txt"; : > "${OUT}/goldens.tsv"; : > "${OUT}/cannot-run.txt"
: > "${OUT}/fingerprints.txt"
ln -sfn "${OUT}" "${GATE_HOME}/latest" 2>/dev/null || true

# shellcheck source=local-gate/lib.sh
. "${GATE_DIR}/lib.sh"

PREVIEW="${MDMM_GATE_PREVIEW:-${HOME}/Documents/Gearmulator Preview}"
MD_ROM="${GEARMULATOR_MD_FIRMWARE_BIN:-$(first_file "${PREVIEW}/Machinedrum/roms")}"
MM_ROM="${GEARMULATOR_MM_FIRMWARE_BIN:-$(first_file "${PREVIEW}/Monomachine/roms")}"
MD_CACHE="${MDMM_GATE_MD_CACHE:-${PREVIEW}/Machinedrum/nvram/md-uw-1.63-factory-v2.cache}"
MM_PATCH="${MDMM_GATE_MM_PATCH:-${PREVIEW}/Monomachine/nvram/mm-factory-live3-be.bin}"
FIXTURES="${MDMM_GATE_FIXTURES:-${PREVIEW}/fixtures/sysex}"
# the override is for testing the gate
GOLDENS="${MDMM_GATE_GOLDENS:-${ROOT}/source/elektron/md/mdLibTest/goldens/mdmm-goldens.json}"
GATE_MIN_SCENARIOS="md-busy md-factory md-song mm-a01 mm-busy mm-song"	# a goldens file must cover at least these
JOBS="${MDMM_GATE_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
# --background: the standalone's output is zeroed (silence)
JOURNEY_ARGS="${MDMM_GATE_JOURNEY_ARGS:---host both --background}"
SOAK_SECONDS="${MDMM_GATE_SOAK_SECONDS:-600}"
SOAK_WARMUP="${MDMM_GATE_SOAK_WARMUP:-30}"
SOAK_LOCK_US="${MDMM_GATE_SOAK_LOCK_US:-1000}"
COMPONENTS="${MDMM_GATE_COMPONENTS_DIR:-${HOME}/Library/Audio/Plug-Ins/Components}"
AU_SWAP_DIR="${GATE_HOME}/au-swap"
ARCH="$(uname -m)"
PRODUCTS="${BUILD}/products/Release"
MISSING_WHAT=""
# ctest runs everything but these (named in the summary): the two firmware tests that need arguments or more time
# than their ctest entry allows run in stage 2b; the AU zip validations need a CPack package (auval runs in 7b);
# test_*_VST are the VST2 plug-in tests (VST2 is off).
CTEST_EXCLUDE='^mmDeskFirmwareTest$|^mdSysexLifecycleTest$|_AU_Validate$|^test_.*_VST$'
# NAME=reason: a ctest skip that is not red because the thing it needs cannot exist here. None known.
TOLERATED_SKIPS=()
# The tools stages 3 to 5 need. A target the tree does not define yet makes that stage PENDING, which is red.
ROM_LOAD_TOOL="mdmmRomLoadTest"; SYSEX_TOOL="mdmmSysexRoundTripTest"; PERF_TOOL="mdmmPerfGateTest"

# seams for testing the gate itself (stand-ins in a test, never set otherwise)
PLUGINVAL_CMD="${MDMM_GATE_PLUGINVAL:-${here}/mdmm-pluginval.sh}"
DEV_CMD="${MDMM_GATE_DEV:-${here}/mdmm-dev.sh}"
JOURNEYS_CMD="${MDMM_GATE_JOURNEYS:-${here}/mdmm-journeys.sh}"

output_restore_stale || true	# the output of a gate that was killed with it muted comes back before anything else

info_set() { printf '%s=%s\n' "$1" "$2" >> "${OUT}/info.txt"; }
cache_value() { sed -n "s/^$1=//p" "${BUILD}/CMakeCache.txt" 2> /dev/null | head -n 1; }

# --- the AU bundles: macOS finds an AU in its registry, not by path ------------------------------------------
AU_INSTALLED=(); AU_ASIDE=(); AU_ACTIVE=0; AU_PROBLEM=""

au_swap_in() {	# put this build's AU bundles where macOS looks, the owner's own set aside until au_swap_out
	local name built installed
	if [ -d "${AU_SWAP_DIR}" ] && [ -n "$(ls -A "${AU_SWAP_DIR}" 2>/dev/null)" ]; then
		AU_PROBLEM="${AU_SWAP_DIR} still holds components an earlier run set aside (it was killed): move them back"
		AU_PROBLEM+=" into ${COMPONENTS} by hand, then run again"
		return 1
	fi
	mkdir -p "${COMPONENTS}" "${AU_SWAP_DIR}"
	AU_ACTIVE=1
	for name in "${MDMM_PRODUCT_NAME_MD}" "${MDMM_PRODUCT_NAME_MM}"; do
		built="${PRODUCTS}/AU/${name}.component"
		installed="${COMPONENTS}/${name}.component"
		[ -d "${built}" ] || { AU_PROBLEM="not built: ${built}"; return 1; }
		if [ -e "${installed}" ] || [ -L "${installed}" ]; then
			# the same files already: nothing to swap, and nothing to remove afterwards
			if [ -d "${installed}" ] && diff -rq "${built}" "${installed}" > /dev/null 2>&1; then continue; fi
			mv "${installed}" "${AU_SWAP_DIR}/" || { AU_PROBLEM="cannot set ${installed} aside"; return 1; }
			AU_ASIDE+=("${name}.component")
		fi
		ditto "${built}" "${installed}" || { AU_PROBLEM="cannot install ${built}"; return 1; }
		AU_INSTALLED+=("${installed}")
	done
	killall -9 AudioComponentRegistrar 2> /dev/null
	sleep 2
	return 0
}

au_swap_out() {	# remove what au_swap_in installed, put the owner's own back. Safe to call twice
	[ "${AU_ACTIVE}" = 1 ] || return 0
	local item
	for item in ${AU_INSTALLED[@]+"${AU_INSTALLED[@]}"}; do rm -rf "${item}"; done
	for item in ${AU_ASIDE[@]+"${AU_ASIDE[@]}"}; do
		mv "${AU_SWAP_DIR}/${item}" "${COMPONENTS}/${item}" \
			|| echo "!!! could not put ${item} back: it is in ${AU_SWAP_DIR}" >&2
	done
	rmdir "${AU_SWAP_DIR}" 2> /dev/null
	AU_ACTIVE=0; AU_INSTALLED=(); AU_ASIDE=()
	killall -9 AudioComponentRegistrar 2> /dev/null
	return 0
}

# --- the end of a run, on any road -------------------------------------------------------------------------------
FINISHED=0
write_summary() {
	info_set total "$((SECONDS / 60)) min $((SECONDS % 60)) s"
	gate_py summary --out "${OUT}"
}
# shellcheck disable=SC2329  # runs from the EXIT trap
on_exit() {
	local status=$?
	trap - EXIT
	kill_descendants $$
	au_swap_out
	output_restore
	if [ "${FINISHED}" != 1 ]; then
		info_set aborted 1
		echo "!!! the gate stopped before its end (status ${status})" >&2
		write_summary
		exit 1
	fi
	exit "${status}"
}
trap on_exit EXIT
trap 'kill_descendants $$; exit 130' INT
trap 'kill_descendants $$; exit 143' TERM

# =====================================================================================================================
# 0. inputs
# =====================================================================================================================
MD_FIXTURES=(); MM_FIXTURES=(); FIXTURE_MD=""; FIXTURE_MM=""
stage_inputs() {
	stage_begin 0 "Inputs"
	local problems="" numbers="" f
	have_roms both || problems="${MISSING_WHAT}"
	for f in "${MD_ROM}" "${MM_ROM}"; do
		[ -f "${f}" ] || continue
		[ "$(stat -f %z "${f}")" = 8388608 ] || problems="${problems:+${problems}; }${f} is not an 8 MiB image"
	done
	[ -f "${MD_ROM}" ] && info_set md_rom "${MD_ROM}" \
		&& info_set md_rom_sha256 "$(shasum -a 256 "${MD_ROM}" | awk '{print $1}')"
	[ -f "${MM_ROM}" ] && info_set mm_rom "${MM_ROM}" \
		&& info_set mm_rom_sha256 "$(shasum -a 256 "${MM_ROM}" | awk '{print $1}')"
	while IFS= read -r f; do MD_FIXTURES+=("${f}"); done \
		< <(find "${FIXTURES}" -type f -iname 'md*.syx' 2> /dev/null | sort)
	while IFS= read -r f; do MM_FIXTURES+=("${f}"); done \
		< <(find "${FIXTURES}" -type f -iname 'mm*.syx' 2> /dev/null | sort)
	[ ${#MD_FIXTURES[@]} -gt 0 ] && FIXTURE_MD="${MD_FIXTURES[0]}"
	[ ${#MM_FIXTURES[@]} -gt 0 ] && FIXTURE_MM="${MM_FIXTURES[0]}"
	if [ ${#MD_FIXTURES[@]} -eq 0 ] || [ ${#MM_FIXTURES[@]} -eq 0 ]; then
		problems="${problems:+${problems}; }SysEx fixtures missing in ${FIXTURES}"
		problems+=" (md*.syx: ${#MD_FIXTURES[@]}, mm*.syx: ${#MM_FIXTURES[@]})"
	fi
	numbers="ROMs $(basename "${MD_ROM:-none}"), $(basename "${MM_ROM:-none}");"
	numbers+=" ${#MD_FIXTURES[@]} md and ${#MM_FIXTURES[@]} mm SysEx fixtures;"
	numbers+=" load $(sysctl -n vm.loadavg 2> /dev/null | awk '{print $2}') on $(sysctl -n hw.ncpu 2> /dev/null) cores"
	if [ -n "${problems}" ]; then
		stage_end FAIL "${numbers}" "${problems}"
	else
		stage_end PASS "${numbers}" "$(busy_note)"
	fi
}

# =====================================================================================================================
# 1. build as shipped
# =====================================================================================================================
LOG_DIR="${OUT}/logs"
stage_build() {
	if [ "${MDMM_GATE_SKIP_BUILD:-0}" = 1 ] && [ -f "${BUILD}/CMakeCache.txt" ]; then
		stage_skip 1 "Build as shipped" "MDMM_GATE_SKIP_BUILD=1: ${BUILD} used as it is, NOT rebuilt"
		return
	fi
	stage_begin 1 "Build as shipped"
	local log="${LOG_DIR}/1-build.log" tool answer flags=() home="${BUILD}/build-runtime-home"
	for tool in cmake ninja xcrun python3; do
		command -v "${tool}" > /dev/null 2>&1 || { stage_end FAIL "" "${tool} not found"; return; }
	done
	SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
	export SDKROOT
	# The configure of scripts/macos/build_mdmm.sh, but for this Mac's architecture and without its signing,
	# packaging and receipt steps (it insists on all of them); scripts/mdmm-dev.sh's SDK pin and Ninja.
	flags=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="${ARCH}" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
		-DCMAKE_OSX_SYSROOT="${SDKROOT}" -DXCODE_VERSION="${XCODE_VERSION:-16}"
		-DGEARMULATOR_MDMM_APPLE_THINLTO=ON -DGEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON
		-DGEARMULATOR_MDMM_APPLE_PGO_MODE=none -DGEARMULATOR_MDMM_APPLE_PGO_PROFILE=
		-DGEARMULATOR_JUCE_PRODUCTS_ROOT="${BUILD}/products" -DBUILD_TESTING=ON)
	if [ "${QUICK}" = 1 ]; then
		flags+=(-Dgearmulator_BUILD_JUCEPLUGIN=OFF)
	else
		flags+=(-Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON
			-Dgearmulator_BUILD_JUCEPLUGIN_AU=ON -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON)
	fi
	# JUCE runs each VST3 while it builds it: that load gets a data root inside the build tree (no ROM in it).
	mkdir -p "${home}/Documents"
	gate_run --timeout 900 --log "${log}" -- cmake -S "${ROOT}" -B "${BUILD}" -G Ninja "${flags[@]}" \
		|| { stage_end FAIL "" "configure failed (${log})"; show_tail "${log}"; return; }
	gate_run --append --timeout 14400 --log "${log}" -- HOME="${home}" GEARMULATOR_DATA_ROOT="${home}/Documents" \
		cmake --build "${BUILD}" --parallel "${JOBS}" -- -k 0 \
		|| { stage_end FAIL "" "build failed (${log})"; show_tail "${log}" 25; return; }
	if [ "${QUICK}" != 1 ]; then
		# Targets that are not part of "all" but have tests or are used below (the list of scripts/macos/build_mdmm.sh).
		gate_run --append --timeout 7200 --log "${log}" -- HOME="${home}" GEARMULATOR_DATA_ROOT="${home}/Documents" \
			cmake --build "${BUILD}" --parallel "${JOBS}" --target \
			mdJucePlugin_VST3 mmJucePlugin_VST3 mdJucePlugin_AU mmJucePlugin_AU mdJucePlugin_Standalone \
			mmJucePlugin_Standalone \
			pluginTester latency_host vst3ProgramChangeTest mdProgramChangeProbe_VST3 mdAudioProbePlugin_VST3 \
			|| { stage_end FAIL "" "the plug-in and host targets failed (${log})"; show_tail "${log}" 25; return; }
	fi
	# Every registered test must have its program: ctest would only fail a missing one at run time, or run a stale one.
	local registered missing=0
	answer="$(ctest --test-dir "${BUILD}" -C Release -N --show-only=json-v1 -E "${CTEST_EXCLUDE}" |
		gate_py ctest-missing)" || missing=1
	registered="$(printf '%s\n' "${answer}" | key numbers)"
	if [ "${missing}" = 1 ]; then
		stage_end FAIL "${registered}" "$(printf '%s\n' "${answer}" | key notes)"
		return
	fi
	if [ "${QUICK}" != 1 ]; then
		# The cache read back: ThinLTO, the DSP optimisation and the architecture are really on (the release script's
		# check).
		if ! python3 -B "${ROOT}/scripts/macos/write_mdmm_receipt.py" --source "${ROOT}" \
			--validate-build-optimization "${BUILD}/CMakeCache.txt" --expected-architecture "${ARCH}" \
			--expected-products-root "${BUILD}/products" > "${LOG_DIR}/1-optimization.json" 2>> "${log}"; then
			stage_end FAIL "${registered}" \
				"the build is not the optimised one (see ${log}, ${LOG_DIR}/1-optimization.json)"
			show_tail "${log}" 8
			return
		fi
	fi
	local built
	built="$(cache_value CMAKE_BUILD_TYPE:STRING) ${ARCH}, ThinLTO $(cache_value GEARMULATOR_MDMM_APPLE_THINLTO:BOOL)"
	built+=", DSP optimisation $(cache_value GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP:BOOL)"
	if [ "${QUICK}" = 1 ]; then built+=", NO plug-ins (--quick)"; fi
	stage_end PASS "${built}; ${registered}"
}

# =====================================================================================================================
# 2. every registered test; 2b the firmware tests ctest cannot run
# =====================================================================================================================
stage_ctest() {
	stage_begin 2 "ctest, every registered test"
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	sandbox_new ctest
	local junit="${OUT}/ctest-junit.xml" log="${LOG_DIR}/2-ctest.log" name answer status=0 args=() item
	for name in $(ctest --test-dir "${BUILD}" -C Release -N -R "${CTEST_EXCLUDE}" |
		sed -n 's/^ *Test *#[0-9]*: *//p'); do
		case "${name}" in
			mmDeskFirmwareTest|mdSysexLifecycleTest) ;;	# run directly in 2b
			*_AU_Validate)
				cannot_run "${name}" \
					"zip-package AU validation (needs the CPack zip); auval runs on the built AU in 7b" ;;
			*) cannot_run "${name}" "VST2 plug-in test; the VST2 build is off" ;;
		esac
	done
	rm -f "${junit}"
	run_box 10800 "${log}" ctest --test-dir "${BUILD}" -C Release -j4 --output-on-failure --no-tests=error \
		--timeout 900 --output-junit "${junit}" -E "${CTEST_EXCLUDE}" || status=$?
	for item in ${TOLERATED_SKIPS[@]+"${TOLERATED_SKIPS[@]}"}; do args+=(--tolerate "${item}"); done
	local judged=0
	answer="$(gate_py junit "${junit}" ${args[@]+"${args[@]}"})" || judged=1
	if [ "${status}" = 0 ] && [ "${judged}" = 0 ]; then
		stage_end PASS "$(printf '%s\n' "${answer}" | key numbers)" "$(printf '%s\n' "${answer}" | key notes)"
	else
		stage_end FAIL "$(printf '%s\n' "${answer}" | key numbers)" \
			"ctest exit ${status}; $(printf '%s\n' "${answer}" | key notes); log ${log}"
		grep -E '^\s*[0-9]+/[0-9]+ Test +#[0-9]+: .*(Failed|\*\*\*|Timeout)' "${log}" | head -n 12 |
			sed 's/^/        | /'
	fi
}

DIRECT_NOTE=""
direct_run() {	# <label> <timeout> <ERE the log must match> <command...>: one firmware program run directly
	local label="$1" limit="$2" pattern="$3" log status=0
	shift 3
	log="${LOG_DIR}/2b-${label}.log"
	run_box "${limit}" "${log}" "$@" || status=$?
	if [ "${status}" = 0 ] && grep -Eq "${pattern}" "${log}"; then
		DIRECT_NOTE="${label} PASS"
		return 0
	fi
	if [ "${status}" = 124 ]; then
		DIRECT_NOTE="${label} FAILED (time limit ${limit} s)"
	else
		DIRECT_NOTE="${label} FAILED (exit ${status}, or no pass line)"
	fi
	show_tail "${log}" 8
	return 1
}

stage_direct() {
	stage_begin "2b" "Firmware tests ctest cannot run"
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	sandbox_new direct
	local bad=0 notes="" tool
	# mmDeskFirmwareTest has a ctest entry, but its 400 s limit is too short and without -DGEARMULATOR_MM_ROM it only
	# skips; mdDeskFirmwareTest is its Machinedrum twin and has none. Both end with a "PASS (n failure...)" line.
	tool="$(find_tool mmDeskFirmwareTest)"
	if [ -n "${tool}" ]; then
		direct_run mmDeskFirmwareTest 3600 '^PASS \(0 failures\)' "${tool}" "${MM_ROM}" || bad=1
	else
		DIRECT_NOTE="mmDeskFirmwareTest not built"; bad=1
	fi
	notes="${DIRECT_NOTE}"
	tool="$(find_tool mdDeskFirmwareTest)"
	if [ -n "${tool}" ]; then
		direct_run mdDeskFirmwareTest 3600 '^mdDeskFirmwareTest: PASS \(0 failure\(s\)\)' "${tool}" "${MD_ROM}" || bad=1
	else
		DIRECT_NOTE="mdDeskFirmwareTest not built"; bad=1
	fi
	notes="${notes}; ${DIRECT_NOTE}"
	tool="$(find_tool mdSysexLifecycleTest)"
	if [ -z "${tool}" ]; then
		if [ "${QUICK}" = 1 ]; then
			cannot_run mdSysexLifecycleTest "needs the plug-in code, which --quick does not build"
		else
			notes="${notes}; mdSysexLifecycleTest not built"; bad=1
		fi
	elif [ ! -f "${MD_CACHE}" ] || [ ! -f "${MM_PATCH}" ]; then
		local lifecycle_reason="needs the Machinedrum factory cache (${MD_CACHE}) and the Monomachine factory patch RAM"
		cannot_run mdSysexLifecycleTest "${lifecycle_reason} (${MM_PATCH}); absent on this Mac"
		notes="${notes}; mdSysexLifecycleTest cannot run here (fixtures absent, named in the summary)"
	else
		direct_run mdSysexLifecycleTest 2400 'LIFECYCLE ALL PASS' "${tool}" "${MD_ROM}" "${MD_CACHE}" "${MM_ROM}" \
			"${MM_PATCH}" || bad=1
		notes="${notes}; ${DIRECT_NOTE}"
	fi
	if [ "${bad}" = 0 ]; then
		stage_end PASS "desk smoke tests with the contract checks" "${notes}"
	else
		stage_end FAIL "" "${notes}"
	fi
}

# =====================================================================================================================
# 3. ROM loading   4. SysEx round trip
# =====================================================================================================================
tool_or_pending() {	# <name> <var>: the tool's path in <var>; else the stage is PENDING and the caller returns
	local path
	path="$(find_tool "$1")"
	if [ -z "${path}" ]; then
		stage_end PENDING "" "$1 is not in this tree yet (no such target built)"
		return 1
	fi
	printf -v "$2" '%s' "${path}"
	return 0
}

stage_romload() {
	stage_begin 3 "ROM loading"
	local tool="" model rom log numbers="" bad=0 line
	tool_or_pending "${ROM_LOAD_TOOL}" tool || return
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	sandbox_new romload
	for model in md mm; do
		rom="$(rom_for "${model}")"
		log="${LOG_DIR}/3-romload-${model}.log"
		if run_box 900 "${log}" "${tool}" "${rom}" && last_line "${log}" | grep -q 'mdmmRomLoadTest: PASS'; then
			line="$(grep -m1 'fingerprint=' "${log}")"
			numbers="${numbers:+${numbers}; }$(label "${model}")"
			numbers+=" $(printf '%s\n' "${line}" | sed -n 's/.*\(boot_s=[^ ]*\).*/\1/p')"
		else
			bad=1
			numbers="${numbers:+${numbers}; }$(label "${model}") FAILED: $(last_line "${log}")"
			show_tail "${log}" 8
		fi
		grep 'fingerprint=' "${log}" >> "${OUT}/fingerprints.txt" 2> /dev/null
	done
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}"; else stage_end FAIL "${numbers}"; fi
}

stage_sysex() {
	stage_begin 4 "SysEx round trip"
	local tool="" model rom log numbers="" bad=0 count
	tool_or_pending "${SYSEX_TOOL}" tool || return
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	sandbox_new sysex
	for model in md mm; do
		rom="$(rom_for "${model}")"
		log="${LOG_DIR}/4-sysex-${model}.log"
		if [ "${model}" = md ]; then
			count=${#MD_FIXTURES[@]}
			[ "${count}" -gt 0 ] || { bad=1; numbers="${numbers:+${numbers}; }MD no fixtures"; continue; }
			run_box 1800 "${log}" "${tool}" "${rom}" md "${MD_FIXTURES[@]}"
		else
			count=${#MM_FIXTURES[@]}
			[ "${count}" -gt 0 ] || { bad=1; numbers="${numbers:+${numbers}; }MM no fixtures"; continue; }
			run_box 1800 "${log}" "${tool}" "${rom}" mm "${MM_FIXTURES[@]}"
		fi
		if last_line "${log}" | grep -q 'mdmmSysexRoundTripTest: PASS'; then
			numbers="${numbers:+${numbers}; }$(label "${model}") ${count} fixture(s) round-tripped"
		else
			bad=1
			numbers="${numbers:+${numbers}; }$(label "${model}") FAILED: $(last_line "${log}")"
			show_tail "${log}" 10
		fi
	done
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}"; else stage_end FAIL "${numbers}"; fi
}

# =====================================================================================================================
# 5. playing goldens   5b CPU bench sanity
# =====================================================================================================================
# <index> <record|compare> <scenario> <outputs> <speedups> <seconds> <model>: result in goldens/<index>.result
golden_one() {
	local index="$1" mode="$2" scenario="$3" outputs="$4" speedups="$5" seconds="$6" model="$7"
	local log extra=() status=0 verdict detail
	log="${OUT}/goldens/${index}.log"
	[ "${speedups}" = off ] && extra=(GEARMULATOR_MDMM_SPEEDUPS=0)
	local flags=(--scenario "${scenario}" --outputs "${outputs}" --golden "${GOLDENS}")
	[ "${mode}" = record ] && flags+=(--record)
	run_box 1800 "${log}" ${extra[@]+"${extra[@]}"} "${PERF}" "$(rom_for "${model}")" "${model}" "${seconds}" \
		"${flags[@]}" || status=$?
	detail="$(last_line "${log}")"
	if [ "${status}" = 0 ] && { [ "${mode}" = record ] || printf '%s' "${detail}" | grep -q 'golden: PASS'; }; then
		verdict=PASS
	else
		verdict=FAIL
	fi
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${scenario}" "${outputs}" "speedups-${speedups}" "${seconds}" "${verdict}" \
		"$(scrub "${detail}" | cut -c1-160)" > "${OUT}/goldens/${index}.result"
}

golden_pass() {	# <record|compare> <combos file>: every RUN line; compare mode runs ${MDMM_GATE_JOBS:-4} at a time
	local mode="$1" combos="$2" index=0 running=0 kind scenario outputs speedups seconds model limit tab=$'\t'
	limit="${MDMM_GATE_GOLDEN_JOBS:-4}"
	[ "${mode}" = record ] && limit=1
	while IFS="${tab}" read -r kind scenario outputs speedups seconds model; do
		[ "${kind}" = RUN ] || continue
		index=$((index + 1))
		golden_one "${mode}-${index}" "${mode}" "${scenario}" "${outputs}" "${speedups}" "${seconds}" "${model}" &
		running=$((running + 1))
		if [ "${running}" -ge "${limit}" ]; then wait; running=0; fi
	done < "${combos}"
	wait
}

stage_goldens() {
	stage_begin 5 "Playing goldens"
	local mode=compare combos="${OUT}/goldens-combos.tsv" bad=0 pass=0 total=0
	local kind a b c line notes="" tab=$'\t' equal
	PERF=""
	tool_or_pending "${PERF_TOOL}" PERF || return
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	[ "${RECORD}" = 1 ] && mode=record
	if [ "${mode}" = compare ] && [ ! -f "${GOLDENS}" ]; then
		stage_end PENDING "" \
			"no goldens file at ${GOLDENS#"${ROOT}"/}: record it once with --record-goldens and get it signed off"
		return
	fi
	gate_py goldens "${GOLDENS}" --mode "${mode}" --defaults "${GATE_MIN_SCENARIOS}" \
		--extra "${MDMM_GATE_SCENARIOS:-}" > "${combos}"
	if grep -q '^BAD' "${combos}"; then
		stage_end FAIL "" "$(grep '^BAD' "${combos}" | head -n 1 | cut -f2)"
		return
	fi
	sandbox_new goldens
	mkdir -p "${OUT}/goldens"
	: > "${OUT}/goldens.tsv"
	info_set goldens_file "${GOLDENS#"${ROOT}"/}"
	if [ "${mode}" = record ]; then
		cat <<'BANNER'

    ################################################################################
    #  --record-goldens: the playing goldens are being WRITTEN, not compared.      #
    #  They are the reference every later build is held to. A changed number is    #
    #  a changed machine: read the diff, and get Radek's sign-off before commit.   #
    ################################################################################

BANNER
		info_set goldens_recorded 1
		golden_pass record "${combos}"
		for line in "${OUT}"/goldens/record-*.result; do
			[ -f "${line}" ] && grep -q "${tab}FAIL${tab}" "${line}" && bad=1
		done
		[ "${bad}" = 0 ] || { stage_end FAIL "" "a recording run failed (logs in ${OUT}/goldens)"; return; }
		echo "    recorded; now comparing the file against fresh runs"
		gate_py goldens "${GOLDENS}" --mode compare --defaults "${GATE_MIN_SCENARIOS}" \
			--extra "${MDMM_GATE_SCENARIOS:-}" > "${combos}"
		mode=compare
	fi
	while IFS="${tab}" read -r kind a b c _ _; do
		if [ "${kind}" = MISSING ]; then
			total=$((total + 1)); bad=1
			printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${a}" "${b}" "speedups-${c}" "-" "FAIL" \
				"no entry in the goldens file for this run (record it: --record-goldens)" >> "${OUT}/goldens.tsv"
		fi
	done < "${combos}"
	golden_pass compare "${combos}"
	local runs index
	runs="$(grep -c '^RUN' "${combos}")"
	for ((index = 1; index <= runs; index++)); do
		line="${OUT}/goldens/compare-${index}.result"
		[ -f "${line}" ] || continue
		cat "${line}" >> "${OUT}/goldens.tsv"
		total=$((total + 1))
		if grep -q "${tab}PASS${tab}" "${line}"; then pass=$((pass + 1)); else bad=1; fi
	done
	notes="$(grep "${tab}FAIL${tab}" "${OUT}/goldens.tsv" | head -n 4 | cut -f1-3,6 | tr '\t' ' ' | tr '\n' ';' |
		sed 's/;*$//')"
	if [ "${bad}" = 0 ] && [ "${total}" -gt 0 ]; then
		equal="${pass} of ${total} runs equal their golden"
		stage_end PASS "${equal} (scenarios x stereo/all x speed-ups on/off, each bit-exact to its golden)"
	else
		stage_end FAIL "${pass} of ${total} runs equal their golden" "${notes:-no runs}; logs ${OUT}/goldens"
	fi
}

note_goldens_state() {	# the file as it is after stage 5 (--record-goldens changes it)
	local modified=0
	[ -f "${GOLDENS}" ] && info_set goldens_sha256 "$(shasum -a 256 "${GOLDENS}" | awk '{print $1}')"
	[ -n "$(git -C "${ROOT}" status --porcelain -- "${GOLDENS}" 2> /dev/null)" ] && modified=1
	info_set goldens_modified "${modified}"
	return 0
}

stage_bench() {
	stage_begin "5b" "CPU bench sanity"
	local md_tool mm_tool bad=0 numbers=""
	md_tool="$(find_tool mdCpuBenchTest)"; mm_tool="$(find_tool mmCpuBenchTest)"
	if [ -z "${md_tool}" ] || [ -z "${mm_tool}" ]; then
		stage_end PENDING "" "mdCpuBenchTest / mmCpuBenchTest are not in this tree"
		return
	fi
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	sandbox_new bench
	# "<ROM> 1 6": one instance, six seconds. They exit 1 when the machine does not play (the play head stands still).
	if run_box 900 "${LOG_DIR}/5b-bench-md.log" "${md_tool}" "${MD_ROM}" 1 6; then
		numbers="MD plays"
	else
		bad=1; numbers="MD FAILED"; show_tail "${LOG_DIR}/5b-bench-md.log" 6
	fi
	if run_box 900 "${LOG_DIR}/5b-bench-mm.log" "${mm_tool}" "${MM_ROM}" 1 6; then
		numbers="${numbers}; MM plays"
	else
		bad=1; numbers="${numbers}; MM FAILED"; show_tail "${LOG_DIR}/5b-bench-mm.log" 6
	fi
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}"; else stage_end FAIL "${numbers}"; fi
}

# =====================================================================================================================
# 6. timing
# =====================================================================================================================
stage_rt() {
	stage_begin "6a" "Real-time work while editing (rt-check)"
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	local log="${LOG_DIR}/6a-rt-check.log" status=0 fails
	sandbox_new rt
	# scripts/mdmm-rt-check.sh counts retired instructions, so what else runs does not move it; --plock adds B-010's
	# sequencer timing. It makes its own scratch data root; HOME stays the sandbox's.
	run_box 7200 "${log}" "${here}/mdmm-rt-check.sh" --md "${MD_ROM}" --mm "${MM_ROM}" --build "${BUILD}" \
		--plock || status=$?
	fails="$(grep -c 'FAIL' "${log}")"
	if [ "${status}" = 0 ] && last_line "${log}" | grep -q '^rt-check: PASS'; then
		stage_end PASS "$(last_line "${log}"); $(grep -c '^  [a-zA-Z]* *[0-9]* |' "${log}") actions measured"
	else
		stage_end FAIL "$(last_line "${log}")" "${fails} FAIL lines; log ${log}"
		grep 'FAIL' "${log}" | head -n 8 | sed 's/^/        | /'
	fi
}

stage_capacity() {
	stage_begin "6b" "Core capacity of the built VST3"
	local host receipt="${OUT}/mdmm-core-capacity.json" log="${LOG_DIR}/6b-core-capacity.log" status=0 answer seed=()
	# With no flash cache in its fresh data folder the Machinedrum does its first-start flash work while the notes
	# play, and the capture is silent in about one run in three ("produced no finite audible output"): the cache is
	# given, as a person has it.
	[ -f "${MD_CACHE}" ] && seed=(--md-flash-cache "${MD_CACHE}")
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	host="$(find_tool latency_host)"
	[ -n "${host}" ] || { stage_end FAIL "" "latency_host is not built"; return; }
	rm -rf "${OUT}/core-capacity" "${receipt}"
	# The release script's own microgate, with its own parameters (build_mdmm.sh): three unpaced runs must stay under
	# 0.90 of the block budget at the median; the paced tail is reported, not judged. Needs the pinned release ROMs.
	run_plain 3600 "${log}" python3 -B "${ROOT}/scripts/macos/check_mdmm_core_capacity.py" \
		--host "${host}" --host-architecture "${ARCH}" \
		--md-plugin "${PRODUCTS}/VST3/${MDMM_PRODUCT_NAME_MD}.vst3" \
		--mm-plugin "${PRODUCTS}/VST3/${MDMM_PRODUCT_NAME_MM}.vst3" \
		--md-firmware "${MD_ROM}" --mm-firmware "${MM_ROM}" --work-root "${OUT}/core-capacity" --output "${receipt}" \
		--rate 48000 --block 128 --seconds 20 --warm-start-seconds 12 --capacity-repeats 3 --paced-repeats 3 \
		--capacity-p50-limit 0.90 ${seed[@]+"${seed[@]}"} || status=$?
	if [ -f "${receipt}" ]; then
		answer="$(gate_py capacity "${receipt}")"
		if [ "${status}" = 0 ] && gate_py capacity "${receipt}" > /dev/null; then
			stage_end PASS "$(printf '%s\n' "${answer}" | key numbers)" "$(printf '%s\n' "${answer}" | key notes)"
		else
			stage_end FAIL "$(printf '%s\n' "${answer}" | key numbers)" \
				"exit ${status}; $(printf '%s\n' "${answer}" | key notes)"
		fi
	else
		stage_end FAIL "" "no receipt (exit ${status}); $(last_line "${log}")"
		show_tail "${log}" 10
	fi
}

# =====================================================================================================================
# 6c. soak: ten minutes of busy play per machine, the plug-in's own performance capture judged
# =====================================================================================================================
stage_soak() {
	stage_begin "6c" "Soak, busy play with the performance capture"
	local host model product machine note vst3 log jsonl answer status bad=0 numbers="" notes=""
	local seconds="${SOAK_SECONDS}" min
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	host="$(find_tool latency_host)"
	[ -n "${host}" ] || { stage_end FAIL "" "latency_host is not built"; return; }
	# latency_host takes 20 to 600 s; the capture itself ends at 10 minutes or 8 MiB
	[ "${seconds}" -lt 20 ] && seconds=20
	[ "${seconds}" -gt 600 ] && seconds=600
	min=$((seconds * 80 / 100))
	rm -rf "${OUT}/soak"; mkdir -p "${OUT}/soak"; : > "${OUT}/soak.tsv"
	# latency_host (scripts/pluginTester/latency) loads the real VST3 and renders it in real time, 48 kHz in blocks of
	# 128, with no audio device: the plug-in's processor starts the performance capture itself with
	# GEARMULATOR_RT_INSTRUMENTATION=1 (doc/md_mm_performance_diagnostics.md). The load is its busiest scenario,
	# "chords": six voices every 251 ms (the Machinedrum's pads 1 to 6, the Monomachine's tracks 1 to 6) from second
	# 10 on.
	for model in md mm; do
		if [ "${model}" = md ]; then
			product="${MDMM_PRODUCT_NAME_MD}"; machine=Machinedrum; note=36
		else
			product="${MDMM_PRODUCT_NAME_MM}"; machine=Monomachine; note=60
		fi
		vst3="${PRODUCTS}/VST3/${product}.vst3"
		sandbox_new "soak-${model}"
		# the factory flash cache and the factory patch RAM, as a person's machine has them (without them the first
		# start does its flash work in the first seconds, and a callback waits 36 ms for the synth lock)
		mkdir -p "${SB_DATA}/Gearmulator Preview/${machine}/nvram"
		if [ "${model}" = md ]; then
			[ -f "${MD_CACHE}" ] && cp "${MD_CACHE}" "${SB_DATA}/Gearmulator Preview/${machine}/nvram/"
		else
			[ -f "${MM_PATCH}" ] && cp "${MM_PATCH}" "${SB_DATA}/Gearmulator Preview/${machine}/nvram/"
		fi
		log="${LOG_DIR}/6c-soak-${model}.log"
		echo "    $(label "${model}"): ${seconds} s of play, started $(date +%H:%M:%S)"
		run_box $((seconds + 300)) "${log}" GEARMULATOR_RT_INSTRUMENTATION=1 "${host}" "${vst3}" \
			"${OUT}/soak/${model}" 48000 128 "${seconds}" -1 fixed -1 paced "${note}" 127 chords messages -1
		status=$?
		jsonl="$(find "${SB_DATA}/Gearmulator Preview/${machine}/logs" -name 'performance-*.jsonl' 2> /dev/null |
			sort | tail -n 1)"
		if [ "${status}" != 0 ] || [ -z "${jsonl}" ]; then
			bad=1
			numbers="${numbers:+${numbers}; }$(label "${model}") no capture"
			notes="${notes:+${notes}; }$(label "${model}"): latency_host exit ${status}${jsonl:+, capture written}"
			[ -z "${jsonl}" ] && notes+=", no performance capture was written"
			show_tail "${log}" 8
			rm -f "${OUT}/soak/${model}.wav"
			continue
		fi
		cp "${jsonl}" "${OUT}/soak/${model}.jsonl"
		answer="$(gate_py soak "${OUT}/soak/${model}.jsonl" --machine "$(label "${model}")" \
			--warmup "${SOAK_WARMUP}" --lock-us "${SOAK_LOCK_US}" --min-seconds "${min}" \
			--tsv "${OUT}/soak.tsv" --blocks "${OUT}/soak/${model}.blocks.csv" \
			--receipt "${OUT}/soak/${model}.json")" || bad=1
		numbers="${numbers:+${numbers}; }$(printf '%s\n' "${answer}" | key numbers)"
		notes="${notes:+${notes}; }$(printf '%s\n' "${answer}" | key notes)"
		rm -f "${OUT}/soak/${model}.wav" "${OUT}/soak/${model}.blocks.csv"	# the audio is not kept: 170 MB each
	done
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}" "${notes}"; else stage_end FAIL "${numbers}" "${notes}"; fi
}

# =====================================================================================================================
# 7. the plug-in: 7a start with firmware, 7b auval, 7c pluginval, 7d journeys
# =====================================================================================================================
stage_vst3_start() {
	stage_begin "7a" "VST3 start with firmware"
	local tester model product rom log bad=0 numbers="" status
	tester="$(find_tool pluginTester)"
	[ -n "${tester}" ] || { stage_end FAIL "" "pluginTester is not built"; return; }
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	for model in md mm; do
		if [ "${model}" = md ]; then product="${MDMM_PRODUCT_NAME_MD}"; else product="${MDMM_PRODUCT_NAME_MM}"; fi
		rom="$(rom_for "${model}")"
		# scripts/macos/verify_mdmm_package.sh's firmware smoke: 256 blocks with the ROM, no device-start error.
		sandbox_new "start-${model}"
		log="${LOG_DIR}/7a-${model}-firmware.log"
		status=0
		run_box 900 "${log}" "${tester}" -verify-audio-buses -blocks 256 \
			-plugin "${PRODUCTS}/VST3/${product}.vst3" || status=$?
		if [ "${status}" = 0 ] && grep -Fq "Progress: 100% (256/256 blocks)" "${log}" \
			&& ! grep -Eiq -e 'Failed to create device|Device Initialization failed' \
				-e 'firmware rom .* required|DSP execution fault' "${log}"; then
			numbers="${numbers:+${numbers}; }${model} 256 blocks with firmware"
		else
			bad=1; numbers="${numbers:+${numbers}; }${model} FAILED (exit ${status})"; show_tail "${log}" 8
		fi
		# scripts/macos/build_mdmm.sh's wrapper check: no ROM, buses and the automation smoke.
		sandbox_new "start-${model}-norom" norom
		log="${LOG_DIR}/7a-${model}-norom.log"
		status=0
		run_box 600 "${log}" "${tester}" -blocks 16 -verify-audio-buses -automation-smoke \
			-plugin "${PRODUCTS}/VST3/${product}.vst3" || status=$?
		if [ "${status}" = 0 ]; then
			numbers="${numbers}, automation smoke without ROM"
		else
			bad=1; numbers="${numbers}, automation smoke FAILED (exit ${status})"; show_tail "${log}" 8
		fi
	done
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}"; else stage_end FAIL "${numbers}"; fi
}

AU_READY=0
stage_auval() {
	stage_begin "7b" "auval on the built AU"
	local name short plist type subtype manufacturer log bad=0 numbers="" scratch
	command -v auval > /dev/null 2>&1 || { stage_end FAIL "" "auval not found"; return; }
	if ! au_swap_in; then stage_end FAIL "" "${AU_PROBLEM}"; au_swap_out; return; fi
	AU_READY=1
	scratch="${OUT}/sandbox/auval/Documents"
	mkdir -p "${scratch}"
	for name in "${MDMM_PRODUCT_NAME_MD}" "${MDMM_PRODUCT_NAME_MM}"; do
		short="$(printf '%s' "${name}" | cut -c1-3 | tr '[:upper:]' '[:lower:]')"
		plist="${COMPONENTS}/${name}.component/Contents/Info.plist"
		type="$(plutil -extract AudioComponents.0.type raw -o - "${plist}")"
		subtype="$(plutil -extract AudioComponents.0.subtype raw -o - "${plist}")"
		manufacturer="$(plutil -extract AudioComponents.0.manufacturer raw -o - "${plist}")"
		log="${LOG_DIR}/7b-auval-${short}.log"
		# As CI runs it (smoke_mdmm.sh): auval -v, a scratch data root with no ROM.
		if gate_run --timeout 900 --log "${log}" -- GEARMULATOR_DATA_ROOT="${scratch}" auval -v "${type}" "${subtype}" \
			"${manufacturer}"; then
			numbers="${numbers:+${numbers}; }${name} AU: auval -v ${type} ${subtype} ${manufacturer} passed"
		else
			bad=1; numbers="${numbers:+${numbers}; }${name} AU: auval FAILED"; show_tail "${log}" 12
		fi
	done
	if [ "${bad}" = 0 ]; then stage_end PASS "${numbers}"; else stage_end FAIL "${numbers}"; fi
}

stage_pluginval() {
	stage_begin "7c" "pluginval on the built VST3 and AU"
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	local out="${OUT}/pluginval" log="${LOG_DIR}/7c-pluginval.log" status=0 answer judged=0 au_skipped=0
	# scripts/mdmm-pluginval.sh: strictness 5 and 8, the firmware copied into its own scratch data root, the owner's
	# config files checksummed and restored. It validates an AU only when the installed one is this build, which 7b's
	# swap arranges. It checks that against the owner's real HOME, so it runs in the owner's environment.
	run_plain 7200 "${log}" "${PLUGINVAL_CMD}" --products "${PRODUCTS}" --out "${out}" || status=$?
	[ "${AU_READY}" = 1 ] || au_skipped=1
	grep -q 'SKIP: .*component' "${log}" && au_skipped=1
	if [ ! -f "${out}/pluginval-summary.md" ]; then
		stage_end FAIL "" "no pluginval summary (exit ${status}); log ${log}"
		show_tail "${log}" 10
		return
	fi
	answer="$(gate_py pluginval "${out}/pluginval-summary.md")" || judged=1
	if [ "${status}" = 0 ] && [ "${judged}" = 0 ] && [ "${au_skipped}" = 0 ]; then
		stage_end PASS "$(printf '%s\n' "${answer}" | key numbers)"
	else
		local not_validated=""
		[ "${au_skipped}" = 1 ] && not_validated="; the AU was NOT validated (not this build, or the swap failed)"
		stage_end FAIL "$(printf '%s\n' "${answer}" | key numbers)" \
			"exit ${status}; $(printf '%s\n' "${answer}" | key notes)${not_validated}; log ${log}"
		show_tail "${log}" 10
	fi
}

stage_journeys() {
	stage_begin "7d" "User journeys, both editors"
	have_roms both || { stage_end FAIL "" "${MISSING_WHAT}"; return; }
	local log="${LOG_DIR}/7d-journeys.log" build_log="${LOG_DIR}/7d-diagnostics-build.log"
	local report status=0 answer muted_note="" notes capture
	# The journeys need the page's self-tests, which only a diagnostics build has (-Dgearmulator_MDMM_DIAGNOSTICS=ON):
	# scripts/mdmm-dev.sh builds one in its own tree, its products kept out of the checkout's bin/.
	gate_run --timeout 7200 --log "${build_log}" -- MDMM_DEV_BUILD="${DIAG_BUILD}" \
		MDMM_DEV_ARGS="-DGEARMULATOR_JUCE_PRODUCTS_ROOT=${DIAG_BUILD}/products" "${DEV_CMD}" build \
		|| { stage_end FAIL "" "the diagnostics build failed (${build_log})"; show_tail "${build_log}" 20; return; }
	rm -rf "${OUT}/journeys"
	# Silence: the standalones open the audio device (the VST3 host plays to none). --background zeroes the
	# standalone's output after the machine made it, so nothing reaches any speaker or interface whatever the
	# person's saved audio setup is (a copy of it is used), and no input is opened (JUCE's standalone does not open
	# one by default and the saved setup has none). On top of that the system output is muted for the stage, and put
	# back after it and on any exit.
	if output_mute 7d; then
		muted_note="${MUTE_NOTE}"
	elif [ "${MDMM_GATE_ALLOW_UNMUTED:-0}" = 1 ]; then
		muted_note="${MUTE_NOTE}; MDMM_GATE_ALLOW_UNMUTED=1: ran anyway, the --background output is zeroed"
	else
		muted_note="${MUTE_NOTE}; a stage that opens an audio device does not run with the output live"
		muted_note+=" (MDMM_GATE_ALLOW_UNMUTED=1 overrides it)"
		stage_end FAIL "" "${muted_note}"
		return
	fi
	# The sampler journeys draw canvases and skip unless their window is drawing: --background keeps WebKit drawing
	# while the window is covered (a sleeping display still stops it). Journeys that skip for that reason make this
	# stage red.
	# MDMM_JOURNEY_PERF=1 records the audio callbacks of each editor (the performance capture), judged below for the
	# record.
	# shellcheck disable=SC2086
	gate_run --timeout 10800 --log "${log}" -- MDMM_APP_DIR="${DIAG_BUILD}/products/Release/Standalone" \
		MDMM_VST3_DIR="${DIAG_BUILD}/products/Release/VST3" MDMM_JOURNEY_OUT="${OUT}/journeys" MDMM_JOURNEY_PERF=1 \
		GEARMULATOR_MD_FIRMWARE_BIN="${MD_ROM}" GEARMULATOR_MM_FIRMWARE_BIN="${MM_ROM}" \
		"${JOURNEYS_CMD}" ${JOURNEY_ARGS} both || status=$?
	output_restore
	report="${OUT}/journeys/report.txt"
	if [ -f "${report}" ]; then
		answer="$(gate_py journeys "${report}")"
		notes="$(printf '%s\n' "${answer}" | key notes)"
		capture="$(journeys_capture_note)"
		if [ "${status}" = 0 ] && gate_py journeys "${report}" > /dev/null; then
			stage_end PASS "$(printf '%s\n' "${answer}" | key numbers)" \
				"${notes}${muted_note:+${notes:+; }${muted_note}}${capture:+; ${capture}}"
		else
			stage_end FAIL "$(printf '%s\n' "${answer}" | key numbers)" \
				"exit ${status}; ${notes}; report ${report}${muted_note:+; ${muted_note}}"
		fi
	else
		stage_end FAIL "" "no report (exit ${status}); log ${log}${muted_note:+; ${muted_note}}"
		show_tail "${log}" 12
	fi
}

journeys_capture_note() {	# the audio thread while the page edited the machine: for the record, not judged
	local file label brief=""
	for file in "${OUT}"/journeys/*/performance-*.jsonl; do
		[ -f "${file}" ] || continue
		label="$(basename "$(dirname "${file}")")"
		brief="${brief:+${brief}, }$(gate_py soak "${file}" --machine "${label}" --warmup 30 --info --brief |
			key numbers)"
	done
	[ -n "${brief}" ] &&
		printf 'audio thread during the journeys (the first 10 minutes of each editor, not judged): %s' "${brief}"
	return 0
}

# =====================================================================================================================
# M1. the updater end to end: a person at a window, so only printed
# =====================================================================================================================
stage_updater() {
	stage_begin "M1" "Updater end to end (manual)"
	local log="${LOG_DIR}/M1-updater-check.log" status=0 facts
	"${GATE_DIR}/updater-manual.sh" check > "${log}" 2>&1 || status=$?
	facts="$(grep -E '^(candidate version|update public key|published manifest)' "${log}" |
		sed 's/  */ /g; s/ (source[^)]*)//' | paste -sd'|' - | sed 's/|/; /g')"
	{
		echo "Updater end to end: needs a window and a person (about 10 minutes). State now:"
		sed 's/^/  /' "${log}"
		echo
		echo "  scripts/local-gate/updater-manual.sh prepare    # a copy of this tree with an older version," \
			"standalones built"
		echo "  scripts/local-gate/updater-manual.sh run md     # then: run mm. Press Update, watch the download," \
			"the Installer, Quit"
		echo "  The steps are printed by 'run' and written in doc/release/LOCAL-GATE.md. Put the outcome in the" \
			"release review."
	} > "${OUT}/manual.txt"
	case "${status}" in
		0) stage_end MANUAL "${facts}" \
			"can be run now: scripts/local-gate/updater-manual.sh prepare, then run md / run mm" ;;
		3) stage_end MANUAL "${facts}" "Update cannot be exercised yet (see manual steps); Download only" ;;
		*) stage_end MANUAL "${facts}" \
			"could not read the published manifest; run scripts/local-gate/updater-manual.sh check later" ;;
	esac
}

# =====================================================================================================================
# the run
# =====================================================================================================================
record_identity() {
	local describe commit branch dirty=0 modified=0 run_mode
	describe="$(git -C "${ROOT}" describe --tags --always --dirty 2> /dev/null || echo unknown)"
	commit="$(git -C "${ROOT}" rev-parse --short=12 HEAD 2> /dev/null || echo unknown)"
	branch="$(git -C "${ROOT}" rev-parse --abbrev-ref HEAD 2> /dev/null || echo unknown)"
	[ -n "$(git -C "${ROOT}" status --porcelain --untracked-files=no 2> /dev/null)" ] && dirty=1
	[ -n "$(git -C "${ROOT}" status --porcelain -- "${GOLDENS}" 2> /dev/null)" ] && modified=1
	info_set describe "${describe}"; info_set commit "${commit}"; info_set branch "${branch}"; info_set dirty "${dirty}"
	run_mode="$([ "${QUICK}" = 1 ] && echo 'quick (no plug-ins; not a release gate)' || echo 'full')"
	if [ "${SKIP_PLUGIN}" = 1 ]; then run_mode+=', plug-in checks skipped'; fi
	if [ "${SKIP_JOURNEYS}" = 1 ]; then run_mode+=', journeys skipped'; fi
	info_set mode "${run_mode}"
	info_set build_dir "${BUILD}"; info_set arch "${ARCH}"; info_set started "${STARTED}"
	info_set thinlto "$(cache_value GEARMULATOR_MDMM_APPLE_THINLTO:BOOL)"
	info_set dsp_optimised "$(cache_value GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP:BOOL)"
	info_set build_type "$(cache_value CMAKE_BUILD_TYPE:STRING)"
	info_set host "$(sysctl -n machdep.cpu.brand_string 2> /dev/null || uname -m)"
	info_set macos "$(sw_vers -productVersion 2> /dev/null || echo ?)"
	info_set xcode "$(xcodebuild -version 2> /dev/null | tr '\n' ' ' | sed 's/ *$//')"
	info_set cmake "$(cmake --version 2> /dev/null | head -n 1)"
	info_set goldens_modified "${modified}"
	[ -f "${GOLDENS}" ] && info_set goldens_sha256 "$(shasum -a 256 "${GOLDENS}" | awk '{print $1}')"
	info_set goldens_file "${GOLDENS#"${ROOT}"/}"
	return 0
}

echo "== MD/MM local release gate: ${ROOT}"
echo "   build ${BUILD}"
echo "   run   ${OUT}"
record_identity

# MDMM_GATE_ONLY="3 5" runs only those stages on the tree as it is (stage 0 always runs): for working on one stage
# of the gate; the summary calls such a run partial.
ONLY="${MDMM_GATE_ONLY:-}"
wanted() { [ -z "${ONLY}" ] || [[ " ${ONLY} " == *" $1 "* ]]; }
[ -n "${ONLY}" ] && info_set only "${ONLY}"

stage_inputs
if wanted 1; then stage_build; else STAGE_RESULT=SKIP; fi
BUILD_OK=0
{ [ "${STAGE_RESULT}" = PASS ] || [ "${STAGE_RESULT}" = SKIP ]; } && BUILD_OK=1
if [ "${BUILD_OK}" = 1 ]; then
	wanted 2 && stage_ctest
	wanted 2b && stage_direct
	wanted 3 && stage_romload
	wanted 4 && stage_sysex
	wanted 5 && { stage_goldens; note_goldens_state; }
	wanted 5b && stage_bench
	wanted 6a && stage_rt
else
	for pair in "2:ctest, every registered test" "2b:Firmware tests ctest cannot run" "3:ROM loading" \
		"4:SysEx round trip" "5:Playing goldens" "5b:CPU bench sanity" "6a:Real-time work while editing (rt-check)"; do
		stage_begin "${pair%%:*}" "${pair#*:}"
		stage_end FAIL "" "not run: the build failed"
	done
fi
PLUGIN_STAGES="6b:Core capacity of the built VST3|6c:Soak, busy play with the performance capture"
PLUGIN_STAGES+="|7a:VST3 start with firmware|7b:auval on the built AU"
PLUGIN_STAGES+="|7c:pluginval on the built VST3 and AU|7d:User journeys, both editors"
leave_out() {	# <reason> <FAIL|SKIP>: every stage of the plug-in group, not run
	local pair
	IFS='|'
	for pair in ${PLUGIN_STAGES}; do
		stage_begin "${pair%%:*}" "${pair#*:}"
		if [ "$2" = SKIP ]; then stage_end SKIP "" "$1"; else stage_end FAIL "" "$1"; fi
	done
	IFS=$' \t\n'
}
if [ -n "${ONLY}" ]; then
	wanted 6b && [ "${QUICK}" != 1 ] && stage_capacity
	wanted 6c && [ "${QUICK}" != 1 ] && stage_soak
	wanted 7a && [ "${QUICK}" != 1 ] && stage_vst3_start
	wanted 7b && [ "${QUICK}" != 1 ] && stage_auval
	wanted 7c && [ "${QUICK}" != 1 ] && stage_pluginval
	au_swap_out
	wanted 7d && [ "${QUICK}" != 1 ] && stage_journeys
elif [ "${QUICK}" = 1 ]; then
	leave_out "--quick builds no plug-ins" SKIP
elif [ "${SKIP_PLUGIN}" = 1 ]; then
	leave_out "--skip-plugin" SKIP
elif [ "${BUILD_OK}" = 0 ]; then
	leave_out "not run: the build failed" FAIL
else
	stage_capacity
	if [ "${SKIP_SOAK}" = 1 ]; then
		cannot_run "6c soak" "off by default (--soak runs it, 21 minutes)"
	else
		stage_soak
	fi
	stage_vst3_start
	stage_auval
	stage_pluginval
	au_swap_out	# the AU bundles are done with: the owner's own are back before the journeys start
	if [ "${SKIP_JOURNEYS}" = 1 ]; then
		stage_skip "7d" "User journeys, both editors" "--skip-journeys"
	else
		stage_journeys
	fi
fi
au_swap_out
stage_updater

if [ "${RECORD}" = 1 ]; then
	echo
	echo "!!! GOLDENS WERE RECORDED: ${GOLDENS#"${ROOT}"/} changed. Read 'git diff' of it and get Radek's sign-off" \
		"before it is committed."
fi
FINISHED=1
write_summary
status=$?
echo
[ "${RECORD}" = 1 ] && echo "!!! reminder: the goldens changed in this run and need sign-off"
echo "summary: ${OUT}/summary.md"
exit "${status}"
