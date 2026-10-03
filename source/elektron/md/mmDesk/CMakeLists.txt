cmake_minimum_required(VERSION 3.15)

project(mmDesk)

# Monomachine Editor behind its page: documents in, validated delivery (SYSEX RECV
# session, live CC/NRPN/SysEx), firmware read-backs and machine state out. Pure:
# no JUCE, no device; the host implements mmDesk::Desk::Port.
add_library(mmDesk STATIC)
target_sources(mmDesk PRIVATE
	mmDesk.cpp mmDesk.h
	mmDeskAdapter.h
	mmDeskTelemetry.h
	mmDeskChain.cpp
	mmDeskCommands.cpp
	mmDeskDelivery.cpp
	mmDeskEdit.cpp
	mmDeskLoad.cpp
	mmDeskMachine.cpp mmDeskMachine.h mmDeskMachineParts.h
	mmDeskNotes.cpp
	mmDeskRecord.cpp
	mmDeskWatch.h
	mmDeskModel.cpp mmDeskModel.h
	mmRecv.cpp mmRecv.h
)
target_link_libraries(mmDesk PUBLIC deskCore elektronData)
target_include_directories(mmDesk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mmDesk PUBLIC cxx_std_17)
set_property(TARGET mmDesk PROPERTY FOLDER "Elektron")

# mmDeskWirePort.h bridges mmDesk's DevicePort to deskWire (P4 HW MIDI): it is not part of
# mmDesk's own sources (only the engines that speak MIDI need it), so it gets its own tiny
# INTERFACE target rather than making every mmDesk consumer pull in deskWire.
add_library(mmDeskWirePort INTERFACE)
target_sources(mmDeskWirePort INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/mmDeskWirePort.h)
target_link_libraries(mmDeskWirePort INTERFACE mmDesk deskWire)

if(BUILD_TESTING)
	add_executable(mmDeskTest mmDeskTest.cpp)
	target_link_libraries(mmDeskTest PRIVATE mmDesk deskHost)
	target_compile_definitions(mmDeskTest PRIVATE MMDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/mm-data-contract.schema.json"
		MMDESK_CATALOGUE="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/mm-catalogue.json"
		MMDESK_INTENT_CASES="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/intent-cases.json")
	add_test(NAME mmDeskTest COMMAND mmDeskTest)
	set_tests_properties(mmDeskTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mmDeskTest PROPERTY FOLDER "Elektron/test")
endif()
