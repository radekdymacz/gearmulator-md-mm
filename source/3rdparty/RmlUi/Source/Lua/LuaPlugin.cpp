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

#include "LuaPlugin.h"
#include "LuaDocumentElementInstancer.h"
#include "LuaEventListenerInstancer.h"
#include "RmlUi.h"
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/CoreInstance.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Lua.h>
#include <RmlUi/Lua/LuaType.h>
#include <RmlUi/Lua/Utilities.h>
// the types I made
#include "Colourb.h"
#include "Colourf.h"
#include "Context.h"
#include "ContextDocumentsProxy.h"
#include "Document.h"
#include "Element.h"
#include "ElementAttributesProxy.h"
#include "ElementChildNodesProxy.h"
#include "ElementInstancer.h"
#include "ElementStyleProxy.h"
#include "ElementText.h"
#include "Event.h"
#include "EventParametersProxy.h"
#include "GlobalLuaFunctions.h"
#include "Log.h"
#include "RmlUiContextsProxy.h"
#include "Vector2f.h"
#include "Vector2i.h"
// Control types
#include "Elements/ElementForm.h"
#include "Elements/ElementFormControl.h"
#include "Elements/ElementFormControlInput.h"
#include "Elements/ElementFormControlSelect.h"
#include "Elements/ElementFormControlTextArea.h"
#include "Elements/ElementTabSet.h"
#include "Elements/SelectOptionsProxy.h"

namespace Rml {
namespace Lua {

static const char* const CORE_INSTANCE_REGISTRY_KEY = "RmlUi.CoreInstance";

/** This will populate the global Lua table with all of the Lua core types by calling LuaType<T>::Register
@remark This is called automatically by LuaPlugin::OnInitialise(). */
static void RegisterTypes(lua_State* L);

CoreInstance& GetCoreInstance(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, CORE_INSTANCE_REGISTRY_KEY);
	auto* ci = static_cast<CoreInstance*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	RMLUI_ASSERT(ci);
	return *ci;
}

static void StoreCoreInstance(lua_State* L, CoreInstance& _coreInstance)
{
	lua_pushlightuserdata(L, &_coreInstance);
	lua_setfield(L, LUA_REGISTRYINDEX, CORE_INSTANCE_REGISTRY_KEY);
}

LuaPlugin::LuaPlugin(CoreInstance& _core_instance, lua_State* lua_state)
	: core_instance(_core_instance)
{
	core_instance.lua_state = lua_state;
}

int LuaPlugin::GetEventClasses()
{
	return EVT_BASIC;
}

void LuaPlugin::OnInitialise()
{
	if (core_instance.lua_state == nullptr)
	{
		Log::Message(Log::LT_INFO, "Loading Lua plugin using a new Lua state.");
		core_instance.lua_state = luaL_newstate();

		// Open only safe standard libraries. Deliberately exclude io, os, package, and debug
		// as they would allow skin scripts to access the filesystem, run shell commands, or
		// load arbitrary native code.
		static const luaL_Reg safeLibs[] = {
			{LUA_GNAME, luaopen_base},
			{LUA_COLIBNAME, luaopen_coroutine},
			{LUA_TABLIBNAME, luaopen_table},
			{LUA_STRLIBNAME, luaopen_string},
			{LUA_MATHLIBNAME, luaopen_math},
			{LUA_UTF8LIBNAME, luaopen_utf8},
			{nullptr, nullptr}
		};
		for (const luaL_Reg* lib = safeLibs; lib->func; lib++)
		{
			luaL_requiref(core_instance.lua_state, lib->name, lib->func, 1);
			lua_pop(core_instance.lua_state, 1);
		}

		owns_lua_state = true;
	}
	else
	{
		Log::Message(Log::LT_INFO, "Loading Lua plugin using the provided Lua state.");
		owns_lua_state = false;
	}

	StoreCoreInstance(core_instance.lua_state, core_instance);
	RegisterTypes(core_instance.lua_state);

	lua_document_element_instancer = new LuaDocumentElementInstancer();
	lua_event_listener_instancer = new LuaEventListenerInstancer();
	core_instance.factory->RegisterElementInstancer("body", lua_document_element_instancer);
	core_instance.factory->RegisterEventListenerInstancer(lua_event_listener_instancer);
}

void LuaPlugin::OnShutdown()
{
	delete lua_document_element_instancer;
	delete lua_event_listener_instancer;
	lua_document_element_instancer = nullptr;
	lua_event_listener_instancer = nullptr;

	if (owns_lua_state)
		lua_close(core_instance.lua_state);

	core_instance.lua_state = nullptr;

	delete this;
}

static void RegisterTypes(lua_State* L)
{
	RMLUI_ASSERT(L);

	LuaType<Vector2i>::Register(L);
	LuaType<Vector2f>::Register(L);
	LuaType<Colourf>::Register(L);
	LuaType<Colourb>::Register(L);
	LuaType<Log>::Register(L);
	LuaType<ElementStyleProxy>::Register(L);
	LuaType<Element>::Register(L);
	// things that inherit from Element
	LuaType<Document>::Register(L);
	LuaType<ElementText>::Register(L);
	LuaType<ElementPtr>::Register(L);
	LuaType<Event>::Register(L);
	LuaType<Context>::Register(L);
	LuaType<LuaRmlUi>::Register(L);
	LuaType<ElementInstancer>::Register(L);
	// Proxy tables
	LuaType<ContextDocumentsProxy>::Register(L);
	LuaType<EventParametersProxy>::Register(L);
	LuaType<ElementAttributesProxy>::Register(L);
	LuaType<ElementChildNodesProxy>::Register(L);
	LuaType<RmlUiContextsProxy>::Register(L);
	OverrideLuaGlobalFunctions(L);
	// push the global variable "rmlui" to use the "RmlUi" methods
	LuaRmlUiPushrmluiGlobal(L);

	// Control types
	LuaType<ElementForm>::Register(L);
	LuaType<ElementFormControl>::Register(L);
	// Inherits from ElementFormControl
	LuaType<ElementFormControlSelect>::Register(L);
	LuaType<ElementFormControlInput>::Register(L);
	LuaType<ElementFormControlTextArea>::Register(L);
	LuaType<ElementTabSet>::Register(L);
	// proxy tables
	LuaType<SelectOptionsProxy>::Register(L);
}

} // namespace Lua
} // namespace Rml
