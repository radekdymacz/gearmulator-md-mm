# The Machinedrum and Monomachine Editors: this fork's own build (doc/modern-ux/UPSTREAM.md).
#
# Included once from md/CMakeLists.txt, right after mdLib. Every library, source and test of the
# editors is declared here or in the editors' own folders, never in upstream's CMakeLists: those
# carry one include line each, so a merge from upstream meets no lines of ours.

add_subdirectory(elektronData)
add_subdirectory(deskCore)
add_subdirectory(deskHost)
add_subdirectory(mdDataLink)
add_subdirectory(mdDesk)
add_subdirectory(mmDesk)
add_subdirectory(deskWire)

# Pure CC parameter-change maps (P6): MD kit parameters and MM parameters/mute as MIDI CCs on a
# base channel, and back. No device, no emulator, no JUCE: deskWire's wire engines and the
# firmware test rigs encode with this same code, and mdLib links it for its own SysEx automation
# glue (mdautomationsync, mdsysexautomation). Upstream compiles mdautomation.cpp into mdLib; it
# moves into this library, so a program that links both has one copy.
add_library(mdAutomation STATIC mdLib/mdautomation.cpp mdLib/mdautomation.h)
target_include_directories(mdAutomation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mdAutomation PUBLIC cxx_std_17)
set_property(TARGET mdAutomation PROPERTY FOLDER "Elektron")

get_target_property(_mdmmMdLibSources mdLib SOURCES)
list(FILTER _mdmmMdLibSources EXCLUDE REGEX "(^|/)mdautomation\\.(cpp|h)$")
set_property(TARGET mdLib PROPERTY SOURCES ${_mdmmMdLibSources})
unset(_mdmmMdLibSources)

# The editors' parts of mdLib: the machine's state for the editor pages (md::DeskDevice, the MD and
# MM telemetry), the named panel key presses and the ROM check.
target_sources(mdLib PRIVATE
	mdLib/mddeskdevice.cpp mdLib/mddeskdevice.h
	mdLib/mdpanelsequence.cpp mdLib/mdpanelsequence.h
	mdLib/mdsequencerstate.h
	mdLib/mmtelemetry.h
	mdLib/mdromcheck.h)
target_link_libraries(mdLib PUBLIC elektronData mdAutomation)

# The JUCE web view (the editor pages are web pages): on where JUCE's backend ships with the OS
# (WKWebView on macOS, Edge/IE on Windows). Linux would also need webkit2gtk and NEEDS_WEB_BROWSER.
# juce.cmake compiles JUCE_WEB_BROWSER=0 into juce_plugin_modules; this switches it.
if(APPLE OR WIN32)
	set(_mdmmWebBrowserDefault ON)
else()
	set(_mdmmWebBrowserDefault OFF)
endif()
option(${CMAKE_PROJECT_NAME}_JUCE_WEB_BROWSER "Compile JUCE WebBrowserComponent into plugins" ${_mdmmWebBrowserDefault})
unset(_mdmmWebBrowserDefault)
if(TARGET juce_plugin_modules)
	if(${CMAKE_PROJECT_NAME}_JUCE_WEB_BROWSER)
		set(_mdmmWebBrowser 1)
	else()
		set(_mdmmWebBrowser 0)
	endif()
	foreach(_mdmmProperty COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
		get_target_property(_mdmmDefinitions juce_plugin_modules ${_mdmmProperty})
		if(NOT _mdmmDefinitions)
			set(_mdmmDefinitions)
		endif()
		list(FILTER _mdmmDefinitions EXCLUDE REGEX "^JUCE_WEB_BROWSER=")
		list(APPEND _mdmmDefinitions JUCE_WEB_BROWSER=${_mdmmWebBrowser})
		set_property(TARGET juce_plugin_modules PROPERTY ${_mdmmProperty} ${_mdmmDefinitions})
	endforeach()
	unset(_mdmmDefinitions)
	unset(_mdmmProperty)
	unset(_mdmmWebBrowser)
endif()

if(BUILD_TESTING)
	include(mdLibTest/mdmmTests.cmake)
endif()
