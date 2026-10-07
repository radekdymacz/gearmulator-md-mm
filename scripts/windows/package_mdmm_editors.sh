#!/bin/bash
# Splits elektron-windows.yml's package (Gearmulator-Elektron-Windows-x64.zip: both standalones, both VST3
# bundles, LICENSE.md) into one zip per machine for the release (mdmm-editors-release.yml), each with a README and
# the WebView2 loader's licence.
# Runs on Linux (unzip, zip).
#
#   scripts/windows/package_mdmm_editors.sh <Gearmulator-Elektron-Windows-x64.zip> <output dir>
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
source_dir="$(cd "${script_dir}/../.." && pwd)"
zip_in="$(realpath "$1")"
mkdir -p "$2"
out="$(realpath "$2")"
version="$(sed -n 's/^set(MDMM_EDITOR_VERSION \([0-9.]*\))$/\1/p' "${source_dir}/source/elektron/md/mdJucePlugin/mdmmPlugins.cmake")"
mkdir -p "${out}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
unzip -q "${zip_in}" -d "${work}/all"

for machine in md mm; do
	if [[ "${machine}" == md ]]; then
		product="Gearmulator MD"; editor="Machinedrum Editor"; asset="Machinedrum-Editor"
	else
		product="Gearmulator MM"; editor="Monomachine Editor"; asset="Monomachine-Editor"
	fi
	for need in "${work}/all/${product}.exe" "${work}/all/${product}.vst3" "${work}/all/LICENSE.md"; do
		if [[ ! -e "${need}" ]]; then
			echo "Not in the Windows package: $(basename "${need}")" >&2
			exit 1
		fi
	done
	dir="${work}/${asset}-Windows-x64"
	mkdir -p "${dir}"
	cp -R "${work}/all/${product}.exe" "${work}/all/${product}.vst3" "${work}/all/LICENSE.md" "${dir}/"
	# The WebView2 loader is linked into both binaries (mdmmWindowsWebView.cmake); its licence asks for the notice.
	sed 's/$/\r/' "${script_dir}/WebView2-LICENSE.txt" > "${dir}/WebView2-LICENSE.txt"
	sed -e "s/@EDITOR@/${editor}/g" -e "s/@PRODUCT@/${product}/g" -e "s/@VERSION@/${version}/g" \
		"${script_dir}/README-Windows-mdmm.txt" | sed 's/$/\r/' > "${dir}/README.txt"
	zip_out="${out}/${asset}-Windows-x64-not-tested.zip"
	rm -f "${zip_out}"
	(cd "${work}" && zip -q -r -X "${zip_out}" "$(basename "${dir}")")
	echo "WINDOWS_MDMM_EDITOR_ZIP=${zip_out}"
	unzip -l "${zip_out}"
done
