#!/usr/bin/env python3
# Regenerates the Machinedrum Editor skin's markup and stylesheet
# (source/elektron/md/mdJucePlugin/skins/mdStudio/mdStudio.html, mdDesk.css)
# from the approved mockup, doc/modern-ux/mockup/index.html, so a design round
# needs this script plus the behaviour the round changed in the page's scripts (APP below).
#   python3 doc/modern-ux/sync-mdstudio-skin.py          write the skin
#   python3 doc/modern-ux/sync-mdstudio-skin.py --check  only report drift
#
# What it does, in order:
#  1. Markup: the mockup's <div class="app"> as is, plus the real machine's
#     status and error lines. Mockup-only elements (the first-run preview link,
#     the rules footer) must not come back. Controls the real machine cannot do
#     yet are marked disabled here (HONEST below), so the page never offers them.
#  2. Stylesheet: the mockup's, in its order: its own <style> blocks and the
#     shared stylesheets it links (skins/shared/*.css: the LCD, the modal layer,
#     the start-up card, SysEx import, the AUDIO / MIDI panel), with the bundled
#     fonts (skins/shared/deskFonts.css) instead of Google Fonts, then the real
#     machine's states (skins/mdStudio/mdOverrides.css). Every text replacement
#     is asserted to match.
#  3. Contract check: every element id the page's scripts look up must exist
#     in the markup or be made by the scripts. A design round that renames or
#     drops one fails here instead of in the plug-in.
# The mockup's own script is example state and is never copied; the page files
# both editors share (skins/shared/*.js) are loaded by the mockup and the page
# alike, so there is nothing to copy or compare.
import os
import re
import sys

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + '/'
SK = R + 'source/elektron/md/mdJucePlugin/skins/mdStudio/'
SHARED = R + 'source/elektron/md/mdJucePlugin/skins/shared/'
MOCKUP = R + 'doc/modern-ux/mockup/index.html'
check_only = '--check' in sys.argv

src = open(MOCKUP).read()
m = src[src.index('<div class="app">'):src.index('<script')]


def mockup_css(page):
    """The mockup's stylesheet in cascade order: its <style> blocks and the local stylesheets it links,
    read from their files (a shared block replaced its own text in the mockup at the same place)."""
    head = page[page.index('<style>'):page.index('<div class="app">')]
    out, prev = '', None
    for x in re.finditer(r'<style>(.*?)</style>|<link rel="stylesheet" href="([^"]+)">', head, re.S):
        if x.group(1) is not None:
            text = x.group(1)
            if prev == 'link':	# the newline after a linked block is the file's own last one
                assert text.startswith('\n'), 'mockup: a <style> after a linked stylesheet starts on a new line'
                text = text[1:]
            out, prev = out + text, 'style'
        else:
            out, prev = out + open(os.path.join(os.path.dirname(MOCKUP), x.group(2))).read(), 'link'
    return out


css = mockup_css(src)

# ---- 1. markup ----
MOCK_ONLY = [r'<p class="firstlink">', r'<details class="rules">']
for pattern in MOCK_ONLY:
    assert not re.search(pattern, m), 'mockup: a mockup-only element came back (%s): drop it in the mockup' % pattern
BODY = '  <div class="body" id="body">'
assert m.count(BODY) == 1, 'mockup: no #body, or more than one'
m = m.replace(BODY, '  <p class="statusline" id="status" role="status" hidden></p>\n'
              '  <p class="errline" id="errline" role="alert" hidden></p>\n' + BODY)
# Controls the machine cannot do yet: (mockup text, skin text). Each must match.
HONEST = [
    # P4: HW MIDI is real (the plug-in's MIDI in/out); nothing to disable here now.
]
for a, b in HONEST:
    assert a in m, 'mockup changed: ' + a
    m = m.replace(a, b)
# The page's modules, in load order (a module that does not exist yet is skipped).
# mdDeskSelfTest.js last: the self-tests, in the plug-in only with the diagnostics (an empty script otherwise).
# desk*.js are the files both editors share (skins/shared/, the editor finds a page file by its name);
# deskAudioSelfTest.js goes with the self-tests (diagnostics builds only).
# APP is the page's own app, one file a concern (mdDeskApp.js says what each holds), in load order: each only
# defines at load, and mdDeskRender.js (last) renders the page and says it is ready.
APP = ['mdDeskApp.js', 'mdDeskSoundGroups.js', 'mdDeskTop.js', 'mdDeskSeq.js', 'mdDeskSound.js', 'mdDeskEditors.js', 'mdDeskMix.js',
       'mdDeskSampler.js', 'mdDeskSong.js', 'mdDeskPicker.js', 'mdDeskControl.js', 'mdDeskGenUi.js', 'mdDeskComforts.js', 'mdDeskRom.js',
       'mdDeskGestures.js', 'mdDeskRender.js']
SCRIPTS = ['deskModal.js', 'deskCaps.js', 'deskBoot.js', 'deskSyx.js', 'deskBridge.js', 'deskDocs.js', 'deskOverlay.js', 'mdDeskModel.js', 'deskGen.js', 'mdDeskGen.js',
           'deskKeys.js', 'mdDeskKeys.js', 'mdDeskMod.js', 'deskTogglePaint.js'] + APP + ['mdDeskLive.js', 'mdDeskLibrary.js', 'mdDeskGlobal.js', 'deskAudio.js', 'mdDeskAudio.js',
           'deskAudioSelfTest.js', 'mdDeskSelfTest.js']
for f in APP:
    assert os.path.exists(SK + f), 'APP lists %s, which is not in skins/mdStudio/' % f
# every page script in the skin is loaded (a new file not in SCRIPTS would never run), the node tests apart
unlisted = sorted(f for f in os.listdir(SK) if f.endswith('.js') and not f.endswith('Test.js') and f not in SCRIPTS)
assert not unlisted, 'skins/mdStudio has page scripts SCRIPTS does not load: ' + ', '.join(unlisted)


def script_path(f):
    return (SHARED if f.startswith('desk') else SK) + f


title = re.search(r'<title>(.*?)</title>', src).group(1)
page = '''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>%s</title>
<!-- The Machinedrum Editor page: the approved mockup
     doc/modern-ux/mockup/index.html on the plug-in's documents. Markup and
     stylesheet are generated by doc/modern-ux/sync-mdstudio-skin.py; do not
     edit them by hand. The editor inlines the stylesheet, scripts and any
     bundled fonts before loading (WKWebView reads only this file). -->
<link rel="stylesheet" href="mdDesk.css">
</head>
<body>
%s
%s
</body>
</html>
''' % (title, m.rstrip(), '\n'.join('<script src="%s"></script>' % f for f in SCRIPTS if os.path.exists(script_path(f))))

# ---- 2. stylesheet ----
head = """/* Machinedrum Editor stylesheet: the mockup's (doc/modern-ux/mockup/index.html, with the shared
   stylesheets it links) as is, with bundled fonts instead of Google Fonts and the real machine's states
   at the end. Generated by doc/modern-ux/sync-mdstudio-skin.py. Fonts: Barlow Condensed, IBM Plex Mono and
   Silkscreen, SIL Open Font License 1.1, in skins/mdStudio/fonts/ with their OFL-*.txt; the editor inlines them. */
""" + open(SHARED + 'deskFonts.css').read()
for a, b in [('--sans:"Barlow Condensed","Arial Narrow",system-ui,sans-serif;',
              '--sans:"Barlow Condensed","Avenir Next Condensed","Arial Narrow",system-ui,sans-serif;'),
             ('--mono:"IBM Plex Mono",ui-monospace,Menlo,monospace;', '--mono:"IBM Plex Mono",Menlo,ui-monospace,monospace;'),
             ('--pix:"Silkscreen",ui-monospace,monospace;', '--pix:"Silkscreen",Menlo,ui-monospace,monospace;')]:
    assert css.count(a) == 1, 'mockup stylesheet: ' + a
    css = css.replace(a, b)
assert 'fonts.googleapis' not in css, 'mockup stylesheet: Google Fonts belong in the mockup\'s <link>, not its stylesheet'
tail = '\n' + open(SK + 'mdOverrides.css').read()
out_css = head + css.strip('\n') + '\n' + tail

# ---- 3. contract check: ids the scripts look up ----
scripts = ''.join(open(d + f).read() for d in (SK, SHARED) for f in sorted(os.listdir(d)) if f.endswith('.js'))
wanted = (set(re.findall(r'\$\("#([A-Za-z][\w-]*)', scripts)) | set(re.findall(r'getElementById\("([\w-]+)"\)', scripts))
          | set(re.findall(r'closest\("#([A-Za-z][\w-]*)"\)', scripts)))
made = (set(re.findall(r'id="([A-Za-z][\w-]*)"', scripts)) | set(re.findall(r'id=\\"([\w-]+)', scripts))
        | set(re.findall(r'\.id\s*=\s*"([\w-]+)"', scripts)))
present = set(re.findall(r'id="([\w-]+)"', m))
# the MIDI mapping UI (the Control tab, LEARN) is out of the markup until the controller feature (FOUNDATION.md)
optional = {'learnkey'}
missing = sorted(wanted - present - made - optional)
if missing:
    print('contract check: the page looks up ids the mockup markup no longer has:', ', '.join(missing))
    sys.exit(1)

# ---- the pages against the contract: the ops they send, their arguments, the capabilities ----
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import page_contract_check as pc
schema = pc.load(R + 'doc/modern-ux/md-data-contract.schema.json')
table = pc.command_table(schema)
page_js = {f: open(script_path(f)).read() for f in SCRIPTS if os.path.exists(script_path(f))}
problems = []
for f, text in page_js.items():
    sends = []
    for args in pc.calls(text, 'cmd'):
        keys = pc.object_keys(args[1]) if len(args) > 1 and args[1].startswith('{') else (set() if len(args) == 1 else None)
        sends.append((pc.ops_of(args[0]), keys))
    for args in pc.calls(text, 'songCmd'):
        keys = pc.object_keys(args[1]) if len(args) > 1 and args[1].startswith('{') else None
        sends.append((pc.ops_of(args[0]), None if keys is None else keys | {'s'}))
    sends += list(pc.literals_with_op(text))
    problems += pc.check_sends(sends, table, f)
    problems += pc.check_audio(text, table, f)
app = ''.join(page_js[f] for f in APP)	# the app's files, as one text (each table is in one of them)
cap_table = re.search(r'const CAP_CONTROLS = \{(.*?)\};', app, re.S)
cap_info = re.search(r'const CAP_INFO = \[(.*?)\];', app, re.S)
if not cap_table or not cap_info:
    problems.append('the app: no CAP_CONTROLS / CAP_INFO')
else:
    problems += pc.check_caps(re.findall(r'^\s*(\w+):', cap_table.group(1), re.M), re.findall(r'"(\w+)"', cap_info.group(1)), schema, 'the app (mdDeskTop.js)')
# LIFE (machine.lifecycle -> the page's words) and the message types onMessage handles, both
# against the contract, both ways (a page can gate on nothing the contract will never send, and
# nothing the contract sends can go unhandled).
life = pc.find_object(app, 'LIFE')
if life is None:
    problems.append('the app: no LIFE')
else:
    problems += pc.check_lifecycle(pc.object_keys(life) or set(), schema, 'the app (mdDeskTop.js)')
problems += pc.check_mapping_gate(app, ['S.mapping = false;', 'applyMapping(m.doc.enabled)', 'when: () => S.mapping && S.ctl.learn',
                                        'mapping: ws === "control", when: ws === "control" ? () => S.mapping : null',
                                        'tb.dataset.ws === "control" && !S.mapping'], 'the app')
all_js = ''.join(page_js.values())
problems += pc.check_message_types(pc.message_types_handled(all_js), schema, 'mdStudio')
if problems:
    print('contract check: ' + '; '.join(problems))
    sys.exit(1)

if check_only:
    now_html = open(SK + 'mdStudio.html').read()
    now_css = open(SK + 'mdDesk.css').read()
    drift = [n for n, a, b in [('mdStudio.html', now_html, page), ('mdDesk.css', now_css, out_css)] if a != b]
    print('drift: ' + ', '.join(drift) if drift else 'in step with the mockup')
    sys.exit(1 if drift else 0)
open(SK + 'mdStudio.html', 'w').write(page)
open(SK + 'mdDesk.css', 'w').write(out_css)
print('synced: %d bytes markup, %d bytes stylesheet, %d ids checked' % (len(page), len(out_css), len(wanted)))
