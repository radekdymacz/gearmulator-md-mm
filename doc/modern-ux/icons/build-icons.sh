#!/bin/sh
# Builds the app icons from the SVG sources: PNGs (16-1024) and a macOS .icns per app.
# Needs rsvg-convert and iconutil. 16 and 32 px use the simplified *-small.svg.
#   doc/modern-ux/icons/build-icons.sh [out dir]   (default: doc/modern-ux/icons/build)
set -e
H=$(cd "$(dirname "$0")" && pwd); OUT=${1:-$H/build}
mkdir -p "$OUT"
for k in md mm; do
	set_=$OUT/$k.iconset; rm -rf "$set_"; mkdir -p "$set_"
	for z in 16 32 64 128 256 512 1024; do
		src=$H/$k-icon.svg; [ $z -le 32 ] && src=$H/$k-icon-small.svg
		rsvg-convert -w $z -h $z "$src" -o "$OUT/$k-$z.png"
	done
	cp "$OUT/$k-16.png" "$set_/icon_16x16.png"; cp "$OUT/$k-32.png" "$set_/icon_16x16@2x.png"
	cp "$OUT/$k-32.png" "$set_/icon_32x32.png"; cp "$OUT/$k-64.png" "$set_/icon_32x32@2x.png"
	cp "$OUT/$k-128.png" "$set_/icon_128x128.png"; cp "$OUT/$k-256.png" "$set_/icon_128x128@2x.png"
	cp "$OUT/$k-256.png" "$set_/icon_256x256.png"; cp "$OUT/$k-512.png" "$set_/icon_256x256@2x.png"
	cp "$OUT/$k-512.png" "$set_/icon_512x512.png"; cp "$OUT/$k-1024.png" "$set_/icon_512x512@2x.png"
	iconutil -c icns "$set_" -o "$OUT/$k.icns"
done
echo "built in $OUT"
