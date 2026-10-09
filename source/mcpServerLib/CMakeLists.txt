cmake_minimum_required(VERSION 3.15)

project(mcpServerLib)

add_library(mcpServerLib STATIC)

set(SOURCES
	discoveryFile.cpp discoveryFile.h
	httpGuard.h
	httpRequest.h
	httpResponse.h
	httpServer.cpp httpServer.h
	jsonHelpers.h
	jsonRpc.h
	mcpPluginServer.cpp mcpPluginServer.h
	mcpServer.cpp mcpServer.h
	mcpTool.h
	mcpTypes.h
)

target_sources(mcpServerLib PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(mcpServerLib PUBLIC networkLib jucePluginLib)
target_include_directories(mcpServerLib PUBLIC ../ ../JUCE/modules)
target_compile_definitions(mcpServerLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
set_property(TARGET mcpServerLib PROPERTY FOLDER "MCP")

if(BUILD_TESTING)
	# The HTTP layer needs networkLib only: its tests build its sources directly instead of linking
	# mcpServerLib, which needs the plug-in libraries and JUCE.
	add_executable(mcpHttpGuardTest httpGuardTest.cpp httpGuard.h httpRequest.h)
	add_test(NAME mcpHttpGuardTest COMMAND mcpHttpGuardTest)
	set_tests_properties(mcpHttpGuardTest PROPERTIES LABELS "UnitTest")
	set_property(TARGET mcpHttpGuardTest PROPERTY FOLDER "Tests")

	add_executable(mcpHttpServerTest httpServerTest.cpp httpServer.cpp httpServer.h httpGuard.h httpRequest.h httpResponse.h)
	target_link_libraries(mcpHttpServerTest PRIVATE networkLib)
	add_test(NAME mcpHttpServerTest COMMAND mcpHttpServerTest)
	set_tests_properties(mcpHttpServerTest PROPERTIES LABELS "UnitTest" TIMEOUT 120)
	set_property(TARGET mcpHttpServerTest PROPERTY FOLDER "Tests")
endif()
