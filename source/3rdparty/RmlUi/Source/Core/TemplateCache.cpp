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

#include "TemplateCache.h"
#include "../../Include/RmlUi/Core/Log.h"
#include "StreamFile.h"
#include "Template.h"
#include "RmlUi/Core/CoreInstance.h"

namespace Rml {

TemplateCache::TemplateCache(CoreInstance& in_core_instance) : core_instance(in_core_instance)
{
	RMLUI_ASSERT(core_instance.template_cache == nullptr);
	core_instance.template_cache = this;
}

TemplateCache::~TemplateCache()
{
	for (Templates::iterator itr = templates.begin(); itr != templates.end(); ++itr)
	{
		delete (*itr).second;
	}

	core_instance.template_cache = nullptr;
}

bool TemplateCache::Initialise(CoreInstance& in_core_instance)
{
	new TemplateCache(in_core_instance);

	return true;
}

void TemplateCache::Shutdown(CoreInstance& in_core_instance)
{
	delete in_core_instance.template_cache;
}

Template* TemplateCache::LoadTemplate(const String& name)
{
	// Check if the template is already loaded
	Templates::iterator itr = templates.find(name);
	if (itr != templates.end())
		return (*itr).second;

	// Nope, we better load it
	Template* new_template = nullptr;
	auto stream = MakeUnique<StreamFile>(core_instance);
	if (stream->Open(name))
	{
		new_template = new Template(core_instance);
		if (!new_template->Load(stream.get()))
		{
			Log::Message(core_instance, Log::LT_ERROR, "Failed to load template %s.", name.c_str());
			delete new_template;
			new_template = nullptr;
		}
		else if (new_template->GetName().empty())
		{
			Log::Message(core_instance, Log::LT_ERROR, "Failed to load template %s, template is missing its name.", name.c_str());
			delete new_template;
			new_template = nullptr;
		}
		else
		{
			templates[name] = new_template;
			template_ids[new_template->GetName()] = new_template;
		}
	}
	else
	{
		Log::Message(core_instance, Log::LT_ERROR, "Failed to open template file %s.", name.c_str());
	}

	return new_template;
}

Template* TemplateCache::GetTemplate(const String& name)
{
	// Check if the template is already loaded
	Templates::iterator itr = template_ids.find(name);
	if (itr != template_ids.end())
		return (*itr).second;

	return nullptr;
}

void TemplateCache::Clear()
{
	for (Templates::iterator i = templates.begin(); i != templates.end(); ++i)
		delete (*i).second;

	templates.clear();
	template_ids.clear();
}

} // namespace Rml
