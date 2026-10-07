# Living on top of upstream

This fork keeps merging joelanders' work (`upstream`, [joelanders/gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm))
with `git merge`, never a rebase. He in turn merges dsp56300/gearmulator, which moves whole folders
(for example `source/jucePluginLib` to `source/framework/juce/jucePluginLib`). Every line we change
in a file he owns is a future conflict; a file we add is not.

## The rule

- **Our code lives in our files.** New libraries, sources, tests, CMake and CI workflows are files
  of ours, in our folders or beside his.
- **Upstream files get hooks only:** one include, one call, one member, one argument. A hook calls
  into our file and carries a comment that points here.
- **Never delete an upstream file.** Keep it out of our build and product from our own files
  (a filtered source list, `EXCLUDE_FROM_ALL`, a disabled test). A deleted file becomes a
  modify/delete conflict every time he edits it.
- **An upstream file that needs no change is byte-identical to the merge base.**
- Check before a merge or a PR: `scripts/mdmm-upstream-footprint.sh` lists every upstream file we
  change, with line counts, and fails if one was deleted.

## The hooks

| Upstream file | Hook | Why it cannot be ours |
|---|---|---|
| `README.md` | the top section | the fork's front page |
| `source/elektron/md/CMakeLists.txt` | `include(mdmmEditors.cmake)` after `mdLib` | adds our libraries (`elektronData`, `desk*`, `mdDesk`, `mmDesk`, `mdDataLink`), our part of `mdLib` (`mdAutomation` split out, `mddeskdevice`, `mdpanelsequence`, the telemetry headers), the JUCE web view switch and our `mdLibTest/mdmmTests.cmake` |
| `source/elektron/md/mdJucePlugin/CMakeLists.txt` | `include(mdmmPlugins.cmake)` after the skin globs | the editors' version (0.3.0), sources, page files in the binary data (instead of the panel skins) and icons must be values before his `juce_add_binary_data` and `createJucePlugin`; the rest (libraries, definitions, bundle steps, tests, the disabled panel pointer tests) runs deferred at the end of his file |
| `source/juce.cmake` | `${GEARMULATOR_PLUGIN_EXTRA_ARGS_<target>}` in `juce_add_plugin`; `GEARMULATOR_PLUGIN_BUNDLE_ID_<target>` instead of his `local.gearmulator.preview.*` when set | JUCE takes the app icons and the bundle identifier only as `juce_add_plugin` arguments, at configure time (a repeated `BUNDLE_ID` in the extra arguments would lose to his); the editors' own identifiers: `doc/release/SIGNING.md`, Identifiers |
| `mdJucePlugin/mdPluginProcessor.h` | `getDeskHost()`, `m_desk` | the processor owns the editors' state (`mdDeskHost.h`: the setup chunk and the session) |
| `mdJucePlugin/mdPluginProcessor.cpp` | includes; the `MDSK` chunk saved and read; a load starts from the default setup; the host made and started; the host destroyed **after** `destroyEditorState()` (the page detaches from the session); `md::DeskDevice` in `createDevice`; with no ROM `createDevice` returns `makeNoRomDevice()` (`mdDeskHost.h`) instead of throwing `FirmwareMissing` (the throw ended in `jucePluginLib/processor.cpp`'s native "Device Initialization failed" alert and Finder; now the page asks for the ROM); the state-restore alert is `genericUI::MessageBox` (routed to the page) instead of `juce::NativeMessageBox`; the config file named `editorConfigFileName` and copied once from his (`prepareConfig`, `mdSettingsMigration.h`); `getStateInformation` / `setStateInformation` / `restoreHeldState` (one line each: with no ROM the stand-in keeps the project it was given and hands the same bytes back, so a session without a ROM cannot overwrite the saved project, `DeskHost::holdState`) | the processor's lifetime and state are upstream's |
| `mdJucePlugin/mdPluginEditorState.cpp` | include; `editorPageSkins`, `keepEditorPage`, `createEditorPage` (`mdEditorPages.h`) | the editor page is the only UI; his panel skins and skin policy stay unchanged |
| `mdJucePlugin/mdController.h`, `.cpp` | `evDeviceSysex` and its one call in `parseSysexMessage` | the only place every SysEx from the machine passes |
| `jucePluginLib/processor.h`, `.cpp` | `ExternalMidi` member and accessor; `takeIn`, the device output dropped, `flushOut`; `protected:` / `private:` around `get/setStateInformation` (`.h`); `Properties::configFileName` and its two lines in `getConfigFile` | HW MIDI: the processor's MIDI paths (`externalMidi.h` holds the logic); the md processor overrides the two state calls; the base constructor opens the config file, so its name must be a property, not an override (the editors' own config file, `mdSettingsMigration.h`) |
| `jucePluginEditorLib/pluginEditorState.h`, `.cpp` | `openMenu` split into `fillMenu` (+ a null check on Settings) | the menu is built inline in `openMenu`; the menu bar and the page's context menu need it as data (`editorPopupMenu.h`) |
| `jucePluginEditorLib/pluginEditorWindow.h`, `.cpp` | an `EditorWindowFit` member and five calls | free-size pages and the screen fit are the window's sizing (`editorWindowFit.h`, `windowFit.h`) |
| `juceRmlUi/rmlMenu.h` | `forEachEntry` | a menu's entries are private |
| `juceUiLib/messageBox.cpp` | one `messageRoute::offer...` line at the top of each of the five functions (`messageRoute.h` is ours, header only) | every question and warning of the shared code passes here; the editors show them in the page, not as native alerts |

What moved out of upstream's files: `md::Device`'s telemetry and panel sequences into `md::DeskDevice`
(`mdLib/mddeskdevice.*`, the plug-in makes it); `panelKeySequence` into `mdLib/mdpanelsequence.*`;
the processor's setup chunk and session into `mdJucePlugin/mdDeskHost.*`; the skin choice into
`mdJucePlugin/mdEditorPages.*`; the window title, Audio/MIDI Settings and free size into
`mdStandaloneApp.cpp` and `jucePluginEditorLib/editorTraits.h` (`FreeSizeEditor`,
`AudioMidiSettingsEditor`: the page says so, `Editor` stays his; his "I Agree" disclaimer alert is silenced by `createEditorPage` setting its config value `disclaimerSeen`, no hook); our mdLibTest and mdJucePlugin
tests into `mdLibTest/mdmmTests.cmake` and `mdJucePlugin/mdmmPlugins.cmake` (same targets, labels
and output folders); the tag-time installers into `.github/workflows/mdmm-editors-release.yml`
(his `elektron-prerelease.yml` and `elektron-macos.yml` are his again; a tag also runs his, which
builds without uploading); it calls his `elektron-windows.yml` as is, and our `mdmm-editors-linux.yml`, for the not-tested
Windows and Linux archives, [doc/release/LINUX.md](../release/LINUX.md)).

Upstream files that stay in the tree but not in our product: the panel skins
`skins/mdDefault/*` and `skins/mmSfx60/*` (not in the sources or the binary data),
`mdLcdEditorPointerTest.cpp` (its two tests are `EXCLUDE_FROM_ALL` and disabled),
`mdProductSkins.h` (not called).

## Files we add in his folders

Our headers in `jucePluginEditorLib/` (`standaloneApp.h`, `windowFit.h`, `editorPopupMenu.h`,
`editorTraits.h`, `editorWindowFit.h`) and `jucePluginLib/` (`externalMidi.h`) sit beside the files
that include them. When upstream moves those folders (the `source/framework/juce/` move), git
moves them along and reports a "file location" conflict for each; accept the move, or merge with
`git -c merge.directoryRenames=true merge ...` and they merge cleanly.

## Syncing

- `scripts/mdmm-sync-upstream.sh` (on the `chore/upstream-sync-oss-prep` branch) fetches upstream
  and merges it.
- Before and after a merge, run `scripts/mdmm-upstream-footprint.sh` (`--committed` for HEAD only):
  the list should stay the hooks above. A conflict in a hook is resolved by taking his side and
  putting the hook back.
- To see what a branch or PR of his would do before it lands:
  `git merge-tree --write-tree --name-only HEAD upstream/<branch>` (a commit is needed; for
  uncommitted work, make one with `git commit-tree` on a temporary index).
