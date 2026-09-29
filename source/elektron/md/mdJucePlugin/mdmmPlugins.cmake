# The Machinedrum and Monomachine Editors' plug-ins (doc/modern-ux/UPSTREAM.md).
#
# Included once from this folder's CMakeLists.txt, after its skin globs and before its binary data:
# the editors' version, sources, page files and icons go in here as values upstream's own lines
# then use. What needs the plug-in targets (libraries, definitions, bundle steps, tests) runs at the
# end of the folder (cmake_language DEFER), after upstream's lines made them.

# The Machinedrum and Monomachine Editors have their own release version, apart from
# Gearmulator's (the bundles, the AU version, the installers and the site use it).
set(MDMM_EDITOR_VERSION 0.2.1)
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

# The editor page is the only UI (P5): upstream's panel skins stay in the tree, not in the product.
list(FILTER SOURCES EXCLUDE REGEX "^skins/(mdDefault|mmSfx60)/")
list(APPEND SOURCES
	mdEditorPages.cpp mdEditorPages.h
	mdDeskHost.cpp mdDeskHost.h
	mdStandaloneApp.cpp
	mdDeskSession.cpp mdDeskSession.h
	mdCtlProfile.cpp mdCtlProfile.h
	mdMidiLearnCommands.cpp mdMidiLearnCommands.h
	mdPageEditor.cpp mdPageEditor.h
	mdRomInstall.cpp mdRomInstall.h
	mdSessionMd.cpp mdSessionMm.cpp mdSessions.h
	mdStudioLink.cpp mdStudioLink.h
	mdWebPageHost.cpp mdWebPageHost.h
	mmStudioLink.cpp mmStudioLink.h
	$<$<PLATFORM_ID:Darwin>:mdStudioWebZoom.mm>
	mdAudioMidiLink.cpp mdAudioMidiLink.h

	skins/mdStudio/mdStudio.rml
	skins/mdStudio/mdStudio.html
	skins/mdStudio/mdDesk.css
	skins/mdStudio/mdDeskApp.js
	skins/mdStudio/mdDeskBridge.js
	skins/mdStudio/mdDeskModal.js
	skins/mdStudio/mdDeskBoot.js
	skins/mdStudio/mdDeskSyx.js
	skins/mdStudio/mdDeskModel.js
	skins/mdStudio/mdDeskMod.js
	skins/mmStudio/mmStudio.rml
	skins/mmStudio/mmStudio.html
	skins/mmStudio/mmStudio.css
	skins/mmStudio/mmMockup.js
	skins/mmStudio/mmConvert.js
	skins/mmStudio/mmAdapter.js
	skins/mmStudio/mmConvertTest.js
	skins/mmStudio/mmSelfTest.js
	skins/mdStudio/mdDeskSelfTest.js
	skins/mdStudio/mdDeskModelTest.js
	skins/mdStudio/mdDeskLive.js
	skins/mdStudio/mdDeskLibrary.js
	skins/mdStudio/mdDeskKeys.js
	skins/mdStudio/mdDeskGlobal.js
	skins/mdStudio/mdDeskAudio.js
	skins/mdStudio/deskController.js)

# P6: the editors' diagnostics (the log of the web view, the window chrome and the session's
# state, and the pages' self-tests: mdDeskSelfTest.js, mmSelfTest.js) observe the editors. Off by
# default for every generator and configuration (single- and multi-config), so a release package
# has none of it; a test or development build turns it on (-Dgearmulator_MDMM_DIAGNOSTICS=ON).
option(gearmulator_MDMM_DIAGNOSTICS "The Machinedrum/Monomachine Editors' diagnostics log and self-tests" OFF)
if(gearmulator_MDMM_DIAGNOSTICS)
	list(APPEND SOURCES mdDiagnostics.cpp mdDiagnostics.h)
endif()
# DESIGN-edit-flow.md: the edit-flow driver (mdEditFlowDriver.h) replays the page's messages into the
# plug-in's session when GEARMULATOR_EDITFLOW_DRIVE is set, for mdVst3EditFlowHost. A test build only.
option(gearmulator_MDMM_EDITFLOW_DRIVER "The Machinedrum/Monomachine Editors' edit-flow driver (test builds)" OFF)
if(gearmulator_MDMM_EDITFLOW_DRIVER)
	list(APPEND SOURCES mdEditFlowDriver.cpp mdEditFlowDriver.h mdEditFlowCounters.h)
endif()

# The pages instead of the panel skins in the plug-ins' binary data.
file(GLOB MD_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mdStudio/*.rml" "skins/mdStudio/*.html" "skins/mdStudio/*.css" "skins/mdStudio/*.js"
	"skins/mdStudio/fonts/*.woff2" "skins/mdStudio/fonts/*.ttf")
file(GLOB MM_SKIN_ASSETS CONFIGURE_DEPENDS
	"skins/mmStudio/*.rml" "skins/mmStudio/*.html" "skins/mmStudio/*.css"
	"skins/mmStudio/mmMockup.js" "skins/mmStudio/mmConvert.js" "skins/mmStudio/mmAdapter.js"
	# shared with the Machinedrum Editor: the page bridge, the controller panel and the OFL fonts
	"skins/mdStudio/mdDeskBridge.js" "skins/mdStudio/deskController.js" "skins/mdStudio/fonts/*.ttf")
# The tests are not the page, named one by one, not by a file-name pattern: the node tests never
# ship, the self-tests only with the diagnostics. A test that was renamed or moved stops the
# configure, so it cannot slip into the glob. The MM glob lists its page files already.
set(MD_NODE_TESTS "skins/mdStudio/mdDeskModelTest.js" "skins/mdStudio/deskControllerPageTest.js" "skins/mdStudio/deskControllerBrowserTest.js")
set(MD_SELF_TESTS "skins/mdStudio/mdDeskSelfTest.js")
set(MM_SELF_TESTS "skins/mmStudio/mmSelfTest.js")
foreach(test ${MD_NODE_TESTS} ${MD_SELF_TESTS} ${MM_SELF_TESTS})
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

# App icons (doc/modern-ux/icons, built with build-icons.sh), through juce.cmake's per-target arguments.
set(GEARMULATOR_PLUGIN_EXTRA_ARGS_mdJucePlugin
	ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/md-1024.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icons/md-32.png")
set(GEARMULATOR_PLUGIN_EXTRA_ARGS_mmJucePlugin
	ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/mm-1024.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icons/mm-32.png")

function(mdmm_plugin_targets)
	foreach(plugin_target mdJucePlugin mmJucePlugin)
		target_link_libraries(${plugin_target} PRIVATE elektronData mdDataLink mdDesk mmDesk deskHost deskWire deskController)
		target_compile_definitions(${plugin_target} PUBLIC
			# jucePluginEditorLib/standaloneApp.h: native title bar and menu bar (P4).
			JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
			MDMM_DIAGNOSTICS=$<BOOL:${gearmulator_MDMM_DIAGNOSTICS}>
			MDMM_EDITFLOW_DRIVER=$<BOOL:${gearmulator_MDMM_EDITFLOW_DRIVER}>)
	endforeach()

	if(APPLE)
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
	target_compile_definitions(mdRomInstallTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1)
	add_test(NAME mdRomInstallTest COMMAND mdRomInstallTest)
	set_tests_properties(mdRomInstallTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdRomInstallTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdWindowFitTest mdWindowFitTest.cpp)
	target_include_directories(mdWindowFitTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	add_test(NAME mdWindowFitTest COMMAND mdWindowFitTest)
	set_tests_properties(mdWindowFitTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdWindowFitTest PROPERTY FOLDER "Elektron/test")

	# P6: the Machinedrum page's model (the pure view and the optimistic overlay), when node is here.
	find_program(GEARMULATOR_NODE node)
	if(GEARMULATOR_NODE)
		add_test(NAME mdDeskModelPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/mdDeskModelTest.js)
		set_tests_properties(mdDeskModelPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# The controller profile's panel (DESIGN-tr06.md): its document and its commands against both contracts.
		add_test(NAME deskControllerPageTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/deskControllerPageTest.js)
		set_tests_properties(deskControllerPageTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
		# The same panel on both shipped pages in a headless Chrome (dev mode, a fake host): the page's own
		# dropdowns, the CONTROL workspace's key, the device and activity hints. Skips (77) without Chrome.
		add_test(NAME deskControllerBrowserTest COMMAND ${GEARMULATOR_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/skins/mdStudio/deskControllerBrowserTest.js)
		set_tests_properties(deskControllerBrowserTest PROPERTIES LABELS "UnitTest" TIMEOUT 240 SKIP_RETURN_CODE 77)
	endif()

	# P4: the editor's setup (MDSK chunk) round-trips with the plug-in state.
	add_executable(mdDeskSetupStateTest mdDeskSetupStateTest.cpp)
	target_link_libraries(mdDeskSetupStateTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules deskController
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
	juce_add_console_app(mdVst3EditFlowHost PRODUCT_NAME "mdVst3EditFlowHost")
	target_sources(mdVst3EditFlowHost PRIVATE mdVst3EditFlowHost.cpp)
	target_link_libraries(mdVst3EditFlowHost PRIVATE juce::juce_audio_processors juce::juce_events)
	target_compile_definitions(mdVst3EditFlowHost PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_PLUGINHOST_VST3=1
		JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
	set_property(TARGET mdVst3EditFlowHost PROPERTY FOLDER "Elektron/test")

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

	# The controller profile (DESIGN-tr06.md) on the firmware, simulated TR-06 MIDI through the processor:
	# in ctest, each skips (77) without GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN.
	add_executable(mdTr06FirmwareTest mdTr06FirmwareTest.cpp)
	target_link_libraries(mdTr06FirmwareTest PRIVATE
		mdJucePlugin jucePluginEditorLib mdLib juce_plugin_modules
		juce::juce_opengl)
	target_include_directories(mdTr06FirmwareTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../..)
	target_compile_definitions(mdTr06FirmwareTest PRIVATE
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		MDDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/md-data-contract.schema.json"
		MMDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/mm-data-contract.schema.json")
	set_property(TARGET mdTr06FirmwareTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdTr06FirmwareTest COMMAND mdTr06FirmwareTest md)
	add_test(NAME mmTr06FirmwareTest COMMAND mdTr06FirmwareTest mm)
	set_tests_properties(mdTr06FirmwareTest mmTr06FirmwareTest PROPERTIES LABELS "Integration;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
endfunction()

cmake_language(DEFER CALL mdmm_plugin_targets)
