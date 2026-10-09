# Visual Studio leaves the test programs of an excluded directory out of the solution (see jucePluginLib in
# ../CMakeLists.txt): exclude bridgeLib only when there are no tests to build.
if(BUILD_TESTING)
	add_subdirectory(bridgeLib)
else()
	add_subdirectory(bridgeLib EXCLUDE_FROM_ALL)
endif()
add_subdirectory(client)
add_subdirectory(server)
