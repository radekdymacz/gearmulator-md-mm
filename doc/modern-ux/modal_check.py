# The modal layer (P7) is one block of script and one of stylesheet, the same text in the MD mockup,
# the MM mockup (src/57-modal.js, src/20-mm.css) and the MD skin (mdDeskModal.js; its stylesheet is the
# MD mockup's). Both sync scripts call check() and stop on drift, as for the AUDIO / MIDI panel.
import re

R = re.compile(r'/\* MODAL BEGIN.*?/\* MODAL END \*/', re.S)


def blocks(path):
    return R.findall(open(path).read())


def check(root):
    md = blocks(root + 'doc/modern-ux/mockup/index.html')
    if len(md) != 2:
        return ['the MD mockup needs one MODAL stylesheet block and one script block, has %d' % len(md)]
    css, js = md
    problems = []
    if blocks(root + 'doc/modern-ux/mm-mockup/src/20-mm.css') != [css]:
        problems.append('the MM mockup MODAL stylesheet block differs from the MD mockup')
    if blocks(root + 'doc/modern-ux/mm-mockup/src/57-modal.js') != [js]:
        problems.append('the MM mockup MODAL script (src/57-modal.js) differs from the MD mockup')
    if blocks(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskModal.js') != [js]:
        problems.append('the MD skin MODAL script (mdDeskModal.js) differs from the MD mockup')
    return problems
