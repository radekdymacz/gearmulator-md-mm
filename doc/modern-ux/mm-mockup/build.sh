#!/bin/sh
# The Monomachine Editor mockup as one page (index.html, opens on its own): its own sources (src/) and the
# page files both editors share (skins/shared/: the modal layer, the start-up card, SysEx import, the key
# dispatcher, the AUDIO / MIDI panel, the generators), concatenated in this order.
# sync-mmstudio-skin.py reads the same two lists (CSS, JS) from this file for the plug-in's page.
# DEMO: what the demo host (src/54-demo.js, DESIGN-UNIFY.md 4.6) needs on its own, in a script of its own after the
# mockup's: the example's documents (demo-docs.json) and the catalogue, and the page files the plug-in's page loads
# beside the mockup (the document store, the overlay, the translation and the view of the documents).
cd "$(dirname "$0")"
SHARED=../../../source/elektron/md/mdJucePlugin/skins/shared
CSS="src/10-md-base.css src/20-mm.css $SHARED/deskLcd.css $SHARED/deskModal.css $SHARED/deskBoot.css $SHARED/deskSyx.css $SHARED/deskAudio.css $SHARED/deskKeyView.css $SHARED/deskGlobal.css src/25-mm.css"
JS="src/40-data.js src/50-state.js $SHARED/deskGen.js src/52-gen.js src/53-seam.js src/54-demo.js src/55-host.js $SHARED/deskKeys.js $SHARED/deskKeyView.js $SHARED/deskTogglePaint.js $SHARED/deskSelect.js src/56-keys.js $SHARED/deskModal.js $SHARED/deskMenu.js $SHARED/deskCaps.js $SHARED/deskBoot.js $SHARED/deskSyx.js src/60-ui.js src/70-seq.js src/75-comforts.js src/76-gen.js src/77-select.js src/80-notes.js src/85-sound-groups.js src/90-sound.js src/100-mix.js src/110-perform.js src/115-control.js $SHARED/deskGlobal.js src/117-global.js src/120-song.js src/125-lib.js $SHARED/deskAudio.js $SHARED/deskAudioSelfTest.js src/127-audio.js src/130-main.js"
SKIN=../../../source/elektron/md/mdJucePlugin/skins/mmStudio
DEMO="$SHARED/deskDocs.js $SHARED/deskOverlay.js $SKIN/mmConvert.js $SKIN/mmView.js"
{
printf '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>Monomachine Editor</title>\n<meta name="description" content="Screen-native editor mockup for the Elektron Monomachine (SFX-6 / SFX-60, OS 1.32)">\n<link rel="preconnect" href="https://fonts.googleapis.com">\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600&family=IBM+Plex+Mono:wght@400;500&family=Silkscreen&display=swap">\n<style>\n'
cat $CSS
printf '</style>\n'
cat src/30-body.html
printf '<script>\n'
cat $JS
printf '</script>\n<script>\nwindow.MM_DEMO=Object.assign('
cat demo-docs.json
printf ',{catalogue:'
cat ../mm-catalogue.json
printf '});\n'
cat $DEMO
printf '</script>\n'
} > index.html
