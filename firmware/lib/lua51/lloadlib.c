/* lloadlib.c — minimal package/require for ESP32 Lua 5.1.
** Checks package.loaded first, then package.preload (C loader functions).
** No file-system or dynamic loader. Register modules via package.preload.
*/
#define lloadlib_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int ll_require(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  lua_settop(L, 1);

  lua_getglobal(L, "package");    /* 2: package */
  lua_getfield(L, 2, "loaded");   /* 3: package.loaded */
  lua_getfield(L, 3, name);       /* 4: package.loaded[name] */
  if (!lua_isnil(L, 4))
    return 1;
  lua_pop(L, 1);

  lua_getfield(L, 2, "preload");  /* 4: package.preload */
  lua_getfield(L, 4, name);       /* 5: loader function */
  if (lua_isnil(L, 5))
    return luaL_error(L, "module '%s' not found", name);

  lua_pushvalue(L, 1);            /* 6: name arg */
  lua_call(L, 1, 1);             /* call loader(name) → result at 5 */
  lua_pushvalue(L, 5);
  lua_setfield(L, 3, name);       /* package.loaded[name] = result */
  return 1;
}

static const luaL_Reg pk_funcs[] = {
  {NULL, NULL}
};

LUALIB_API int luaopen_package(lua_State *L) {
  luaL_register(L, LUA_LOADLIBNAME, pk_funcs);
  lua_newtable(L); lua_setfield(L, -2, "loaded");
  lua_newtable(L); lua_setfield(L, -2, "preload");
  lua_pushcfunction(L, ll_require);
  lua_setglobal(L, "require");
  return 1;
}
