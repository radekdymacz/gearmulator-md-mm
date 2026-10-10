# A minimal VST3 host that opens a plug-in's editor in a window for a while: the CI start test of the editors'
# VST3s on Windows (mdmm-editors-windows.yml, scripts/windows/smoke_mdmm.ps1) and macOS (mdmm-editors-macos.yml,
# scripts/macos/smoke_mdmm.sh); doc/modern-ux/FOUNDATION.md, "CI start tests". Also the user journeys in the VST3s
# (scripts/mdmm-journeys.sh --host vst3, with --background: hostMac.mm). Its own project on the repository's JUCE
# (source/JUCE), apart from the product build.
#
#   cmake -S scripts/vst3EditorHost -B <build> && cmake --build <build> --config Release
#   mdmmVst3EditorHost.exe "<path to .vst3 bundle>" <seconds>
#   mdmmVst3EditorHost.app/Contents/MacOS/mdmmVst3EditorHost "<path to .vst3 bundle>" <seconds> [--background]
cmake_minimum_required(VERSION 3.22)
project(mdmmVst3EditorHost VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../source/JUCE ${CMAKE_BINARY_DIR}/JUCE)

juce_add_gui_app(mdmmVst3EditorHost PRODUCT_NAME "mdmmVst3EditorHost")
target_sources(mdmmVst3EditorHost PRIVATE main.cpp $<$<PLATFORM_ID:Darwin>:hostMac.mm>)
target_compile_definitions(mdmmVst3EditorHost PRIVATE
	JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(mdmmVst3EditorHost PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics)
