cmake_minimum_required(VERSION 3.10)

project(bridgeServer)

add_executable(bridgeServer)

set(SOURCES
	bridgeServer.cpp
	clientConnection.cpp clientConnection.h
	config.cpp config.h
	server.cpp server.h
	import.cpp import.h
	romPool.cpp romPool.h
	udpServer.cpp udpServer.h
)

target_sources(bridgeServer PRIVATE ${SOURCES})

source_group("source" FILES ${SOURCES})

target_link_libraries(bridgeServer PUBLIC bridgeLib)

set_property(TARGET bridgeServer PROPERTY FOLDER "Bridge")
set_property(TARGET bridgeServer PROPERTY OUTPUT_NAME "dsp56300EmuServer")

set_target_properties(bridgeServer PROPERTIES TUS_PRODUCT_NAME "DSPBridge")
tus_exportTarget(bridgeServer)

install(TARGETS bridgeServer DESTINATION . COMPONENT DSPBridgeServer)

if(BUILD_TESTING)
	# The pool's sources directly: bridgeServer is a program, not a library
	add_executable(bridgeServerRomPoolTest romPoolTest.cpp romPool.cpp romPool.h config.cpp config.h)
	target_link_libraries(bridgeServerRomPoolTest PRIVATE bridgeLib)
	add_test(NAME bridgeServerRomPoolTest COMMAND bridgeServerRomPoolTest)
	set_tests_properties(bridgeServerRomPoolTest PROPERTIES LABELS "UnitTest" TIMEOUT 60)
	set_property(TARGET bridgeServerRomPoolTest PROPERTY FOLDER "Tests")
endif()
