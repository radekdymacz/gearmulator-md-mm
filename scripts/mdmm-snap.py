#!/usr/bin/env python3
"""Screenshots of an editor's page without Screen Recording (macOS refuses screencapture to a shell without it):
a journey calls Journey.snapshot(name) (skins/shared/deskJourney.js), which logs the page's DOM with its canvases'
drawings in pieces ("SNAP <name> <i>/<n> <text>"); this script joins them from the page logs given, writes
<out>/<name>.html beside the skin's stylesheet and fonts (a <base> to the skin folder in the source tree) and
renders <out>/<name>.png in headless Chrome at the window's size (the page's own, not Chrome's window: headless
Chrome lays a page out shorter than its window, so the window is taller by that much and the picture cut to size).
  scripts/mdmm-snap.py [--size WxH]... [--safari15] [--check] <out folder> <page log>...
  e.g. after scripts/mdmm-journeys.sh --background md journey-md-shots-sound:
       scripts/mdmm-snap.py /tmp/shots temp/journeys/<run>/*.log
  --size WxH   lay the page out at that size instead of the one it was taken at (again for more sizes):
               <name>-WxH.png. The page's scripts do not run again: right for the CSS layout (the Sound rows), not
               for what a script sized
  --safari15   the page as macOS 12's WebKit 15 reads its stylesheets (B-054): no subgrid, no :has(), the colour
               rewrite (skins/shared/deskCompat.js safari15, ?compat=safari15): <name>[-WxH]-safari15.png
  --check      the Sound rows as drawn (skins/shared/deskSoundLayout.js soundRowsCheck): a line each, "CHECK <png>
               ok|FAIL ..."; exits 1 when one fails
Environment: MDMM_CHROME the Chrome binary (default: Google Chrome in /Applications)."""
import html as html_lib, json, os, re, struct, subprocess, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKINS = os.path.join(ROOT, 'source/elektron/md/mdJucePlugin/skins')
CHROME = os.environ.get('MDMM_CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
args, sizes, safari15, check = sys.argv[1:], [], False, False
while args and args[0].startswith('--'):
    a = args.pop(0)
    if a == '--size': sizes.append(tuple(int(x) for x in args.pop(0).split('x')))
    elif a == '--safari15': safari15 = True
    elif a == '--check': check = True
    else: sys.exit('unknown option ' + a)
if len(args) < 2: sys.exit(__doc__)
out = args[0]
os.makedirs(out, exist_ok=True)

def chrome(*a, tries=3):
    """headless Chrome, again when it fails now and then (exit 2 under load)"""
    for i in range(tries):
        r = subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files'] + list(a),
                           capture_output=True, text=True, timeout=120)
        if r.returncode == 0 or i == tries - 1:
            r.check_returncode()
            return r.stdout

def short_by():
    """how much shorter than its window headless Chrome lays a page out"""
    probe = os.path.join(out, '.viewport.html')
    open(probe, 'w').write('<body><script>document.body.textContent="VP "+innerHeight</script>')
    m = re.search(r'VP (\d+)', chrome('--window-size=800,800', '--dump-dom', 'file://' + probe))
    os.remove(probe)
    return 800 - int(m.group(1)) if m else 0

def crop_rows(path, keep):
    """the picture's first `keep` rows (PNG rows filter on the row above only, so cutting the bottom is safe)"""
    d, pos, chunks = open(path, 'rb').read(), 8, []
    while pos < len(d):
        n, = struct.unpack('>I', d[pos:pos + 4]); chunks.append((d[pos + 4:pos + 8], d[pos + 8:pos + 8 + n])); pos += 12 + n
    ihdr = chunks[0][1]; w, h, depth, kind = struct.unpack('>IIBB', ihdr[:10])
    if keep >= h: return
    row = 1 + w * {0: 1, 2: 3, 4: 2, 6: 4}[kind] * depth // 8
    raw = zlib.decompress(b''.join(c for t, c in chunks if t == b'IDAT'))[:keep * row]
    ch = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(path, 'wb').write(d[:8] + ch(b'IHDR', struct.pack('>II', w, keep) + ihdr[8:]) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))

CHECK_JS = ('<script src="file://%s/shared/deskSoundLayout.js"></script><script>addEventListener("load",()=>setTimeout(()=>{'
            'const r=soundRowsCheck(document),p=document.createElement("pre");p.id="snapcheck";p.textContent="SOUNDCHECK "+'
            'JSON.stringify(Object.assign(r,{size:innerWidth+"x"+innerHeight}))+" END";document.body.appendChild(p)},300))</script>' % SKINS)
parts = {}
for log in args[1:]:
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'SNAP (\S+) (\d+)/(\d+) (.*)$', line.rstrip('\r\n'))
        if m:
            parts.setdefault(m.group(1), {})[int(m.group(2))] = (int(m.group(3)), m.group(4))
extra, failed = short_by(), 0
for name, p in sorted(parts.items()):
    n = next(iter(p.values()))[0]
    if len(p) != n:
        print('%s: %d of %d pieces' % (name, len(p), n))
        continue
    html = json.loads(''.join(p[i][1] for i in range(1, n + 1)))
    skin = 'mmStudio' if 'mmStudio.css' in html or 'mmMockup' in html else 'mdStudio'
    size = re.search(r'data-snap-size="(\d+)x(\d+)"', html)
    taken = (int(size.group(1)), int(size.group(2))) if size else (1440, 900)
    html = re.sub(r'<head>', '<head><base href="file://%s/%s/">' % (SKINS, skin), html, count=1)
    if safari15:	# after the stylesheets, as the page has it: rewrites them in place before the first paint
        html = html.replace('</head>', '<script src="file://%s/shared/deskCompat.js"></script></head>' % SKINS, 1)
    for w, h in sizes or [taken]:
        stem = name + ('-%dx%d' % (w, h) if sizes else '') + ('-safari15' if safari15 else '')
        page = os.path.join(out, stem + '.html')
        open(page, 'w', encoding='utf-8').write(html)
        url = 'file://' + page + ('?compat=safari15' if safari15 else '')
        png = os.path.join(out, stem + '.png')
        chrome('--force-device-scale-factor=2', '--window-size=%d,%d' % (w, h + extra), '--screenshot=' + png, url)
        crop_rows(png, 2 * h)
        print('%s %dx%d' % (png, w, h))
        if check:
            open(page, 'w', encoding='utf-8').write(html.replace('</body>', CHECK_JS + '</body>', 1))
            m = re.search(r'SOUNDCHECK (\{.*?\}) END', chrome('--window-size=%d,%d' % (w, h + extra), '--virtual-time-budget=3000', '--dump-dom', url))
            r = json.loads(html_lib.unescape(m.group(1))) if m else {'problems': ['no measurement'], 'screens': []}
            if not r['screens'] and not r['problems']: print('CHECK %s no Sound rows with screens' % png)
            elif r['problems']: failed += 1; print('CHECK %s FAIL at %s: %s' % (png, r.get('size'), '; '.join(r['problems'])))
            else: print('CHECK %s ok at %s: screens %s px' % (png, r['size'], sorted(set(r['screens']))))
            open(page, 'w', encoding='utf-8').write(html)
sys.exit(1 if failed else 0)
