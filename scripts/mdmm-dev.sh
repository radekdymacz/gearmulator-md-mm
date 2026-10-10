#!/bin/sh
# The fast local build of the editors (doc/modern-ux/FOUNDATION.md, "Build and check"): this Mac's architecture only
# (no universal binary), Ninja, ccache when it is installed, the Machinedrum and Monomachine targets only,
# diagnostics on (the page log, the self-tests and the journeys), the standalones, VST3s and AUs. One build tree per
# checkout, temp/dev, used by every helper (mdmm-journeys.sh, mdmm-pluginval.sh, ...: their defaults are this
# tree's bin/plugins/Release). Not a release build: those stay scripts/macos/build_mdmm.sh (universal, ThinLTO).
#
#   scripts/mdmm-dev.sh configure            configure temp/dev (again: keeps what is built)
#   scripts/mdmm-dev.sh build [target...]    build the four editor targets (default) or the ones named;
#                                            configures first when needed
#   scripts/mdmm-dev.sh tests                build and run the unit tests (ctest -E "Plugin|_AU|VST|FirmwareTest")
#   scripts/mdmm-dev.sh play md|mm           build that machine's standalone only and open it (the dev loop: build,
#                                            play, accept; no tests, doc/release/CI.md)
#   scripts/mdmm-dev.sh stats                ccache's statistics
#
# ccache: `brew install ccache` (macOS) or `sudo apt-get install ccache` (Linux), once. With it a fresh worktree or a
# second checkout builds from the cache (paths are hashed relative to the checkout, CCACHE_BASEDIR), and switching
# branches back and forth costs only what differs. Without it the script still works, uncached.
# Environment: MDMM_DEV_BUILD another build folder (default <checkout>/temp/dev), MDMM_DEV_JOBS parallel jobs
# (default: all cores), MDMM_DEV_ARGS more cmake arguments for configure (e.g. -DMDMM_INSTALL_DEV_PLUGINS=ON).
# JUCE loads each VST3 while it builds it (its moduleinfo.json): that load gets a scratch data root inside the
# build tree (GEARMULATOR_DATA_ROOT), so a build never reads or writes ~/Documents/Gearmulator Preview.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${MDMM_DEV_BUILD:-"$ROOT/temp/dev"}
JOBS=${MDMM_DEV_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)}
ARCH=$(uname -m)
cmd=${1:-build}
[ $# -gt 0 ] && shift
# The selected Xcode's macOS SDK, for this build and the ones it starts (JUCE's juceaide is configured inside the
# configure): a bare `xcrun --show-sdk-path`, CMake's own guess, may name the Command Line Tools' SDK, whose newer
# libSystem.tbd the Xcode linker cannot read ("tapi error: malformed file ... unknown architecture").
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
	SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
	export SDKROOT
fi

launcher_args() {	# ccache with its settings baked into the launcher, so a plain `cmake --build temp/dev` uses them too
	command -v ccache >/dev/null 2>&1 || { echo "   ccache not found (brew install ccache): building uncached" >&2; return 0; }
	ccache=$(command -v ccache)
	l="/usr/bin/env;CCACHE_BASEDIR=$ROOT;CCACHE_SLOPPINESS=include_file_mtime,include_file_ctime,time_macros,pch_defines;CCACHE_COMPILERCHECK=%compiler% -v;$ccache"
	printf '%s\n' "-DCMAKE_C_COMPILER_LAUNCHER=$l" "-DCMAKE_CXX_COMPILER_LAUNCHER=$l" "-DCMAKE_OBJC_COMPILER_LAUNCHER=$l" "-DCMAKE_OBJCXX_COMPILER_LAUNCHER=$l"
}

configure() {
	set --
	while IFS= read -r a; do set -- "$@" "$a"; done <<EOF
$(launcher_args)
EOF
	formats="-Dgearmulator_BUILD_JUCEPLUGIN_AU=OFF"
	[ "$(uname -s)" = Darwin ] && formats="-Dgearmulator_BUILD_JUCEPLUGIN_AU=ON -DCMAKE_OSX_ARCHITECTURES=$ARCH -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 -DCMAKE_OSX_SYSROOT=$SDKROOT"
	[ "$(uname -s)" = Linux ] && formats="$formats -Dgearmulator_JUCE_WEB_BROWSER=ON"
	# shellcheck disable=SC2086
	cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		"$@" $formats ${MDMM_DEV_ARGS:-} \
		-DBUILD_TESTING=ON -Dgearmulator_MDMM_DIAGNOSTICS=ON \
		-Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_FX_PLUGIN=OFF \
		-Dgearmulator_BUILD_JUCEPLUGIN_VST2=OFF -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
		-Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF -Dgearmulator_BUILD_JUCEPLUGIN_LV2=OFF \
		-Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON \
		-Dgearmulator_SYNTH_ELEKTRON=ON -Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF \
		-Dgearmulator_SYNTH_VAVRA=OFF -Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF \
		-Dgearmulator_SYNTH_JE8086=OFF
}

build() {
	[ -f "$BUILD/build.ninja" ] || configure
	[ $# -gt 0 ] || set -- mdJucePlugin_Standalone mmJucePlugin_Standalone mdJucePlugin_VST3 mmJucePlugin_VST3
	if [ "$(uname -s)" = Darwin ] && [ "$*" = "mdJucePlugin_Standalone mmJucePlugin_Standalone mdJucePlugin_VST3 mmJucePlugin_VST3" ]; then
		set -- "$@" mdJucePlugin_AU mmJucePlugin_AU
	fi
	mkdir -p "$BUILD/build-data"
	GEARMULATOR_DATA_ROOT="$BUILD/build-data/" cmake --build "$BUILD" --parallel "$JOBS" --target "$@"
}

case "$cmd" in
	configure) configure ;;
	build) build "$@" ;;
	tests)
		[ -f "$BUILD/build.ninja" ] || configure
		mkdir -p "$BUILD/build-data"
		GEARMULATOR_DATA_ROOT="$BUILD/build-data/" cmake --build "$BUILD" --parallel "$JOBS"
		GEARMULATOR_DATA_ROOT="$BUILD/build-data/" ctest --test-dir "$BUILD" -j "$JOBS" --output-on-failure -E "Plugin|_AU|VST|FirmwareTest" ;;
	play)
		case "${1:-}" in
			md) target=mdJucePlugin_Standalone; app="Machinedrum Editor" ;;
			mm) target=mmJucePlugin_Standalone; app="Monomachine Editor" ;;
			*) echo "usage: $0 play md|mm" >&2; exit 2 ;;
		esac
		build "$target"
		if [ "$(uname -s)" = Darwin ]; then
			bundle=$(find "$BUILD" -name "$app.app" -type d -path '*Standalone*' -prune 2>/dev/null | head -n 1)
			[ -n "$bundle" ] || { echo "no $app.app under $BUILD" >&2; exit 1; }
			echo "opening $bundle"
			open -n "$bundle"
		else
			exe=$(find "$BUILD" -type f -perm -u+x -name "$app" -path '*Standalone*' 2>/dev/null | head -n 1)
			[ -n "$exe" ] || { echo "no $app under $BUILD" >&2; exit 1; }
			echo "starting $exe"
			"$exe" &
		fi ;;
	stats) ccache --show-stats ;;
	*) echo "usage: $0 configure | build [target...] | tests | play md|mm | stats" >&2; exit 2 ;;
esac
