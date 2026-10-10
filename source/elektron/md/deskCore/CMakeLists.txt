cmake_minimum_required(VERSION 3.15)

project(deskCore)

# The engine-neutral parts of the Machinedrum and Monomachine Editors (P6,
# doc/modern-ux/DESIGN-P6-simple-core.md): documents as observed + pending, the
# core, the Machine adapter protocol, undo, the load queue, latest-wins pushes,
# the lifecycle, sequences on facts, capabilities and the command table. Pure: no
# JUCE, no device, no model.
add_library(deskCore STATIC)

target_sources(deskCore PRIVATE
	deskAdapter.h
	deskBlocks.h
	deskCapabilities.h
	deskChain.h
	deskCommands.cpp deskCommands.h
	deskContract.h
	deskEdits.h
	deskCore.h
	deskDesk.h
	deskLcd.h
	deskHistory.h
	deskLifecycle.cpp deskLifecycle.h
	deskLoadQueue.h
	deskMod.cpp deskMod.h
	deskNotes.h
	deskPacer.h
	deskPush.h
	deskRef.h
	deskSequence.h
	deskSongRow.h
	deskStream.h
)

target_link_libraries(deskCore PUBLIC elektronJson)
target_include_directories(deskCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(deskCore PUBLIC cxx_std_17)
set_property(TARGET deskCore PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(deskCoreTest deskCoreTest.cpp)
	target_link_libraries(deskCoreTest PRIVATE deskCore)
	add_test(NAME deskCoreTest COMMAND deskCoreTest)
	set_tests_properties(deskCoreTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET deskCoreTest PROPERTY FOLDER "Elektron/test")
endif()
