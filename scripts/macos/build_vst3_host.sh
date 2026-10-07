#!/bin/bash
# Builds the minimal VST3 host of the start test (scripts/vst3EditorHost) on the fork's JUCE submodule and prints
# VST3_HOST=<its binary> on stdout (for $GITHUB_ENV); everything else goes to stderr.
#
#   scripts/macos/build_vst3_host.sh <build dir>
set -euo pipefail

source_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$1"
{
	if [[ ! -f "${source_dir}/source/JUCE/CMakeLists.txt" ]]; then
		git -C "${source_dir}" submodule update --init --depth 1 source/JUCE
	fi
	cmake -S "${source_dir}/scripts/vst3EditorHost" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
	cmake --build "${build_dir}" --config Release --parallel 4
} >&2
host="$(find "${build_dir}" -path '*mdmmVst3EditorHost.app/Contents/MacOS/mdmmVst3EditorHost' -type f | head -n 1)"
if [[ ! -x "${host}" ]]; then
	echo "mdmmVst3EditorHost was not built" >&2
	exit 1
fi
echo "VST3_HOST=${host}"
