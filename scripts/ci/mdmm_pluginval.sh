#!/bin/bash
# Runs Tracktion's pluginval on plug-ins of the editors (doc/modern-ux/FOUNDATION.md, "CI start tests"). Used by the
# mdmm-* workflows on macOS, Linux (wrap it in xvfb-run) and Windows (Git Bash), and by scripts/mdmm-pluginval.sh.
#
#   scripts/ci/mdmm_pluginval.sh --out <dir> [options] <plug-in path>...
#     --levels "5 8"        strictness levels, one run per plug-in and level (default 5)
#     --timeout-ms N        pluginval's "no output" timeout (default 120000)
#     --max-seconds N       hard limit for one run (default 1800); the run is killed and counts as a failure
#     --skip-gui-tests      pass pluginval's --skip-gui-tests (no editor windows)
#     --disabled-tests FILE pluginval's list of disabled test names (one per line)
#     --data-root DIR       GEARMULATOR_DATA_ROOT (default: an empty scratch folder, so no ROM and nothing of the
#                           caller's own folders is touched; a given folder is NOT removed)
#     --pluginval PATH      use this pluginval (default: the pinned release of scripts/pluginval.env is downloaded
#                           to $MDMM_PLUGINVAL_CACHE or the temp folder and its sha256 verified)
#     --install-au          macOS: an AU is loaded by the system from its registry, not from a path, so each .component
#                           given is copied to ~/Library/Audio/Plug-Ins/Components for the run and removed after (a runner
#                           only; never on a developer's Mac, and it refuses to replace a component that is there)
#     --label TEXT          written into the summary (the system, the job)
# Writes <out>/<plug-in>-s<level>.log per run and <out>/pluginval-summary.md (appended to $GITHUB_STEP_SUMMARY too).
# Exit status: 0 if every run passed, 1 otherwise (also for a timeout or a crash).
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
. "${here}/../pluginval.env"

out=""; levels="5"; timeout_ms=120000; max_seconds=1800; skip_gui=0; disabled=""; data_root=""; pv=""; label=""; install_au=0
plugins=()
while (( $# )); do
	case "$1" in
		--out) out="$2"; shift 2 ;;
		--levels) levels="$2"; shift 2 ;;
		--timeout-ms) timeout_ms="$2"; shift 2 ;;
		--max-seconds) max_seconds="$2"; shift 2 ;;
		--skip-gui-tests) skip_gui=1; shift ;;
		--disabled-tests) disabled="$2"; shift 2 ;;
		--data-root) data_root="$2"; shift 2 ;;
		--pluginval) pv="$2"; shift 2 ;;
		--install-au) install_au=1; shift ;;
		--label) label="$2"; shift 2 ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) plugins+=("$1"); shift ;;
	esac
done
if [[ -z "${out}" || ${#plugins[@]} -eq 0 ]]; then
	echo "usage: $0 --out <dir> [options] <plug-in>..." >&2
	exit 2
fi
mkdir -p "${out}"
out="$(cd "${out}" && pwd)"

case "$(uname -s)" in
	Darwin) os=macos ;;
	Linux) os=linux ;;
	MINGW*|MSYS*|CYGWIN*) os=windows ;;
	*) echo "unsupported system: $(uname -s)" >&2; exit 2 ;;
esac
# Git Bash on a runner: RUNNER_TEMP is a Windows path.
[[ "${os}" == windows && -n "${RUNNER_TEMP:-}" ]] && RUNNER_TEMP="$(cygpath -u "${RUNNER_TEMP}")"
native_path() { if [[ "${os}" == windows ]]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
sha256_of() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

# --- pluginval: the pinned release, verified
if [[ -z "${pv}" ]]; then
	case "${os}" in
		macos) zip_name="pluginval_macOS.zip"; want="${PLUGINVAL_SHA256_MACOS}" ;;
		linux) zip_name="pluginval_Linux.zip"; want="${PLUGINVAL_SHA256_LINUX}" ;;
		windows) zip_name="pluginval_Windows.zip"; want="${PLUGINVAL_SHA256_WINDOWS}" ;;
	esac
	cache="${MDMM_PLUGINVAL_CACHE:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/pluginval-${PLUGINVAL_VERSION}}"
	mkdir -p "${cache}"
	zip_file="${cache}/${zip_name}"
	if [[ ! -f "${zip_file}" ]] || [[ "$(sha256_of "${zip_file}")" != "${want}" ]]; then
		curl -fsSL --retry 3 -o "${zip_file}" \
			"https://github.com/Tracktion/pluginval/releases/download/v${PLUGINVAL_VERSION}/${zip_name}" || {
			echo "::error::could not download ${zip_name} (pluginval ${PLUGINVAL_VERSION})"; exit 1; }
	fi
	got="$(sha256_of "${zip_file}")"
	if [[ "${got}" != "${want}" ]]; then
		echo "::error::pluginval ${zip_name}: sha256 ${got}, pinned ${want} (scripts/pluginval.env)"
		exit 1
	fi
	echo "pluginval ${PLUGINVAL_VERSION} ${zip_name}: sha256 ${got} (matches the pin)"
	if [[ ! -d "${cache}/unpacked" ]]; then
		mkdir -p "${cache}/unpacked"
		(cd "${cache}/unpacked" && cmake -E tar xf "$(native_path "${zip_file}")")
	fi
	case "${os}" in
		macos) pv="${cache}/unpacked/pluginval.app/Contents/MacOS/pluginval"; xattr -rd com.apple.quarantine "${cache}/unpacked/pluginval.app" 2>/dev/null || true ;;
		linux) pv="${cache}/unpacked/pluginval" ;;
		windows) pv="${cache}/unpacked/pluginval.exe" ;;
	esac
	chmod +x "${pv}" 2>/dev/null || true
fi
[[ -x "${pv}" ]] || { echo "::error::pluginval not found at ${pv}"; exit 1; }
echo "pluginval: ${pv} ($("${pv}" --version 2>&1 | tail -n 1))"

# --- the data root: empty (no ROM) unless given
scratch=""
if [[ -z "${data_root}" ]]; then
	scratch="$(mktemp -d)"
	data_root="${scratch}/data"
	mkdir -p "${data_root}"
fi
installed_au=()
cleanup() {
	for c in ${installed_au[@]+"${installed_au[@]}"}; do rm -rf "${c}"; done
	[[ -n "${scratch}" ]] && rm -rf "${scratch}"
	return 0
}
trap cleanup EXIT
if (( install_au )); then
	components="${HOME}/Library/Audio/Plug-Ins/Components"
	mkdir -p "${components}"
	for i in "${!plugins[@]}"; do
		[[ "${plugins[$i]}" == *.component && -d "${plugins[$i]}" ]] || continue
		target="${components}/${plugins[$i]##*/}"
		if [[ -e "${target}" ]]; then
			echo "::error::${target} exists already; not replacing it"
			exit 1
		fi
		ditto "${plugins[$i]}" "${target}"
		installed_au+=("${target}")
		plugins[i]="${target}"
	done
	killall -9 AudioComponentRegistrar 2>/dev/null || true
	sleep 2
fi
GEARMULATOR_DATA_ROOT="$(native_path "${data_root}")/"
export GEARMULATOR_DATA_ROOT
echo "GEARMULATOR_DATA_ROOT=${GEARMULATOR_DATA_ROOT}"

status=0
rows=()
for plugin in "${plugins[@]}"; do
	if [[ ! -e "${plugin}" ]]; then
		echo "::error::not found: ${plugin}"
		rows+=("| ${plugin##*/} | - | MISSING | - |")
		status=1
		continue
	fi
	name="${plugin##*/}"
	for level in ${levels}; do
		log="${out}/${name// /_}-s${level}.log"
		args=(--strictness-level "${level}" --timeout-ms "${timeout_ms}" --validate "$(native_path "${plugin}")")
		(( skip_gui )) && args+=(--skip-gui-tests)
		[[ -n "${disabled}" ]] && args+=(--disabled-tests "$(native_path "${disabled}")")
		echo "== pluginval strictness ${level}: ${name}"
		began=${SECONDS}
		"${pv}" "${args[@]}" > "${log}" 2>&1 &
		pid=$!
		( sleep "${max_seconds}"; echo "pluginval run killed after ${max_seconds} s" >> "${log}"; pkill -P "${pid}" 2>/dev/null; kill -9 "${pid}" 2>/dev/null ) &
		watchdog=$!
		wait "${pid}"; code=$?
		kill "${watchdog}" 2>/dev/null; wait "${watchdog}" 2>/dev/null
		took=$((SECONDS - began))
		if grep -q "pluginval run killed after" "${log}"; then
			result="TIMEOUT (killed after ${max_seconds} s)"
		elif (( code == 0 )); then
			result="PASS"
		else
			result="FAIL (exit ${code})"
		fi
		tail -n 25 "${log}"
		# Worth a line even in a pass: pluginval's own failure lines, NaN/Inf, denormals.
		odd="$(grep -c -i -E 'nan|[^a-z]inf[^a-z]|denormal|FAIL|ERROR' "${log}" || true)"
		echo "${name} s${level}: ${result} in ${took} s (${odd} log lines mention nan/inf/denormal/fail/error)"
		rows+=("| ${name} | ${level} | ${result} | ${took} s |")
		if [[ "${result}" != PASS ]]; then
			status=1
			echo "::error::pluginval failed on ${name} at strictness ${level}: ${result}"
		fi
	done
done

{
	echo "### pluginval ${PLUGINVAL_VERSION}${label:+ - ${label}}"
	echo
	echo "| Plug-in | Strictness | Result | Time |"
	echo "|---|---|---|---|"
	printf '%s\n' "${rows[@]}"
	echo
} > "${out}/pluginval-summary.md"
cat "${out}/pluginval-summary.md"
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "${out}/pluginval-summary.md" >> "${GITHUB_STEP_SUMMARY}"
exit "${status}"
