# shellcheck shell=bash
# The Mac stays silent while the gate runs (doc/release/LOCAL-GATE.md, "Silence"). Sourced by scripts/local-gate/lib.sh and
# by updater-manual.sh, never run. bash 3.2.
#
# Only a stage that opens a real audio device can make a sound: the standalone of the user journeys and the standalone of
# the manual updater step. Those get (1) a quiet setup where there is one (the journeys run with --background, which
# zeroes the standalone's output after the machine made it, and never open an input) and (2) the system output muted for
# the stage, whatever the setup. Every other stage must not open a device at all; lib.sh's audio guard checks that.
#
# output_mute <label>   mutes the output, remembering the muted state and the volume; the state goes to a file as well, so a gate
#                       that was killed with no chance to restore it is put right by the next one (or by `output_restore`)
# output_restore        puts the muted state and the volume back; safe to call twice and from a trap
#
# GATE_AUDIO_STATE names the state file (default temp/local-gate/audio-state.txt); `osascript` is found on the PATH.

GATE_AUDIO_STATE="${GATE_AUDIO_STATE:-${GATE_HOME:-${ROOT:-.}/temp/local-gate}/audio-state.txt}"
MUTE_ACTIVE=0
MUTE_NOTE=""

volume_field() {	# <field>: "output volume" or "output muted" out of `get volume settings`
	osascript -e 'get volume settings' 2> /dev/null | tr ',' '\n' | sed -n "s/^ *$1:\\(.*\\)\$/\\1/p" | head -n 1
}

state_value() { sed -n "s/^$1=//p" "${GATE_AUDIO_STATE}" 2> /dev/null | head -n 1; }

apply_saved_state() {	# the file's state back on the output, then the file removed
	local muted volume changed
	muted="$(state_value muted)"; volume="$(state_value volume)"; changed="$(state_value changed_volume)"
	[ "${changed}" = 1 ] && [ -n "${volume}" ] && osascript -e "set volume output volume ${volume}" > /dev/null 2>&1
	case "${muted}" in
		false) osascript -e 'set volume output muted false' > /dev/null 2>&1 ;;
		true) osascript -e 'set volume output muted true' > /dev/null 2>&1 ;;
	esac
	rm -f "${GATE_AUDIO_STATE}"
	echo "    output restored (muted ${muted:-unknown}, volume ${volume:-unknown})"
}

output_restore() {
	[ "${MUTE_ACTIVE}" = 1 ] || return 0
	MUTE_ACTIVE=0
	[ -f "${GATE_AUDIO_STATE}" ] && apply_saved_state
	return 0
}

output_restore_stale() {	# a state file of a gate that is gone: put its output right before doing anything else
	local owner
	[ -f "${GATE_AUDIO_STATE}" ] || return 0
	owner="$(state_value owner)"
	if [ -n "${owner}" ] && kill -0 "${owner}" 2> /dev/null; then
		return 1	# another gate is running and has the output muted
	fi
	echo "    !!! an earlier gate (pid ${owner:-?}) died with the output muted: restoring it from ${GATE_AUDIO_STATE}"
	apply_saved_state
	return 0
}

output_mute() {	# <label>
	local volume muted
	[ "${MUTE_ACTIVE}" = 1 ] && return 0
	MUTE_NOTE=""
	if ! command -v osascript > /dev/null 2>&1; then
		MUTE_NOTE="the output could NOT be muted (no osascript)"
		return 1
	fi
	output_restore_stale || { MUTE_NOTE="the output could NOT be muted (another gate has it muted)"; return 1; }
	volume="$(volume_field 'output volume')"
	muted="$(volume_field 'output muted')"
	if [ -z "${volume}${muted}" ]; then
		MUTE_NOTE="the output could NOT be muted (the volume settings cannot be read)"
		return 1
	fi
	mkdir -p "$(dirname "${GATE_AUDIO_STATE}")"
	printf 'owner=%s\nmuted=%s\nvolume=%s\nchanged_volume=0\n' "$$" "${muted}" "${volume}" > "${GATE_AUDIO_STATE}"
	MUTE_ACTIVE=1
	osascript -e 'set volume output muted true' > /dev/null 2>&1
	if [ "$(volume_field 'output muted')" != true ]; then
		# an output with no mute control (some interfaces, HDMI): turn it down instead, and say so in the state
		sed -i.bak 's/^changed_volume=0$/changed_volume=1/' "${GATE_AUDIO_STATE}"; rm -f "${GATE_AUDIO_STATE}.bak"
		osascript -e 'set volume output volume 0' > /dev/null 2>&1
		if [ "$(volume_field 'output volume')" != 0 ]; then
			MUTE_NOTE="the output could NOT be muted or turned down"
			output_restore
			return 1
		fi
		MUTE_NOTE="output turned down to 0 for stage $1 (no mute control; was muted ${muted}, volume ${volume})"
	else
		MUTE_NOTE="output muted for stage $1 (was muted ${muted}, volume ${volume})"
	fi
	echo "    ${MUTE_NOTE}"
	return 0
}
