#!/bin/bash
# The macOS start test (doc/modern-ux/FOUNDATION.md, "CI start tests"; mdmm-editors-macos.yml). Starts each
# standalone of the universal package by path, and each VST3 in a minimal host (scripts/vst3EditorHost), with no
# ROM and a scratch data root (GEARMULATOR_DATA_ROOT), and checks what can be checked without a person:
#   - the app is still running after a while;
#   - its page runs in WebKit (a com.apple.WebKit.WebContent process is up);
#   - the bridge went both ways: the page said "ready" (page -> plug-in), the plug-in answered with the machine's
#     state (plug-in -> page), and the page shows "<machine> firmware needed". The text is read through the
#     Accessibility API (ui_probe.swift), so the shipped build needs no log;
#   - auval validates each AU (installed into ~/Library/Audio/Plug-Ins/Components for the run, removed after);
#   - a screenshot of each window (md-standalone.png, md-vst3.png, ...), kept as an artifact.
# Needs the Accessibility permission for the shell (GitHub's macOS runners have it).
#
#   scripts/macos/smoke_mdmm.sh <unpacked package dir> <output dir> [<mdmmVst3EditorHost binary>]
set -uo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../mdmm-product.env
. "${script_dir}/../mdmm-product.env"
package="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
vst3_host="${3:-}"
timeout_seconds="${MDMM_SMOKE_TIMEOUT:-90}"

scratch="$(mktemp -d)"
export GEARMULATOR_DATA_ROOT="${scratch}/data/"
mkdir -p "${GEARMULATOR_DATA_ROOT}"
components="${HOME}/Library/Audio/Plug-Ins/Components"
installed_components=()
cleanup() {
	for c in ${installed_components[@]+"${installed_components[@]}"}; do rm -rf "${c}"; done
	rm -rf "${scratch}"
}
trap cleanup EXIT

probe="${scratch}/ui_probe"
if ! swiftc -O -o "${probe}" "${script_dir}/ui_probe.swift"; then
	echo "::error::could not build ui_probe.swift"
	exit 1
fi

status=0
summary=()	# "| check | result |" rows
fail() { echo "::error::$1"; status=1; }
record() { summary+=("| $1 | $2 | $3 |"); }

if ! "${probe}" trusted; then
	fail "this shell may not use the Accessibility API (System Settings > Privacy & Security > Accessibility): the page text cannot be read"
fi
sw_vers
echo "data root: ${GEARMULATOR_DATA_ROOT}"

# One app or host: start, wait for the firmware card, screenshot, stop.
#   run <name> <label> <machine> <program> [args...]
run() {
	local name="$1" label="$2" machine="$3"
	shift 3
	local wanted="${machine} firmware needed"
	echo "== ${label}: $*"
	"$@" > "${out}/${name}-stdout.txt" 2> "${out}/${name}-stderr.txt" &
	local pid=$!
	local found=0 alive=1 waited=0
	while (( waited < timeout_seconds )); do
		sleep 3
		waited=$((waited + 3))
		if ! kill -0 "${pid}" 2>/dev/null; then alive=0; break; fi
		"${probe}" texts "${pid}" > "${out}/${name}-ui.txt" 2>/dev/null || true
		# Case-blind: the card shows the text in capitals (CSS), and that is what the accessibility tree says.
		if grep -q -i -F "${wanted}" "${out}/${name}-ui.txt"; then found=1; break; fi
	done
	sleep 2
	kill -0 "${pid}" 2>/dev/null || alive=0
	local webcontent
	webcontent="$(pgrep -f 'com.apple.WebKit.WebContent' | wc -l | tr -d ' ')"
	ps -axo pid,ppid,etime,args | grep -E "WebKit|${pid}" | grep -v grep > "${out}/${name}-processes.txt" || true
	local window
	window="$("${probe}" window "${pid}" 2>/dev/null || true)"
	if [[ -n "${window}" ]]; then
		screencapture -x -o -l"${window}" "${out}/${name}.png" || screencapture -x "${out}/${name}.png" || true
	else
		screencapture -x "${out}/${name}.png" || true
	fi

	if (( ! alive )); then
		local code=0
		wait "${pid}" 2>/dev/null || code=$?
		fail "${label} exited (code ${code})"
		tail -n 40 "${out}/${name}-stderr.txt" "${out}/${name}-stdout.txt" || true
		record "${label}" "running" "no: exited"
		return
	fi
	echo "${label}: running"
	record "${label}" "running" "yes"
	if (( webcontent > 0 )); then
		echo "${label}: WebKit is running (${webcontent} WebContent process(es))"
		record "${label}" "WebKit page process" "yes (${webcontent})"
	else
		fail "${label}: no com.apple.WebKit.WebContent process: the page did not start"
		record "${label}" "WebKit page process" "no"
	fi
	if (( found )); then
		echo "${label}: the page shows '${wanted}' (page -> plug-in -> page) after ${waited} s"
		record "${label}" "page shows \"${wanted}\"" "yes (${waited} s)"
	else
		fail "${label}: the page never showed '${wanted}' within ${timeout_seconds} s"
		echo "-- texts in the accessibility tree (first 80)"
		head -n 80 "${out}/${name}-ui.txt" | sed 's/^/   /'
		record "${label}" "page shows \"${wanted}\"" "no"
	fi
	kill "${pid}" 2>/dev/null || true
	sleep 2
	kill -9 "${pid}" 2>/dev/null || true
	wait "${pid}" 2>/dev/null || true
}

machines=("md|Machinedrum|${MDMM_PRODUCT_NAME_MD}" "mm|Monomachine|${MDMM_PRODUCT_NAME_MM}")
for entry in "${machines[@]}"; do
	IFS='|' read -r short machine product <<< "${entry}"
	app="${package}/${product}.app"
	if [[ -d "${app}" ]]; then
		run "${short}-standalone" "${product} standalone" "${machine}" "${app}/Contents/MacOS/${product}"
	else
		fail "not in the package: ${product}.app"
	fi
	if [[ -n "${vst3_host}" ]]; then
		vst3="${package}/${product}.vst3"
		if [[ -d "${vst3}" ]]; then
			run "${short}-vst3" "${product} VST3 in mdmmVst3EditorHost" "${machine}" \
				"${vst3_host}" "${vst3}" "$((timeout_seconds + 30))"
		else
			fail "not in the package: ${product}.vst3"
		fi
	fi
done

# AU: auval finds components only where macOS registers them, so install them for the run.
mkdir -p "${components}"
for entry in "${machines[@]}"; do
	IFS='|' read -r short machine product <<< "${entry}"
	component="${package}/${product}.component"
	if [[ ! -d "${component}" ]]; then
		fail "not in the package: ${product}.component"
		continue
	fi
	target="${components}/${product}.component"
	if [[ -e "${target}" ]]; then
		fail "${target} exists already; not replacing it"
		continue
	fi
	ditto "${component}" "${target}"
	installed_components+=("${target}")
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
for entry in "${machines[@]}"; do
	IFS='|' read -r short machine product <<< "${entry}"
	plist="${package}/${product}.component/Contents/Info.plist"
	[[ -f "${plist}" ]] || continue
	type="$(plutil -extract AudioComponents.0.type raw -o - "${plist}")"
	subtype="$(plutil -extract AudioComponents.0.subtype raw -o - "${plist}")"
	manufacturer="$(plutil -extract AudioComponents.0.manufacturer raw -o - "${plist}")"
	echo "== auval -v ${type} ${subtype} ${manufacturer} (${product})"
	if auval -v "${type}" "${subtype}" "${manufacturer}" > "${out}/${short}-auval.txt" 2>&1; then
		echo "${product} AU: auval passed"
		record "${product} AU" "auval -v ${type} ${subtype} ${manufacturer}" "passed"
	else
		fail "${product} AU: auval failed"
		tail -n 60 "${out}/${short}-auval.txt"
		record "${product} AU" "auval -v ${type} ${subtype} ${manufacturer}" "FAILED"
	fi
done

find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'gearmulator-*' 2>/dev/null | while read -r f; do echo "temp: ${f}"; done

{
	echo "### macOS start test ($(sw_vers -productVersion), $(uname -m))"
	echo
	echo "| What | Check | Result |"
	echo "|---|---|---|"
	printf '%s\n' "${summary[@]}"
} > "${out}/summary.md"
cat "${out}/summary.md"
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "${out}/summary.md" >> "${GITHUB_STEP_SUMMARY}"
exit "${status}"
