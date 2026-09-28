cmake_minimum_required(VERSION 3.15)

project(mdDesk)

# MD Desk's editing model: UI commands -> document edits, undo history, the
# live edits a working-kit change needs, and the page-facing orchestrator.
# Depends on elektronData and mdDataLink only; no JUCE, no device.
add_library(mdDesk STATIC)

target_sources(mdDesk PRIVATE
	mdDesk.cpp mdDesk.h
	mdDeskChain.cpp mdDeskChain.h
	mdDeskDelivery.cpp mdDeskDelivery.h
	mdDeskEdit.cpp mdDeskEdit.h
	mdDeskHistory.h
	mdDeskLibrary.cpp mdDeskLibrary.h
	mdDeskMachine.cpp mdDeskMachine.h
	mdDeskModel.cpp mdDeskModel.h
	mdDeskMod.cpp mdDeskMod.h
	mdDeskRecord.cpp mdDeskRecord.h
	mdDeskSetup.cpp mdDeskSetup.h
	mdDeskTelemetry.h
	mdDeskWorkingKit.cpp mdDeskWorkingKit.h
)

target_link_libraries(mdDesk PUBLIC deskCore elektronData mdDataLink)
target_include_directories(mdDesk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mdDesk PUBLIC cxx_std_17)
set_property(TARGET mdDesk PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDeskTest mdDeskTest.cpp)
	target_link_libraries(mdDeskTest PRIVATE mdDesk)
	target_compile_definitions(mdDeskTest PRIVATE MDDESK_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../../../../doc/modern-ux/md-data-contract.schema.json")
	target_compile_definitions(mdDeskTest PRIVATE
		MDDESK_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../elektronData/testdata")
	add_test(NAME mdDeskTest COMMAND mdDeskTest)
	set_tests_properties(mdDeskTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdDeskTest PROPERTY FOLDER "Elektron/test")
endif()
