cmake_minimum_required(VERSION 3.15)

project(deskController)

# Controller profiles (doc/modern-ux/DESIGN-tr06.md): a Roland TR-06 on one MIDI channel plays the
# Machinedrum or the Monomachine. Pure (no JUCE, no device): the controller's facts and the default
# mapping are data (tr06.json, compiled in), the input translation is lock-free for the MIDI
# threads, the knob pump paces edits for the desk. The plug-in's session and its MIDI input filter
# use it; the unit test runs it alone.
set(_deskControllerJson ${CMAKE_CURRENT_SOURCE_DIR}/tr06.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_deskControllerJson})
file(READ ${_deskControllerJson} DESK_CONTROLLER_TR06_JSON)
configure_file(tr06Json.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tr06Json.h @ONLY)
unset(_deskControllerJson)

add_library(deskController STATIC)
target_sources(deskController PRIVATE deskController.cpp deskController.h tr06.json tr06Json.h.in)
target_link_libraries(deskController PUBLIC elektronData)
target_include_directories(deskController PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(deskController PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_features(deskController PUBLIC cxx_std_17)
set_property(TARGET deskController PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(deskControllerTest deskControllerTest.cpp)
	target_link_libraries(deskControllerTest PRIVATE deskController)
	target_compile_definitions(deskControllerTest PRIVATE DESK_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux")
	add_test(NAME deskControllerTest COMMAND deskControllerTest)
	set_tests_properties(deskControllerTest PROPERTIES LABELS "UnitTest;Midi")
	set_property(TARGET deskControllerTest PROPERTY FOLDER "Elektron/test")
endif()
