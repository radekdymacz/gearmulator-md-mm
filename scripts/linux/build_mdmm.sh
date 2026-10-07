#!/bin/bash
# The Machinedrum and Monomachine Editors on Linux x86_64 (doc/release/LINUX.md): configure, build the
# standalones and VST3s with the unit tests, run the tests, and package one .tar.gz per machine.
#
#   scripts/linux/build_mdmm.sh [source dir] [build dir] [output dir]
#
# MDMM_LINUX_SKIP_TESTS=1 builds and packages without the tests. MDMM_LINUX_DIAGNOSTICS=1 configures the
# editors' diagnostics (the page log and self-tests; never for a release package). CMAKE_C(XX)_COMPILER_LAUNCHER
# in the environment (sccache) is passed on. Needs: cmake, ninja, pkg-config, the JUCE X11/ALSA headers and
# libwebkit2gtk-4.0-dev + libgtk-3-dev (the workflow's apt line, .github/workflows/mdmm-editors-linux.yml).

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
source_dir="$(cd "${1:-${script_dir}/../..}" && pwd)"
build_dir="$(realpath -m "${2:-${source_dir}/build/linux-mdmm}")"
output_dir="$(realpath -m "${3:-${source_dir}/artifacts/linux-mdmm}")"
config=Release
parallel="${MDMM_LINUX_PARALLEL:-$(nproc)}"
skip_tests="${MDMM_LINUX_SKIP_TESTS:-0}"
diagnostics="${MDMM_LINUX_DIAGNOSTICS:-0}"
products="${build_dir}/products"
# JUCE loads each VST3 to write its moduleinfo.json: keep what that load writes out of the real home.
build_home="${build_dir}/build-home"
mkdir -p "${build_home}"

launcher_args=()
if [[ -n "${CMAKE_CXX_COMPILER_LAUNCHER:-}" ]]; then
	launcher_args+=("-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER:-${CMAKE_CXX_COMPILER_LAUNCHER}}"
		"-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
fi

cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
	-DCMAKE_BUILD_TYPE="${config}" \
	${launcher_args[@]+"${launcher_args[@]}"} \
	-DGEARMULATOR_JUCE_PRODUCTS_ROOT="${products}" \
	-DBUILD_TESTING="$([[ "${skip_tests}" == 1 ]] && echo OFF || echo ON)" \
	-Dgearmulator_MDMM_DIAGNOSTICS="$([[ "${diagnostics}" == 1 ]] && echo ON || echo OFF)" \
	-Dgearmulator_JUCE_WEB_BROWSER=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN=ON \
	-Dgearmulator_BUILD_FX_PLUGIN=OFF \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST2=OFF \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF \
	-Dgearmulator_BUILD_JUCEPLUGIN_LV2=OFF \
	-Dgearmulator_BUILD_JUCEPLUGIN_AU=OFF \
	-Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON \
	-Dgearmulator_SYNTH_ELEKTRON=ON \
	-Dgearmulator_SYNTH_OSIRUS=OFF \
	-Dgearmulator_SYNTH_OSTIRUS=OFF \
	-Dgearmulator_SYNTH_VAVRA=OFF \
	-Dgearmulator_SYNTH_XENIA=OFF \
	-Dgearmulator_SYNTH_NODALRED2X=OFF \
	-Dgearmulator_SYNTH_JE8086=OFF

# The products first: they must build.
HOME="${build_home}" cmake --build "${build_dir}" --config "${config}" --parallel "${parallel}" --target \
	mdJucePlugin_VST3 mmJucePlugin_VST3 mdJucePlugin_Standalone mmJucePlugin_Standalone

if [[ "${skip_tests}" != 1 ]]; then
	# Then everything else (the tests). -k 0: every compile error in one run. A few of upstream's firmware
	# tests do not compile on Linux (std::pmr SysexBuffer against std::vector, doc/release/LINUX.md); their
	# tests are reported and left out below instead of stopping the build.
	if ! HOME="${build_home}" cmake --build "${build_dir}" --config "${config}" --parallel "${parallel}" -- -k 0; then
		echo "::warning title=Some targets did not build::Tests whose program is missing are skipped (listed below)"
	fi
	# The unit tests (doc/modern-ux/FOUNDATION.md, "Build and check"): no plug-in hosting, no firmware.
	# synthLibMidiClockTimingTest is a known failure (-Ofast and isfinite, release notes' history).
	# A display for the tests that make JUCE components: xvfb-run in the workflow.
	exclude="Plugin|_AU|VST|FirmwareTest|synthLibMidiClockTimingTest"
	missing="$(ctest --test-dir "${build_dir}" -C "${config}" -N --show-only=json-v1 -E "${exclude}" | python3 -c '
import json, os, sys
for t in json.load(sys.stdin)["tests"]:
	cmd = t.get("command") or []
	if not cmd or not os.path.exists(cmd[0]):
		print(t["name"])
')"
	if [[ -n "${missing}" ]]; then
		echo "::warning title=Tests not run (program did not build)::$(echo "${missing}" | tr '\n' ' ')"
		exclude="${exclude}|^($(echo "${missing}" | paste -sd '|' -))\$"
	fi
	ctest --test-dir "${build_dir}" -C "${config}" --output-on-failure --timeout 600 --no-tests=error -E "${exclude}"
fi

# One archive per machine: the standalone, the VST3 bundle, the two shims (in both places), licence and README.
mkdir -p "${output_dir}"
version="$(sed -n 's/^set(MDMM_EDITOR_VERSION \([0-9.]*\))$/\1/p' "${source_dir}/source/elektron/md/mdJucePlugin/mdmmPlugins.cmake")"
stage="$(mktemp -d)"
trap 'rm -rf "${stage}"' EXIT
for machine in md mm; do
	if [[ "${machine}" == md ]]; then
		product="Gearmulator MD"; editor="Machinedrum Editor"; asset="Machinedrum-Editor"
	else
		product="Gearmulator MM"; editor="Monomachine Editor"; asset="Monomachine-Editor"
	fi
	standalone="${products}/${config}/Standalone/${product}"
	vst3="${products}/${config}/VST3/${product}.vst3"
	for need in "${standalone}" "${vst3}/Contents/x86_64-linux/${product}.so" \
		"$(dirname "${standalone}")/libwebkit2gtk-4.0.so" "$(dirname "${standalone}")/libgtk-3.so" \
		"${vst3}/Contents/x86_64-linux/libwebkit2gtk-4.0.so" "${vst3}/Contents/x86_64-linux/libgtk-3.so"; do
		if [[ ! -f "${need}" ]]; then
			echo "Missing product file: ${need}" >&2
			exit 1
		fi
	done
	dir="${stage}/${asset}-Linux-x64"
	mkdir -p "${dir}/Standalone" "${dir}/VST3"
	cp "${standalone}" "$(dirname "${standalone}")/libwebkit2gtk-4.0.so" "$(dirname "${standalone}")/libgtk-3.so" \
		"${dir}/Standalone/"
	# The bundle as built, without runtime files a plug-in load may have left in it.
	cp -R "${vst3}" "${dir}/VST3/"
	find "${dir}/VST3" -type f ! -name '*.so' ! -name 'moduleinfo.json' -delete
	cp "${source_dir}/LICENSE.md" "${dir}/"
	sed -e "s/@EDITOR@/${editor}/g" -e "s/@PRODUCT@/${product}/g" -e "s/@VERSION@/${version}/g" \
		"${script_dir}/README-Linux.txt" > "${dir}/README.txt"
	if find "${dir}" -type f | grep -E -i '\.(bin|rom|nvram|syx|wav)$'; then
		echo "Firmware or private runtime material found in the package" >&2
		exit 1
	fi
	tarball="${output_dir}/${asset}-Linux-x64-not-tested.tar.gz"
	tar -C "${stage}" -czf "${tarball}" "$(basename "${dir}")"
	echo "LINUX_MDMM_TARBALL=${tarball}"
	sha256sum "${tarball}"
done
