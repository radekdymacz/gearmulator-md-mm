// The MD and MM standalones use the shared native-window application
// (jucePluginEditorLib/standaloneApp.h): native title bar, menu bar menus.
#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include "jucePluginEditorLib/standaloneApp.h"

JUCE_CREATE_APPLICATION_DEFINE(jucePluginEditorLib::StandaloneApp)
#endif
