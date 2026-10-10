cmake_minimum_required(VERSION 3.15)

# ----------------- Add our cmake scripts to IDE

add_custom_target(cmakeScripts SOURCES 
	exporttarget.cmake
	juce.cmake 
	macsetup.cmake 
	macsetup.command.in 
	runAuValidation.cmake 
	skins.cmake
	skins.h.in
	../base.cmake)

# ----------------- DSP56300 emulator

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(dsp56300/source)

# ----------------- Common libraries

add_subdirectory(baseLib)
add_subdirectory(synthLib)
add_subdirectory(libresample)

add_subdirectory(3rdparty)

include(macsetup.cmake)
include(skins.cmake)
include(exporttarget.cmake)

# ----------------- network bridge

add_subdirectory(ptypes EXCLUDE_FROM_ALL)
add_subdirectory(networkLib EXCLUDE_FROM_ALL)
add_subdirectory(bridge)

# ----------------- MCP server

# MCP server is always built, but can be enabled/disabled at runtime via plugin settings

# ----------------- Juce based audio plugin dependencies

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
	add_subdirectory(JUCE EXCLUDE_FROM_ALL)
	# Visual Studio omits test executables from an excluded subdirectory's
	# top-level solution, so explicit `cmake --build --target` calls cannot find
	# them. Keep normal release builds lazy while making shared tests addressable
	# on every generator when testing is requested.
	if(BUILD_TESTING)
		add_subdirectory(jucePluginLib)
	else()
		add_subdirectory(jucePluginLib EXCLUDE_FROM_ALL)
	endif()
	add_subdirectory(juceRmlUi)
	add_subdirectory(juceRmlPlugin)
	add_subdirectory(juceUiLib EXCLUDE_FROM_ALL)
	add_subdirectory(jucePluginEditorLib EXCLUDE_FROM_ALL)
	add_subdirectory(jucePluginData EXCLUDE_FROM_ALL)
	add_subdirectory(pluginTester)
	add_subdirectory(midiLearnTest)
	include(juce.cmake)
endif()

# ----------------- dependencies

add_subdirectory(mc68k EXCLUDE_FROM_ALL)
add_subdirectory(hardwareLib EXCLUDE_FROM_ALL)

# ----------------- the Machinedrum and Monomachine editors

add_subdirectory(elektron)
