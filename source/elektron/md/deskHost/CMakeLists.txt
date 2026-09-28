cmake_minimum_required(VERSION 3.15)

project(deskHost)

# The plug-in's own page commands (P6), the same for both editors: the engine map, the ROM
# folder, MIDI learn, the page's MIDI, the audio devices, the menu. One table as data; the
# plug-in's session and window act on its rows. Pure (no JUCE), so the contract tests can
# generate the schema from it.
add_library(deskHost STATIC)
target_sources(deskHost PRIVATE deskHost.cpp deskHost.h)
target_link_libraries(deskHost PUBLIC deskCore)
target_include_directories(deskHost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(deskHost PUBLIC cxx_std_17)
set_property(TARGET deskHost PROPERTY FOLDER "Elektron")
