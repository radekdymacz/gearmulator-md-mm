/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementInstancer.h"
#include "../../Include/RmlUi/Core/Factory.h"
#include "../../Include/RmlUi/Core/FileInterface.h"
#include "../../Include/RmlUi/Core/FontEngineInterface.h"
#include "../../Include/RmlUi/Core/Plugin.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "../../Include/RmlUi/Core/RenderManager.h"
#include "../../Include/RmlUi/Core/StyleSheetSpecification.h"
#include "../../Include/RmlUi/Core/SystemInterface.h"
#include "../../Include/RmlUi/Core/TextInputHandler.h"
#include "../../Include/RmlUi/Core/Types.h"
#include "ComputeProperty.h"
#include "ControlledLifetimeResource.h"
#include "ElementMeta.h"
#include "EventSpecification.h"
#include "FileInterfaceDefault.h"
#include "Layout/LayoutPools.h"
#include "PluginRegistry.h"
#include "RenderManagerAccess.h"
#include "StyleSheetFactory.h"
#include "StyleSheetParser.h"
#include "TemplateCache.h"

#ifdef RMLUI_FONT_ENGINE_FREETYPE
	#include "FontEngineDefault/FontEngineInterfaceDefault.h"
#endif

#ifdef RMLUI_LOTTIE_PLUGIN
	#include "../Lottie/LottiePlugin.h"
#endif

#ifdef RMLUI_SVG_PLUGIN
	#include "../SVG/SVGPlugin.h"
#endif

#include <algorithm>

#include "RmlUi/Core/CoreInstance.h"

namespace Rml {

static void InitializeMemoryPools(CoreInstance& instance)
{
	Detail::InitializeElementInstancerPools(instance);
	ElementMetaPool::Initialize(instance);
	LayoutPools::Initialize(instance);
}
static void ReleaseMemoryPools(CoreInstance& instance)
{
	LayoutPools::Shutdown(instance);
	ElementMetaPool::Shutdown(instance);
	ElementInstancerElement::CheckPoolsOnShutdown(instance);
	Detail::ShutdownElementInstancerPools(instance);
}

#ifndef RMLUI_VERSION
	#define RMLUI_VERSION "custom"
#endif

bool Initialise(CoreInstance& in_core_instance)
{
	// [DSP56300] we use this to ease merging if RmlUi changes in the future.
	auto& initialised = in_core_instance.initialised;
	auto& system_interface = in_core_instance.system_interface;
	auto& render_interface = in_core_instance.render_interface;
	auto& file_interface = in_core_instance.file_interface;
	auto& font_interface = in_core_instance.font_interface;
	auto& text_input_handler = in_core_instance.text_input_handler;
	auto& core_data = in_core_instance.core_data;

	RMLUI_ASSERTMSG(!initialised, "Rml::Initialise() called, but RmlUi is already initialised!");

	InitializeMemoryPools(in_core_instance);
	InitializeComputeProperty(in_core_instance);

	core_data.Initialize();

	// Install default interfaces as appropriate.
	if (!system_interface)
	{
		core_data->default_system_interface = MakeUnique<SystemInterface>(in_core_instance);
		system_interface = core_data->default_system_interface.get();
	}

	if (!file_interface)
	{
#ifndef RMLUI_NO_FILE_INTERFACE_DEFAULT
		core_data->default_file_interface = MakeUnique<FileInterfaceDefault>(in_core_instance);
		file_interface = core_data->default_file_interface.get();
#else
		Log::Message(in_core_instance, Log::LT_ERROR, "No file interface set!");
		return false;
#endif
	}

	if (!font_interface)
	{
#ifdef RMLUI_FONT_ENGINE_FREETYPE
		core_data->default_font_interface = MakeUnique<FontEngineInterfaceDefault>(in_core_instance);
		font_interface = core_data->default_font_interface.get();
#else
		Log::Message(in_core_instance, Log::LT_ERROR, "No font engine interface set!");
		return false;
#endif
	}

	if (!text_input_handler)
	{
		core_data->default_text_input_handler = MakeUnique<TextInputHandler>();
		text_input_handler = core_data->default_text_input_handler.get();
	}

	EventSpecificationInterface::Initialize(in_core_instance);

	Detail::InitializeObserverPtrPool(in_core_instance);

	if (render_interface)
		core_data->render_managers[render_interface] = MakeUnique<RenderManager>(in_core_instance, render_interface);

	font_interface->Initialize();

	StyleSheetSpecification::Initialise(in_core_instance);
	StyleSheetParser::Initialise(in_core_instance);
	StyleSheetFactory::Initialise(in_core_instance);

	TemplateCache::Initialise(in_core_instance);

	in_core_instance.factory->Initialise();

	// Initialise plugins integrated with Core.
#ifdef RMLUI_LOTTIE_PLUGIN
	Lottie::Initialise();
#endif
#ifdef RMLUI_SVG_PLUGIN
	SVG::Initialise();
#endif

	// Notify all plugins we're starting up.
	PluginRegistry::NotifyInitialise(in_core_instance);

	initialised = true;

	return true;
}

void Shutdown(CoreInstance& in_core_instance)
{
	// [DSP56300] we use this to ease merging if RmlUi changes in the future.
	auto& initialised = in_core_instance.initialised;
	auto& core_data = in_core_instance.core_data;
	auto& font_interface = in_core_instance.font_interface;
	auto& render_interface = in_core_instance.render_interface;
	auto& text_input_handler = in_core_instance.text_input_handler;
	auto& file_interface = in_core_instance.file_interface;
	auto& system_interface = in_core_instance.system_interface;

	RMLUI_ASSERTMSG(initialised, "Rml::Shutdown() called, but RmlUi is not initialised!");

	// Clear out all contexts, which should also clean up all attached elements.
	core_data->contexts.clear();

	// Notify all plugins we're being shutdown.
	PluginRegistry::NotifyShutdown(in_core_instance);

	in_core_instance.factory->Shutdown();
	TemplateCache::Shutdown(in_core_instance);
	StyleSheetFactory::Shutdown(in_core_instance);
	StyleSheetParser::Shutdown(in_core_instance);
	StyleSheetSpecification::Shutdown(in_core_instance);

	font_interface->Shutdown();

	core_data->render_managers.clear();

	Detail::ShutdownObserverPtrPool(in_core_instance);

	initialised = false;

	text_input_handler = nullptr;
	font_interface = nullptr;
	render_interface = nullptr;
	file_interface = nullptr;
	system_interface = nullptr;

	core_data.Shutdown();

	EventSpecificationInterface::Shutdown(in_core_instance);

	ShutdownComputeProperty(in_core_instance);
	ReleaseMemoryPools(in_core_instance);
}

String GetVersion()
{
	return RMLUI_VERSION;
}

void SetSystemInterface(CoreInstance& instance, SystemInterface* _system_interface)
{
	instance.system_interface = _system_interface;
}

SystemInterface* GetSystemInterface(CoreInstance& instance)
{
	return instance.system_interface;
}

void SetRenderInterface(CoreInstance& instance, RenderInterface* _render_interface)
{
	instance.render_interface = _render_interface;
}

RenderInterface* GetRenderInterface(CoreInstance& instance)
{
	return instance.render_interface;
}

void SetFileInterface(CoreInstance& instance, FileInterface* _file_interface)
{
	instance.file_interface = _file_interface;
}

FileInterface* GetFileInterface(CoreInstance& instance)
{
	return instance.file_interface;
}

void SetFontEngineInterface(CoreInstance& instance, FontEngineInterface* _font_interface)
{
	instance.font_interface = _font_interface;
}

FontEngineInterface* GetFontEngineInterface(CoreInstance& instance)
{
	return instance.font_interface;
}

void SetTextInputHandler(CoreInstance& instance, TextInputHandler* _text_input_handler)
{
	instance.text_input_handler = _text_input_handler;
}

TextInputHandler* GetTextInputHandler(CoreInstance& core_instance)
{
	return core_instance.text_input_handler;
}

Context* CreateContext(CoreInstance& instance, const String& name, const Vector2i dimensions, RenderInterface* render_interface_for_context,
	TextInputHandler* text_input_handler_for_context)
{
	if (!instance.initialised)
		return nullptr;

	if (!render_interface_for_context)
		render_interface_for_context = instance.render_interface;

	if (!text_input_handler_for_context)
		text_input_handler_for_context = instance.text_input_handler;

	if (!render_interface_for_context)
	{
		Log::Message(instance, Log::LT_WARNING, "Failed to create context '%s', no render interface specified and no default render interface exists.",
			name.c_str());
		return nullptr;
	}

	if (GetContext(instance, name))
	{
		Log::Message(instance, Log::LT_WARNING, "Failed to create context '%s', context already exists.", name.c_str());
		return nullptr;
	}

	// Each unique render interface gets its own render manager.
	auto& render_manager = instance.core_data->render_managers[render_interface_for_context];
	if (!render_manager)
		render_manager = MakeUnique<RenderManager>(instance, render_interface_for_context);

	ContextPtr new_context = instance.factory->InstanceContext(name, render_manager.get(), text_input_handler_for_context);
	if (!new_context)
	{
		Log::Message(instance, Log::LT_WARNING, "Failed to instance context '%s', instancer returned nullptr.", name.c_str());
		return nullptr;
	}

	new_context->SetDimensions(dimensions);

	Context* new_context_raw = new_context.get();
	instance.core_data->contexts[name] = std::move(new_context);

	PluginRegistry::NotifyContextCreate(new_context_raw);

	return new_context_raw;
}

bool RemoveContext(CoreInstance& core_instance, const String& name)
{
	return core_instance.core_data->contexts.erase(name) != 0;
}

Context* GetContext(CoreInstance& core_instance, const String& name)
{
	auto it = core_instance.core_data->contexts.find(name);
	if (it == core_instance.core_data->contexts.end())
		return nullptr;

	return it->second.get();
}

Context* GetContext(CoreInstance& core_instance, int index)
{
	if (index < 0 || index >= GetNumContexts(core_instance))
		return nullptr;

	auto it = core_instance.core_data->contexts.begin();
	std::advance(it, index);

	if (it == core_instance.core_data->contexts.end())
		return nullptr;

	return it->second.get();
}

int GetNumContexts(CoreInstance& core_instance)
{
	return (int)core_instance.core_data->contexts.size();
}

bool LoadFontFace(CoreInstance& core_instance, const String& file_path, bool fallback_face, Style::FontWeight weight, int face_index)
{
	return core_instance.font_interface->LoadFontFace(file_path, face_index, fallback_face, weight);
}

bool LoadFontFace(CoreInstance& core_instance, Span<const byte> data, const String& family, Style::FontStyle style, Style::FontWeight weight, bool fallback_face, int face_index)
{
	return core_instance.font_interface->LoadFontFace(data, face_index, family, style, weight, fallback_face);
}

void RegisterPlugin(CoreInstance& core_instance, Plugin* plugin)
{
	if (core_instance.initialised)
		plugin->OnInitialise();

	PluginRegistry::RegisterPlugin(core_instance, plugin);
}

void UnregisterPlugin(CoreInstance& core_instance, Plugin* plugin)
{
	PluginRegistry::UnregisterPlugin(core_instance, plugin);

	if (core_instance.initialised)
		plugin->OnShutdown();
}

EventId RegisterEventType(CoreInstance& in_core_instance, const String& type, bool interruptible, bool bubbles, DefaultActionPhase default_action_phase)
{
	return EventSpecificationInterface::InsertOrReplaceCustom(in_core_instance, type, interruptible, bubbles, default_action_phase);
}

StringList GetTextureSourceList(CoreInstance& core_instance)
{
	StringList result;
	if (!core_instance.core_data)
		return result;
	for (const auto& render_manager : core_instance.core_data->render_managers)
	{
		RenderManagerAccess::GetTextureSourceList(render_manager.second.get(), result);
	}
	return result;
}

void ReleaseTextures(CoreInstance& core_instance, RenderInterface* match_render_interface)
{
	if (!core_instance.core_data)
		return;
	for (auto& render_manager : core_instance.core_data->render_managers)
	{
		if (!match_render_interface || render_manager.first == match_render_interface)
			RenderManagerAccess::ReleaseAllTextures(render_manager.second.get());
	}
}

bool ReleaseTexture(CoreInstance& core_instance, const String& source, RenderInterface* match_render_interface)
{
	bool result = false;
	if (!core_instance.core_data)
		return result;
	for (auto& render_manager : core_instance.core_data->render_managers)
	{
		if (!match_render_interface || render_manager.first == match_render_interface)
		{
			if (RenderManagerAccess::ReleaseTexture(render_manager.second.get(), source))
				result = true;
		}
	}
	return result;
}

void ReleaseCompiledGeometry(CoreInstance& core_instance, RenderInterface* match_render_interface)
{
	if (!core_instance.core_data)
		return;
	for (auto& render_manager : core_instance.core_data->render_managers)
	{
		if (!match_render_interface || render_manager.first == match_render_interface)
			RenderManagerAccess::ReleaseAllCompiledGeometry(render_manager.second.get());
	}
}

void ReleaseFontResources(CoreInstance& core_instance)
{
	if (!core_instance.font_interface)
		return;

	for (const auto& name_context : core_instance.core_data->contexts)
		name_context.second->GetRootElement()->DirtyFontFaceRecursive();

	core_instance.font_interface->ReleaseFontResources();

	for (const auto& name_context : core_instance.core_data->contexts)
		name_context.second->Update();
}

void ReleaseRenderManagers(CoreInstance& core_instance)
{
	auto& contexts = core_instance.core_data->contexts;
	auto& render_managers = core_instance.core_data->render_managers;

	ReleaseFontResources(core_instance);

	for (auto it = render_managers.begin(); it != render_managers.end();)
	{
		RenderManager* render_manager = it->second.get();
		const auto num_contexts_using_manager = std::count_if(contexts.begin(), contexts.end(),
			[&](const auto& context_pair) { return &context_pair.second->GetRenderManager() == render_manager; });

		if (num_contexts_using_manager == 0)
			it = render_managers.erase(it);
		else
			++it;
	}
}

// Functions that need to be accessible within the Core library, but not publicly.
namespace CoreInternal {

	bool HasRenderManager(CoreInstance& core_instance, RenderInterface* match_render_interface)
	{
		return core_instance.core_data && core_instance.core_data->render_managers.find(match_render_interface) != core_instance.core_data->render_managers.end();
	}

} // namespace CoreInternal

} // namespace Rml
