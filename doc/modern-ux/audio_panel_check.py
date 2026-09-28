# The AUDIO / MIDI panel (P6) is one block of script and one of stylesheet, the same text in the MD
# mockup, the MM mockup and the MD skin (the MM skin is generated from its mockup). Both skin sync
# scripts call check() and stop on drift, so a design round changes all copies or none.
# The panel's self-test (audioSelfTest, the end of the script block) is diagnostics: the mockups keep
# it in the block, the MD skin has it in mdDeskSelfTest.js (the self-test bundle), not in the page.
import re

R_JS = re.compile(r'/\* AUDIO-MIDI PANEL BEGIN.*?AUDIO-MIDI PANEL END \*/', re.S)
TEST_START, END = "/* The panel's self-test", '/* AUDIO-MIDI PANEL END */'


def self_test(block):
    """The block without its self-test, and the self-test."""
    i, j = block.find(TEST_START), block.rfind(END)
    return (block, '') if i < 0 else (block[:i] + block[j:], block[i:j])


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
        panel, test = self_test(md[1])
        if skin_js != [panel]:
            problems.append('the MD skin (mdDeskAudio.js) differs from the MD mockup (without its self-test)')
        if not test or test not in open(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskSelfTest.js').read():
            problems.append('the MD skin self-test (mdDeskSelfTest.js) lacks the MD mockup panel self-test')
    return problems
