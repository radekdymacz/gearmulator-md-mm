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

#include "PluginRegistry.h"
#include "../../Include/RmlUi/Core/Plugin.h"
#include "ControlledLifetimeResource.h"
#include <algorithm>

#include "RmlUi/Core/CoreInstance.h"

namespace Rml {

struct PluginVectors {
	Vector<Plugin*> basic;
	Vector<Plugin*> document;
	Vector<Plugin*> element;
};

static void EnsurePluginVectorsInitialized(CoreInstance& core_instance)
{
	if (!core_instance.plugin_vectors)
	{
		core_instance.plugin_vectors.Initialize();
	}
}

void PluginRegistry::RegisterPlugin(CoreInstance& core_instance, Plugin* plugin)
{
	EnsurePluginVectorsInitialized(core_instance);

	int event_classes = plugin->GetEventClasses();

	if (event_classes & Plugin::EVT_BASIC)
		core_instance.plugin_vectors->basic.push_back(plugin);
	if (event_classes & Plugin::EVT_DOCUMENT)
		core_instance.plugin_vectors->document.push_back(plugin);
	if (event_classes & Plugin::EVT_ELEMENT)
		core_instance.plugin_vectors->element.push_back(plugin);
}

void PluginRegistry::UnregisterPlugin(CoreInstance& core_instance, Plugin* plugin)
{
	auto erase_value = [](Vector<Plugin*>& container, Plugin* value) {
		container.erase(std::remove(container.begin(), container.end(), value), container.end());
	};

	int event_classes = plugin->GetEventClasses();
	if (event_classes & Plugin::EVT_BASIC)
		erase_value(core_instance.plugin_vectors->basic, plugin);
	if (event_classes & Plugin::EVT_DOCUMENT)
		erase_value(core_instance.plugin_vectors->document, plugin);
	if (event_classes & Plugin::EVT_ELEMENT)
		erase_value(core_instance.plugin_vectors->element, plugin);
}

void PluginRegistry::NotifyInitialise(CoreInstance& core_instance)
{
	EnsurePluginVectorsInitialized(core_instance);

	for (Plugin* plugin : core_instance.plugin_vectors->basic)
		plugin->OnInitialise();
}

void PluginRegistry::NotifyShutdown(CoreInstance& core_instance)
{
	while (!core_instance.plugin_vectors->basic.empty())
	{
		Plugin* plugin = core_instance.plugin_vectors->basic.back();
		UnregisterPlugin(core_instance, plugin);
		plugin->OnShutdown();
	}

	core_instance.plugin_vectors.Shutdown();
}

void PluginRegistry::NotifyContextCreate(Context* context)
{
	auto& core_instance = context->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->basic)
		plugin->OnContextCreate(context);
}

void PluginRegistry::NotifyContextDestroy(Context* context)
{
	auto& core_instance = context->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->basic)
		plugin->OnContextDestroy(context);
}

void PluginRegistry::NotifyDocumentOpen(Context* context, const String& document_path)
{
	auto& core_instance = context->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->document)
		plugin->OnDocumentOpen(context, document_path);
}

void PluginRegistry::NotifyDocumentLoad(ElementDocument* document)
{
	auto& core_instance = document->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->document)
		plugin->OnDocumentLoad(document);
}

void PluginRegistry::NotifyDocumentUnload(ElementDocument* document)
{
	auto& core_instance = document->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->document)
		plugin->OnDocumentUnload(document);
}

void PluginRegistry::NotifyElementCreate(Element* element)
{
	auto& core_instance = element->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->element)
		plugin->OnElementCreate(element);
}

void PluginRegistry::NotifyElementDestroy(Element* element)
{
	auto& core_instance = element->GetCoreInstance();
	for (Plugin* plugin : core_instance.plugin_vectors->element)
		plugin->OnElementDestroy(element);
}

} // namespace Rml
