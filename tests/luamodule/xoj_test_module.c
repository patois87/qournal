/*
 * Qournal
 *
 * A Lua module written in C, as luarocks installs them: the plugin tests load it to see that such modules work
 *
 * @license GNU GPLv2 or later
 */

#include <lauxlib.h>
#include <lua.h>

static int answer(lua_State* L) {
    lua_pushinteger(L, 42);
    return 1;
}

#if defined(_WIN32)
__declspec(dllexport)
#endif
int luaopen_xoj_test_module(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, answer);
    lua_setfield(L, -2, "answer");
    return 1;
}
