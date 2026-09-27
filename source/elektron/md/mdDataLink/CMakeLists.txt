cmake_minimum_required(VERSION 3.15)

project(mdDataLink)

# Transport glue for MD Desk: value-level requests/pushes over the firmware's
# SysEx, plus the machine state the UI needs (queued pattern, unsaved kit).
# Depends on elektronData only; bytes cross only at the device edge.
add_library(mdDataLink STATIC)

target_sources(mdDataLink PRIVATE
	mdDataLink.cpp mdDataLink.h
)

target_link_libraries(mdDataLink PUBLIC elektronData)
target_include_directories(mdDataLink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mdDataLink PUBLIC cxx_std_17)
set_property(TARGET mdDataLink PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDataLinkTest mdDataLinkTest.cpp)
	target_link_libraries(mdDataLinkTest PRIVATE mdDataLink)
	add_test(NAME mdDataLinkTest COMMAND mdDataLinkTest)
	set_tests_properties(mdDataLinkTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mdDataLinkTest PROPERTY FOLDER "Elektron/test")
endif()
