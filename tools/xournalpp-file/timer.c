/*
 * Qournal
 *
 * A Lua module for Xournal++ 1.3.8 that calls a function later, from the main loop of GTK: the plugin that writes
 * the test file needs the document to be open first. Xournal++ has no such function itself.
 *
 * The functions of Lua 5.3 and GLib are declared here and found in the running program, so that no headers of
 * them are needed.
 *
 * @license GNU GPLv2 or later
 */

#include <stdio.h>

typedef struct lua_State lua_State;
typedef long long lua_Integer;
typedef int (*lua_CFunction)(lua_State* L);

#define LUA_REGISTRYINDEX (-1000000 - 1000)

int lua_rawgeti(lua_State* L, int idx, lua_Integer n);
int luaL_ref(lua_State* L, int t);
void luaL_unref(lua_State* L, int t, int ref);
int lua_pcallk(lua_State* L, int nargs, int nresults, int errfunc, void* ctx, void* k);
void lua_pushcclosure(lua_State* L, lua_CFunction fn, int n);
lua_Integer luaL_checkinteger(lua_State* L, int arg);
void lua_settop(lua_State* L, int idx);
const char* lua_tolstring(lua_State* L, int idx, unsigned long* len);
unsigned int g_timeout_add(unsigned int interval, int (*function)(void* data), void* data);

static lua_State* state;

static int call(void* data) {
    const int ref = (int)(long)data;
    lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
    luaL_unref(state, LUA_REGISTRYINDEX, ref);
    if (lua_pcallk(state, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "timer: %s\n", lua_tolstring(state, -1, 0));
        lua_settop(state, -2);
    }
    return 0;  // once
}

/// after(milliseconds, function)
static int after(lua_State* L) {
    const unsigned int ms = (unsigned int)luaL_checkinteger(L, 1);
    lua_settop(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    state = L;
    g_timeout_add(ms, call, (void*)(long)ref);
    return 0;
}

int luaopen_timer(lua_State* L) {
    lua_pushcclosure(L, after, 0);
    return 1;
}
