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

#ifndef RMLUI_LUA_INTERPRETER_H
#define RMLUI_LUA_INTERPRETER_H

#include "Header.h"
#include <RmlUi/Core/Types.h>

typedef struct lua_State lua_State;

namespace Rml {

class CoreInstance;

namespace Lua {

	namespace Interpreter {
		/** This function calls luaL_loadfile and then lua_pcall, reporting the errors (if any)
		@param[in] core_instance CoreInstance to use for file loading and lua_State lookup.
		@param[in] file Fully qualified file name to execute.
		@remark Somewhat misleading name if you are used to the Lua meaning of "load file". It behaves
		exactly as luaL_dofile does.            */
		RMLUILUA_API bool LoadFile(CoreInstance& core_instance, const String& file);
		/** Calls lua_dostring and reports the errors.
		@param[in] L The lua_State to execute in.
		@param[in] code String to execute
		@param[in] name Name for the code that will show up in the Log  */
		RMLUILUA_API bool DoString(lua_State* L, const String& code, const String& name = "");
		/** Same as DoString, except does NOT call pcall on it. It will leave the compiled (but not executed) string
		on top of the stack. It behaves exactly like luaL_loadstring, but you get to specify the name
		@param[in] L The lua_State to compile in.
		@param[in] code String to compile
		@param[in] name Name for the code that will show up in the Log    */
		RMLUILUA_API bool LoadString(lua_State* L, const String& code, const String& name = "");

		/** Clears all of the items on the stack, and pushes the function from funRef on top of the stack. Only use
		this if you used lua_ref instead of luaL_ref
		@param[in] L The lua_State to operate on.
		@param[in] funRef Lua reference that you would recieve from calling lua_ref   */
		RMLUILUA_API void BeginCall(lua_State* L, int funRef);
		/** Uses lua_pcall on a function, which executes the function with params number of parameters and pushes
		res number of return values on to the stack.
		@param[in] L The lua_State to operate on.
		@pre Before you call this, your stack should look like:
		[1] function to call;
		[2...top] parameters to pass to the function (if any).
		Or, in words, make sure to push the function on the stack before the parameters.
		@post After this function, the params and function will be popped off, and 'res'
		number of items will be pushed.     */
		RMLUILUA_API bool ExecuteCall(lua_State* L, int params = 0, int res = 0);
		/** removes 'res' number of items from the stack
		@param[in] L The lua_State to operate on.
		@param[in] res Number of results to remove from the stack.   */
		RMLUILUA_API void EndCall(lua_State* L, int res = 0);
	} // namespace Interpreter

} // namespace Lua
} // namespace Rml
#endif
