# The AUDIO / MIDI panel (P6) is one block of script and one of stylesheet, the same text in the MD
# mockup, the MM mockup and the MD skin (the MM skin is generated from its mockup). Both skin sync
# scripts call check() and stop on drift, so a design round changes all copies or none.
import re

R_JS = re.compile(r'/\* AUDIO-MIDI PANEL BEGIN.*?AUDIO-MIDI PANEL END \*/', re.S)


def blocks(path):
    text = open(path).read()
    return R_JS.findall(text)


def check(root):
    md = blocks(root + 'doc/modern-ux/mockup/index.html')
    mm_css = blocks(root + 'doc/modern-ux/mm-mockup/src/20-mm.css')
    mm_js = blocks(root + 'doc/modern-ux/mm-mockup/src/127-audio.js')
    skin_js = blocks(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskAudio.js')
    problems = []
    if len(md) != 2:
        problems.append('the MD mockup needs one stylesheet and one script block, has %d blocks' % len(md))
    else:
        if mm_css != [md[0]]:
            problems.append('the MM mockup stylesheet block differs from the MD mockup')
        if mm_js != [md[1]]:
            problems.append('the MM mockup script block (src/127-audio.js) differs from the MD mockup')
        if skin_js != [md[1]]:
            problems.append('the MD skin (mdDeskAudio.js) differs from the MD mockup')
    return problems
