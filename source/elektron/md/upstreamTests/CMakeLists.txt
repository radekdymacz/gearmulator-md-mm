# Tests the fork carries for upstream's code (doc/modern-ux/UPSTREAM.md): synthLib's plug-in wrapper, which the fork
# fixed and upstream has no tests for. Added from mdmmEditors.cmake when BUILD_TESTING is on; the ctest names are the
# ones the CI lists use. (The MCP server, the DSP bridge and their tests were deleted in 0.5.)

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

