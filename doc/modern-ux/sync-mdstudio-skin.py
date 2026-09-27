#!/usr/bin/env python3
# Regenerates the Machinedrum Editor skin's markup and stylesheet
# (source/elektron/md/mdJucePlugin/skins/mdStudio/mdStudio.html, mdDesk.css)
# from the approved mockup, doc/modern-ux/mockup/index.html, so a design round
# only needs this script. The page's behaviour (mdDeskApp.js) is ported by
# hand; the mockup's script is example state and is not copied.
#   python3 doc/modern-ux/sync-mdstudio-skin.py
import os
import re
R=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')+'/'
SK=R+'source/elektron/md/mdJucePlugin/skins/mdStudio/'
src=open(R+'doc/modern-ux/mockup/index.html').read()
css=src[src.index('<style>')+7:src.index('</style>')]
m=src[src.index('<div class="app">'):src.index('<script>')]
first=re.search(r'<p class="firstlink">.*?</p>', m, re.S)
assert first, 'firstlink'
m=m.replace(first.group(0),'<p class="firstlink"><span class="note">Machinedrum OS 1.63 · every edit goes to the emulated machine and is read back</span></p>')
m=m.replace('  <div class="body" id="body">','  <p class="statusline" id="status" role="status" hidden></p>\n  <p class="errline" id="errline" role="alert" hidden></p>\n  <div class="body" id="body">')
assert 'id="status"' in m
title=re.search(r'<title>(.*?)</title>',src).group(1)
page='''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>%s</title>
<!-- The Machinedrum Editor page (P2): the approved mockup
     doc/modern-ux/mockup/index.html on the plug-in's documents. Markup and
     stylesheet follow the mockup; the editor inlines the stylesheet, scripts
     and any bundled fonts before loading (WKWebView reads only this file). -->
<link rel="stylesheet" href="mdDesk.css">
</head>
<body>
%s
<script src="mdDeskBridge.js"></script>
<script src="mdDeskModel.js"></script>
<script src="mdDeskApp.js"></script>
</body>
</html>
''' % (title, m.rstrip())
open(SK+'mdStudio.html','w').write(page)
head='''/* Machinedrum Editor stylesheet: the mockup's (doc/modern-ux/mockup/index.html)
   as is, with local fonts instead of Google Fonts and a few P2 states at the
   end. Fonts: Barlow Condensed, IBM Plex Mono and Silkscreen are SIL Open Font
   License 1.1. They are not in the repository: drop the .woff2 files named
   below into skins/mdStudio/fonts/ (with OFL.txt) and the editor bundles them;
   without them the page falls back to system fonts. */
@font-face{font-family:"Barlow Condensed";font-weight:500;src:local("Barlow Condensed Medium"),local("BarlowCondensed-Medium"),url("fonts/BarlowCondensed-Medium.woff2") format("woff2")}
@font-face{font-family:"Barlow Condensed";font-weight:600;src:local("Barlow Condensed SemiBold"),local("BarlowCondensed-SemiBold"),url("fonts/BarlowCondensed-SemiBold.woff2") format("woff2")}
@font-face{font-family:"Barlow Condensed";font-weight:700;src:local("Barlow Condensed Bold"),local("BarlowCondensed-Bold"),url("fonts/BarlowCondensed-Bold.woff2") format("woff2")}
@font-face{font-family:"IBM Plex Mono";font-weight:400;src:local("IBM Plex Mono"),local("IBMPlexMono"),url("fonts/IBMPlexMono-Regular.woff2") format("woff2")}
@font-face{font-family:"IBM Plex Mono";font-weight:500;src:local("IBM Plex Mono Medium"),local("IBMPlexMono-Medium"),url("fonts/IBMPlexMono-Medium.woff2") format("woff2")}
@font-face{font-family:"Silkscreen";font-weight:400;src:local("Silkscreen"),local("Silkscreen-Regular"),url("fonts/Silkscreen-Regular.woff2") format("woff2")}
'''
for a,b in [('--sans:"Barlow Condensed","Arial Narrow",system-ui,sans-serif;','--sans:"Barlow Condensed","Avenir Next Condensed","Arial Narrow",system-ui,sans-serif;'),
            ('--mono:"IBM Plex Mono",ui-monospace,Menlo,monospace;','--mono:"IBM Plex Mono",Menlo,ui-monospace,monospace;'),
            ('--pix:"Silkscreen",ui-monospace,monospace;','--pix:"Silkscreen",Menlo,ui-monospace,monospace;')]:
    assert a in css, a
    css=css.replace(a,b)
tail='''
/* ===== P2: states the real machine has ===== */
.st.past{opacity:.45}
.statusline,.errline{margin:6px 0 0;padding:6px 10px;border-radius:3px;font:12px var(--pix);text-transform:uppercase}
.statusline{background:var(--lcd);color:var(--ink)}
.errline{background:var(--rec);color:#fff}
.firstlink .note{font-size:12px}
'''
open(SK+'mdDesk.css','w').write(head+css.strip('\n')+'\n'+tail)
print('synced', len(page), len(css))
