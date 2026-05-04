#include "utils/interpreter/LuaEngine.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

char LuaEngine::exitSentinel = '\0';

// ── Registry key — unique address for storing engine pointer ──────────

static const char kRegKey = '\0';

// ── Allocator: always uses internal SRAM ──────────────────────────────

void* LuaEngine::_alloc(void*, void* ptr, size_t, size_t nsize) {
  if (nsize == 0) {
    heap_caps_free(ptr);
    return nullptr;
  }
  uint32_t caps =
#ifdef BOARD_HAS_PSRAM
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
  return heap_caps_realloc(ptr, nsize, caps);
}

// ── Count hook: fires every 1000 instructions to check exit flag ──────

void LuaEngine::_countHook(lua_State* L, lua_Debug*) {
  LuaEngine* eng = _fromState(L);
  if (eng && eng->_exitRequested) {
    lua_pushlightuserdata(L, &LuaEngine::exitSentinel);
    lua_error(L);
  }
}

// ── Retrieve engine from registry ─────────────────────────────────────

LuaEngine* LuaEngine::_fromState(lua_State* L) {
  lua_pushlightuserdata(L, (void*)&kRegKey);
  lua_rawget(L, LUA_REGISTRYINDEX);
  auto* eng = static_cast<LuaEngine*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return eng;
}

// ── Lifecycle ─────────────────────────────────────────────────────────

bool LuaEngine::init() {
  if (_lua) deinit();
  _lua = lua_newstate(_alloc, nullptr);
  if (!_lua) return false;

  lua_pushlightuserdata(_lua, (void*)&kRegKey);
  lua_pushlightuserdata(_lua, this);
  lua_rawset(_lua, LUA_REGISTRYINDEX);

  lua_sethook(_lua, _countHook, LUA_MASKCOUNT, 1000);

  luaL_openlibs(_lua);
  _registerBindings();
  _exitRequested = false;
  _chunkRef = LUA_NOREF;
  return true;
}

void LuaEngine::deinit() {
  if (_chunkRef != LUA_NOREF && _lua) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _chunkRef);
    _chunkRef = LUA_NOREF;
  }
  if (_lua) { lua_close(_lua); _lua = nullptr; }
  _exitRequested = false;
}

// ── Script loading ────────────────────────────────────────────────────

bool LuaEngine::loadScript(const char* src, String& errOut) {
  if (!_lua) return false;
  if (_chunkRef != LUA_NOREF) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _chunkRef);
    _chunkRef = LUA_NOREF;
  }
  _exitRequested = false;

  int rc = luaL_loadbuffer(_lua, src, strlen(src), "script");
  if (rc != 0) {
    errOut = lua_tostring(_lua, -1);
    lua_pop(_lua, 1);
    return false;
  }
  _chunkRef = luaL_ref(_lua, LUA_REGISTRYINDEX);
  return true;
}

// ── Loop step ─────────────────────────────────────────────────────────

bool LuaEngine::stepLoop(String& errOut) {
  if (!_lua || _chunkRef == LUA_NOREF || _exitRequested) return false;

  lua_rawgeti(_lua, LUA_REGISTRYINDEX, _chunkRef);
  int rc = lua_pcall(_lua, 0, 0, 0);
  if (rc == 0) return true;

  if (lua_islightuserdata(_lua, -1) &&
      lua_touserdata(_lua, -1) == &LuaEngine::exitSentinel) {
    lua_pop(_lua, 1);
    return false; // clean exit via exit()
  }
  errOut = lua_isstring(_lua, -1) ? lua_tostring(_lua, -1) : "unknown error";
  lua_pop(_lua, 1);
  return false;
}

// ── Binding registration ──────────────────────────────────────────────

void LuaEngine::_registerBindings() {
  lua_register(_lua, "exit", _lua_exit);

  // uni table
  lua_newtable(_lua);

  lua_pushcfunction(_lua, _uni_debug);  lua_setfield(_lua, -2, "debug");
  lua_pushcfunction(_lua, _uni_delay);  lua_setfield(_lua, -2, "delay");
  lua_pushcfunction(_lua, _uni_btn);    lua_setfield(_lua, -2, "btn");
  lua_pushcfunction(_lua, _uni_heap);   lua_setfield(_lua, -2, "heap");
  lua_pushcfunction(_lua, _uni_millis); lua_setfield(_lua, -2, "millis");
  lua_pushcfunction(_lua, _uni_beep);   lua_setfield(_lua, -2, "beep");

  // uni.lcd sub-table
  lua_newtable(_lua);
  lua_pushcfunction(_lua, _lcd_clear);     lua_setfield(_lua, -2, "clear");
  lua_pushcfunction(_lua, _lcd_print);     lua_setfield(_lua, -2, "print");
  lua_pushcfunction(_lua, _lcd_rect);      lua_setfield(_lua, -2, "rect");
  lua_pushcfunction(_lua, _lcd_line);      lua_setfield(_lua, -2, "line");
  lua_pushcfunction(_lua, _lcd_color);     lua_setfield(_lua, -2, "color");
  lua_pushcfunction(_lua, _lcd_textSize);  lua_setfield(_lua, -2, "textSize");
  lua_pushcfunction(_lua, _lcd_textColor); lua_setfield(_lua, -2, "textColor");
  lua_pushcfunction(_lua, _lcd_w);         lua_setfield(_lua, -2, "w");
  lua_pushcfunction(_lua, _lcd_h);         lua_setfield(_lua, -2, "h");
  lua_setfield(_lua, -2, "lcd");

  // uni.sd sub-table
  lua_newtable(_lua);
  lua_pushcfunction(_lua, _sd_read);   lua_setfield(_lua, -2, "read");
  lua_pushcfunction(_lua, _sd_write);  lua_setfield(_lua, -2, "write");
  lua_pushcfunction(_lua, _sd_append); lua_setfield(_lua, -2, "append");
  lua_pushcfunction(_lua, _sd_exists); lua_setfield(_lua, -2, "exists");
  lua_pushcfunction(_lua, _sd_list);   lua_setfield(_lua, -2, "list");
  lua_setfield(_lua, -2, "sd");

  lua_setglobal(_lua, "uni");
}

// ── exit() ────────────────────────────────────────────────────────────

int LuaEngine::_lua_exit(lua_State* L) {
  lua_pushlightuserdata(L, &LuaEngine::exitSentinel);
  lua_error(L);
  return 0;
}

// ── uni.* ─────────────────────────────────────────────────────────────

int LuaEngine::_uni_debug(lua_State* L) {
  const char* msg = luaL_checkstring(L, 1);
  Serial.println(msg);
  return 0;
}

int LuaEngine::_uni_delay(lua_State* L) {
  int ms = (int)luaL_checknumber(L, 1);
  LuaEngine* eng = _fromState(L);
  uint32_t end = millis() + (uint32_t)ms;
  while (millis() < end) {
    if (eng && eng->_exitRequested) break;
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) {
        if (eng) eng->requestExit();
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return 0;
}

int LuaEngine::_uni_btn(lua_State* L) {
  if (!Uni.Nav->wasPressed()) { lua_pushstring(L, "none"); return 1; }
  LuaEngine* eng = _fromState(L);
  switch (Uni.Nav->readDirection()) {
    case INavigation::DIR_UP:    lua_pushstring(L, "up");    break;
    case INavigation::DIR_DOWN:  lua_pushstring(L, "down");  break;
    case INavigation::DIR_LEFT:  lua_pushstring(L, "left");  break;
    case INavigation::DIR_RIGHT: lua_pushstring(L, "right"); break;
    case INavigation::DIR_PRESS: lua_pushstring(L, "ok");    break;
    case INavigation::DIR_BACK:
      lua_pushstring(L, "back");
      if (eng) eng->requestExit();
      break;
    default:                     lua_pushstring(L, "none");  break;
  }
  return 1;
}

int LuaEngine::_uni_heap(lua_State* L) {
  lua_pushnumber(L, (lua_Number)ESP.getFreeHeap());
  return 1;
}

int LuaEngine::_uni_millis(lua_State* L) {
  lua_pushnumber(L, (lua_Number)millis());
  return 1;
}

int LuaEngine::_uni_beep(lua_State* L) {
#ifdef DEVICE_HAS_SOUND
  int freq = (int)luaL_checknumber(L, 1);
  int ms   = (int)luaL_checknumber(L, 2);
  if (Uni.Speaker) Uni.Speaker->tone(freq, ms);
#endif
  return 0;
}

// ── uni.lcd.* ─────────────────────────────────────────────────────────

int LuaEngine::_lcd_clear(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  Uni.Lcd.fillRect(eng->_bx, eng->_by, eng->_bw, eng->_bh, TFT_BLACK);
  return 0;
}

int LuaEngine::_lcd_print(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x         = (int)luaL_checknumber(L, 1);
  int y         = (int)luaL_checknumber(L, 2);
  const char* s = luaL_checkstring(L, 3);
  Uni.Lcd.drawString(s, eng->_bx + x, eng->_by + y);
  return 0;
}

int LuaEngine::_lcd_rect(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  uint32_t c = (uint32_t)luaL_checknumber(L, 5);
  Uni.Lcd.fillRect(eng->_bx + x, eng->_by + y, w, h, c);
  return 0;
}

int LuaEngine::_lcd_line(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  uint32_t c = (uint32_t)luaL_checknumber(L, 5);
  Uni.Lcd.drawLine(eng->_bx + x0, eng->_by + y0, eng->_bx + x1, eng->_by + y1, c);
  return 0;
}

int LuaEngine::_lcd_color(lua_State* L) {
  int r = (int)luaL_checknumber(L, 1);
  int g = (int)luaL_checknumber(L, 2);
  int b = (int)luaL_checknumber(L, 3);
  uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  lua_pushnumber(L, (lua_Number)c);
  return 1;
}

int LuaEngine::_lcd_textSize(lua_State* L) {
  Uni.Lcd.setTextSize((uint8_t)luaL_checknumber(L, 1));
  return 0;
}

int LuaEngine::_lcd_textColor(lua_State* L) {
  uint32_t c = (uint32_t)luaL_checknumber(L, 1);
  Uni.Lcd.setTextColor(c);
  return 0;
}

int LuaEngine::_lcd_w(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  lua_pushnumber(L, eng ? (lua_Number)eng->_bw : 0);
  return 1;
}

int LuaEngine::_lcd_h(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  lua_pushnumber(L, eng ? (lua_Number)eng->_bh : 0);
  return 1;
}

// ── uni.sd.* ──────────────────────────────────────────────────────────

static IStorage* _storage() {
  return (Uni.Storage && Uni.Storage->isAvailable()) ? Uni.Storage : nullptr;
}

int LuaEngine::_sd_read(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushnil(L); return 1; }
  const char* path = luaL_checkstring(L, 1);
  String data = s->readFile(path);
  lua_pushstring(L, data.c_str());
  return 1;
}

int LuaEngine::_sd_write(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  const char* path    = luaL_checkstring(L, 1);
  const char* content = luaL_checkstring(L, 2);
  lua_pushboolean(L, s->writeFile(path, content) ? 1 : 0);
  return 1;
}

int LuaEngine::_sd_append(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  const char* path    = luaL_checkstring(L, 1);
  const char* content = luaL_checkstring(L, 2);
  fs::File f = s->open(path, "a");
  if (!f) { lua_pushboolean(L, 0); return 1; }
  f.print(content);
  f.close();
  lua_pushboolean(L, 1);
  return 1;
}

int LuaEngine::_sd_exists(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  lua_pushboolean(L, s->exists(luaL_checkstring(L, 1)) ? 1 : 0);
  return 1;
}

int LuaEngine::_sd_list(lua_State* L) {
  lua_newtable(L);
  IStorage* s = _storage();
  if (!s) return 1;
  const char* path = luaL_checkstring(L, 1);
  static IStorage::DirEntry entries[32];
  uint8_t n = s->listDir(path, entries, 32);
  for (uint8_t i = 0; i < n; i++) {
    lua_pushnumber(L, i + 1);
    lua_newtable(L);
    lua_pushstring(L, "name");  lua_pushstring(L, entries[i].name.c_str()); lua_rawset(L, -3);
    lua_pushstring(L, "isDir"); lua_pushboolean(L, entries[i].isDir ? 1 : 0); lua_rawset(L, -3);
    lua_rawset(L, -3);
  }
  return 1;
}
