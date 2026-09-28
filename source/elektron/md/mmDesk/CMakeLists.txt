cmake_minimum_required(VERSION 3.15)

project(mmDesk)

# Monomachine Editor behind its page: documents in, validated delivery (SYSEX RECV
# session, live CC/NRPN/SysEx), firmware read-backs and machine state out. Pure:
# no JUCE, no device; the host implements mmDesk::Desk::Port.
add_library(mmDesk STATIC)
target_sources(mmDesk PRIVATE
	mmDesk.cpp mmDesk.h
	mmDeskAdapter.h
	mmDeskMachine.cpp mmDeskMachine.h
	mmDeskModel.cpp mmDeskModel.h
	mmRecv.cpp mmRecv.h
)
target_link_libraries(mmDesk PUBLIC deskCore elektronData)
target_include_directories(mmDesk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mmDesk PUBLIC cxx_std_17)
set_property(TARGET mmDesk PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mmDeskTest mmDeskTest.cpp)
	target_link_libraries(mmDeskTest PRIVATE mmDesk deskHost)
	target_compile_definitions(mmDeskTest PRIVATE MMDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/mm-data-contract.schema.json")
	add_test(NAME mmDeskTest COMMAND mmDeskTest)
	set_tests_properties(mmDeskTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mmDeskTest PROPERTY FOLDER "Elektron/test")
endif()
