#!/bin/sh
# Finds the editors' old names (Gearmulator MD / Gearmulator MM, up to 0.3.1) where a user reads them: the site, the
# README, the installer and disk-image texts, the packaging scripts. The product names live in scripts/mdmm-product.env;
# the text files carry them as words, so after a rename run this and fix what it lists. A line that says it is about
# the old names (it mentions 0.3.1 or a LEGACY/OLD_NAME placeholder) is an upgrade note and passes.
#   scripts/check_mdmm_names.sh        exit 1 and the lines when any are left
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2
hits=$(grep -rnE "Gearmulator M[DM]\b" \
	site/public README.md marketing/BRAND.md \
	scripts/macos scripts/windows scripts/mdmm-journeys.sh scripts/mdmm-demo-video.sh \
	scripts/md-editor-cpu.sh scripts/mm-editor-cpu.sh \
	source/elektron/md/mdJucePlugin/CMakeLists.txt source/elektron/md/mdJucePlugin/mdmmPlugins.cmake \
	2>/dev/null | grep -vE "0\.3\.1|LEGACY|OLD_NAME|old_name|legacy")
if [ -n "$hits" ]; then
	echo "The old names are still here (scripts/mdmm-product.env holds the new ones):"
	echo "$hits"
	exit 1
fi
echo "No old names left where users read them."
