#!/bin/sh
cd "$(dirname "$0")"
{
printf '<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>Monomachine Editor</title>\n<meta name="description" content="Screen-native editor mockup for the Elektron Monomachine (SFX-6 / SFX-60, OS 1.32)">\n<link rel="preconnect" href="https://fonts.googleapis.com">\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600&family=IBM+Plex+Mono:wght@400;500&family=Silkscreen&display=swap">\n<style>\n'
cat src/10-md-base.css src/20-mm.css
printf '</style>\n'
cat src/30-body.html
printf '<script>\n'
cat src/40-data.js src/50-state.js src/55-host.js src/60-ui.js src/70-seq.js src/80-notes.js src/90-sound.js src/100-mix.js src/110-perform.js src/115-control.js src/120-song.js src/125-lib.js src/127-audio.js src/130-main.js
printf '</script>\n'
} > index.html
