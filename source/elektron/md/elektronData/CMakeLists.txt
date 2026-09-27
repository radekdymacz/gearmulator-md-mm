cmake_minimum_required(VERSION 3.15)

project(elektronData)

# Pure value codec for Elektron MD user data (pattern, kit, song, global dumps). No JUCE,
# no device: it depends on nothing but the standard library.
add_library(elektronData STATIC)

target_sources(elektronData PRIVATE
	dumpIo.h
	mdCommands.cpp mdCommands.h
	mdGlobal.cpp mdGlobal.h
	mdKit.cpp mdKit.h
	mdMachines.cpp mdMachines.h
	mdPattern.cpp mdPattern.h
	mdSong.cpp mdSong.h
	sysex7bit.cpp sysex7bit.h
)

target_include_directories(elektronData PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(elektronData PUBLIC cxx_std_17)
set_property(TARGET elektronData PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(elektronDataTest elektronDataTest.cpp)
	target_link_libraries(elektronDataTest PRIVATE elektronData)
	add_test(NAME elektronDataTest COMMAND elektronDataTest)
	set_tests_properties(elektronDataTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET elektronDataTest PROPERTY FOLDER "Elektron/test")
endif()
