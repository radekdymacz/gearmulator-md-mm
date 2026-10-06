#!/bin/bash
#
# Check an installed Machinedrum Editor / Monomachine Editor package.
# Run it after installing one or both packages, for all users or for you only:
#
#   sudo installer -pkg Machinedrum-Editor-macOS.pkg -target /                      (all users)
#   installer -pkg Monomachine-Editor-macOS.pkg -target CurrentUserHomeDirectory    (for me only)
#   scripts/macos/verify_mdmm_pkg_install.sh [md] [mm]
#
# With no arguments it checks both. It needs no sudo. For each machine it finds
# where it is installed (/ for all users, the home folder for you only; both at
# once is reported, since a DAW then lists two), and checks the Installer
# receipts, that the three bundles are there with valid signatures, and that
# the installed Audio Unit passes auval.
#
# auval loads the AU in this process. Set GEARMULATOR_DATA_ROOT to an empty
# folder to run it without touching your own Gearmulator settings (CI does).
#
# MDMM_EXPECT_SIGNED=1 also requires Developer ID signatures with the hardened
# runtime on all three bundles and Gatekeeper (spctl) acceptance of the app.

set -euo pipefail

expect_signed="${MDMM_EXPECT_SIGNED:-0}"
machines=("$@")
if [[ ${#machines[@]} -eq 0 ]]; then
  machines=(md mm)
fi

failed=0
fail() {
  echo "FAIL: $*" >&2
  failed=1
}

for machine in "${machines[@]}"; do
  case "${machine}" in
    md) stem="Gearmulator MD"; app="Machinedrum Editor"; subtype="Tmdr" ;;
    mm) stem="Gearmulator MM"; app="Monomachine Editor"; subtype="Tmno" ;;
    *) echo "unknown machine: ${machine} (use md or mm)" >&2; exit 2 ;;
  esac
  id="com.nativekloud.mdmm.${machine}"
  echo "== ${app}"

  # The domain it is installed in: the root of Applications and Library.
  roots=()
  [[ -e "/Applications/${app}.app" || -e "/Library/Audio/Plug-Ins/VST3/${stem}.vst3" || -e "/Library/Audio/Plug-Ins/Components/${stem}.component" ]] && roots+=("")
  [[ -e "${HOME}/Applications/${app}.app" || -e "${HOME}/Library/Audio/Plug-Ins/VST3/${stem}.vst3" || -e "${HOME}/Library/Audio/Plug-Ins/Components/${stem}.component" ]] && roots+=("${HOME}")
  if [[ ${#roots[@]} -eq 0 ]]; then
    fail "${app} is installed neither for all users nor for you only"
    continue
  fi
  if [[ ${#roots[@]} -eq 2 ]]; then
    echo "note: installed for all users and for you only: the DAW lists two; remove one"
  fi

  for root in "${roots[@]}"; do
    where="${root:-/}"
    echo "-- installed in ${where} ($([[ -n "${root}" ]] && echo "for you only" || echo "for all users"))"
    volume=()
    [[ -n "${root}" ]] && volume=(--volume "${root}")
    for part in app vst3 au; do
      if pkgutil ${volume[@]+"${volume[@]}"} --pkg-info "${id}.${part}" >/dev/null 2>&1; then
        echo "receipt ${id}.${part}: $(pkgutil ${volume[@]+"${volume[@]}"} --pkg-info "${id}.${part}" | awk '/^version:/{print $2}')"
      else
        fail "no Installer receipt for ${id}.${part} in ${where}"
      fi
    done

    for bundle in \
        "${root}/Applications/${app}.app" \
        "${root}/Library/Audio/Plug-Ins/VST3/${stem}.vst3" \
        "${root}/Library/Audio/Plug-Ins/Components/${stem}.component"; do
      executable="${bundle}/Contents/MacOS/${stem}"
      if [[ ! -x "${executable}" ]]; then
        fail "missing ${executable}"
        continue
      fi
      if codesign --verify --deep --strict "${bundle}" 2>/dev/null; then
        echo "ok  ${bundle} ($(lipo -archs "${executable}"))"
      else
        fail "signature does not verify: ${bundle}"
        continue
      fi
      if [[ "${expect_signed}" == "1" ]]; then
        details="$(codesign -dvvv "${bundle}" 2>&1)"
        [[ "${details}" == *"Authority=Developer ID Application:"* ]] \
          || fail "not Developer ID signed: ${bundle}"
        grep -Eq 'flags=0x[0-9a-f]+\([^)]*runtime' <<< "${details}" || fail "no hardened runtime: ${bundle}"
        if [[ "${bundle}" == *.app ]]; then
          if spctl --assess --type execute -vv "${bundle}"; then
            echo "ok  Gatekeeper accepts ${bundle}"
          else
            fail "spctl rejects ${bundle}"
          fi
        fi
      fi
    done
  done

  auval_log="$(mktemp "${TMPDIR:-/tmp}/mdmm-auval.XXXXXX")"
  if auval -v aumu "${subtype}" GmPv >"${auval_log}" 2>&1 \
      && grep -q "AU VALIDATION SUCCEEDED" "${auval_log}"; then
    echo "ok  auval aumu ${subtype} GmPv: AU VALIDATION SUCCEEDED"
  else
    tail -40 "${auval_log}" >&2
    fail "auval aumu ${subtype} GmPv did not succeed (log: ${auval_log})"
    continue
  fi
  rm -f "${auval_log}"
done

if [[ ${failed} -ne 0 ]]; then
  echo "Package install verification FAILED" >&2
  exit 1
fi
echo "Package install verification passed"
