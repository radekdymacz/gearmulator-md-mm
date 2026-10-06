#!/bin/sh
# Builds mdmm-recorder (main.swift: ScreenCaptureKit, no third-party code) into temp/mdmm-recorder/ of this tree, or
# into $1. Rebuilds only when main.swift is newer than the binary. Prints the binary's path.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-"$ROOT/temp/mdmm-recorder"}
BIN="$OUT/mdmm-recorder"
mkdir -p "$OUT"
if [ ! -x "$BIN" ] || [ "$HERE/main.swift" -nt "$BIN" ]; then
	# the Xcode toolchain and SDK (the Command Line Tools' SDK is broken here: FOUNDATION.md, Build and check)
	export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
	xcrun --sdk macosx swiftc -O -o "$BIN" "$HERE/main.swift" >&2
	codesign -s - -f "$BIN" >/dev/null 2>&1 || true
fi
echo "$BIN"
