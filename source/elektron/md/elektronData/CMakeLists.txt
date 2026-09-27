cmake_minimum_required(VERSION 3.15)

project(elektronData)

# Pure value codec for Elektron MD user data (pattern, kit, song, global dumps). No JUCE,
# no device: it depends on nothing but the standard library.
add_library(elektronData STATIC)

target_sources(elektronData PRIVATE
	dumpIo.h
	json.cpp json.h
	mdCommands.cpp mdCommands.h
	mdGlobal.cpp mdGlobal.h
	mdJson.cpp mdJson.h
	mdKit.cpp mdKit.h
	mdMachines.cpp mdMachines.h
	mdPattern.cpp mdPattern.h
	mdSong.cpp mdSong.h
	mdValidate.cpp mdValidate.h
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

	# Byte-exact + JSON-exact round trip of every dump under the given paths.
	# The committed fixtures are programmed (not factory) data captured from the
	# emulated firmware; a full local backup can be added with
	# -DELEKTRONDATA_CORPUS_DIR=<dir of .syx dumps>.
	add_executable(elektronDataCorpusTest elektronDataCorpusTest.cpp)
	target_link_libraries(elektronDataCorpusTest PRIVATE elektronData baseLib)
	add_test(NAME elektronDataCorpusTest COMMAND elektronDataCorpusTest ${CMAKE_CURRENT_SOURCE_DIR}/testdata)
	set_tests_properties(elektronDataCorpusTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET elektronDataCorpusTest PROPERTY FOLDER "Elektron/test")
	if(ELEKTRONDATA_CORPUS_DIR)
		add_test(NAME elektronDataLocalCorpusTest COMMAND elektronDataCorpusTest ${ELEKTRONDATA_CORPUS_DIR})
		set_tests_properties(elektronDataLocalCorpusTest PROPERTIES LABELS "UnitTest")
	endif()
endif()
