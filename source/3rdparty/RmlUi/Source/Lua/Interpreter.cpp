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

#include "LuaDocumentElementInstancer.h"
#include "LuaEventListenerInstancer.h"
#include "LuaPlugin.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/CoreInstance.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Lua/Interpreter.h>
#include <RmlUi/Lua/LuaType.h>

#include <chrono>

namespace Rml {
namespace Lua {

static constexpr double SCRIPT_TIMEOUT_SECONDS = 3.0;
static constexpr int HOOK_INSTRUCTION_COUNT = 10000;

static int ErrorHandler(lua_State* L)
{
	// Remove the timeout hook before building the traceback, so the error handler itself
	// does not trigger a spurious timeout
	lua_sethook(L, nullptr, 0, 0);

	const char* msg = lua_tostring(L, 1);
	if (msg == NULL)
	{
		if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
			return 1;
		else
			msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
	}
	luaL_traceback(L, L, msg, 1);
	return 1;
}

static void TimeoutHook(lua_State* L, lua_Debug*)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "RmlUi.ScriptStartTime");
	const auto startMs = reinterpret_cast<intptr_t>(lua_touserdata(L, -1));
	lua_pop(L, 1);

	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	if (static_cast<double>(nowMs - startMs) / 1000.0 > SCRIPT_TIMEOUT_SECONDS)
		luaL_error(L, "script execution timeout (exceeded %d seconds)", static_cast<int>(SCRIPT_TIMEOUT_SECONDS));
}

static bool LuaCall(lua_State* L, int nargs, int nresults)
{
	// Install timeout hook
	const auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<intptr_t>(startMs)));
	lua_setfield(L, LUA_REGISTRYINDEX, "RmlUi.ScriptStartTime");
	lua_sethook(L, TimeoutHook, LUA_MASKCOUNT, HOOK_INSTRUCTION_COUNT);

	int errfunc = -2 - nargs;
	lua_pushcfunction(L, ErrorHandler);
	lua_insert(L, errfunc);
	const int result = lua_pcall(L, nargs, nresults, errfunc);

	// Remove timeout hook
	lua_sethook(L, nullptr, 0, 0);

	if (result != LUA_OK)
	{
		Log::Message(GetCoreInstance(L), Log::LT_WARNING, "%s", lua_tostring(L, -1));
		lua_pop(L, 2);
		return false;
	}
	lua_remove(L, -1 - nresults);
	return true;
}

bool Interpreter::LoadFile(CoreInstance& core_instance, const String& file)
{
	lua_State* L = core_instance.lua_state;

	// use the file interface to get the contents of the script
	FileInterface* file_interface = GetFileInterface(core_instance);
	FileHandle handle = file_interface->Open(file);
	if (handle == 0)
	{
		Log::Message(Log::LT_WARNING, "LoadFile: Unable to open file: %s", file.c_str());
		return false;
	}

	size_t size = file_interface->Length(handle);
	if (size == 0)
	{
		Log::Message(Log::LT_WARNING, "LoadFile: File is 0 bytes in size: %s", file.c_str());
		return false;
	}
	UniquePtr<char[]> file_contents(new char[size]);
	file_interface->Read(file_contents.get(), size, handle);
	file_interface->Close(handle);

	if (luaL_loadbuffer(L, file_contents.get(), size, ("@" + file).c_str()) != 0)
	{
		Log::Message(Log::LT_WARNING, "%s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}
	return LuaCall(L, 0, 0);
}

bool Interpreter::DoString(lua_State* L, const String& code, const String& name)
{
	return LoadString(L, code, name) && LuaCall(L, 0, 0);
}

bool Interpreter::LoadString(lua_State* L, const String& code, const String& name)
{
	if (luaL_loadbuffer(L, code.c_str(), code.length(), name.c_str()) != 0)
	{
		Log::Message(Log::LT_WARNING, "%s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}
	return true;
}

void Interpreter::BeginCall(lua_State* L, int funRef)
{
	lua_settop(L, 0); // empty stack
	lua_rawgeti(L, LUA_REGISTRYINDEX, (int)funRef);
}

bool Interpreter::ExecuteCall(lua_State* L, int params, int res)
{
	return LuaCall(L, params, res);
}

void Interpreter::EndCall(lua_State* L, int res)
{
	lua_pop(L, res);
}

} // namespace Lua
} // namespace Rml
