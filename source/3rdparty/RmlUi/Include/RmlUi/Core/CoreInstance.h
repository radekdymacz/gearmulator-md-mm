#pragma once

/*
Added by DSP56300:

As we are audio plugins, and these plugins may be different instances of the same plugin, with a different UI skin, but
run in the same process, we are in desperate need of separating all global variables and static data. This includes
font manager, rml plugins, file interface, etc.

*/

#include "Core/ControlledLifetimeResource.h"

extern "C"
{
	struct FT_LibraryRec_;
	typedef FT_LibraryRec_* FT_Library;
}

typedef struct lua_State lua_State;

namespace Rml
{
	struct PropertyParserNumberData;
	struct PropertyParserDecoratorData;
	struct PropertyParserColourData;
	struct PropertyParserAnimationData;
	class Factory;
	struct ObserverPtrData;
	struct EventSpecificationData;
	struct StyleSheetParserData;
	class StyleSheetSpecification;
	class StyleSheetFactory;
	class FontProvider;
	class TemplateCache;
	struct XmlParserData;
	struct PluginVectors;
	struct ElementInstancerPools;
	struct ComputedPropertyData;
	struct LayoutPoolsData;
	struct FactoryData;
	struct ElementMetaPool;
	class RenderInterface;
	class TextInputHandler;
	class SystemInterface;
	class FileInterface;
	class FontEngineInterface;

	class CoreInstance final : NonCopyMoveable
	{
	public:
		CoreInstance();
		~CoreInstance();

		RenderInterface* render_interface = nullptr;
		SystemInterface* system_interface = nullptr;
		FileInterface* file_interface = nullptr;
		FontEngineInterface* font_interface = nullptr;
		TextInputHandler* text_input_handler = nullptr;
		StyleSheetSpecification* styleSheetSpecification = nullptr;
		TemplateCache* template_cache = nullptr;
		FontProvider* font_provider = nullptr;
		StyleSheetFactory* style_sheet_factory = nullptr;
		StyleSheetParserData* style_sheet_property_parsers = nullptr;
		ObserverPtrData* observer_ptr_data = nullptr;

		struct CoreData {
			// Default interfaces should be created and destroyed on Initialise and Shutdown, respectively.
			UniquePtr<SystemInterface> default_system_interface;
			UniquePtr<FileInterface> default_file_interface;
			UniquePtr<FontEngineInterface> default_font_interface;
			UniquePtr<TextInputHandler> default_text_input_handler;

			SmallUnorderedMap<RenderInterface*, UniquePtr<RenderManager>> render_managers;
			UnorderedMap<String, ContextPtr> contexts;
		};

		ControlledLifetimeResource<CoreData> core_data;

		ControlledLifetimeResource<ElementInstancerPools> element_instancer_pools;
		ControlledLifetimeResource<ElementMetaPool> element_meta_pool;

		ControlledLifetimeResource<LayoutPoolsData> layout_pools_data;

		ControlledLifetimeResource<ComputedPropertyData> computed_property_data;

		ControlledLifetimeResource<PluginVectors> plugin_vectors;

		ControlledLifetimeResource<XmlParserData> xml_parser_data;

		ControlledLifetimeResource<EventSpecificationData> event_specification_data;

		ControlledLifetimeResource<PropertyParserAnimationData> parser_animation_data;

		ControlledLifetimeResource<PropertyParserColourData> parser_color_data;

		ControlledLifetimeResource<PropertyParserDecoratorData> parser_decorator_data;

		ControlledLifetimeResource<PropertyParserNumberData> parser_number_data;

		Factory* factory;

		FT_Library ft_library = nullptr;

		lua_State* lua_state = nullptr;

		bool initialised = false;
	};
}
