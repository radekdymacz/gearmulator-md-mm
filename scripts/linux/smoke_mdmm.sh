#!/bin/bash
# Starts each packaged Linux standalone (scripts/linux/build_mdmm.sh's archives) on a virtual X11 display with a
# fresh home, no ROM and no audio device, and checks what can be checked without a person:
#   - the app is still running after a while;
#   - its web view runs: JUCE's GTK child (the app started again with --juce-gtkwebkitfork-child) and WebKit's
#     own WebKitWebProcess exist, so the shims found a webkit2gtk and the page is being loaded;
#   - a screenshot of the display (kept as an artifact, to look at) and its number of distinct colours.
#
#   scripts/linux/smoke_mdmm.sh <dir with the .tar.gz files> <output dir>
set -euo pipefail

archives="$(realpath "$1")"
mkdir -p "$2"
out="$(realpath "$2")"
mkdir -p "${out}"
display=":97"
Xvfb "${display}" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
trap 'kill ${xvfb_pid} 2>/dev/null || true' EXIT
sleep 2
export DISPLAY="${display}"

status=0
for tarball in "${archives}"/*-Linux-x64-not-tested.tar.gz; do
	name="$(basename "${tarball}" .tar.gz)"
	work="$(mktemp -d)"
	tar -C "${work}" -xzf "${tarball}"
	app="$(find "${work}" -path '*/Standalone/*' -type f -name 'Gearmulator *' ! -name '*.so' -print -quit)"
	echo "== ${name}: ${app}"
	home="${work}/home"
	mkdir -p "${home}"
	HOME="${home}" XDG_CONFIG_HOME="${home}/.config" "${app}" > "${out}/${name}.log" 2>&1 &
	pid=$!
	# Every 2 s for 40 s: is WebKit's web process there (when it appears, and whether it goes away).
	: > "${out}/${name}-webprocess.txt"
	for second in $(seq 2 2 40); do
		sleep 2
		printf '%3ss %s\n' "${second}" "$(pgrep -f WebKitWebProcess | tr '\n' ' ')" >> "${out}/${name}-webprocess.txt"
	done
	echo "-- WebKitWebProcess pids, every 2 s"
	cat "${out}/${name}-webprocess.txt"
	echo "-- kernel log (a crashed process shows here)"
	sudo dmesg 2>/dev/null | grep -i -E 'segfault|trap|killed|oom|WebKit' | tail -n 20 || true
	if ! kill -0 "${pid}" 2>/dev/null; then
		echo "::error::${name}: the standalone exited"
		cat "${out}/${name}.log"
		status=1
		continue
	fi
	ps -eo pid,ppid,args --forest > "${out}/${name}-processes.txt"
	xwininfo -root -tree > "${out}/${name}-windows.txt" 2>&1 || true
	import -display "${display}" -window root "${out}/${name}.png" || true
	colours="$(convert "${out}/${name}.png" -format '%k' info: 2>/dev/null || echo 0)"
	echo "${name}: ${colours} distinct colours on screen"
	if grep -q -- '--juce-gtkwebkitfork-child' "${out}/${name}-processes.txt"; then
		echo "${name}: JUCE's GTK web view child is running"
	else
		echo "::error::${name}: no JUCE web view child process (--juce-gtkwebkitfork-child)"
		status=1
	fi
	if grep -q 'WebKitWebProcess' "${out}/${name}-processes.txt"; then
		echo "${name}: WebKitWebProcess is running (webkit2gtk found through the shim)"
	else
		echo "::error::${name}: no WebKitWebProcess: the web view did not start"
		status=1
	fi
	grep -E -i 'webkit|gtk' "${out}/${name}-processes.txt" || true
	# The bridge's plug-in -> page files (mdPageBridge.h): the page reads them and says so, the plug-in deletes them,
	# so only the last few are left. Many left means the page stopped reading.
	left="$(find /tmp /var/tmp -maxdepth 1 -name 'gearmulator-*.html.recv-*.js' 2>/dev/null | wc -l)"
	pages="$(find /tmp /var/tmp -maxdepth 1 -name 'gearmulator-*.html' 2>/dev/null | wc -l)"
	echo "${name}: ${pages} page file(s) in the temp folder"
	echo "${name}: ${left} unread bridge file(s) in the temp folder"
	if [[ "${left}" -gt 200 ]]; then
		echo "::error::${name}: ${left} bridge files left: the page does not read them"
		status=1
	fi
	kill "${pid}" 2>/dev/null || true
	sleep 2
	kill -9 "${pid}" 2>/dev/null || true
	pkill -9 -f -- '--juce-gtkwebkitfork-child' 2>/dev/null || true
	echo "-- app output (tail)"
	tail -n 40 "${out}/${name}.log" || true
done
exit "${status}"
