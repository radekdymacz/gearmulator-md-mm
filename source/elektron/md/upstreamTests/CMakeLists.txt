# Tests the fork carries for upstream's code (doc/modern-ux/UPSTREAM.md): the DSP bridge and synthLib's plug-in wrapper, which the fork fixed and upstream has no tests for. They live here, not in
# upstream's folders and CMakeLists, so a merge from upstream meets none of their lines. Added from mdmmEditors.cmake
# when BUILD_TESTING is on; the ctest names are the ones the CI lists use.
#
# networkLib and bridgeLib are not in the default build (EXCLUDE_FROM_ALL); a target here that needs one of them
# builds it as a dependency, on every generator. The bridge server is a program, so its test compiles the sources it
# tests directly. (The MCP server and its tests were deleted in 0.5.)

set(_mdmmSource ${CMAKE_CURRENT_SOURCE_DIR}/../../..)	# source/

# The bridge protocol: sizes and counts from the peer checked, a malformed command ends its connection, not the process
add_executable(bridgeLibTest bridgeLibTest.cpp)
target_link_libraries(bridgeLibTest PRIVATE bridgeLib)
add_test(NAME bridgeLibTest COMMAND bridgeLibTest)
set_tests_properties(bridgeLibTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
set_property(TARGET bridgeLibTest PROPERTY FOLDER "Tests")

# The bridge server's ROM cache: named after the content only, inside the ROM folder, bounded in size
add_executable(bridgeServerRomPoolTest romPoolTest.cpp
	${_mdmmSource}/bridge/server/romPool.cpp ${_mdmmSource}/bridge/server/romPool.h
	${_mdmmSource}/bridge/server/config.cpp ${_mdmmSource}/bridge/server/config.h)
target_include_directories(bridgeServerRomPoolTest PRIVATE ${_mdmmSource}/bridge/server)
target_link_libraries(bridgeServerRomPoolTest PRIVATE bridgeLib)
add_test(NAME bridgeServerRomPoolTest COMMAND bridgeServerRomPoolTest)
set_tests_properties(bridgeServerRomPoolTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
set_property(TARGET bridgeServerRomPoolTest PROPERTY FOLDER "Tests")

# synthLib::Plugin: a state save encodes after the process lock is released (Device::StateCapture), and a full MIDI
# ring never replays a stale event
add_executable(synthLibStateCaptureTest synthLibStateCaptureTest.cpp)
target_link_libraries(synthLibStateCaptureTest PRIVATE synthLib)
add_test(NAME synthLibStateCaptureTest COMMAND synthLibStateCaptureTest)
set_tests_properties(synthLibStateCaptureTest PROPERTIES LABELS "UnitTest")
set_property(TARGET synthLibStateCaptureTest PROPERTY FOLDER "Tests")

add_executable(synthLibMidiQueueTest synthLibMidiQueueTest.cpp)
target_link_libraries(synthLibMidiQueueTest PRIVATE synthLib)
add_test(NAME synthLibMidiQueueTest COMMAND synthLibMidiQueueTest)
set_tests_properties(synthLibMidiQueueTest PROPERTIES LABELS "UnitTest;Midi" TIMEOUT 60)
set_property(TARGET synthLibMidiQueueTest PROPERTY FOLDER "Tests")

unset(_mdmmSource)
