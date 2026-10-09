# The Machinedrum and Monomachine Editors' plug-ins (doc/modern-ux/UPSTREAM.md).
#
# Included once from this folder's CMakeLists.txt, after its skin globs and before its binary data:
# the editors' version, sources, page files and icons go in here as values upstream's own lines
# then use. What needs the plug-in targets (libraries, definitions, bundle steps, tests) runs at the
# end of the folder (cmake_language DEFER), after upstream's lines made them.

# The Machinedrum and Monomachine Editors have their own release version, apart from
# Gearmulator's (the bundles, the AU version, the installers and the site use it).
set(MDMM_EDITOR_VERSION 0.3.5)
string(REPLACE "." ";" _mdmmVersionParts "${MDMM_EDITOR_VERSION}")
list(GET _mdmmVersionParts 0 _mdmmVersionMajor)
list(GET _mdmmVersionParts 1 _mdmmVersionMinor)
list(GET _mdmmVersionParts 2 _mdmmVersionPatch)
foreach(_mdmmPrefix PROJECT mdJucePlugin CMAKE_PROJECT)
	set(${_mdmmPrefix}_VERSION ${MDMM_EDITOR_VERSION})
	set(${_mdmmPrefix}_VERSION_MAJOR ${_mdmmVersionMajor})
	set(${_mdmmPrefix}_VERSION_MINOR ${_mdmmVersionMinor})
	set(${_mdmmPrefix}_VERSION_PATCH ${_mdmmVersionPatch})
endforeach()
unset(_mdmmPrefix)
unset(_mdmmVersionParts)
unset(_mdmmVersionMajor)
unset(_mdmmVersionMinor)
unset(_mdmmVersionPatch)

# The version as code: one generated file (mdmmVersion.h), rewritten only when the version changes, instead of a
# definition on every file of both plug-ins (a bump rebuilt all of them, JUCE's modules included).
configure_file(mdmmVersion.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/mdmmVersion.cpp @ONLY)
add_library(mdmmVersion STATIC ${CMAKE_CURRENT_BINARY_DIR}/mdmmVersion.cpp mdmmVersion.h)
target_include_directories(mdmmVersion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET mdmmVersion PROPERTY FOLDER "Elektron")

# The editor page is the only UI (P5): upstream's panel skins stay in the tree, not in the product.
list(FILTER SOURCES EXCLUDE REGEX "^skins/(mdDefault|mmSfx60)/")
list(APPEND SOURCES
	mdEditorPages.cpp mdEditorPages.h
	mdDeskHost.cpp mdDeskHost.h
	mdStandaloneApp.cpp
	mdBootDiagnostics.cpp mdBootDiagnostics.h
	mdDeskSession.cpp mdDeskSession.h
	mdMidiLearnCommands.cpp mdMidiLearnCommands.h
	mdPageEditor.cpp mdPageEditor.h mdPageZoom.h mdEditorMenu.h
	mdRomInstall.cpp mdRomInstall.h
	mdSettingsMigration.cpp mdSettingsMigration.h
	mdSessionMd.cpp mdSessionMm.cpp mdSessions.h
	mdStudioLink.cpp mdStudioLink.h
	mdWebPageHost.cpp mdWebPageHost.h
	mmStudioLink.cpp mmStudioLink.h
	$<$<PLATFORM_ID:Darwin>:mdStudioWebZoom.mm> mdWebFocus.h
	$<$<PLATFORM_ID:Darwin>:mdBackgroundRun.mm> mdBackgroundRun.h
	$<$<PLATFORM_ID:Windows>:mdWebView2Page.cpp>
	mdWebView2Page.h
	mdAudioMidiLink.cpp mdAudioMidiLink.h
	mdUpdater.cpp mdUpdater.h

	skins/mdStudio/mdStudio.rml
	skins/mdStudio/mdStudio.html
	skins/mdStudio/mdDesk.css
	skins/mdStudio/mdDeskApp.js skins/mdStudio/mdDeskSoundGroups.js skins/mdStudio/mdDeskTop.js skins/mdStudio/mdDeskSeq.js
	skins/mdStudio/mdDeskSound.js skins/mdStudio/mdDeskEditors.js skins/mdStudio/mdDeskMix.js skins/mdStudio/mdDeskSampler.js
	skins/mdStudio/mdDeskSong.js skins/mdStudio/mdDeskPicker.js skins/mdStudio/mdDeskControl.js skins/mdStudio/mdDeskGenUi.js
	skins/mdStudio/mdDeskComforts.js skins/mdStudio/mdDeskSelect.js skins/mdStudio/mdDeskRom.js skins/mdStudio/mdDeskGestures.js skins/mdStudio/mdDeskRender.js
	skins/mdStudio/mdOverrides.css
	skins/mdStudio/mdDeskModel.js
	skins/mdStudio/mdDeskGen.js
	skins/mdStudio/mdDeskMod.js
	skins/mmStudio/mmStudio.rml
	skins/mmStudio/mmStudio.html
	skins/mmStudio/mmStudio.css
	skins/mmStudio/mmOverrides.css
	skins/mmStudio/mmMockup.js
	skins/mmStudio/mmConvert.js
	skins/mmStudio/mmAdapter.js
	skins/mmStudio/mmView.js
	skins/mmStudio/mmViewTest.js
	skins/mmStudio/mmConvertTest.js
	skins/mmStudio/mmKeysTest.js
	skins/mmStudio/mmGenTest.js
	skins/mmStudio/mmSoundTest.js
	skins/mmStudio/mmSelfTest.js
	skins/mmStudio/mmJourneys.js
	skins/mdStudio/mdDeskSelfTest.js
	skins/mdStudio/mdDeskJourneys.js
	skins/shared/deskJourney.js
	skins/mdStudio/mdDeskModelTest.js
	skins/mdStudio/mdDeskGenTest.js
	skins/mdStudio/mdDeskKeysTest.js
	skins/mdStudio/mdDeskPageTest.js
	skins/mdStudio/mdDeskLive.js
	skins/mdStudio/mdDeskLibrary.js
	skins/mdStudio/mdDeskKeys.js
	skins/mdStudio/mdDeskGlobal.js
	skins/mdStudio/mdDeskAudio.js
	skins/shared/deskModal.js skins/shared/deskModal.css skins/shared/deskModalTest.js skins/shared/deskMenu.js skins/shared/deskMenuTest.js
	skins/shared/deskCaps.js
	skins/shared/deskBoot.js skins/shared/deskBoot.css
	skins/shared/deskSyx.js skins/shared/deskSyx.css skins/shared/deskSyxTest.js
	skins/shared/deskAudio.js skins/shared/deskAudio.css skins/shared/deskAudioSelfTest.js
	skins/shared/deskLcd.css skins/shared/deskFonts.css
	skins/shared/deskBridge.js skins/shared/deskBridgeTest.js
	skins/shared/deskDocs.js
	skins/shared/deskOverlay.js skins/shared/deskOverlayTest.js
	skins/shared/deskGen.js skins/shared/deskGenTest.js
	skins/shared/deskKeys.js skins/shared/deskKeysTest.js skins/shared/deskKeymapTest.js skins/shared/deskKeyView.js skins/shared/deskKeyView.css skins/shared/deskKeyViewTest.js
	skins/shared/deskTogglePaint.js skins/shared/deskTogglePaintTest.js
	skins/shared/deskCompat.js skins/shared/deskCompatTest.js
	skins/shared/deskZoom.js
	skins/shared/deskAbout.js skins/shared/deskAboutTest.js)

# P6: the editors' diagnostics (the log of the web view, the window chrome and the session's
# state, and the pages' self-tests: mdDeskSelfTest.js, mmSelfTest.js) observe the editors. Off by
# default for every generator and configuration (single- and multi-config), so a release package
# has none of it; a test or development build turns it on (-Dgearmulator_MDMM_DIAGNOSTICS=ON).
option(gearmulator_MDMM_DIAGNOSTICS "The Machinedrum/Monomachine Editors' diagnostics log and self-tests" OFF)
if(gearmulator_MDMM_DIAGNOSTICS)
	list(APPEND SOURCES mdDiagnostics.cpp mdDiagnostics.h $<$<PLATFORM_ID:Darwin>:mdOsKeys.mm> mdOsKeys.h)
endif()
# DESIGN-edit-flow.md: the edit-flow driver (mdEditFlowDriver.h) replays the page's messages into the
# plug-in's session when GEARMULATOR_EDITFLOW_DRIVE is set, for mdVst3EditFlowHost. A test build only.
option(gearmulator_MDMM_EDITFLOW_DRIVER "The Machinedrum/Monomachine Editors' edit-flow driver (test builds)" OFF)
if(gearmulator_MDMM_EDITFLOW_DRIVER)
	list(APPEND SOURCES mdEditFlowDriver.cpp mdEditFlowDriver.h mdEditFlowCounters.h)
endif()

# Developer convenience (macOS): after each build, copy the editors' VST3 and AU bundles to the user's
# plug-in folders and the standalone apps to ~/Applications. OFF by default: it overwrites whatever
# release is installed there. Never on in CI or for release packages.
option(MDMM_INSTALL_DEV_PLUGINS "Copy the built editors to ~/Library/Audio/Plug-Ins and ~/Applications after each build (macOS, developers only)" OFF)

# The pages instead of the panel skins in the plug-ins' binary data. The stylesheets are the generated
# ones only (mdDesk.css, mmStudio.css: the sync scripts concatenate the mockups', the shared ones and
# mdOverrides.css / mmOverrides.css into them). The editor finds a page file by its name, so the shared
# files (skins/shared/, named desk*) sit beside each page's own.
# The Machinedrum page loads the shared scripts as files; the Monomachine page has them inside
# mmMockup.js (sync-mmstudio-skin.py) but for the bridge.
set(MD_SHARED_PAGE_FILES
	"skins/shared/deskModal.js" "skins/shared/deskMenu.js" "skins/shared/deskCaps.js" "skins/shared/deskBoot.js" "skins/shared/deskSyx.js" "skins/shared/deskBridge.js"
	"skins/shared/deskDocs.js" "skins/shared/deskOverlay.js" "skins/shared/deskGen.js" "skins/shared/deskKeys.js" "skins/shared/deskKeyView.js" "skins/shared/deskTogglePaint.js"
	"skins/shared/deskAudio.js" "skins/shared/deskCompat.js" "skins/shared/deskZoom.js" "skins/shared/deskAbout.js")
file(GLOB MD_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mdStudio/*.rml" "skins/mdStudio/*.html" "skins/mdStudio/mdDesk.css" "skins/mdStudio/*.js"
	"skins/mdStudio/fonts/*.woff2" "skins/mdStudio/fonts/*.ttf")
list(APPEND MD_SKIN_ASSETS ${MD_SHARED_PAGE_FILES})
file(GLOB MM_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mmStudio/*.rml" "skins/mmStudio/*.html" "skins/mmStudio/mmStudio.css"
	"skins/mmStudio/mmMockup.js" "skins/mmStudio/mmConvert.js" "skins/mmStudio/mmAdapter.js" "skins/mmStudio/mmView.js"
	# shared with the Machinedrum Editor: the page bridge, the document store and its overlays, the OFL fonts
	"skins/shared/deskBridge.js" "skins/shared/deskDocs.js" "skins/shared/deskOverlay.js" "skins/mdStudio/fonts/*.ttf"
	# the older-WebKit rewrite (B-001), first in the page's <head>; the page's zoom keys
	"skins/shared/deskCompat.js" "skins/shared/deskZoom.js"
	# the version the page shows (0.3.4)
	"skins/shared/deskAbout.js")
# The tests are not the page, named one by one, not by a file-name pattern: the node tests never
# ship, the self-tests only with the diagnostics. A test that was renamed or moved stops the
# configure, so it cannot slip into the glob. The MM glob lists its page files already.
set(MD_NODE_TESTS "skins/mdStudio/mdDeskModelTest.js" "skins/mdStudio/mdDeskGenTest.js" "skins/mdStudio/mdDeskKeysTest.js" "skins/mdStudio/mdDeskPageTest.js")
# the AUDIO / MIDI panel's self-test (shared, diagnostics only) goes with the MD page's self-tests, and so do the
# user journeys (the shared runner deskJourney.js and each page's journeys)
set(MD_SELF_TESTS "skins/mdStudio/mdDeskSelfTest.js" "skins/shared/deskAudioSelfTest.js"
	"skins/mdStudio/mdDeskJourneys.js" "skins/shared/deskJourney.js")
set(MM_SELF_TESTS "skins/mmStudio/mmSelfTest.js" "skins/mmStudio/mmJourneys.js" "skins/shared/deskJourney.js")
# the MM glob names its page files, so its node tests (mmConvertTest.js, mmKeysTest.js, mmGenTest.js, mmSoundTest.js,
# mmViewTest.js and its fixture) never ship; checked to be there
set(MM_NODE_TESTS "skins/mmStudio/mmConvertTest.js" "skins/mmStudio/mmKeysTest.js" "skins/mmStudio/mmGenTest.js" "skins/mmStudio/mmSoundTest.js"
	"skins/mmStudio/mmViewTest.js" "skins/mmStudio/mmViewFixture.json")
# the shared page files' node tests (never in a glob, so never shipped); checked to be there
set(SHARED_NODE_TESTS "skins/shared/deskGenTest.js" "skins/shared/deskOverlayTest.js" "skins/shared/deskBridgeTest.js"
	"skins/shared/deskTogglePaintTest.js" "skins/shared/deskKeysTest.js" "skins/shared/deskModalTest.js" "skins/shared/deskMenuTest.js" "skins/shared/deskCompatTest.js"
	"skins/shared/deskKeymapTest.js" "skins/shared/deskKeyViewTest.js" "skins/shared/deskAboutTest.js" "skins/shared/deskSyxTest.js")
foreach(test ${MD_NODE_TESTS} ${MD_SELF_TESTS} ${MM_SELF_TESTS} ${MM_NODE_TESTS} ${SHARED_NODE_TESTS} ${MD_SHARED_PAGE_FILES})
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${test}")
		message(FATAL_ERROR "${test} is not there: update the editors' test lists in ${CMAKE_CURRENT_LIST_FILE}")
	endif()
endforeach()
foreach(test ${MD_NODE_TESTS} ${MD_SELF_TESTS})
	list(REMOVE_ITEM MD_SKIN_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/${test}")
endforeach()
if(gearmulator_MDMM_DIAGNOSTICS)
	list(APPEND MD_SKIN_ASSETS ${MD_SELF_TESTS})
	list(APPEND MM_SKIN_ASSETS ${MM_SELF_TESTS})
endif()

# The product names, the vendor and the old (0.3.1) names: scripts/mdmm-product.env, the one place they are set
# (the packaging scripts source the same file). MDMM_PRODUCT_NAME_MD / _MM name the bundles, the executables and
# the plug-in a DAW lists (upstream's createJucePlugin takes them in this folder's CMakeLists.txt).
set(_mdmmProductEnv "${CMAKE_CURRENT_LIST_DIR}/../../../../scripts/mdmm-product.env")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mdmmProductEnv}")
file(STRINGS "${_mdmmProductEnv}" _mdmmProductLines REGEX "^MDMM_[A-Z_]+=\".*\"$")
foreach(_mdmmLine ${_mdmmProductLines})
	string(REGEX MATCH "^(MDMM_[A-Z_]+)=\"(.*)\"$" _mdmmMatch "${_mdmmLine}")
	set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()
foreach(_mdmmKey MDMM_PRODUCT_NAME_MD MDMM_PRODUCT_NAME_MM MDMM_VENDOR MDMM_WEBSITE MDMM_LEGACY_NAME_MD MDMM_LEGACY_NAME_MM)
	if(NOT ${_mdmmKey})
		message(FATAL_ERROR "${_mdmmKey} missing from ${_mdmmProductEnv}")
	endif()
endforeach()
unset(_mdmmProductEnv)
unset(_mdmmProductLines)
unset(_mdmmLine)
unset(_mdmmMatch)
unset(_mdmmKey)

# Per-target juce_add_plugin arguments (juce.cmake passes them last, so they win over upstream's):
# - the app icons (doc/modern-ux/icons, built with build-icons.sh);
# - the maker a DAW shows (AU "Future Native Audio: Machinedrum Editor", the VST3 vendor, the Windows file details)
#   instead of upstream's "Gearmulator Preview". The data folder keeps that name (mdPluginProcessor.cpp,
#   g_dataFolderVendor), and the VST3 class IDs and AU codes come from GmPv + Tmdr/Tmno, not from names;
# - the microphone prompt's text;
# - the LV2 URI as it was with the old product name (it would otherwise follow the name).
foreach(_mdmmTarget md mm)
	string(TOUPPER "${_mdmmTarget}" _mdmmUpper)
	set(GEARMULATOR_PLUGIN_EXTRA_ARGS_${_mdmmTarget}JucePlugin
		ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/${_mdmmTarget}-1024.png"
		ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icons/${_mdmmTarget}-32.png"
		COMPANY_NAME "${MDMM_VENDOR}"
		COMPANY_WEBSITE "${MDMM_WEBSITE}"
		COMPANY_COPYRIGHT "Copyright (C) The Usual Suspects (Gearmulator), joelanders and NativeKloud Consulting Radoslaw Dymacz. GNU GPL v3."
		MICROPHONE_PERMISSION_TEXT "${MDMM_PRODUCT_NAME_${_mdmmUpper}} uses audio input to process external instruments."
		LV2URI "http://theusualsuspects.lv2/Gearmulator${_mdmmUpper}")
endforeach()
unset(_mdmmTarget)
unset(_mdmmUpper)

# The editors' own bundle identifiers (doc/release/SIGNING.md, "Identifiers"), on the app, the VST3 and the AU
# alike (JUCE gives every format of a target the same one). Upstream's local.gearmulator.preview.GearmulatorMD/MM
# belong to upstream's builds: with the same identifier LaunchServices opened whichever it found, and a signed
# app's identifier is what its permissions (microphone) are tied to. The VST3 class IDs and the AU
# type/subtype/manufacturer come from the four-character codes, not from this or from the names.
set(GEARMULATOR_PLUGIN_BUNDLE_ID_mdJucePlugin "com.nativekloud.machinedrum-editor")
set(GEARMULATOR_PLUGIN_BUNDLE_ID_mmJucePlugin "com.nativekloud.monomachine-editor")

function(mdmm_plugin_targets)
	foreach(plugin_target mdJucePlugin mmJucePlugin)
		target_link_libraries(${plugin_target} PRIVATE elektronData mdDataLink mdDesk mmDesk deskHost deskWire)
		# public: mdPageEditor.h includes mdUpdater.h, and the tests that build on the plug-in include both
		target_link_libraries(${plugin_target} PUBLIC mdmmUpdate)
		# the version (mdmmVersion.h): the update check, the page's ?version=, the About box
		target_link_libraries(${plugin_target} PUBLIC mdmmVersion)
		target_compile_definitions(${plugin_target} PUBLIC
			# jucePluginEditorLib/standaloneApp.h: native title bar and menu bar (P4).
			JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
			# The names the code shows (mdPluginProcessor.cpp, mdStandaloneApp.cpp): scripts/mdmm-product.env.
			"MDMM_PRODUCT_NAME_MD=\"${MDMM_PRODUCT_NAME_MD}\""
			"MDMM_PRODUCT_NAME_MM=\"${MDMM_PRODUCT_NAME_MM}\""
			# the editor menu's first line and the standalone's About box (mdAbout.h)
			"MDMM_VENDOR=\"${MDMM_VENDOR}\""
			"MDMM_WEBSITE=\"${MDMM_WEBSITE}\""
			MDMM_DIAGNOSTICS=$<BOOL:${gearmulator_MDMM_DIAGNOSTICS}>
			MDMM_EDITFLOW_DRIVER=$<BOOL:${gearmulator_MDMM_EDITFLOW_DRIVER}>)

		# JUCE gives its version definitions (JucePlugin_Version, _VersionString, _VersionCode: the version a host
		# is told), and upstream's juce.cmake its PluginVersionMajor/Minor/Patch, to every file of the shared code
		# and of what links it, so a bump rebuilt both plug-ins whole, JUCE's modules included. They go to the files
		# that use them instead: the format wrappers (VST3, AU, Standalone: the versions hosts read), the two of
		# ours that name JucePlugin_VersionString (mdPluginProcessor.cpp, mdStandaloneApp.cpp through
		# standaloneApp.h; mdRecordMenu.cpp is in the _Standalone target) and upstream's serverPlugin.cpp (the
		# bridge's plug-in description, bridge/client/plugin.h). A file that names them without having them does
		# not compile.
		set(_mdmmJuceVersionDefinitions "")
		set(_mdmmVersionDefinition "^(JucePlugin_Version(String|Code)?|PluginVersion(Major|Minor|Patch))=")
		foreach(_mdmmProperty COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
			get_target_property(_mdmmDefinitions ${plugin_target} ${_mdmmProperty})
			if(NOT _mdmmDefinitions)
				continue()
			endif()
			set(_mdmmVersionOnes ${_mdmmDefinitions})
			list(FILTER _mdmmVersionOnes INCLUDE REGEX "${_mdmmVersionDefinition}")
			list(APPEND _mdmmJuceVersionDefinitions ${_mdmmVersionOnes})
			list(FILTER _mdmmDefinitions EXCLUDE REGEX "${_mdmmVersionDefinition}")
			set_property(TARGET ${plugin_target} PROPERTY ${_mdmmProperty} ${_mdmmDefinitions})
		endforeach()
		list(REMOVE_DUPLICATES _mdmmJuceVersionDefinitions)
		list(LENGTH _mdmmJuceVersionDefinitions _mdmmCount)
		if(NOT _mdmmCount EQUAL 6)
			message(FATAL_ERROR "${plugin_target}: expected six version definitions (JUCE's three, juce.cmake's three), found: ${_mdmmJuceVersionDefinitions}")
		endif()
		# JUCE's default ARA factory ID ends in the version (<bundle id>.arafactory.<version>) and is a definition on
		# every file too; the editors are no ARA plug-ins (JucePlugin_Enable_ARA=0), so it keeps the bundle ID only.
		get_target_property(_mdmmBundleId ${plugin_target} JUCE_BUNDLE_ID)
		set_property(TARGET ${plugin_target} PROPERTY JUCE_ARA_FACTORY_ID "\"${_mdmmBundleId}.arafactory\"")
		get_target_property(_mdmmWrappers ${plugin_target} JUCE_ACTIVE_PLUGIN_TARGETS)
		foreach(_mdmmWrapper IN LISTS _mdmmWrappers)
			target_compile_definitions(${_mdmmWrapper} PRIVATE ${_mdmmJuceVersionDefinitions})
		endforeach()
	endforeach()
	# both plug-ins have the same version; their shared files are compiled once per plug-in with these
	set_property(SOURCE mdPluginProcessor.cpp mdStandaloneApp.cpp serverPlugin.cpp
		APPEND PROPERTY COMPILE_DEFINITIONS ${_mdmmJuceVersionDefinitions})
	unset(_mdmmJuceVersionDefinitions)
	unset(_mdmmDefinitions)
	unset(_mdmmVersionOnes)
	unset(_mdmmWrappers)
	unset(_mdmmCount)
	unset(_mdmmBundleId)
	unset(_mdmmVersionDefinition)

	if(APPLE)
		# The standalone apps' Record menu (mdRecordMenu.h): its sources and ScreenCaptureKit go
		# into the _Standalone targets alone, so the VST3 and AU neither build nor link them.
		# ScreenCaptureKit is weak (macOS 12.3+; the deployment target is 10.13): the menu says
		# what it needs on an older Mac.
		set_source_files_properties(mdScreenRecorder.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
		foreach(app_target mdJucePlugin_Standalone mmJucePlugin_Standalone)
			if(NOT TARGET ${app_target})
				continue()
			endif()
			target_sources(${app_target} PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR}/mdRecordMenu.cpp ${CMAKE_CURRENT_SOURCE_DIR}/mdRecordMenu.h
				${CMAKE_CURRENT_SOURCE_DIR}/mdScreenRecorder.mm ${CMAKE_CURRENT_SOURCE_DIR}/mdScreenRecorder.h)
			target_link_libraries(${app_target} PRIVATE
				"-weak_framework ScreenCaptureKit" "-framework AVFoundation" "-framework CoreMedia")
		endforeach()

		foreach(plugin_target
			mdJucePlugin_VST3 mmJucePlugin_VST3
			mdJucePlugin_AU mmJucePlugin_AU
			mdJucePlugin_Standalone mmJucePlugin_Standalone)
			if(NOT TARGET ${plugin_target})
				continue()
			endif()
			# The app icon on the VST3 and AU bundles too: JUCE copies Icon.icns into every
			# bundle but names it in the Info.plist of the standalone only.
			if(plugin_target MATCHES "^(md|mm)JucePlugin_(VST3|AU)$")
				add_custom_command(TARGET ${plugin_target} POST_BUILD
					COMMAND /bin/sh -c "/usr/libexec/PlistBuddy -c 'Delete :CFBundleIconFile' \"$1\" 2>/dev/null; /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string Icon.icns' \"$1\"" sh
						"$<TARGET_BUNDLE_DIR:${plugin_target}>/Contents/Info.plist"
					COMMENT "App icon on ${plugin_target}"
					VERBATIM
				)
			endif()
			# Finder and the Dock cache a bundle's icon by its folder date: touch the bundle so
			# a rebuilt app with a new icon shows it (P5: the MD app showed none).
			add_custom_command(TARGET ${plugin_target} POST_BUILD
				COMMAND /usr/bin/touch "$<TARGET_BUNDLE_DIR:${plugin_target}>"
				VERBATIM
			)
			# Upstream's ad-hoc signing ran before these steps changed the Info.plist: sign the
			# completed bundle again.
			add_custom_command(TARGET ${plugin_target} POST_BUILD
				COMMAND /usr/bin/codesign --force --deep --sign -
					"$<TARGET_BUNDLE_DIR:${plugin_target}>"
				COMMENT "Ad-hoc signing the editor's ${plugin_target} bundle"
				VERBATIM
			)
			if(MDMM_INSTALL_DEV_PLUGINS)
				if(plugin_target MATCHES "_VST3$")
					set(_mdmmInstallDir "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
				elseif(plugin_target MATCHES "_AU$")
					set(_mdmmInstallDir "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
				else()
					set(_mdmmInstallDir "$ENV{HOME}/Applications")
				endif()
				add_custom_command(TARGET ${plugin_target} POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E make_directory "${_mdmmInstallDir}"
					COMMAND /bin/sh -c "rm -rf \"$1/$(basename \"$2\")\" && cp -R \"$2\" \"$1/\"" sh
						"${_mdmmInstallDir}" "$<TARGET_BUNDLE_DIR:${plugin_target}>"
					COMMENT "Installing ${plugin_target} to ${_mdmmInstallDir} (MDMM_INSTALL_DEV_PLUGINS)"
					VERBATIM
				)
				unset(_mdmmInstallDir)
			endif()
		endforeach()
	endif()

	# Windows (mdmmWindowsWebView.cmake): the WebView2 SDK's headers and static loader.
	if(TARGET mdmmWebView2)
		foreach(plugin_target mdJucePlugin mmJucePlugin)
			target_link_libraries(${plugin_target} PRIVATE mdmmWebView2)
		endforeach()
	endif()

	# Linux (mdmmLinuxWebView.cmake): the webkit2gtk and GTK shims beside the standalone and the VST3 module,
	# found through $ORIGIN (dlopen searches the run path of the object that calls it).
	if(TARGET mdmmLinuxWebkitShim)
		foreach(plugin_target mdJucePlugin_VST3 mmJucePlugin_VST3 mdJucePlugin_Standalone mmJucePlugin_Standalone)
			if(NOT TARGET ${plugin_target})
				continue()
			endif()
			set_property(TARGET ${plugin_target} PROPERTY BUILD_RPATH "\$ORIGIN")
			add_dependencies(${plugin_target} mdmmLinuxWebkitShim mdmmLinuxGtkShim)
			add_custom_command(TARGET ${plugin_target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"$<TARGET_FILE:mdmmLinuxWebkitShim>" "$<TARGET_FILE:mdmmLinuxGtkShim>"
					"$<TARGET_FILE_DIR:${plugin_target}>"
				COMMENT "Web view shims beside ${plugin_target}"
				VERBATIM
			)
		endforeach()
	endif()

	if(NOT BUILD_TESTING)
		return()
	endif()

	# The panel editor's pointer tests (mdLcdEditorPointerTest.cpp) went with the panel skins (P5):
	# the editor page is the only UI, the panel skins are not in the plug-ins' binary data.
	foreach(test mdLcdEditorPointerTest mmLcdEditorPointerTest)
		if(TARGET ${test})
			set_property(TARGET ${test} PROPERTY EXCLUDE_FROM_ALL TRUE)
			set_tests_properties(${test} PROPERTIES DISABLED TRUE)
		endif()
	endforeach()

	# jucePluginEditorLib/standaloneApp.h: native title bar and menu bar (P4).
	if(TARGET mdAudioIoLayoutTest)
		target_compile_definitions(mdAudioIoLayoutTest PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
	endif()

	add_executable(mdRomInstallTest mdRomInstallTest.cpp mdRomInstall.cpp)
	target_include_directories(mdRomInstallTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
	target_link_libraries(mdRomInstallTest PRIVATE juce::juce_core)
	target_compile_definitions(mdRomInstallTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0)
	add_test(NAME mdRomInstallTest COMMAND mdRomInstallTest)
	set_tests_properties(mdRomInstallTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdRomInstallTest PROPERTY FOLDER "Elektron/test")

	# The editors' own settings files: the one-time copy from upstream's names (mdSettingsMigration.h).
	add_executable(mdSettingsMigrationTest mdSettingsMigrationTest.cpp mdSettingsMigration.cpp)
	target_include_directories(mdSettingsMigrationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
	target_link_libraries(mdSettingsMigrationTest PRIVATE juce::juce_core)
	target_compile_definitions(mdSettingsMigrationTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0)
	add_test(NAME mdSettingsMigrationTest COMMAND mdSettingsMigrationTest)
	set_tests_properties(mdSettingsMigrationTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdSettingsMigrationTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdWindowFitTest mdWindowFitTest.cpp)
	target_include_directories(mdWindowFitTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	add_test(NAME mdWindowFitTest COMMAND mdWindowFitTest)
	set_tests_properties(mdWindowFitTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdWindowFitTest PROPERTY FOLDER "Elektron/test")

	# The page bridge's transport (mdPageBridge.h, pure): long batches in pieces, the outbox split into numbered calls; and the notice route (juceUiLib/messageRoute.h): a sink per window.
	add_executable(mdPageBridgeTest mdPageBridgeTest.cpp)
	target_link_libraries(mdPageBridgeTest PRIVATE elektronJson)
	target_include_directories(mdPageBridgeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../..)	# ../../..: juceUiLib/messageRoute.h
	add_test(NAME mdPageBridgeTest COMMAND mdPageBridgeTest)
	set_tests_properties(mdPageBridgeTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdPageBridgeTest PROPERTY FOLDER "Elektron/test")

	# I-008: the editor's menu as data (mdEditorMenu.h): the page's editorMenu message and the numbers its entries run by
	add_executable(mdEditorMenuTest mdEditorMenuTest.cpp mdEditorMenu.h)
	target_link_libraries(mdEditorMenuTest PRIVATE elektronJson)
	target_include_directories(mdEditorMenuTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
	add_test(NAME mdEditorMenuTest COMMAND mdEditorMenuTest)
	set_tests_properties(mdEditorMenuTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdEditorMenuTest PROPERTY FOLDER "Elektron/test")

	# 0.3.4: the editor menu's first line and the About box: the build's product names and version (mdAbout.h)
	add_executable(mdAboutTest mdAboutTest.cpp mdAbout.h)
	target_link_libraries(mdAboutTest PRIVATE mdmmVersion)
	target_compile_definitions(mdAboutTest PRIVATE
		"MDMM_PRODUCT_NAME_MD=\"${MDMM_PRODUCT_NAME_MD}\"" "MDMM_PRODUCT_NAME_MM=\"${MDMM_PRODUCT_NAME_MM}\""
		"MDMM_VENDOR=\"${MDMM_VENDOR}\"" "MDMM_WEBSITE=\"${MDMM_WEBSITE}\"")
	add_test(NAME mdAboutTest COMMAND mdAboutTest)
	set_tests_properties(mdAboutTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdAboutTest PROPERTY FOLDER "Elektron/test")

	# P6: the Machinedrum page's model (the pure view and the optimistic overlay), when node is here.

	find_program(GEARMULATOR_NODE node)
	if(GEARMULATOR_NODE)
		add_test(NAME mdDeskModelPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/mdDeskModelTest.js)
		set_tests_properties(mdDeskModelPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the generators (DESIGN-generators.md): pure, pinned; the shared ones, then the MD's roles and mutation
		add_test(NAME deskGenPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskGenTest.js)
		set_tests_properties(deskGenPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		add_test(NAME mdDeskGenPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/mdDeskGenTest.js)
		set_tests_properties(mdDeskGenPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the shared document store and optimistic layers (DESIGN-UNIFY.md phase 0)
		# the page bridge's transport (BridgeTransport in skins/shared/deskBridge.js): URLs and pieces
		add_test(NAME deskBridgePageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskBridgeTest.js)
		set_tests_properties(deskBridgePageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		add_test(NAME deskOverlayPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskOverlayTest.js)
		set_tests_properties(deskOverlayPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the drag across the M and S keys (both editors): which keys a drag changes
		add_test(NAME deskTogglePaintPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskTogglePaintTest.js)
		set_tests_properties(deskTogglePaintPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the keys' one gating rule (both editors): no page shortcut behind an open dialog or panel
		add_test(NAME deskKeysPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskKeysTest.js)
		set_tests_properties(deskKeysPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# both editors' key maps as data (DESIGN-keymap.md K0): ids, the MD / MM parity, doc/modern-ux/keymap.json and the guide's tables
		add_test(NAME deskKeymapPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskKeymapTest.js)
		set_tests_properties(deskKeymapPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the keyboard view (K-view): every dispatched key drawn in its layer, legends per OS, page filter, search, piano
		add_test(NAME deskKeyViewPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskKeyViewTest.js)
		set_tests_properties(deskKeyViewPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the question dialog's queue (both editors): nothing replaces it, a plug-in notice is always answered
		add_test(NAME deskModalPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskModalTest.js)
		set_tests_properties(deskModalPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the SysEx import panel (both editors): the machine's slot grids, the counts, Shift-click ranges, the report per slot
		add_test(NAME deskSyxPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskSyxTest.js)
		set_tests_properties(deskSyxPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# I-008: the menu drawn in the page (placement, keys, the editor menu's message and picks) and its wiring
		add_test(NAME deskMenuPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskMenuTest.js)
		set_tests_properties(deskMenuPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the pages on an older WebKit (B-001, macOS 12): the stylesheets without color-mix() and :focus-visible
		add_test(NAME deskCompatPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskCompatTest.js)
		set_tests_properties(deskCompatPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the version both pages show is the one this build compiles in (MDMM_EDITOR_VERSION)
		add_test(NAME deskAboutPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/shared/deskAboutTest.js ${MDMM_EDITOR_VERSION})
		set_tests_properties(deskAboutPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the MD page's wiring on its own scripts: solo and the machine's mutes, renders held by a gesture, prepared mutes
		add_test(NAME mdDeskPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/mdDeskPageTest.js)
		set_tests_properties(mdDeskPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# the Monomachine page's documents and view (DESIGN-UNIFY.md phase 1): the derived view, echoes by command id
		add_test(NAME mmViewPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mmStudio/mmViewTest.js)
		set_tests_properties(mmViewPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
	endif()

	# P4: the editor's setup (MDSK chunk) round-trips with the plug-in state.
	add_executable(mdDeskSetupStateTest mdDeskSetupStateTest.cpp)
	target_link_libraries(mdDeskSetupStateTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdDeskSetupStateTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdDeskSetupStateTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	add_test(NAME mdDeskSetupStateTest COMMAND mdDeskSetupStateTest)
	# MallocScribble: a use of freed memory (the teardown check) crashes instead of passing by luck.
	set_tests_properties(mdDeskSetupStateTest PROPERTIES LABELS "UnitTest" TIMEOUT 120 ENVIRONMENT "MallocScribble=1")
	set_property(TARGET mdDeskSetupStateTest PROPERTY FOLDER "Elektron/test")

	# DESIGN-edit-flow.md: one small edit through the real processor, every hop counted (manual: needs a ROM).
	add_executable(mdEditFlowPluginTest mdEditFlowPluginTest.cpp)
	target_link_libraries(mdEditFlowPluginTest PRIVATE mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules juce::juce_opengl)
	target_include_directories(mdEditFlowPluginTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdEditFlowPluginTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	set_property(TARGET mdEditFlowPluginTest PROPERTY FOLDER "Elektron/test")

	# DESIGN-edit-flow.md: a built VST3 bundle in a real host, real-time audio blocks, the bundle's
	# edit-flow driver playing the page (manual: needs the ROM and gearmulator_MDMM_EDITFLOW_DRIVER).
	# macOS only: it finds the bundle and the ROM the Mac way (CoreFoundation).
	if(APPLE)
		juce_add_console_app(mdVst3EditFlowHost PRODUCT_NAME "mdVst3EditFlowHost")
		target_sources(mdVst3EditFlowHost PRIVATE mdVst3EditFlowHost.cpp)
		target_link_libraries(mdVst3EditFlowHost PRIVATE juce::juce_audio_processors juce::juce_events)
		target_compile_definitions(mdVst3EditFlowHost PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_PLUGINHOST_VST3=1
			JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
		set_property(TARGET mdVst3EditFlowHost PROPERTY FOLDER "Elektron/test")
	endif()

	# P5: the app modulators in the processor, no editor (manual: needs the ROM).
	add_executable(mdSessionFirmwareTest mdSessionFirmwareTest.cpp)
	target_link_libraries(mdSessionFirmwareTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdSessionFirmwareTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdSessionFirmwareTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		MDDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/md-data-contract.schema.json")
	set_property(TARGET mdSessionFirmwareTest PROPERTY FOLDER "Elektron/test")
	# P6: in ctest; it skips (77) without GEARMULATOR_MD_FIRMWARE_BIN in the environment.
	add_test(NAME mdSessionFirmwareTest COMMAND mdSessionFirmwareTest)
	set_tests_properties(mdSessionFirmwareTest PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# B-003: the first start without the UW factory cache shows one start-up, not two, and keeps the cache.
	# In ctest; it skips (77) without GEARMULATOR_MD_FIRMWARE_BIN in the environment.
	add_executable(mdFirstStartFirmwareTest mdFirstStartFirmwareTest.cpp)
	target_link_libraries(mdFirstStartFirmwareTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdFirstStartFirmwareTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdFirstStartFirmwareTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	set_property(TARGET mdFirstStartFirmwareTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdFirstStartFirmwareTest COMMAND mdFirstStartFirmwareTest)
	set_tests_properties(mdFirstStartFirmwareTest PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 900)

	# A machine without its ROM is not an error: the session says "missing", the page's card takes the ROM,
	# and the machine starts in place. The plain runs need no ROM; "install" needs the user's own (skips, 77, without).
	add_executable(mdSessionNoRomTest mdSessionNoRomTest.cpp)
	target_link_libraries(mdSessionNoRomTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdSessionNoRomTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdSessionNoRomTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	set_property(TARGET mdSessionNoRomTest PROPERTY FOLDER "Elektron/test")
	foreach(m md mm)
		add_test(NAME mdSessionNoRomTest_${m} COMMAND mdSessionNoRomTest ${m})
		set_tests_properties(mdSessionNoRomTest_${m} PROPERTIES LABELS "Integration" TIMEOUT 120)
		add_test(NAME mdSessionNoRomManageFirmwareTest_${m} COMMAND mdSessionNoRomTest ${m} manage)
		set_tests_properties(mdSessionNoRomManageFirmwareTest_${m} PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 400)
		add_test(NAME mdSessionNoRomInstallFirmwareTest_${m} COMMAND mdSessionNoRomTest ${m} install)
		set_tests_properties(mdSessionNoRomInstallFirmwareTest_${m} PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
	endforeach()
endfunction()

cmake_language(DEFER CALL mdmm_plugin_targets)
