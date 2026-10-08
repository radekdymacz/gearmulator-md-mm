cmake_minimum_required(VERSION 3.15)

project(mdmmUpdate C CXX)

# The in-app updater's pure parts (doc/modern-ux/DESIGN-updates.md): versions, latest.json, the daily schedule,
# SHA-256 and the ed25519 check of a download (vendored Monocypher, monocypher/README.mdmm.md). No JUCE: the
# plug-in's side is mdJucePlugin/mdUpdater.*.
add_library(mdmmMonocypher STATIC
	monocypher/monocypher.c monocypher/monocypher.h
	monocypher/monocypher-ed25519.c monocypher/monocypher-ed25519.h)
target_include_directories(mdmmMonocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mdmmMonocypher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON FOLDER "Elektron")

add_library(mdmmUpdate STATIC)
target_sources(mdmmUpdate PRIVATE updateCore.cpp updateCore.h updateKey.h)
target_link_libraries(mdmmUpdate PUBLIC elektronJson PRIVATE mdmmMonocypher)
target_include_directories(mdmmUpdate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mdmmUpdate PUBLIC cxx_std_17)
set_target_properties(mdmmUpdate PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdmmUpdateTest mdmmUpdateTest.cpp)
	target_link_libraries(mdmmUpdateTest PRIVATE mdmmUpdate)
	add_test(NAME mdmmUpdateTest COMMAND mdmmUpdateTest)
	set_tests_properties(mdmmUpdateTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdmmUpdateTest PROPERTY FOLDER "Elektron/test")

	# The release scripts (scripts/release): RFC 8032 vectors, signing, latest.json, the site's version line.
	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME mdmmReleaseScriptsTest
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../../scripts/release/test_release_scripts.py)
		set_tests_properties(mdmmReleaseScriptsTest PROPERTIES LABELS "UnitTest" TIMEOUT 120)
	endif()
endif()
