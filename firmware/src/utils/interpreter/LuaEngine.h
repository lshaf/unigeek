#pragma once
#include <Arduino.h>

extern "C" {
  #include "lua.h"
  #include "lauxlib.h"
  #include "lualib.h"
}

class LuaEngine {
public:
  // Unique address used as a sentinel to distinguish clean exit() from errors.
  static char exitSentinel;

  bool init();
  void deinit();

  // Compile source; call stepLoop() in a loop afterward.
  bool loadScript(const char* src, String& errOut);

  // Execute one iteration of the compiled chunk.
  // Returns false when done (exit() called, error, or requestExit()).
  bool stepLoop(String& errOut);

  void requestExit()             { _exitRequested = true; }
  bool isExitRequested()   const { return _exitRequested; }

  // Body rect in screen coordinates — set once by the runner screen.
  void setBodyRect(int x, int y, int w, int h) {
    _bx = x; _by = y; _bw = w; _bh = h;
  }

private:
  lua_State* _lua      = nullptr;
  int        _chunkRef = LUA_NOREF;
  bool       _exitRequested = false;
  int        _bx = 0, _by = 0, _bw = 240, _bh = 200;

  static void* _alloc(void* ud, void* ptr, size_t osize, size_t nsize);
  static void  _countHook(lua_State* L, lua_Debug* ar);
  static LuaEngine* _fromState(lua_State* L);
  void _registerBindings();

  // uni.*
  static int _uni_debug(lua_State* L);
  static int _uni_delay(lua_State* L);
  static int _uni_btn(lua_State* L);
  static int _uni_heap(lua_State* L);
  static int _uni_millis(lua_State* L);
  static int _uni_beep(lua_State* L);
  static int _lua_exit(lua_State* L);

  // uni.lcd.*
  static int _lcd_clear(lua_State* L);
  static int _lcd_print(lua_State* L);
  static int _lcd_rect(lua_State* L);
  static int _lcd_line(lua_State* L);
  static int _lcd_color(lua_State* L);
  static int _lcd_textSize(lua_State* L);
  static int _lcd_textColor(lua_State* L);
  static int _lcd_w(lua_State* L);
  static int _lcd_h(lua_State* L);

  // uni.sd.*
  static int _sd_read(lua_State* L);
  static int _sd_write(lua_State* L);
  static int _sd_append(lua_State* L);
  static int _sd_exists(lua_State* L);
  static int _sd_list(lua_State* L);
};
