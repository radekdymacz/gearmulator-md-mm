option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN "Build Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN "Build FX plugin variants" off)

option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST3 "Build VST3 version of Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_AU "Build AU version of Juce plugins" on)
option(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_Standalone "Build Standalone version of Juce plugins" off)

set(USE_VST3 ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_VST3})
set(USE_AU ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_AU})
set(USE_Standalone ${${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN_Standalone})

set(JUCE_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(juce_formats "")
set(plugin_formats "")

if(USE_AU AND APPLE)
	set(juce_formats AU)
	set(plugin_formats AU)
	add_custom_target(PluginFormat_AU)
	set_property(TARGET PluginFormat_AU PROPERTY FOLDER CustomTargets)
endif()

if(USE_VST3)
    list(APPEND juce_formats VST3)
	list(APPEND plugin_formats VST3)
	add_custom_target(PluginFormat_VST3)
	set_property(TARGET PluginFormat_VST3 PROPERTY FOLDER CustomTargets)
endif()

if(USE_Standalone)
    list(APPEND juce_formats Standalone)
    list(APPEND plugin_formats Standalone)
	add_custom_target(PluginFormat_Standalone)
	set_property(TARGET PluginFormat_Standalone PROPERTY FOLDER CustomTargets)
endif()

add_custom_target(ServerPlugins)
set_property(TARGET ServerPlugins PROPERTY FOLDER CustomTargets)

add_library(juce_plugin_modules STATIC)

target_link_libraries(juce_plugin_modules PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
	juce::juce_cryptography
	juce::juce_opengl
)

target_compile_definitions(juce_plugin_modules PUBLIC
	JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
	JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
	JUCE_VST3_CAN_REPLACE_VST2=0
	JUCE_WIN_PER_MONITOR_DPI_AWARE=1
	JUCE_USE_OGGVORBIS=0
	JUCE_USE_MP3AUDIOFORMAT=0
	JUCE_USE_FLAC=0
	JUCE_USE_WINDOWS_MEDIA_FORMAT=0
	JUCE_MODULE_AVAILABLE_juce_core=1
	JUCE_MODULE_AVAILABLE_juce_audio_basics=1
	JUCE_MODULE_AVAILABLE_juce_audio_utils=1
	JUCE_MODULE_AVAILABLE_juce_audio_devices=1
	JUCE_MODULE_AVAILABLE_juce_audio_processors=1
	JUCE_MODULE_AVAILABLE_juce_cryptopgraphy=1
)

target_include_directories(juce_plugin_modules
    INTERFACE
        $<TARGET_PROPERTY:juce_plugin_modules,INCLUDE_DIRECTORIES>)

_juce_fixup_module_source_groups()

# Keep the historical source-tree destination for ordinary developer builds,
# while allowing release builds to own an isolated products directory.  A
# build root is safe to clean and cannot be overwritten by another build tree.
set(GEARMULATOR_JUCE_PRODUCTS_ROOT "${CMAKE_SOURCE_DIR}/bin/plugins" CACHE PATH
	"Root directory for completed JUCE plug-in and standalone products")

# juce::juce_audio_plugin_client is the lib that every plugin links. However, this pulls in lots of juce modules that are 
# all INTERFACE targets, causing all the sources to end up in every plugin we build. We remove this dependency as we already
# link them via our rebuilt static lib that we created above. This causes all juce modules to only show up in our static
# lib instead of every plugin

macro(removeJuceDependencies targetName)
	# Get the current link libraries before modifying

	get_target_property(pluginLibs ${targetName} LINK_LIBRARIES)
	list(REMOVE_ITEM pluginLibs juce::juce_dsp juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_basics juce::juce_audio_plugin_client $<LINK_ONLY:juce::juce_audio_plugin_client>)
	set_target_properties(${targetName} PROPERTIES LINK_LIBRARIES "${pluginLibs}")

	get_target_property(pluginLibs ${targetName} INTERFACE_LINK_LIBRARIES)
	list(REMOVE_ITEM pluginLibs juce::juce_dsp juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_basics juce::juce_audio_plugin_client $<LINK_ONLY:juce::juce_audio_plugin_client>)
	set_target_properties(${targetName} PROPERTIES INTERFACE_LINK_LIBRARIES "${pluginLibs}")
endmacro()

macro(createJucePlugin targetName productName isSynth plugin4CC binaryDataProject synthLibProject)
	string(REPLACE " " "" productNameIdentifier "${productName}")
	# A product can own its bundle identifier (GEARMULATOR_PLUGIN_BUNDLE_ID_<target>); the others keep upstream's.
	if(DEFINED GEARMULATOR_PLUGIN_BUNDLE_ID_${targetName})
		set(pluginBundleId "${GEARMULATOR_PLUGIN_BUNDLE_ID_${targetName}}")
	else()
		set(pluginBundleId "local.gearmulator.preview.${productNameIdentifier}")
	endif()
	juce_add_plugin(${targetName}
		# VERSION ...                                     # Set this if the plugin version is different to the project version
		# ICON_BIG ...                                    # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
		# ICON_SMALL ...
		COMPANY_NAME "Gearmulator Preview"                 # Specify the name of the plugin's author
		COMPANY_WEBSITE "https://dsp56300.wordpress.com"
		IS_SYNTH ${isSynth}                               # Is this a synth or an effect?
		NEEDS_MIDI_INPUT TRUE                             # Does the plugin need midi input?
		NEEDS_MIDI_OUTPUT TRUE                            # Does the plugin need midi output?
		IS_MIDI_EFFECT FALSE                              # Is this plugin a MIDI effect?
		EDITOR_WANTS_KEYBOARD_FOCUS TRUE                  # Does the editor need keyboard focus?
		COPY_PLUGIN_AFTER_BUILD FALSE                     # Should the plugin be installed to a default location after building?
		MICROPHONE_PERMISSION_ENABLED TRUE               # Standalone exposes the physical stereo input
		MICROPHONE_PERMISSION_TEXT "Gearmulator uses audio input for processing external instruments."
		PLUGIN_MANUFACTURER_CODE GmPv                     # A four-character manufacturer id with at least one upper-case character
		PLUGIN_CODE ${plugin4CC}                          # A unique four-character plugin id with exactly one upper-case character
		PRODUCTS_FOLDER "${GEARMULATOR_JUCE_PRODUCTS_ROOT}/$<CONFIG>"
		                                                  # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
		FORMATS ${juce_formats}                           # The formats to build. Other valid formats are: AAX Unity VST AU AUv3 LV2
		PRODUCT_NAME ${productName}                       # The name of the final executable, which can differ from the target name
		VST3_AUTO_MANIFEST TRUE                           # While generating a moduleinfo.json is nice, Juce does not properly package using cpack on Win/Linux
		                                                  # and completely fails on Linux if we change the suffix to .vst3, so we skip that completely for now
		BUNDLE_ID "${pluginBundleId}"
		LV2URI "http://theusualsuspects.lv2/${productNameIdentifier}"
		${GEARMULATOR_PLUGIN_EXTRA_ARGS_${targetName}}   # Optional per-target arguments, last so they win (doc/modern-ux/UPSTREAM.md)
	)

	# JUCE otherwise puts the internal SharedCode archive beside the final plug-in
	# bundles in the source tree. Independent build trees (for example arm64
	# standalone and universal test builds) then overwrite the same archive and
	# can link the wrong architecture. Keep this intermediate build-local while
	# leaving the user-facing wrapper products in PRODUCTS_FOLDER.
	set_property(TARGET ${targetName} PROPERTY
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shared-code/$<CONFIG>")

	target_sources(${targetName} PRIVATE ${SOURCES} serverPlugin.cpp)

	source_group("source" FILES ${SOURCES})

	removeJuceDependencies(${targetName})

	target_compile_definitions(${targetName} 
	PUBLIC
		PluginName="${productName}"
		PluginVersionMajor=${CMAKE_PROJECT_VERSION_MAJOR}
		PluginVersionMinor=${CMAKE_PROJECT_VERSION_MINOR}
		PluginVersionPatch=${CMAKE_PROJECT_VERSION_PATCH}
		Plugin4CC="${plugin4CC}"
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
	)

	target_link_libraries(${targetName}
	PRIVATE
		${binaryDataProject}
		${synthLibProject}
		jucePluginEditorLib
		juce_plugin_modules
	)

	if(${isSynth})
		createMacSetupScript(${productName})
	endif()

	if(TARGET ${targetName}_rc_lib)
		set_property(TARGET ${targetName}_rc_lib PROPERTY FOLDER ${targetName})
	endif()

	if(TARGET ${binaryDataProject} AND ${isSynth})
		set_property(TARGET ${binaryDataProject} PROPERTY FOLDER ${targetName})
	endif()

	if(UNIX AND NOT APPLE)
		target_link_libraries(${targetName} PUBLIC -static-libgcc -static-libstdc++)
	endif()

	if(USE_VST3)
		if(APPLE)
			install(TARGETS ${targetName}_VST3 DESTINATION . COMPONENT ${productName}-VST3)
			installMacSetupScript(. ${productName}-VST3)
		else()
			get_target_property(vst3OutputFolder ${targetName}_VST3 ARCHIVE_OUTPUT_DIRECTORY)
			if(UNIX)
				set(dest lib/vst3)
				set(pattern "*.so")
			else()
				set(dest .)
				set(pattern "*.vst3")
			endif()
			install(DIRECTORY ${vst3OutputFolder}/${productName}.vst3 DESTINATION ${dest} COMPONENT ${productName}-VST3 FILES_MATCHING PATTERN ${pattern} PATTERN "*.json")
		endif()
		add_dependencies(PluginFormat_VST3 ${targetName}_VST3)
	endif()

	if(USE_AU AND APPLE)
		install(TARGETS ${targetName}_AU DESTINATION . COMPONENT ${productName}-AU)
		installMacSetupScript(. ${productName}-AU)
	endif()

	# processorPropertiesInit.h reads the LV2 URI (LV2 itself is not built)
	target_compile_definitions(${targetName} PUBLIC JucePlugin_Lv2Uri="$<TARGET_PROPERTY:${targetName},JUCE_LV2URI>")

	if(USE_AU AND APPLE AND ${isSynth})
		add_test(NAME ${targetName}_AU_Validate COMMAND ${CMAKE_COMMAND} 
			-DIDCOMPANY=GmPv
			-DIDPLUGIN=${plugin4CC}
			-DBINDIR=${CMAKE_BINARY_DIR}
			-DCOMPONENT_NAME=${productName}
			-DCPACK_FILE=${CPACK_PACKAGE_NAME}-${productName}-AU-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}.zip
			-P ${JUCE_CMAKE_DIR}/runAuValidation.cmake)
		set_tests_properties(${targetName}_AU_Validate PROPERTIES LABELS "PluginTest")
	endif()

	if(USE_Standalone)
		add_dependencies(PluginFormat_Standalone ${targetName}_Standalone)
	endif()

	if(USE_VST3)
		addPluginTest(${targetName}_VST3)
	endif()
	if(USE_AU AND APPLE AND ${isSynth})	# Apparently FX AU plugins are not supported by juce audio plugin host
		addPluginTest(${targetName}_AU)
	endif()

	set_target_properties(${targetName} PROPERTIES TUS_PRODUCT_NAME "${productName}")
	set_target_properties(${targetName} PROPERTIES TUS_PLUGIN_FORMATS "${juce_formats}")
	set_target_properties(${targetName} PROPERTIES TUS_PLUGIN_IS_SYNTH ${isSynth})
	set_target_properties(${targetName} PROPERTIES TUS_PLUGIN_4CC ${plugin4CC})

	if(${isSynth})
		tus_exportTarget(${targetName})
	endif()

	# --------- Server Plugin ---------

	set(serverTarget ${productNameIdentifier}ServerPlugin)

	add_library(${serverTarget} SHARED)

	target_compile_definitions(${serverTarget} PUBLIC 
		PluginName="${productName}"
		PluginVersionMajor=${CMAKE_PROJECT_VERSION_MAJOR}
		PluginVersionMinor=${CMAKE_PROJECT_VERSION_MINOR}
		PluginVersionPatch=${CMAKE_PROJECT_VERSION_PATCH}
		Plugin4CC="${plugin4CC}"
	)
	target_sources(${serverTarget} PRIVATE serverPlugin.cpp)
	target_link_libraries(${serverTarget} ${synthLibProject} bridgeClient)
	set_property(TARGET ${serverTarget} PROPERTY FOLDER ${targetName})

	# build plugins to the "plugins" dir of the server binary output dir
	get_target_property(serverOutputDir bridgeServer BINARY_DIR)

	if(NOT serverOutputDir)
		get_target_property(serverOutputDir bridgeServer RUNTIME_OUTPUT_DIRECTORY)
	endif()
	
	if(serverOutputDir)
		set_property(TARGET ${serverTarget} PROPERTY RUNTIME_OUTPUT_DIRECTORY "${serverOutputDir}/plugins")
		set_property(TARGET ${serverTarget} PROPERTY LIBRARY_OUTPUT_DIRECTORY "${serverOutputDir}/plugins")

		get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
		
		if(isMultiConfig)
			set_property(TARGET ${serverTarget} PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG "${serverOutputDir}/Debug/plugins")
			set_property(TARGET ${serverTarget} PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE "${serverOutputDir}/Release/plugins")
			set_property(TARGET ${serverTarget} PROPERTY LIBRARY_OUTPUT_DIRECTORY_DEBUG "${serverOutputDir}/Debug/plugins")
			set_property(TARGET ${serverTarget} PROPERTY LIBRARY_OUTPUT_DIRECTORY_RELEASE "${serverOutputDir}/Release/plugins")
		endif()
	endif()

	install(TARGETS ${serverTarget} 
		RUNTIME DESTINATION plugins/ COMPONENT DSPBridgeServer 
		LIBRARY DESTINATION plugins/ COMPONENT DSPBridgeServer)

	add_dependencies(ServerPlugins ${serverTarget})
endmacro()

macro(createJucePluginWithFX targetName productName plugin4CCSynth plugin4CCFX binaryDataProject synthLibProject)
	createJucePlugin(${targetName} "${productName}" TRUE "${plugin4CCSynth}" ${binaryDataProject} ${synthLibProject})

	if(${CMAKE_PROJECT_NAME}_BUILD_FX_PLUGIN)
		createJucePlugin(${targetName}_FX "${productName}FX" FALSE "${plugin4CCFX}" ${binaryDataProject} ${synthLibProject})
	endif()
endmacro()
