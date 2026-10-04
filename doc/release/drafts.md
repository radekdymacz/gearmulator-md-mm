> **Superseded:** this is the v0.1.0-alpha draft. Some claims (app LFOs) no longer hold. See v0.3.0.md.

# v0.1.0-alpha: drafts for Radek to send

Nothing here has been sent. Edit freely.

## 1. Discord post

Channel: the Gearmulator Discord, #gearmulator-development (or wherever joelanders prefers, see
message 2). Post only after the release is published and the site's download buttons work.

> **Machinedrum Editor + Monomachine Editor 0.1.0 alpha**
>
> I've been building screen-native editors on top of joelanders' MD/MM emulation. It's the whole
> machine on one page: step grid, p-lock lanes, kit/pattern library, chains, mutes, app LFOs. The
> Monomachine gets a piano-roll sequencer. Both run the real firmware, as a standalone app, VST3 and
> AU on macOS.
>
> There's also a HW MIDI mode that drives a real machine instead of the emulator. It's beta and I
> haven't tried it on real hardware yet, so if you have an MD or MM on a MIDI interface I'd love to
> hear how it goes.
>
> Download and screenshots: https://mdmm.nativekloud.com
> Release notes and source (GPL-3): https://github.com/radekdymacz/gearmulator-md-mm/releases
>
> Bring your own ROM: nothing is bundled. Please don't ask for or share firmware here.
> Huge thanks to joelanders for the MD/MM emulation and to The Usual Suspects for Gearmulator.
> Bugs go to the GitHub issues on my fork, not to them.

## 2. Message to joelanders

Channel: a GitHub issue or discussion on joelanders/gearmulator-md-mm, or a Discord DM. Send it
**before** the Discord post, so he isn't surprised.

> Hi,
>
> I'm Radek. I've been working on a fork of gearmulator-md-mm
> (https://github.com/radekdymacz/gearmulator-md-mm) and wanted to tell you about it before I
> announce anything.
>
> I love what you've built. Running the real MD and MM firmware is what makes this possible. On top
> of it I've made two editors, one for the Machinedrum and one for the Monomachine. Each is a web
> page inside the plug-in that shows the whole machine at once: step grid, p-lock lanes, kit and
> pattern library, song chains, mutes and LFOs. The Monomachine gets a piano roll. There's also a
> HW MIDI mode that drives a real machine over MIDI. The emulation is yours and I've kept it close to
> your branch; the editor code sits beside it, in its own `mdStudio` / `mmStudio` skins and an `mdDesk` library.
>
> I'm about to publish a 0.1.0 alpha for macOS (standalone, VST3, AU). The site is
> https://mdmm.nativekloud.com. It's GPL-3 with full source and bring-your-own-ROM, the same as
> yours. There's an optional pay-what-you-want link that helps fund the work. Your name and The
> Usual Suspects are in the credits on the site, in the README, in the installer and in the
> release notes. The README says clearly that it's an unofficial fork and that support questions
> come to me, not to you or TUS.
>
> A few things I'd like to offer, whichever suits you:
>
> - **Upstreaming.** If any of it is useful to you, the editors or smaller fixes that also
>   apply to your builds, I'm happy to split it into small PRs against your branch.
> - **Linking.** Or we could just link to each other's projects, so people find the one they want:
>   your hardware-panel builds or the editors.
> - **Naming or anything else.** If you'd rather I name or present something differently, or change
>   the credit wording, tell me and I'll change it.
>
> Thanks again for the emulation. It's a joy to work on.
>
> Radek

## Before sending

- Publish the release first (see the note on "latest" in the release hand-off), then check that
  both download buttons on https://mdmm.nativekloud.com work.
- The upstream README says not to discuss firmware or ROMs in Discord. Both drafts keep to that.
