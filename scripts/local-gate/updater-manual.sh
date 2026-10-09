#!/bin/bash
# The updater end to end: the one step of the local release gate that needs a person at the window
# (doc/release/LOCAL-GATE.md, "Updater end to end"; design: doc/modern-ux/DESIGN-updates.md).
#
#   scripts/local-gate/updater-manual.sh check        what can be tested now; no window. Reads https://mdmm.dev/latest.json
#   scripts/local-gate/updater-manual.sh prepare      a scratch copy of this tree with an OLDER version number, its standalones built
#   scripts/local-gate/updater-manual.sh run md|mm    that standalone, in a sandbox, in front; prints what to press
#
# Why a scratch copy: the app asks only https://mdmm.dev/latest.json and downloads only from the repository's
# GitHub releases (no setting, variable or test hook changes that; curl runs with -q, HTTPS only), and it offers an
# update only when the manifest is NEWER than itself. Before a tag the published version is older than the candidate, so
# the candidate has nothing to offer. The copy differs in one line (MDMM_EDITOR_VERSION, default 0.3.0 here:
# UPDATER_TEST_VERSION) and so offers the published release; everything else, the updater included, is the candidate's code.
# An update can be installed only when this tree's source/elektron/md/mdmmUpdate/updateKey.h holds the real public
# key AND the published release is signed with the matching private key; `check` says whether both hold.
# Exit status of `check`: 0 the test can be run now, 3 it cannot yet (the reasons are printed), 2 usage or no network.

# shellcheck source-path=SCRIPTDIR
set -u -o pipefail

here="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${here}/../.." && pwd)"
# shellcheck source=../mdmm-product.env
. "${ROOT}/scripts/mdmm-product.env"
WORK="${ROOT}/temp/local-gate/updater"
COPY="${WORK}/src"
TEST_VERSION="${UPDATER_TEST_VERSION:-0.3.0}"
PREVIEW="${MDMM_GATE_PREVIEW:-${HOME}/Documents/Gearmulator Preview}"
MANIFEST_URL="https://mdmm.dev/latest.json"

candidate_version() { sed -n 's/^set(MDMM_EDITOR_VERSION \([0-9.]*\))$/\1/p' "$1/source/elektron/md/mdJucePlugin/mdmmPlugins.cmake"; }

key_state() {	# real|placeholder
	if grep -Eq 'g_updatePublicKeyHex = "[0-9a-fA-F]{64}"' "${ROOT}/source/elektron/md/mdmmUpdate/updateKey.h"; then echo real; else echo placeholder; fi
}

check() {
	local candidate key manifest published signed problems=0
	candidate="$(candidate_version "${ROOT}")"
	key="$(key_state)"
	echo "candidate version:    ${candidate}"
	echo "update public key:    ${key} (source/elektron/md/mdmmUpdate/updateKey.h)"
	if ! manifest="$(curl -q -fsS --max-time 20 --proto '=https' --user-agent mdmm-editor "${MANIFEST_URL}" 2> /dev/null)"; then
		echo "published manifest:   could not read ${MANIFEST_URL} (offline?)"
		return 2
	fi
	published="$(printf '%s' "${manifest}" | python3 -B -c 'import json,sys; print(json.load(sys.stdin)["version"])' 2> /dev/null)"
	signed="$(printf '%s' "${manifest}" | python3 -B -c '
import json, sys
mac = json.load(sys.stdin)["assets"]["mac"]
print("yes" if all("sig" in mac[m] for m in ("md", "mm")) else "no")' 2> /dev/null)"
	echo "published manifest:   version ${published:-?}, macOS assets signed: ${signed:-?}"
	if [ "${key}" != real ]; then
		echo "CANNOT YET: updateKey.h is the placeholder, so no build can install an update: the banner offers Download (the site), never Update. Paste the public key from scripts/release/update_keygen.py first."
		problems=1
	fi
	if [ "${signed}" != yes ]; then
		echo "CANNOT YET: the published release carries no signature (the site-release workflow signs only with the secret MDMM_UPDATE_SIGNING_KEY), so the app refuses to install it: 'the release is not signed'."
		problems=1
	fi
	if [ "${problems}" = 1 ]; then
		echo "Until both hold, the most a person can see is the banner and its Download button (use 'prepare' and 'run' below; Update will not be offered)."
		return 3
	fi
	echo "OK: build the scratch copy with 'prepare' (version ${TEST_VERSION} < ${published}) and press Update in it."
	return 0
}

prepare() {
	local product
	mkdir -p "${WORK}"
	echo "== copying this tree to ${COPY} (not .git, not temp/, build/, bin/)"
	rsync -a --delete --exclude '.git' --exclude '/temp' --exclude '/build' --exclude '/bin' --exclude '/artifacts' "${ROOT}/" "${COPY}/" || return 1
	sed -i '' "s/^set(MDMM_EDITOR_VERSION .*/set(MDMM_EDITOR_VERSION ${TEST_VERSION})/" "${COPY}/source/elektron/md/mdJucePlugin/mdmmPlugins.cmake" || return 1
	echo "== version in the copy: $(candidate_version "${COPY}") (this tree: $(candidate_version "${ROOT}"))"
	MDMM_DEV_BUILD="${WORK}/build" "${COPY}/scripts/mdmm-dev.sh" build mdJucePlugin_Standalone mmJucePlugin_Standalone || return 1
	for product in "${MDMM_PRODUCT_NAME_MD}" "${MDMM_PRODUCT_NAME_MM}"; do
		echo "built: ${COPY}/bin/plugins/Release/Standalone/${product}.app"
	done
}

run() {
	local machine product rom sandbox="${WORK}/sandbox" app
	case "${1:-}" in
		md) machine=Machinedrum; product="${MDMM_PRODUCT_NAME_MD}" ;;
		mm) machine=Monomachine; product="${MDMM_PRODUCT_NAME_MM}" ;;
		*) echo "usage: $0 run md|mm" >&2; return 2 ;;
	esac
	app="${COPY}/bin/plugins/Release/Standalone/${product}.app"
	[ -x "${app}/Contents/MacOS/${product}" ] || { echo "no scratch build: run '$0 prepare' first" >&2; return 1; }
	rom="${GEARMULATOR_MD_FIRMWARE_BIN:-}"
	[ "${machine}" = Monomachine ] && rom="${GEARMULATOR_MM_FIRMWARE_BIN:-}"
	[ -n "${rom}" ] || rom="$(find "${PREVIEW}/${machine}/roms" -maxdepth 1 -type f 2> /dev/null | head -n 1)"
	rm -rf "${sandbox}"
	mkdir -p "${sandbox}/data/Gearmulator Preview/${machine}/roms" "${sandbox}/home/Library/Application Support" "${sandbox}/home/Library/Caches"
	[ -f "${rom}" ] && ln -s "${rom}" "${sandbox}/data/Gearmulator Preview/${machine}/roms/$(basename "${rom}")"
	cat <<EOF
== ${product} (scratch copy, version $(candidate_version "${COPY}")), sandboxed: your own settings and data are not touched
   1. Right-click the page (or the menu bar): Updates > Check for Updates Now.
   2. Banner "Update available: <published version>" with Update / Later / Don't check.
      (Download instead of Update = this build or the published release cannot install: see 'check'.)
   3. Press Update: "Downloading <version>... N %" with Cancel; it must reach 100 %.
   4. macOS: it verifies size, SHA-256 and signature, then the system Installer opens the .pkg and the banner says
      "Quit the editor, then click Install ...", Quit / OK. Do NOT click Install: close the Installer window, press Quit.
   5. Also: Later hides the banner until tomorrow; Cancel during the download stops it; Don't check unticks Updates > Check Daily.
   6. Windows / Linux ("not tested" builds): the same on such a machine: Restart now replaces the app and starts it again.
   Quit the editor to end (Ctrl-C here also works). The sandbox is ${sandbox}.
EOF
	env GEARMULATOR_DATA_ROOT="${sandbox}/data/" CFFIXED_USER_HOME="${sandbox}/home" "${app}/Contents/MacOS/${product}"
}

case "${1:-}" in
	check) check ;;
	prepare) prepare ;;
	run) shift; run "$@" ;;
	*) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac
