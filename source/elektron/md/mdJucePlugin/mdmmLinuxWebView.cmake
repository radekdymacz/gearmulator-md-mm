# The editors' web view on Linux (doc/release/LINUX.md), included from mdmmEditors.cmake when
# JUCE_WEB_BROWSER is on for a Linux build.
#
# JUCE 7 runs webkit2gtk in a child process and loads libgtk-3 and libwebkit2gtk-4.0 with dlopen
# at run time, so the plug-ins only need the headers to build: no DT_NEEDED on WebKit, and a
# machine without it gets an empty editor instead of a plug-in that fails to load.
#
# A standalone app starts its own executable as that child (--juce-gtkwebkitfork-child). A plug-in
# cannot (a fork of a host is not safe to run JUCE in), so JUCE embeds a small helper program
# (juce_linux_subprocess_helper) that it writes to a temporary file and runs; the helper loads the
# plug-in's .so and calls juce_gtkWebkitMain. JUCE wires that up for targets made with
# NEEDS_WEB_BROWSER; here the JUCE modules are compiled once into juce_plugin_modules (juce.cmake),
# so the same pieces are added to that library instead:
#   - the helper's binary data is generated and compiled in this directory (CMake only generates a
#     custom command's output for targets of the directory that declares it): mdmmLinuxWebViewHelper;
#   - juce_plugin_modules gets the headers (webkit, gtk, the generated binary data header) and
#     JUCE_USE_EXTERNAL_TEMPORARY_SUBPROCESS=1, waits for the header, and links the helper's data.

if(NOT MDMM_LINUX_WEBKIT_FOUND)
	message(FATAL_ERROR "${CMAKE_PROJECT_NAME}_JUCE_WEB_BROWSER is ON but webkit2gtk (4.0 or 4.1) and gtk+-x11-3.0 were not found "
		"by pkg-config. Install libwebkit2gtk-4.0-dev (or -4.1-dev) and libgtk-3-dev, or configure with -D${CMAKE_PROJECT_NAME}_JUCE_WEB_BROWSER=OFF.")
endif()

add_library(mdmmLinuxWebViewHelper STATIC)
juce_link_with_embedded_linux_subprocess(mdmmLinuxWebViewHelper)
set_property(TARGET mdmmLinuxWebViewHelper PROPERTY FOLDER "Elektron")
set_property(TARGET mdmmLinuxWebViewHelper PROPERTY POSITION_INDEPENDENT_CODE ON)

get_directory_property(_mdmmSubprocessTarget JUCE_EMBEDDED_LINUX_SUBPROCESS_TARGET)
if(NOT _mdmmSubprocessTarget)
	message(FATAL_ERROR "JUCE did not create its embedded Linux subprocess target (mdmmLinuxWebView.cmake)")
endif()
get_target_property(_mdmmSubprocessIncludes ${_mdmmSubprocessTarget} INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(juce_plugin_modules PUBLIC ${MDMM_LINUX_WEBKIT_INCLUDE_DIRS} ${_mdmmSubprocessIncludes})
target_compile_options(juce_plugin_modules PRIVATE ${MDMM_LINUX_WEBKIT_CFLAGS_OTHER})
target_compile_definitions(juce_plugin_modules PUBLIC JUCE_USE_EXTERNAL_TEMPORARY_SUBPROCESS=1)
add_dependencies(juce_plugin_modules mdmmLinuxWebViewHelper)
target_link_libraries(juce_plugin_modules INTERFACE mdmmLinuxWebViewHelper)

unset(_mdmmSubprocessTarget)
unset(_mdmmSubprocessIncludes)

# The two libraries JUCE opens by their development names (libwebkit2gtk-4.0.so, libgtk-3.so), as small
# shims that reach the runtime libraries a desktop has (linux/mdmmWebkitShim.c, linux/mdmmGtkShim.c). The
# plug-in targets copy them beside their binaries and run with $ORIGIN in their run path (mdmmPlugins.cmake).
add_library(mdmmLinuxWebkitShim SHARED mdJucePlugin/linux/mdmmWebkitShim.c)
set_target_properties(mdmmLinuxWebkitShim PROPERTIES OUTPUT_NAME "webkit2gtk-4.0" PREFIX "lib" SUFFIX ".so" NO_SONAME ON
	FOLDER "Elektron")
target_link_libraries(mdmmLinuxWebkitShim PRIVATE ${CMAKE_DL_LIBS})

find_library(MDMM_LINUX_GTK3_RUNTIME NAMES libgtk-3.so.0 HINTS ${MDMM_LINUX_WEBKIT_LIBRARY_DIRS})
if(NOT MDMM_LINUX_GTK3_RUNTIME)
	message(FATAL_ERROR "libgtk-3.so.0 was not found (mdmmLinuxWebView.cmake)")
endif()
add_library(mdmmLinuxGtkShim SHARED mdJucePlugin/linux/mdmmGtkShim.c)
set_target_properties(mdmmLinuxGtkShim PROPERTIES OUTPUT_NAME "gtk-3" PREFIX "lib" SUFFIX ".so" NO_SONAME ON
	FOLDER "Elektron")
# --no-as-needed: the shim uses nothing of GTK itself; the dependency is the point.
target_link_libraries(mdmmLinuxGtkShim PRIVATE "-Wl,--no-as-needed" "${MDMM_LINUX_GTK3_RUNTIME}" "-Wl,--as-needed")
