#!/usr/bin/env python3
# Regenerates the Monomachine Editor skin (source/elektron/md/mdJucePlugin/skins/mmStudio/:
# mmStudio.html, mmStudio.css, mmMockup.js) from the approved mockup in
# doc/modern-ux/mm-mockup/src/ (copy the design source there first).
#   python3 doc/modern-ux/sync-mmstudio-skin.py          write the skin
#   python3 doc/modern-ux/sync-mmstudio-skin.py --check  only report drift
#
# Unlike the MD skin, the mockup's own script runs in the plug-in: it is the UI, copied as it
# is (P6: no patches). It hands what the machine does to its host (src/55-host.js):
# mmAdapter.js (hand-written, loaded before it) is the plug-in's host and uses the view through
# window.MMView. What the engine cannot do comes from the machine's capabilities at run time.
#  1. Markup: 30-body.html as it is.
#  2. Stylesheet: build.sh's CSS list (the mockup's src/*.css and the shared skins/shared/*.css),
#     the bundled fonts (skins/shared/deskFonts.css) instead of Google Fonts, then the real
#     machine's states (skins/mmStudio/mmOverrides.css).
#  3. Script: build.sh's JS list (src/40-data.js .. 130-main.js and the shared skins/shared/*.js:
#     the generators, the key dispatcher, the modal layer, the start-up card, SysEx import, the
#     AUDIO / MIDI panel), in its order, as they are, each headed with its name.
#  4. Contract check: every MMView member and id mmAdapter.js uses exists (the self-tests,
#     mmSelfTest.js, are checked on their own), the adapter never touches the view's state
#     (S(), MMView.S, LIB, ENG, H, the panel's globals) nor its markup ($(, $$(, document.),
#     the view exports no state or test (S, audioSelfTest: a diagnostics build's MMDiagnostics
#     only), and the host calls match both ways: the mockup makes no HOST.* call the seam does not
#     list, and the seam lists no host call the mockup never makes. The seam is data (src/53-seam.js,
#     MM_SEAM: the host's calls, the view's members), read here as JSON; mmViewTest.js checks the loaded
#     page's window.MMHost and window.MMView against the same lists, both ways.
#     The documents reach S's document members through MMView.show only (DOC_MEMBERS, DESIGN-UNIFY.md 4.5).
#  5. The pages against the contract (page_contract_check.py): every op the adapter sends, and every edit
#     intent the mockup makes (edit("op", {...})), is in $defs/command with only its declared arguments, the
#     panel's audioSet arguments too, and the mockup's NA_SEL plus NA_INFO name exactly the contract's capabilities.
import json
import os
import re
import sys

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + '/'
SRC = R + 'doc/modern-ux/mm-mockup/src/'
SK = R + 'source/elektron/md/mdJucePlugin/skins/mmStudio/'
check_only = '--check' in sys.argv
SHARED = R + 'source/elektron/md/mdJucePlugin/skins/shared/'

build = open(R + 'doc/modern-ux/mm-mockup/build.sh').read()


def parts(var):
    """build.sh's list of files (CSS or JS), in order: (name, path); the shared ones are named shared/<file>"""
    x = re.search(r'^%s="([^"]*)"$' % var, build, re.M)
    assert x, 'build.sh has no %s list' % var
    out = []
    for f in x.group(1).split():
        if f.startswith('$SHARED/'):
            out.append(('shared/' + f[8:], SHARED + f[8:]))
        else:
            assert f.startswith('src/'), 'build.sh %s: %s is neither src/ nor $SHARED/' % (var, f)
            out.append((f[4:], SRC + f[4:]))
    return out


css_files, js_files = parts('CSS'), parts('JS')
body_file = re.search(r'src/(\d+-body\.html)', build).group(1)
title = re.search(r'<title>(.*?)</title>', build).group(1)

# ---- 1. markup ----
m = open(SRC + body_file).read()
# The host first (it defines window.MMHost, which the mockup reads when it loads), with the shared
# document store and overlays it keeps the documents in (DESIGN-UNIFY.md phase 1), then the UI, then
# the translation and the view of the documents (they read the mockup's tables).
# mmSelfTest.js before the mockup: the self-tests, in the plug-in only with the diagnostics (an empty
# script otherwise); it sets window.MMDiagnostics, where the mockup puts what only tests may touch.
# deskJourney.js and mmJourneys.js last: the user journeys, diagnostics builds only too (FOUNDATION.md, Build and check).
SCRIPTS = ['deskBridge.js', 'deskZoom.js', 'deskAbout.js', 'deskDocs.js', 'deskOverlay.js', 'deskDrop.js',
           'mmAdapter.js', 'mmSelfTest.js', 'mmMockup.js', 'mmConvert.js', 'mmView.js',
           'deskJourney.js', 'mmJourneys.js']
page = '''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>%s</title>
<!-- The Monomachine Editor page: the approved mockup (doc/modern-ux/mm-mockup)
     played by the plug-in: mmAdapter.js is its host (P6). Markup, stylesheet and
     mmMockup.js are copied by doc/modern-ux/sync-mmstudio-skin.py; do not edit
     them by hand. The editor inlines stylesheet, scripts and fonts. -->
<link rel="stylesheet" href="mmStudio.css">
<!-- first, before the page draws: the stylesheet without what an older WebKit lacks (macOS 12) -->
<script src="deskCompat.js"></script>
</head>
<body>
%s
%s
</body>
</html>
''' % (title, m.rstrip(), '\n'.join('<script src="%s"></script>' % f for f in SCRIPTS))

# ---- 2. stylesheet ----
# a stylesheet both pages share at a place in a page's own file: "/* @include shared/<file> */" on a line of its own
# (the MD's sync script and build.sh read it the same way), so the cascade stays as it was
INCLUDE = re.compile(r'^/\* @include shared/([\w.-]+\.css) \*/$', re.M)
def with_includes(text):
    return INCLUDE.sub(lambda x: open(SHARED + x.group(1)).read().rstrip('\n'), text)
css = ''.join(with_includes(open(p).read()) for _, p in css_files)
assert 'fonts.googleapis' not in css, 'mockup stylesheet: Google Fonts belong in build.sh\'s <link>, not a stylesheet'
head = """/* Monomachine Editor stylesheet: the mockup's (doc/modern-ux/mm-mockup, with the shared stylesheets)
   as is, with bundled fonts instead of Google Fonts and the real machine's states at the end. Generated by
   doc/modern-ux/sync-mmstudio-skin.py. Fonts: Barlow Condensed, IBM Plex Mono and Silkscreen, SIL Open Font
   License 1.1, shared with the Machinedrum Editor (skins/mdStudio/fonts/, with their OFL-*.txt). */
""" + open(SHARED + 'deskFonts.css').read()
tail = '\n' + open(SK + 'mmOverrides.css').read()
out_css = head + css.strip('\n') + '\n' + tail

# ---- 3. script ----
js = ''
for name, p in js_files:
    js += '/* ---- %s ---- */\n' % name + open(p).read() + '\n'
out_js = '/* Generated by doc/modern-ux/sync-mmstudio-skin.py from doc/modern-ux/mm-mockup/src and skins/shared. Do not edit. */\n' + js

# ---- 4. contract check ----
# The product seam is checked from mmAdapter.js alone (a release page has no self-tests); the
# self-tests (mmSelfTest.js, diagnostics builds) are checked on their own against the same view.
adapter = open(SK + 'mmAdapter.js').read()
selftest = open(SK + 'mmSelfTest.js').read()
view = re.search(r'window\.MMView=\{(.*?)\};\n', js, re.S)
# The seam as data (src/53-seam.js, review finding 16): what a host implements and what the view gives it, one
# list the page and this check read (mmViewTest.js checks the loaded page's MMHost and MMView keys against it).
seam_data = re.search(r'^const MM_SEAM=(\{.*?\});$', js, re.S | re.M)
SEAM = json.loads(seam_data.group(1)) if seam_data else {'host': [], 'view': []}
exported = set(SEAM['view'])
present = set(re.findall(r'id="([\w-]+)"', m)) | set(re.findall(r'id=\\?"([\w-]+)', js)) | set(re.findall(r'\.id = "([\w-]+)"', js))


def seam(text, name):
    """what one script uses of the view and the markup that the mockup does not have"""
    used = set(re.findall(r'V\(\)\.(\w+)', text)) | set(re.findall(r'(?<![.\w])v\.(\w+)', text))
    wanted = set(re.findall(r'\$\("#([A-Za-z][\w-]*)', text)) | set(re.findall(r'getElementById\("([\w-]+)"\)', text))
    out = ['%s uses MMView.%s' % (name, x) for x in sorted(used - exported)]
    out += ['%s uses #%s' % (name, x) for x in sorted(wanted - present)]
    return out, used, wanted


problems, used, wanted = seam(adapter, 'mmAdapter.js')
test_problems, test_used, test_wanted = seam(selftest, 'mmSelfTest.js')
problems += test_problems
# the adapter reads the view through values and setters, never its state
if re.search(r'\bS\(\)|MMView\.S\b|V\(\)\.S\b|(?<![.\w])v\.S\b|\.(?:LIB|ENG|H)\b|(?<![.\w"])(?:AP|drawAudio|openAudio|audioLevel)\b(?!")', adapter):
    problems.append('mmAdapter.js touches the view\'s state (S(), MMView.S, LIB, ENG, H or the panel\'s globals)')
# ... and never its markup: what it shows goes through MMView's setters
code = re.sub(r'/\*.*?\*/|//[^\n]*', '', adapter, flags=re.S)
for pat, what in [(r'(?<![\w.])\$\(', '$('), (r'(?<![\w.])\$\$\(', '$$('), (r'\bdocument\.', 'document.')]:
    if re.search(pat, code):
        problems.append('mmAdapter.js uses the markup (%s): show it through an MMView setter' % what)
# DESIGN-UNIFY.md 4.5, phase 1: the documents reach S's document members through MMView.show only (the
# one writer, from the view the adapter derives). No other member the view exports writes one of them, but
# the empty start, the library's other slots and the BPM gesture's own write. A shallow check (the exported
# function's own body); the list goes once the renderers read the view directly (phase 8).
DOC_MEMBERS = ['tracks', 'midi', 'locks', 'len', 'mult', 'swingAmt', 'patTrn', 'multi', 'menv', 'workName', 'song', 'songSlot',
               'songs', 'routing', 'mmap', 'bpm', 'pat', 'kit', 'kitState', 'queued', 'plays', 'patKit', 'patInfo', 'mode']
DOC_WRITERS = {'show', 'startEmpty', 'setPatternSlot', 'setKitSlot', 'setTempo'}
doc_write = re.compile(r'\bS\.(%s)\b(?:\[[^\]]*\])?\s*=(?!=)' % '|'.join(DOC_MEMBERS))


def body_of(text, name):
    """the body of `function name(...)` in text (params may hold destructuring braces), or None"""
    m = re.search(r'\bfunction %s\(' % re.escape(name), text)
    if not m:
        return None
    i, depth = m.end(), 1
    while depth and i < len(text):
        depth += {'(': 1, ')': -1}.get(text[i], 0)
        i += 1
    i = text.index('{', i)
    start, depth, quote = i, 0, None
    while i < len(text):
        c = text[i]
        if quote:
            if c == '\\':
                i += 1
            elif c == quote:
                quote = None
        elif c in '"\'`':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if not depth:
                return text[start:i + 1]
        i += 1
    return None


if view:
    block = re.sub(r'/\*.*?\*/', '', view.group(1), flags=re.S)
    for name, inline in re.findall(r'(\w+):\(?[\w,{}]*\)?=>(\{[^}]*\}|[^,\n]*)', block):
        if name not in DOC_WRITERS and doc_write.search(inline):
            problems.append('MMView.%s writes a document member of S: only MMView.show does (DESIGN-UNIFY.md 4.5)' % name)
    js_code = re.sub(r'/\*.*?\*/', '', js, flags=re.S)
    for name in sorted(exported - DOC_WRITERS):
        b = body_of(js_code, name)
        if b and doc_write.search(b):
            problems.append('MMView.%s writes S.%s: the documents reach S through MMView.show only (DESIGN-UNIFY.md 4.5)' % (name, doc_write.search(b).group(1)))
    if 'show' not in exported:
        problems.append('MMView has no show: the documents have no way into S')
# the view exports no state and no test: those are a diagnostics build's (window.MMDiagnostics)
problems += ['MMView exports %s: only window.MMDiagnostics may carry it' % x for x in sorted(exported & {'S', 'audioSelfTest'})]
# the pages against the contract: ops, arguments, capabilities
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import page_contract_check as pc
schema = pc.load(R + 'doc/modern-ux/mm-data-contract.schema.json')
table = pc.command_table(schema)
problems += pc.check_sends(list(pc.literals_with_op(adapter)), table, 'mmAdapter.js')
problems += pc.check_audio(js, table, 'mmMockup.js')
# the mockup's edit intents, edit("op", {args}) (DESIGN-UNIFY.md 4.1): declared ops with declared arguments (the host
# adds p or k and g); an argument object with a spread is not checked member by member
edits = []
for args in pc.calls(js, r'edit'):
    if args and re.match(r'^"\w+"$', args[0]):
        edits.append(({args[0][1:-1]}, pc.object_keys(args[1]) if len(args) > 1 and args[1].startswith('{') else None))
problems += pc.check_sends(edits, table, 'mmMockup.js (edit)')
if not edits:
    problems.append('the mockup sends no edit intents: edit("op", {...}) not found')
na_sel = re.search(r'const NA_SEL=\{(.*?)\};', js, re.S)
na_info = re.search(r'const NA_INFO=\[(.*?)\];', js, re.S)
if not na_sel or not na_info:
    problems.append('the mockup has no NA_SEL / NA_INFO')
else:
    problems += pc.check_caps(re.findall(r'(?:^|[,{\n])\s*(\w+):', na_sel.group(1)), re.findall(r'"(\w+)"', na_info.group(1)), schema, 'the mockup')
problems += pc.check_mapping_gate(js, ['setMapping(!window.MMHost)', 'if(ws==="control"&&!S.mapping)return', 'mapping:true,when:()=>S.mapping&&S.learn'], 'the mockup')
problems += pc.check_mapping_gate(adapter, ['V().setMapping(!!doc.enabled)'], 'mmAdapter.js')
# both ways: every host call the mockup makes is one the adapter has, and every one it has is made
if not seam_data:
    problems.append('the mockup has no MM_SEAM (src/53-seam.js)')
host_calls = set(re.findall(r'HOST\.(\w+)', js))
host_methods = set(SEAM['host'])
problems += ['the mockup calls HOST.%s, which the seam (53-seam.js) does not list' % x for x in sorted(host_calls - host_methods)]
problems += ['the seam (53-seam.js) lists host call %s, which the mockup never makes' % x for x in sorted(host_methods - host_calls)]
# LIFE (machine.lifecycle -> the mockup's engine states) and the message types onMessage handles,
# both against the contract, both ways.
life = pc.find_object(adapter, 'LIFE')
if life is None:
    problems.append('mmAdapter.js: no LIFE')
else:
    problems += pc.check_lifecycle(pc.object_keys(life) or set(), schema, 'mmAdapter.js')
# the shared page files the page loads beside the adapter handle some types themselves (deskZoom.js: zoom; deskDrop.js:
# drop, dragFiles) and send commands of their own (deskZoom.js: pageZoom; deskDrop.js: dropRom, dropSyx)
beside = ''.join(open(SHARED + f).read() for f in SCRIPTS if f.startswith('desk') and f != 'deskJourney.js')
problems += pc.check_message_types(pc.message_types_handled(adapter + beside), schema,
                                   'mmAdapter.js and the shared page files')
problems += pc.check_sends(list(pc.literals_with_op(beside)), table, 'the shared page files')
if problems:
    print('contract check: ' + '; '.join(problems))
    sys.exit(1)

outs = [('mmStudio.html', page), ('mmStudio.css', out_css), ('mmMockup.js', out_js)]
if check_only:
    drift = [n for n, text in outs if not os.path.exists(SK + n) or open(SK + n).read() != text]
    print('drift: ' + ', '.join(drift) if drift else 'in step with the mockup')
    sys.exit(1 if drift else 0)
for n, text in outs:
    open(SK + n, 'w').write(text)
print('synced: %d bytes markup, %d bytes stylesheet, %d bytes script; %d view members, %d host calls and %d ids checked (self-tests: %d members, %d ids)'
      % (len(page), len(out_css), len(out_js), len(used), len(host_methods), len(wanted), len(test_used), len(test_wanted)))
