cmake_minimum_required(VERSION 3.15)

project(deskWire)

# The MIDI wire (P6): a machine at the end of a MIDI cable, paced at DIN speed, and the encoders a
# wire speaks (MD kit parameter / level / mute as CCs, MM parameter CCs and NRPN, PLAY/STOP as
# realtime). Pure (no JUCE) and header only: the plug-in's wire engines and the firmware test rigs
# (mdLibTest) use the same code, so the HW MIDI firmware test exercises the plug-in's wire.
add_library(deskWire INTERFACE)
target_sources(deskWire INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/deskWire.h
	${CMAKE_CURRENT_SOURCE_DIR}/mdWire.h
	${CMAKE_CURRENT_SOURCE_DIR}/mmWire.h)
target_link_libraries(deskWire INTERFACE deskCore mdLib)
target_include_directories(deskWire INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(deskWire INTERFACE cxx_std_17)

if(BUILD_TESTING)
	add_executable(deskWireTest deskWireTest.cpp)
	target_link_libraries(deskWireTest PRIVATE deskWire)
	add_test(NAME deskWireTest COMMAND deskWireTest)
	set_tests_properties(deskWireTest PROPERTIES LABELS "UnitTest;Midi")
	set_property(TARGET deskWireTest PROPERTY FOLDER "Elektron/test")
endif()
