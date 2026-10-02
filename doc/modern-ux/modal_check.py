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
    # the start-up card (BOOT blocks): the same pairs of copies
    RB = re.compile(r'/\* BOOT BEGIN.*?/\* BOOT END \*/', re.S)
    mdb = RB.findall(open(root + 'doc/modern-ux/mockup/index.html').read())
    if len(mdb) != 2:
        return problems + ['the MD mockup needs one BOOT stylesheet block and one script block, has %d' % len(mdb)]
    if RB.findall(open(root + 'doc/modern-ux/mm-mockup/src/20-mm.css').read()) != [mdb[0]]:
        problems.append('the MM mockup BOOT stylesheet block differs from the MD mockup')
    if RB.findall(open(root + 'doc/modern-ux/mm-mockup/src/58-boot.js').read()) != [mdb[1]]:
        problems.append('the MM mockup BOOT script (src/58-boot.js) differs from the MD mockup')
    if RB.findall(open(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskBoot.js').read()) != [mdb[1]]:
        problems.append('the MD skin BOOT script (mdDeskBoot.js) differs from the MD mockup')
    # SysEx import (SYX blocks)
    RS = re.compile(r'/\* SYX BEGIN.*?/\* SYX END \*/', re.S)
    mds = RS.findall(open(root + 'doc/modern-ux/mockup/index.html').read())
    if len(mds) != 2:
        return problems + ['the MD mockup needs one SYX stylesheet block and one script block, has %d' % len(mds)]
    if RS.findall(open(root + 'doc/modern-ux/mm-mockup/src/20-mm.css').read()) != [mds[0]]:
        problems.append('the MM mockup SYX stylesheet block differs from the MD mockup')
    if RS.findall(open(root + 'doc/modern-ux/mm-mockup/src/59-syx.js').read()) != [mds[1]]:
        problems.append('the MM mockup SYX script (src/59-syx.js) differs from the MD mockup')
    if RS.findall(open(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskSyx.js').read()) != [mds[1]]:
        problems.append('the MD skin SYX script (mdDeskSyx.js) differs from the MD mockup')
    # the key map's dispatcher (KEYS block): the MD skin's mdDeskKeys.js and the MM mockup's src/56-keys.js
    RK = re.compile(r'/\* KEYS BEGIN.*?/\* KEYS END \*/', re.S)
    mdk = RK.findall(open(root + 'source/elektron/md/mdJucePlugin/skins/mdStudio/mdDeskKeys.js').read())
    if len(mdk) != 1:
        return problems + ['the MD skin needs one KEYS block in mdDeskKeys.js, has %d' % len(mdk)]
    if RK.findall(open(root + 'doc/modern-ux/mm-mockup/src/56-keys.js').read()) != mdk:
        problems.append('the MM mockup KEYS block (src/56-keys.js) differs from the MD skin (mdDeskKeys.js)')
    return problems
