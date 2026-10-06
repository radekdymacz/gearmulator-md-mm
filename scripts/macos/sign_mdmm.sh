#!/bin/bash
#
# Sign the six MD/MM bundles in place, inside-out, without --deep.
#
#   sign_mdmm.sh BUNDLE_DIR
#
# BUNDLE_DIR holds the bundles as build_mdmm.sh stages them:
#   Gearmulator MD.app  Gearmulator MD.vst3  Gearmulator MD.component
#   Gearmulator MM.app  Gearmulator MM.vst3  Gearmulator MM.component
#
# Environment:
#   MDMM_SIGN_IDENTITY  "Developer ID Application: ..." (name or SHA-1), or "-"
#                       for ad-hoc (the default; what releases had before
#                       Developer ID).
#   MDMM_SIGN_KEYCHAIN  optional keychain holding the identity (CI uses a
#                       temporary one).
#   MDMM_SIGN_HARDENED  1 = hardened runtime + entitlements, 0 = plain.
#                       Defaults to 1 for a real identity and 0 for ad-hoc, so
#                       the unsigned path stays exactly as before. Set it to 1
#                       with ad-hoc to try the hardened runtime locally.
#
# With a real identity every signature gets the hardened runtime and a secure
# timestamp (both required for notarization). The apps get
# entitlements/mdmm-standalone.entitlements (allow-jit for the DSP recompiler,
# audio-input); the plug-ins get none, because a loaded plug-in runs with its
# host's entitlements. Any failure exits non-zero.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
bundle_dir="$(cd "${1:?usage: sign_mdmm.sh BUNDLE_DIR}" && pwd)"
identity="${MDMM_SIGN_IDENTITY:--}"
keychain="${MDMM_SIGN_KEYCHAIN:-}"
app_entitlements="${script_dir}/entitlements/mdmm-standalone.entitlements"

if [[ "${identity}" == "-" ]]; then
  hardened="${MDMM_SIGN_HARDENED:-0}"
else
  hardened="${MDMM_SIGN_HARDENED:-1}"
  if [[ "${hardened}" != "1" ]]; then
    echo "A Developer ID signature must use the hardened runtime (MDMM_SIGN_HARDENED=1)" >&2
    exit 2
  fi
fi
if [[ "${hardened}" != "0" && "${hardened}" != "1" ]]; then
  echo "MDMM_SIGN_HARDENED must be 0 or 1" >&2
  exit 2
fi
plutil -lint "${app_entitlements}" >/dev/null

common_args=(--force --sign "${identity}")
if [[ -n "${keychain}" ]]; then
  common_args+=(--keychain "${keychain}")
fi
if [[ "${identity}" == "-" ]]; then
  common_args+=(--timestamp=none)
else
  common_args+=(--timestamp)
fi
if [[ "${hardened}" == "1" ]]; then
  common_args+=(--options runtime)
fi

# Path depth, so nested code is signed deepest first.
depth() {
  local slashes="${1//[^\/]/}"
  echo "${#slashes}"
}

# Nested code inside a bundle: dylibs, frameworks, nested bundles and loose
# Mach-O executables outside Contents/MacOS. Today's bundles have none, but a
# future JUCE or dependency change must not slip in unsigned or be skipped.
sign_nested() {
  local bundle="$1"
  local items=() line
  while IFS= read -r -d '' line; do
    items+=("$(depth "${line}")|${line}")
  done < <(find "${bundle}/Contents" -mindepth 1 \
      \( -path "${bundle}/Contents/MacOS" -prune \) -o \
      \( -name '*.framework' -o -name '*.bundle' -o -name '*.app' -o -name '*.appex' \
         -o -name '*.xpc' -o -name '*.vst3' -o -name '*.component' -o -name '*.dylib' \
         -o \( -type f -perm -u+x \) \) -print0)
  # Extra executables inside Contents/MacOS besides the main one.
  local main_name
  main_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${bundle}/Contents/Info.plist")"
  while IFS= read -r -d '' line; do
    if [[ "$(basename "${line}")" != "${main_name}" ]]; then
      items+=("$(depth "${line}")|${line}")
    fi
  done < <(find "${bundle}/Contents/MacOS" -mindepth 1 -type f -print0)

  [[ ${#items[@]} -eq 0 ]] && return 0
  local sorted
  sorted="$(printf '%s\n' "${items[@]}" | sort -t '|' -k1,1nr)"
  while IFS='|' read -r _ path; do
    # Only Mach-O files and bundles carry signatures; skip scripts and data.
    if [[ -f "${path}" ]] && ! file -b "${path}" | grep -q 'Mach-O'; then
      continue
    fi
    echo "  nested: ${path#"${bundle}/"}"
    codesign "${common_args[@]}" "${path}"
  done <<< "${sorted}"
}

signed_any=0
for stem in "Gearmulator MD" "Gearmulator MM"; do
  for ext in app vst3 component; do
    bundle="${bundle_dir}/${stem}.${ext}"
    if [[ ! -d "${bundle}" ]]; then
      echo "Missing bundle: ${bundle}" >&2
      exit 3
    fi
    echo "Signing ${bundle} (identity ${identity}, hardened ${hardened})"
    sign_nested "${bundle}"
    args=("${common_args[@]}")
    if [[ "${ext}" == "app" && "${hardened}" == "1" ]]; then
      args+=(--entitlements "${app_entitlements}")
    fi
    codesign "${args[@]}" "${bundle}"
    codesign --verify --strict --deep "${bundle}"

    details="$(codesign -dvvv "${bundle}" 2>&1)"
    if [[ "${hardened}" == "1" ]] && ! grep -Eq 'flags=0x[0-9a-f]+\([^)]*runtime' <<< "${details}"; then
      echo "Hardened runtime flag missing on ${bundle}" >&2
      exit 4
    fi
    if [[ "${identity}" != "-" ]]; then
      if [[ "${details}" != *"Authority=Developer ID Application:"* ]]; then
        echo "Not signed with a Developer ID Application certificate: ${bundle}" >&2
        exit 4
      fi
      if [[ "${details}" != *"Timestamp="* ]]; then
        echo "No secure timestamp on ${bundle}" >&2
        exit 4
      fi
    fi
    if [[ "${ext}" == "app" && "${hardened}" == "1" ]]; then
      entitlements="$(codesign -d --entitlements - --xml "${bundle}" 2>/dev/null)"
      if [[ "${entitlements}" != *"com.apple.security.cs.allow-jit"* ]]; then
        echo "allow-jit entitlement missing on ${bundle}" >&2
        exit 4
      fi
    fi
    signed_any=1
  done
done

[[ "${signed_any}" == "1" ]]
echo "MDMM_SIGNED_IDENTITY=${identity}"
echo "MDMM_SIGNED_HARDENED=${hardened}"
