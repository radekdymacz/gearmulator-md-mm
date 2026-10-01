cmake_minimum_required(VERSION 3.15)

project(elektronData)

# JSON values and the JSON Schema validator (P6): what the engine-neutral deskCore needs, without
# the Elektron codecs.
add_library(elektronJson STATIC)
target_sources(elektronJson PRIVATE json.cpp json.h jsonSchema.cpp jsonSchema.h)
target_include_directories(elektronJson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(elektronJson PUBLIC cxx_std_17)
set_property(TARGET elektronJson PROPERTY FOLDER "Elektron")

# Pure value codecs for Elektron MD and MM user data (pattern, kit, song, global dumps). No JUCE,
# no device: it depends on nothing but the standard library.
add_library(elektronData STATIC)

target_sources(elektronData PRIVATE
	dumpIo.h
	mdAudition.cpp mdAudition.h
	jsonFirmware.cpp jsonFirmware.h
	mdCommands.cpp mdCommands.h
	mdGlobal.cpp mdGlobal.h
	mdJson.cpp mdJson.h
	mdKit.cpp mdKit.h
	mdSamples.cpp mdSamples.h
	mdWorkingKit.cpp mdWorkingKit.h
	mdMachines.cpp mdMachines.h
	mdNames.h
	mdPattern.cpp mdPattern.h
	mdSong.cpp mdSong.h
	mdValidate.cpp mdValidate.h
	mmCommands.cpp mmCommands.h
	mmDump.cpp mmDump.h
	mmGlobal.cpp mmGlobal.h
	mmJson.cpp mmJson.h
	mmKit.cpp mmKit.h
	mmLayout.h
	mmScreen.h
	mmMachines.cpp mmMachines.h
	mmPattern.cpp mmPattern.h
	mmSong.cpp mmSong.h
	mmValidate.cpp mmValidate.h
	sysex7bit.cpp sysex7bit.h
	syxImport.cpp syxImport.h
)

target_link_libraries(elektronData PUBLIC elektronJson)
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
	add_test(NAME elektronDataCorpusTest COMMAND elektronDataCorpusTest
		--schema ${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/md-data-contract.schema.json ${CMAKE_CURRENT_SOURCE_DIR}/testdata)
	set_tests_properties(elektronDataCorpusTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET elektronDataCorpusTest PROPERTY FOLDER "Elektron/test")
	# Monomachine OS 1.32B: the same checks (byte-exact, JSON-exact, valid) on
	# programmed firmware read-backs in testdata/mm; a local factory backup can be
	# added with -DELEKTRONDATA_MM_CORPUS_DIR=<dir of .syx dumps>.
	add_executable(mmDataTest mmDataTest.cpp)
	target_link_libraries(mmDataTest PRIVATE elektronData)
	add_test(NAME mmDataTest COMMAND mmDataTest)
	set_tests_properties(mmDataTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mmDataTest PROPERTY FOLDER "Elektron/test")

	add_executable(mmDataCorpusTest mmDataCorpusTest.cpp)
	target_link_libraries(mmDataCorpusTest PRIVATE elektronData baseLib)
	add_test(NAME mmDataCorpusTest COMMAND mmDataCorpusTest
		--schema ${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/mm-data-contract.schema.json ${CMAKE_CURRENT_SOURCE_DIR}/testdata/mm)
	set_tests_properties(mmDataCorpusTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mmDataCorpusTest PROPERTY FOLDER "Elektron/test")
	if(ELEKTRONDATA_MM_CORPUS_DIR)
		add_test(NAME mmDataLocalCorpusTest COMMAND mmDataCorpusTest ${ELEKTRONDATA_MM_CORPUS_DIR})
		set_tests_properties(mmDataLocalCorpusTest PROPERTIES LABELS "UnitTest")
	endif()
	# The Monomachine Editor page's translation (skins/mmStudio/mmConvert.js), when node is there.
	find_program(GEARMULATOR_NODE node)
	if(GEARMULATOR_NODE)
		set(mm_convert_corpus "${CMAKE_CURRENT_SOURCE_DIR}/testdata/mm")
		if(ELEKTRONDATA_MM_CORPUS_DIR)
			list(APPEND mm_convert_corpus "${ELEKTRONDATA_MM_CORPUS_DIR}")
		endif()
		set(mm_skin "${CMAKE_CURRENT_SOURCE_DIR}/../mdJucePlugin/skins/mmStudio")
		add_test(NAME mmConvertTest COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:mmDataCorpusTest> -DNODE=${GEARMULATOR_NODE}
			-DSKIN=${mm_skin} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/mmConvertTest "-DCORPUS=${mm_convert_corpus}" -P ${mm_skin}/mmConvertTest.cmake)
		set_tests_properties(mmConvertTest PROPERTIES LABELS "UnitTest")
	endif()
	if(ELEKTRONDATA_CORPUS_DIR)
		add_test(NAME elektronDataLocalCorpusTest COMMAND elektronDataCorpusTest ${ELEKTRONDATA_CORPUS_DIR})
		set_tests_properties(elektronDataLocalCorpusTest PROPERTIES LABELS "UnitTest")
	endif()
endif()
