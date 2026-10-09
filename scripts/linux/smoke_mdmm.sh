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
#   - real keys reach the page: a real click on the page, then Ctrl+C Ctrl+V Ctrl+X Ctrl+Z Ctrl+D and ? (xdotool,
#     XTEST), and the page's key probe (GEARMULATOR_MDMM_KEYPROBE=1) lists them;
#   - the page's zoom: it starts at 125 % (the editor's config, seeded) and Ctrl+= makes it 150 %; webkit2gtk has no
#     page zoom, so the plug-in sends it as a message (a batch file), and the web process must live through it (a
#     javascript: URL for it crashed the web process next to the bridge's iframes until the codex review of 2026-10);
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
# The page's key probe (skins/shared/deskKeys.js): the keys that reached it, as a text AT-SPI reads.
export GEARMULATOR_MDMM_KEYPROBE=1
keys_wanted=(cmd+C cmd+V cmd+X cmd+Z cmd+D "shift+?")
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
	# the page's zoom at 125 % from the start (PageEditor's pageZoom, the editor's config in the data folder)
	mkdir -p "${home}/data/Gearmulator Preview/${machine}/config"
	printf '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n  <VALUE name="pageZoom" val="1.25"/>\n</PROPERTIES>\n' \
		> "${home}/data/Gearmulator Preview/${machine}/config/${product_name}.xml"
	local events_before
	events_before="$(wc -l < "${out}/temp-events.txt")"
	HOME="${home}" XDG_CONFIG_HOME="${home}/.config" GEARMULATOR_DATA_ROOT="${home}/data/" \
		"$@" > "${out}/${name}-stdout.txt" 2> "${out}/${name}-stderr.txt" &
	local pid=$!
	local found=0 alive=1 waited=0
	: > "${out}/${name}-ui.txt"
	local crashes_before
	crashes_before="$(sudo dmesg 2>/dev/null | grep -c -E 'segfault|trap' || true)"
	while (( waited < timeout_seconds )); do
		sleep 3
		waited=$((waited + 3))
		if ! kill -0 "${pid}" 2>/dev/null; then alive=0; break; fi
		# Read the page only once it runs (the plug-in deleted an answer file it read): a reader walking WebKit's
		# tree while the page loads is not what this tests.
		tail -n "+$((events_before + 1))" "${out}/temp-events.txt" \
			| grep -q -E '^DELETE gearmulator-.*\.recv-[0-9]+\.js$' || continue
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

	local crashes
	crashes="$(sudo dmesg 2>/dev/null | grep -E 'segfault|trap' | tail -n "+$((crashes_before + 1))" || true)"
	if [[ -n "${crashes}" ]]; then
		echo "-- crashed during this run (kernel log):"
		echo "${crashes}"
		echo "${crashes}" > "${out}/${name}-crashes.txt"
		record "${label}" "crashes (kernel log)" "$(echo "${crashes}" | grep -o -E '[A-Za-z]+\[[0-9]+\]: segfault' | head -n 3 | tr '\n' ' ')"
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
	# Real input (0.3.3 lost Cmd+C / Cmd+V on macOS; this proves Ctrl+C and the rest reach the page here too): a real
	# click (XTEST) at the page's key probe, the bottom-left corner of the window (skins/shared/deskKeys.js), then real
	# key presses; the probe, read through AT-SPI, lists the keys that reached the page.
	if (( found )); then
		local win="" WIDTH=0 HEIGHT=0 X=0 Y=0 best=0
		for w in $(xdotool search --onlyvisible --pid "${pid}" 2>/dev/null); do
			eval "$(xdotool getwindowgeometry --shell "${w}" 2>/dev/null | grep -E '^(WIDTH|HEIGHT|X|Y)=')"
			if (( WIDTH * HEIGHT > best )); then best=$((WIDTH * HEIGHT)); win="${w}"; fi
		done
		local seen="no window of the app found" missing=() attempt=0
		if [[ -n "${win}" ]]; then
			eval "$(xdotool getwindowgeometry --shell "${win}" | grep -E '^(WIDTH|HEIGHT|X|Y)=')"
			# Up to three tries: with no window manager the keyboard focus reaches the page's web view (a GtkPlug
			# in WebKit's own process, XEmbed) only after the click's focus request went round the embedder; on
			# 22.04 the first keys sometimes left before it had (the probe then said "none"; twice on 2026-10-08).
			# The probe keeps every key it saw, so a later try completes the list; never more than three.
			while (( attempt < 3 )); do
				attempt=$((attempt + 1))
				xdotool windowraise "${win}" windowfocus --sync "${win}" 2>/dev/null || true
				xdotool mousemove --sync $((X + 15)) $((Y + HEIGHT - 10)) click 1
				sleep $((attempt + 1))
				xdotool key --delay 400 ctrl+c ctrl+v ctrl+x ctrl+z ctrl+d shift+slash
				sleep 1
				seen="$(timeout 20 python3 "${script_dir}/ui_texts.py" 2>/dev/null | grep -o -E 'Keys seen:.*' | head -n 1)"
				missing=()
				for k in "${keys_wanted[@]}"; do [[ "${seen}" == *" ${k}"* ]] || missing+=("${k}"); done
				(( ${#missing[@]} == 0 )) && break
				echo "${label}: try ${attempt}: ${seen:-no key probe text}"
			done
		else
			missing=("${keys_wanted[@]}")
		fi
		echo "${seen}" > "${out}/${name}-keys.txt"
		if (( ${#missing[@]} == 0 )); then
			echo "${label}: real keys reach the page: ${seen} (try ${attempt})"
			record "${label}" "real keys reach the page (${keys_wanted[*]})" "yes$( (( attempt > 1 )) && echo " (try ${attempt})")"
		else
			fail "${label}: real keys did not reach the page: ${missing[*]} missing (${seen})"
			record "${label}" "real keys reach the page (${keys_wanted[*]})" "no: ${missing[*]} missing"
		fi
		# The zoom (started at 125 %): Ctrl+= makes it 150 %, sent to the page as a message. The same web process must
		# run afterwards, and the bridge still deliver (the page read and deleted a batch file after the key).
		local web_before web_after read_before read_after
		web_before="$(pgrep -f WebKitWebProcess | sort | tr '\n' ' ')"
		read_before="$(grep -c -E '^DELETE gearmulator-.*\.recv-[0-9]+\.js$' "${out}/temp-events.txt" || true)"
		xdotool key --delay 400 ctrl+equal
		sleep 4
		web_after="$(pgrep -f WebKitWebProcess | sort | tr '\n' ' ')"
		read_after="$(grep -c -E '^DELETE gearmulator-.*\.recv-[0-9]+\.js$' "${out}/temp-events.txt" || true)"
		import -display "${display}" -window root "${out}/${name}-zoom.png" 2>/dev/null || true
		if kill -0 "${pid}" 2>/dev/null && [[ -n "${web_after}" && "${web_after}" == "${web_before}" ]] && (( read_after > read_before )); then
			echo "${label}: zoom 125 % -> 150 %: the web process lives on (${web_after}), the bridge delivers"
			record "${label}" "page zoom by message (Ctrl+=)" "yes"
		else
			fail "${label}: after Ctrl+= (page zoom) the web process changed or the bridge stopped (before: ${web_before:-none}, after: ${web_after:-none}; batches read ${read_before} -> ${read_after})"
			record "${label}" "page zoom by message (Ctrl+=)" "no"
		fi
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
	product_name="${product}"	# the editor's config file (run seeds its zoom)
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
