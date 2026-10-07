#!/bin/bash
# The Linux start test (doc/modern-ux/FOUNDATION.md, "CI start tests"; mdmm-editors-linux.yml). Starts each packaged
# standalone (scripts/linux/build_mdmm.sh's archives), and each VST3 in a minimal host (scripts/vst3EditorHost), on
# a virtual X11 display with a fresh home and data root, no ROM and no audio device, and checks what can be checked
# without a person:
#   - the app is still running after a while;
#   - its page runs in WebKit: WebKit's own WebKitWebProcess exists, so the shims found a webkit2gtk;
#   - the bridge went both ways: the plug-in wrote its answers as script files beside the page file and deleted
#     them, which it does only when the page says it read them (mdPageBridge.h; watched with inotify);
#   - the page shows "<machine> firmware needed": read through AT-SPI (ui_texts.py), as a screen reader would;
#   - a screenshot of the display (md-standalone.png, md-vst3.png, ...), kept as an artifact.
#
#   scripts/linux/smoke_mdmm.sh <dir with the .tar.gz files> <output dir> [<mdmmVst3EditorHost binary>]
# Runs itself again inside dbus-run-session when there is no session bus (AT-SPI needs one).
set -uo pipefail

if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
	exec dbus-run-session -- "$0" "$@"
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../mdmm-product.env
. "${script_dir}/../mdmm-product.env"
archives="$(realpath "$1")"
mkdir -p "$2"
out="$(realpath "$2")"
vst3_host="${3:-}"
[[ -n "${vst3_host}" ]] && vst3_host="$(realpath "${vst3_host}")"
timeout_seconds="${MDMM_SMOKE_TIMEOUT:-90}"

display=":97"
Xvfb "${display}" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
watch_pid=""
work_dirs=()
cleanup() {
	[[ -n "${watch_pid}" ]] && kill "${watch_pid}" 2>/dev/null
	kill "${xvfb_pid}" 2>/dev/null
	rm -rf ${work_dirs[@]+"${work_dirs[@]}"}
}
trap cleanup EXIT
sleep 2
export DISPLAY="${display}"

# The accessibility bus, switched on as a desktop with a screen reader does; WebKit then serves the page's tree.
unset NO_AT_BRIDGE
launcher="$(ls /usr/libexec/at-spi-bus-launcher /usr/lib/at-spi2-core/at-spi-bus-launcher 2>/dev/null | head -n 1)"
if [[ -n "${launcher}" ]]; then
	"${launcher}" --launch-immediately >/dev/null 2>&1 &
	sleep 1
fi
gsettings set org.gnome.desktop.interface toolkit-accessibility true 2>/dev/null || true
gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
	--method org.freedesktop.DBus.Properties.Set org.a11y.Status IsEnabled '<true>' >/dev/null 2>&1 \
	|| echo "::warning::could not switch the accessibility bus on (org.a11y.Status IsEnabled)"

status=0
summary=()
fail() { echo "::error::$1"; status=1; }
record() { summary+=("| $1 | $2 | $3 |"); }

# Every file written and deleted in the temp folders (JUCE's is /var/tmp, else /tmp): the bridge's script files
# (<page>.recv-<seq>.js, written whole and renamed into place) come and go there.
inotifywait -q -m -e create -e moved_to -e delete --format '%e %f' /tmp /var/tmp > "${out}/temp-events.txt" 2>/dev/null &
watch_pid=$!
sleep 1

#   run <name> <label> <machine> <home> <program> [args...]
run() {
	local name="$1" label="$2" machine="$3" home="$4"
	shift 4
	local wanted="${machine} firmware needed"
	echo "== ${label}: $*"
	local events_before
	events_before="$(wc -l < "${out}/temp-events.txt")"
	HOME="${home}" XDG_CONFIG_HOME="${home}/.config" GEARMULATOR_DATA_ROOT="${home}/data/" \
		"$@" > "${out}/${name}-stdout.txt" 2> "${out}/${name}-stderr.txt" &
	local pid=$!
	local found=0 alive=1 waited=0
	: > "${out}/${name}-ui.txt"
	while (( waited < timeout_seconds )); do
		sleep 3
		waited=$((waited + 3))
		if ! kill -0 "${pid}" 2>/dev/null; then alive=0; break; fi
		timeout 20 python3 "${script_dir}/ui_texts.py" > "${out}/${name}-ui.txt" 2>"${out}/${name}-ui-errors.txt" || true
		# Case-blind: the card shows the text in capitals (CSS), and that is what the accessibility tree says.
		if grep -q -i -F "${wanted}" "${out}/${name}-ui.txt"; then found=1; break; fi
	done
	sleep 3
	kill -0 "${pid}" 2>/dev/null || alive=0
	ps -eo pid,ppid,args --forest > "${out}/${name}-processes.txt"
	import -display "${display}" -window root "${out}/${name}.png" 2>/dev/null || true
	tail -n "+$((events_before + 1))" "${out}/temp-events.txt" > "${out}/${name}-temp-events.txt"
	local written read_back
	written="$(grep -c -E '^(CREATE|MOVED_TO) gearmulator-.*\.recv-[0-9]+\.js$' "${out}/${name}-temp-events.txt" || true)"
	read_back="$(grep -c -E '^DELETE gearmulator-.*\.recv-[0-9]+\.js$' "${out}/${name}-temp-events.txt" || true)"

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
	if grep -q 'WebKitWebProcess' "${out}/${name}-processes.txt"; then
		echo "${label}: WebKitWebProcess is running (webkit2gtk found through the shim)"
		record "${label}" "WebKit page process" "yes"
	else
		fail "${label}: no WebKitWebProcess: the web view did not start"
		record "${label}" "WebKit page process" "no"
	fi
	if (( written > 0 && read_back > 0 )); then
		echo "${label}: the bridge went both ways (${written} answer file(s) written, ${read_back} read by the page and deleted)"
		record "${label}" "bridge round trip" "yes (${written} written, ${read_back} read)"
	else
		fail "${label}: no bridge round trip (${written} answer file(s) written, ${read_back} read by the page)"
		record "${label}" "bridge round trip" "no (${written} written, ${read_back} read)"
	fi
	if (( found )); then
		echo "${label}: the page shows '${wanted}' after ${waited} s"
		record "${label}" "page shows \"${wanted}\"" "yes (${waited} s)"
	else
		fail "${label}: the page never showed '${wanted}' within ${timeout_seconds} s (read through AT-SPI)"
		echo "-- texts in the accessibility tree (first 80)"
		head -n 80 "${out}/${name}-ui.txt" | sed 's/^/   /'
		head -n 20 "${out}/${name}-ui-errors.txt" | sed 's/^/   /'
		record "${label}" "page shows \"${wanted}\"" "no"
	fi
	kill "${pid}" 2>/dev/null || true
	sleep 2
	kill -9 "${pid}" 2>/dev/null || true
	wait "${pid}" 2>/dev/null || true
	pkill -9 -f -- '--juce-gtkwebkitfork-child' 2>/dev/null || true
	pkill -9 -f WebKitWebProcess 2>/dev/null || true
	echo "-- output (tail)"
	tail -n 20 "${out}/${name}-stdout.txt" "${out}/${name}-stderr.txt" || true
}

for entry in "md|Machinedrum|${MDMM_PRODUCT_NAME_MD}|Machinedrum-Editor" \
             "mm|Monomachine|${MDMM_PRODUCT_NAME_MM}|Monomachine-Editor"; do
	IFS='|' read -r short machine product asset <<< "${entry}"
	tarball="${archives}/${asset}-Linux-x64-not-tested.tar.gz"
	if [[ ! -f "${tarball}" ]]; then
		fail "missing ${tarball}"
		continue
	fi
	work="$(mktemp -d)"
	work_dirs+=("${work}")
	tar -C "${work}" -xzf "${tarball}"
	root="${work}/${asset}-Linux-x64"
	mkdir -p "${work}/home-standalone" "${work}/home-vst3"
	run "${short}-standalone" "${product} standalone" "${machine}" "${work}/home-standalone" \
		"${root}/Standalone/${product}"
	if [[ -n "${vst3_host}" ]]; then
		run "${short}-vst3" "${product} VST3 in mdmmVst3EditorHost" "${machine}" "${work}/home-vst3" \
			"${vst3_host}" "${root}/VST3/${product}.vst3" "$((timeout_seconds + 30))"
	fi
done
echo "-- kernel log (a crashed process shows here)"
sudo dmesg 2>/dev/null | grep -i -E 'segfault|trap|killed|oom|WebKit' | tail -n 20 || true

{
	echo "### Linux start test ($(. /etc/os-release && echo "${PRETTY_NAME}"))"
	echo
	echo "| What | Check | Result |"
	echo "|---|---|---|"
	printf '%s\n' "${summary[@]}"
} > "${out}/summary.md"
cat "${out}/summary.md"
[[ -n "${GITHUB_STEP_SUMMARY:-}" ]] && cat "${out}/summary.md" >> "${GITHUB_STEP_SUMMARY}"
exit "${status}"
