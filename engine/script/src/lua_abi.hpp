// Lua 5.4 C ABI declarations used when distribution packages omit lua.h.
// These signatures mirror the public Lua 5.4 API; no interpreter is emulated.
#pragma once
#include <cstddef>

extern "C" {
struct lua_State;
using lua_Number = double;
using lua_KContext = long long;
using lua_CFunction = int (*)(lua_State*);
using lua_KFunction = int (*)(lua_State*, int, lua_KContext);
lua_State* luaL_newstate();
void luaL_openlibs(lua_State*);
void lua_close(lua_State*);
int luaL_loadbufferx(lua_State*, const char*, size_t, const char*, const char*);
int luaL_loadfilex(lua_State*, const char*, const char*);
int lua_pcallk(lua_State*, int, int, int, lua_KContext, lua_KFunction);
int lua_getglobal(lua_State*, const char*);
void lua_setglobal(lua_State*, const char*);
void lua_createtable(lua_State*, int, int);
void lua_pushnumber(lua_State*, lua_Number);
void lua_pushlightuserdata(lua_State*, void*);
void lua_pushcclosure(lua_State*, lua_CFunction, int);
void lua_setfield(lua_State*, int, const char*);
void* lua_touserdata(lua_State*, int);
lua_Number lua_tonumberx(lua_State*, int, int*);
const char* lua_tolstring(lua_State*, int, size_t*);
int lua_type(lua_State*, int);
void lua_settop(lua_State*, int);
}

inline constexpr int LUA_OK = 0;
inline constexpr int LUA_TFUNCTION = 6;
inline constexpr int LUA_REGISTRYINDEX = -1001000;
inline constexpr const char* LUA_RELEASE = "Lua 5.4";
inline bool nimbus_lua_isnumber(lua_State* state, int index) {
  int isNumber = 0;
  (void)lua_tonumberx(state, index, &isNumber);
  return isNumber != 0;
}
#define lua_pop(L, n) lua_settop((L), -(n)-1)
#define lua_newtable(L) lua_createtable((L), 0, 0)
#define lua_isfunction(L, n) (lua_type((L), (n)) == LUA_TFUNCTION)
#define lua_isnumber(L, n) nimbus_lua_isnumber((L), (n))
#define lua_tonumber(L, n) lua_tonumberx((L), (n), nullptr)
#define lua_tostring(L, n) lua_tolstring((L), (n), nullptr)
#define lua_upvalueindex(i) (LUA_REGISTRYINDEX - (i))
#define luaL_loadbuffer(L, s, sz, n) luaL_loadbufferx((L), (s), (sz), (n), nullptr)
#define luaL_loadfile(L, f) luaL_loadfilex((L), (f), nullptr)
#define lua_pcall(L, n, r, f) lua_pcallk((L), (n), (r), (f), 0, nullptr)
