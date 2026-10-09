# The editors' CMake check (doc/modern-ux/UPSTREAM.md), included from the root CMakeLists.txt right after its
# cmake_minimum_required, before anything else runs.
#
# JUCE (the plug-ins and the standalone apps) needs CMake 3.22. Say so here, plainly, instead of failing inside
# JUCE's own CMakeLists. The root's 3.15 stays as it is (it sets the policies upstream's code was written against),
# and a build without the plug-ins (-Dgearmulator_BUILD_JUCEPLUGIN=OFF, the unit tests) does not need JUCE at all.
if(CMAKE_VERSION VERSION_LESS 3.22 AND (NOT DEFINED gearmulator_BUILD_JUCEPLUGIN OR gearmulator_BUILD_JUCEPLUGIN))
	message(FATAL_ERROR "CMake ${CMAKE_VERSION} is too old: JUCE, which builds the plug-ins, needs CMake 3.22 or "
		"newer. Upgrade CMake, or configure with -Dgearmulator_BUILD_JUCEPLUGIN=OFF to build the libraries and "
		"tests only.")
endif()
