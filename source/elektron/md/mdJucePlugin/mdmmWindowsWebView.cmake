# The editors' web view on Windows (doc/release/WINDOWS.md, B-002), included from mdmmEditors.cmake when
# JUCE_WEB_BROWSER is on for a Windows build.
#
# JUCE 7.0.10's WebBrowserComponent is the Internet Explorer control unless its WebView2 backend is on, and that
# backend loads WebView2Loader.dll by name (beside the host's executable, not beside a VST3) and reports no iframe
# navigations. The editors drive WebView2 themselves (mdWebView2Page.cpp) with the SDK's static loader, so nothing
# ships beside the binaries: the Microsoft.Web.WebView2 NuGet package (headers, WebView2LoaderStatic.lib) is
# fetched here at configure time, checked against its SHA-256. An offline build points
# MDMM_WEBVIEW2_SDK_DIR at an unpacked package (the folder with build/native/include).
#
# Defines the target mdmmWebView2 (headers, the static loader, MDMM_WEBVIEW2=1); mdmmPlugins.cmake links it into
# mdJucePlugin and mmJucePlugin and compiles mdWebView2Page.cpp.

set(MDMM_WEBVIEW2_VERSION "1.0.3856.49")
set(MDMM_WEBVIEW2_SHA256 "bc0f76eb911b569838dc4aa8f8d325269b966bedb592863d26211aef3a099f1a")
set(MDMM_WEBVIEW2_SDK_DIR "" CACHE PATH "An unpacked Microsoft.Web.WebView2 NuGet package (empty: fetched at configure time)")

if(MDMM_WEBVIEW2_SDK_DIR)
	set(_mdmmWebView2Dir "${MDMM_WEBVIEW2_SDK_DIR}")
else()
	set(_mdmmWebView2Dir "${CMAKE_BINARY_DIR}/_deps/webview2-${MDMM_WEBVIEW2_VERSION}")
	if(NOT EXISTS "${_mdmmWebView2Dir}/build/native/include/WebView2.h")
		set(_mdmmWebView2Package "${CMAKE_BINARY_DIR}/_deps/microsoft.web.webview2.${MDMM_WEBVIEW2_VERSION}.nupkg")
		message(STATUS "WebView2 SDK ${MDMM_WEBVIEW2_VERSION}: downloading")
		file(DOWNLOAD
			"https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${MDMM_WEBVIEW2_VERSION}/microsoft.web.webview2.${MDMM_WEBVIEW2_VERSION}.nupkg"
			"${_mdmmWebView2Package}"
			EXPECTED_HASH SHA256=${MDMM_WEBVIEW2_SHA256}
			TLS_VERIFY ON
			STATUS _mdmmWebView2Status)
		list(GET _mdmmWebView2Status 0 _mdmmWebView2Code)
		if(NOT _mdmmWebView2Code EQUAL 0)
			message(FATAL_ERROR "The WebView2 SDK could not be downloaded (${_mdmmWebView2Status}). Set MDMM_WEBVIEW2_SDK_DIR to an "
				"unpacked Microsoft.Web.WebView2 ${MDMM_WEBVIEW2_VERSION} package, or configure with -D${CMAKE_PROJECT_NAME}_JUCE_WEB_BROWSER=OFF.")
		endif()
		file(MAKE_DIRECTORY "${_mdmmWebView2Dir}")
		execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_mdmmWebView2Package}"
			WORKING_DIRECTORY "${_mdmmWebView2Dir}" RESULT_VARIABLE _mdmmWebView2Unpack)
		if(NOT _mdmmWebView2Unpack EQUAL 0)
			message(FATAL_ERROR "The WebView2 SDK package could not be unpacked: ${_mdmmWebView2Package}")
		endif()
		unset(_mdmmWebView2Package)
		unset(_mdmmWebView2Status)
		unset(_mdmmWebView2Code)
		unset(_mdmmWebView2Unpack)
	endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
		set(_mdmmWebView2Arch arm64)
	else()
		set(_mdmmWebView2Arch x64)
	endif()
else()
	set(_mdmmWebView2Arch x86)
endif()
set(_mdmmWebView2Lib "${_mdmmWebView2Dir}/build/native/${_mdmmWebView2Arch}/WebView2LoaderStatic.lib")
if(NOT EXISTS "${_mdmmWebView2Dir}/build/native/include/WebView2.h" OR NOT EXISTS "${_mdmmWebView2Lib}")
	message(FATAL_ERROR "No WebView2 SDK in ${_mdmmWebView2Dir} (build/native/include/WebView2.h, ${_mdmmWebView2Lib})")
endif()

add_library(mdmmWebView2 INTERFACE)
target_include_directories(mdmmWebView2 INTERFACE "${_mdmmWebView2Dir}/build/native/include")
# The static loader needs version.lib (the SDK's Common.targets).
target_link_libraries(mdmmWebView2 INTERFACE "${_mdmmWebView2Lib}" version)
target_compile_definitions(mdmmWebView2 INTERFACE MDMM_WEBVIEW2=1)
# The loader's licence (BSD-style) goes with the binaries: scripts/windows/WebView2-LICENSE.txt, packaged by
# scripts/windows/package_mdmm_editors.sh.

unset(_mdmmWebView2Arch)
unset(_mdmmWebView2Lib)
unset(_mdmmWebView2Dir)
