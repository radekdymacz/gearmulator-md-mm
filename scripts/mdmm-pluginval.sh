#!/bin/bash
# pluginval (Tracktion) on the locally built Machinedrum Editor and Monomachine Editor, VST3 and AU, with the
# firmware present, at strictness 5 and 8 (and 10 with --strict). CI runs the same tool without a ROM
# (scripts/ci/mdmm_pluginval.sh; the pinned release is in scripts/pluginval.env).
#
#   scripts/mdmm-pluginval.sh [--strict] [--gui] [--products <dir>] [--out <dir>] [--no-au] [--au-installed] [md|mm|both]
#
#   --strict     also run strictness 10 (long: fuzzing)
#   --gui        let pluginval open the editor windows. By default --skip-gui-tests is passed, so no window
#                appears and no focus is taken; the editor tests are then NOT exercised
#   --products   the folder holding VST3/ and AU/ of a build (default: $MDMM_PRODUCTS, then
#                build/macos-mdmm-universal/products/Release, then build/macos-mdmm/products/Release)
#   --out        where the logs go (default: build/pluginval-<date>)
#   --au-installed  macOS finds an AU in its registry (installed components), not by path: the AU of the build is
#                validated only if the installed one is byte-identical; with this flag the installed one is
#                validated anyway (and said so). Nothing is ever installed by this script
#
# The ROM is copied (read-only source) into a scratch data root (GEARMULATOR_DATA_ROOT), from
# GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN or ~/Documents/Gearmulator Preview/<machine>/roms; it is
# removed afterwards and nothing is written to your own Gearmulator folders. As a guard, the editors' config files
# and standalone settings are checksummed before and after and restored byte for byte if anything touched them.
# Nothing is installed in ~/Library/Audio/Plug-Ins: pluginval takes the bundle path.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
. "${here}/mdmm-product.env"
root="$(cd "${here}/.." && pwd)"

strict=0; gui=0; au_installed=0; products="${MDMM_PRODUCTS:-}"; out=""; au=1; which="both"
while (( $# )); do
	case "$1" in
		--strict) strict=1; shift ;;
		--gui) gui=1; shift ;;
		--products) products="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--no-au) au=0; shift ;;
		--au-installed) au_installed=1; shift ;;
		md|mm|both) which="$1"; shift ;;
		*) echo "usage: $0 [--strict] [--gui] [--products <dir>] [--out <dir>] [--no-au] [--au-installed] [md|mm|both]" >&2; exit 2 ;;
	esac
done
if [[ -z "${products}" ]]; then
	for candidate in "${root}/build/macos-mdmm-universal/products/Release" "${root}/build/macos-mdmm/products/Release"; do
		[[ -d "${candidate}/VST3" ]] && { products="${candidate}"; break; }
	done
fi
[[ -d "${products}/VST3" ]] || { echo "no build: give --products <dir with VST3/ and AU/> (see scripts/macos/build_mdmm.sh)" >&2; exit 2; }
[[ -n "${out}" ]] || out="${root}/build/pluginval-$(date +%Y%m%d-%H%M%S)"

levels="5 8"
(( strict )) && levels="5 8 10"

tmp="$(mktemp -d)"
data="${tmp}/data"
mkdir -p "${data}"

# The owner's files that must not change, kept for a byte-for-byte restore.
guarded=()
for f in "${HOME}/Documents/Gearmulator Preview/Machinedrum/config/"*.xml \
		"${HOME}/Documents/Gearmulator Preview/Monomachine/config/"*.xml \
		"${HOME}/Library/Application Support/${MDMM_PRODUCT_NAME_MD%% *} Editor.settings" \
		"${HOME}/Library/Application Support/${MDMM_PRODUCT_NAME_MM%% *} Editor.settings"; do
	[[ -f "${f}" ]] && guarded+=("${f}")
done
mkdir -p "${tmp}/guard"
i=0
for f in ${guarded[@]+"${guarded[@]}"}; do
	cp -p "${f}" "${tmp}/guard/${i}"; i=$((i + 1))
done
restore_guarded() {
	local n=0 changed=0
	for f in ${guarded[@]+"${guarded[@]}"}; do
		if ! cmp -s "${f}" "${tmp}/guard/${n}"; then
			echo "WARNING: ${f} changed during the run: restored"
			cp -p "${tmp}/guard/${n}" "${f}"; changed=1
		fi
		n=$((n + 1))
	done
	(( changed )) || echo "your config and settings files: unchanged (${#guarded[@]} checked)"
}
cleanup() { restore_guarded; rm -rf "${tmp}"; }
trap cleanup EXIT
trap 'exit 130' INT TERM

plugins=()
add_machine() {	# <short> <Machine> <product> <firmware env var value>
	local short="$1" machine="$2" product="$3" rom="$4"
	local roms="${data}/Gearmulator Preview/${machine}/roms"
	mkdir -p "${roms}"
	if [[ -z "${rom}" ]]; then
		rom="$(ls "${HOME}/Documents/Gearmulator Preview/${machine}/roms/"*.bin 2>/dev/null | head -n 1)"
	fi
	if [[ -f "${rom}" ]]; then
		cp "${rom}" "${roms}/"
		echo "${machine}: firmware $(basename "${rom}") copied to the scratch data root"
	else
		echo "WARNING: no ${machine} firmware found: that machine is validated without a ROM"
	fi
	[[ -d "${products}/VST3/${product}.vst3" ]] && plugins+=("${products}/VST3/${product}.vst3") || echo "missing: ${product}.vst3"
	if (( au )) && [[ -d "${products}/AU/${product}.component" ]]; then
		# macOS loads an AU from its registry, never from the path it is given, so pluginval would test whatever
		# is installed under that name. Validate the AU only where that is this build (nothing is installed here).
		installed="${HOME}/Library/Audio/Plug-Ins/Components/${product}.component"
		if [[ -d "${installed}" ]] && diff -rq "${products}/AU/${product}.component" "${installed}" > /dev/null 2>&1; then
			plugins+=("${products}/AU/${product}.component")
		elif (( au_installed )) && [[ -d "${installed}" ]]; then
			echo "NOTE: validating the INSTALLED ${product}.component (it differs from this build)"
			plugins+=("${installed}")
		else
			echo "SKIP: ${product}.component: the installed AU is not this build (or none is installed); install this build to validate its AU, or pass --au-installed to validate the installed one"
		fi
	fi
}
[[ "${which}" != mm ]] && add_machine md Machinedrum "${MDMM_PRODUCT_NAME_MD}" "${GEARMULATOR_MD_FIRMWARE_BIN:-}"
[[ "${which}" != md ]] && add_machine mm Monomachine "${MDMM_PRODUCT_NAME_MM}" "${GEARMULATOR_MM_FIRMWARE_BIN:-}"
[[ ${#plugins[@]} -gt 0 ]] || { echo "nothing to validate"; exit 2; }

args=(--out "${out}" --levels "${levels}" --data-root "${data}" --label "local, with firmware" --max-seconds 3600)
(( gui )) || args+=(--skip-gui-tests)
"${here}/ci/mdmm_pluginval.sh" "${args[@]}" "${plugins[@]}"
code=$?
echo "logs: ${out}"
exit "${code}"
