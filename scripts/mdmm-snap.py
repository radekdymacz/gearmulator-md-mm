#!/usr/bin/env python3
"""Screenshots of an editor's page without Screen Recording (macOS refuses screencapture to a shell without it):
a journey calls Journey.snapshot(name) (skins/shared/deskJourney.js), which logs the page's DOM with its canvases'
drawings in pieces ("SNAP <name> <i>/<n> <text>"); this script joins them from the page logs given, writes
<out>/<name>.html beside the skin's stylesheet and fonts (a <base> to the skin folder in the source tree) and
renders <out>/<name>.png in headless Chrome at the window's size.
  scripts/mdmm-snap.py <out folder> <page log>...
  e.g. after scripts/mdmm-journeys.sh --background md journey-md-shots-sound:
       scripts/mdmm-snap.py /tmp/shots temp/journeys/<run>/*.log
Environment: MDMM_CHROME the Chrome binary (default: Google Chrome in /Applications)."""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKINS = os.path.join(ROOT, 'source/elektron/md/mdJucePlugin/skins')
CHROME = os.environ.get('MDMM_CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
parts = {}
for log in sys.argv[2:]:
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'SNAP (\S+) (\d+)/(\d+) (.*)$', line.rstrip('\r\n'))
        if m:
            parts.setdefault(m.group(1), {})[int(m.group(2))] = (int(m.group(3)), m.group(4))
for name, p in sorted(parts.items()):
    n = next(iter(p.values()))[0]
    if len(p) != n:
        print('%s: %d of %d pieces' % (name, len(p), n))
        continue
    html = json.loads(''.join(p[i][1] for i in range(1, n + 1)))
    skin = 'mmStudio' if 'mmStudio.css' in html or 'mmMockup' in html else 'mdStudio'
    size = re.search(r'data-snap-size="(\d+)x(\d+)"', html)
    w, h = (int(size.group(1)), int(size.group(2))) if size else (1440, 900)
    html = re.sub(r'<head>', '<head><base href="file://%s/%s/">' % (SKINS, skin), html, count=1)
    page = os.path.join(out, name + '.html')
    open(page, 'w', encoding='utf-8').write(html)
    png = os.path.join(out, name + '.png')
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files',
                    '--force-device-scale-factor=2', '--window-size=%d,%d' % (w, h), '--screenshot=' + png, 'file://' + page],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    print('%s %dx%d' % (png, w, h))
