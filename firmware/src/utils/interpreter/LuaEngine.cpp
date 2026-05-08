#include "utils/interpreter/LuaEngine.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ConfigManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <cJSON.h>

char LuaEngine::exitSentinel = '\0';

// ── Registry key — unique address for storing engine pointer ──────────

static const char kRegKey = '\0';

// ── Allocator: always uses internal SRAM ──────────────────────────────

void* LuaEngine::_alloc(void*, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    heap_caps_free(ptr);
    return nullptr;
  }
  // VM heap stays in internal SRAM on every board: Lua's many small reallocs
  // and pointer-chasing GC are unsafe under PSRAM cache contention.
  uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  void* p = heap_caps_realloc(ptr, nsize, caps);
  if (!p) {
    Serial.printf("[Lua] ALLOC FAIL nsize=%u osize=%u freeInt=%u freePsram=%u largestPsram=%u\n",
      (unsigned)nsize, (unsigned)osize,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  }
  return p;
}

// ── Count hook: fires every 1000 instructions to check exit flag ──────

void LuaEngine::_countHook(lua_State* L, lua_Debug*) {
  LuaEngine* eng = _fromState(L);
  // Yield every hook tick so the loop task / WDT can run.
  vTaskDelay(0);
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
  if (!_lua) { Serial.println("[Lua] lua_newstate FAILED"); return false; }

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
  // If a script task is running, request exit and wait for it to finish before
  // touching the Lua state — lua_close from another task would race.
  if (_status == STATUS_RUNNING) {
    _exitRequested = true;
    while (_status == STATUS_RUNNING) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  _status = STATUS_IDLE;
  _task   = nullptr;

  if (_pendingSrc) {
    heap_caps_free(_pendingSrc);
    _pendingSrc    = nullptr;
    _pendingSrcLen = 0;
  }
  if (_chunkRef != LUA_NOREF && _lua) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _chunkRef);
    _chunkRef = LUA_NOREF;
  }
  if (_lua) { lua_close(_lua); _lua = nullptr; }
  _exitRequested = false;
  // Drop any popup state so the next script starts with a clean slot.
  _popupType        = POPUP_NONE;
  _popupOptionCount = 0;
}

// ── Script loading ────────────────────────────────────────────────────

bool LuaEngine::loadScript(char* src, size_t len, String& errOut) {
  if (!_lua) {
    if (src) heap_caps_free(src);
    errOut = "engine not initialized";
    return false;
  }
  // Drop any previous pending or compiled chunk.
  if (_pendingSrc) {
    heap_caps_free(_pendingSrc);
    _pendingSrc    = nullptr;
    _pendingSrcLen = 0;
  }
  if (_chunkRef != LUA_NOREF) {
    luaL_unref(_lua, LUA_REGISTRYINDEX, _chunkRef);
    _chunkRef = LUA_NOREF;
  }
  _pendingSrc    = src;
  _pendingSrcLen = len;
  _exitRequested = false;
  return true;
}

// ── Loop step ─────────────────────────────────────────────────────────

// Lua execution lives on its own FreeRTOS task: a generous 32 KB stack so the
// VM + str_format-style recursion can't blow the loop task's 8 KB stack and
// stomp the heap. The first stepLoop() spawns the task; later calls poll its
// status. The task self-deletes after pcall returns.
void LuaEngine::_taskEntry(void* arg) {
  LuaEngine* eng = (LuaEngine*)arg;
  lua_State* L   = eng->_lua;

  // Compile on this task — the parser is recursive and would overflow the
  // 8 KB loop-task stack on large scripts. We have 32 KB here.
  if (eng->_pendingSrc) {
    int crc = luaL_loadbuffer(L, eng->_pendingSrc, eng->_pendingSrcLen, "script");
    heap_caps_free(eng->_pendingSrc);
    eng->_pendingSrc    = nullptr;
    eng->_pendingSrcLen = 0;
    if (crc != 0) {
      eng->_taskErrOut = lua_isstring(L, -1) ? lua_tostring(L, -1) : "compile error";
      Serial.printf("[Lua] compile FAIL: %s\n", eng->_taskErrOut.c_str());
      lua_pop(L, 1);
      eng->_task   = nullptr;
      eng->_status = STATUS_DONE_ERR;
      vTaskDelete(nullptr);
      return;
    }
    eng->_chunkRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  if (eng->_chunkRef == LUA_NOREF) {
    eng->_taskErrOut = "no chunk to run";
    eng->_task   = nullptr;
    eng->_status = STATUS_DONE_ERR;
    vTaskDelete(nullptr);
    return;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, eng->_chunkRef);
  int rc = lua_pcall(L, 0, 0, 0);

  Status next;
  if (rc == 0) {
    next = STATUS_DONE_OK;
  } else if (lua_islightuserdata(L, -1) &&
             lua_touserdata(L, -1) == &LuaEngine::exitSentinel) {
    lua_pop(L, 1);
    next = STATUS_DONE_EXIT;
  } else {
    eng->_taskErrOut = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown error";
    Serial.printf("[Lua] runtime err: %s\n", eng->_taskErrOut.c_str());
    lua_pop(L, 1);
    next = STATUS_DONE_ERR;
  }

  eng->_task   = nullptr;
  eng->_status = next;       // publish AFTER clearing handle
  vTaskDelete(nullptr);
}

bool LuaEngine::stepLoop(String& errOut) {
  if (!_lua) return false;
  // Need either a queued source (waiting to compile) or a compiled chunk.
  if (_chunkRef == LUA_NOREF && !_pendingSrc) return false;

  if (_status == STATUS_IDLE) {
    if (_exitRequested) return false;
    _taskErrOut = "";
    _status = STATUS_RUNNING;
    TaskHandle_t handle = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCore(
      _taskEntry, "lua", 32 * 1024, this, 1, &handle, 1);
    if (ok != pdPASS) {
      _status = STATUS_IDLE;
      errOut  = "task create failed";
      Serial.println("[Lua] task create FAIL");
      return false;
    }
    _task = (void*)handle;
    return true;
  }

  if (_status == STATUS_RUNNING) return true;

  Status s = _status;
  _status  = STATUS_IDLE;
  if (s == STATUS_DONE_ERR) errOut = _taskErrOut;
  return false;
}

// ── Binding registration ──────────────────────────────────────────────

void LuaEngine::_registerBindings() {
  lua_register(_lua, "exit", _lua_exit);

  // uni table — core functions only; lcd/sd/nav loaded on demand via require()
  lua_newtable(_lua);
  lua_pushcfunction(_lua, _uni_debug);  lua_setfield(_lua, -2, "debug");
  lua_pushcfunction(_lua, _uni_delay);  lua_setfield(_lua, -2, "delay");
  lua_pushcfunction(_lua, _uni_heap);   lua_setfield(_lua, -2, "heap");
  lua_pushcfunction(_lua, _uni_millis); lua_setfield(_lua, -2, "millis");
  lua_pushcfunction(_lua, _uni_beep);   lua_setfield(_lua, -2, "beep");
  lua_setglobal(_lua, "uni");

  // Register lazy loaders — tables are built only when require() is called
  lua_getglobal(_lua, "package");
  lua_getfield(_lua, -1, "preload");
  lua_pushcfunction(_lua, _lua_load_lcd);    lua_setfield(_lua, -2, "uni.lcd");
  lua_pushcfunction(_lua, _lua_load_sd);     lua_setfield(_lua, -2, "uni.sd");
  lua_pushcfunction(_lua, _lua_load_nav);    lua_setfield(_lua, -2, "uni.nav");
  lua_pushcfunction(_lua, _lua_load_input);  lua_setfield(_lua, -2, "uni.input");
  lua_pushcfunction(_lua, _lua_load_dialog); lua_setfield(_lua, -2, "uni.dialog");
  lua_pushcfunction(_lua, _lua_load_notify); lua_setfield(_lua, -2, "uni.notify");
  lua_pushcfunction(_lua, _lua_load_json);   lua_setfield(_lua, -2, "uni.json");
  lua_pushcfunction(_lua, _lua_load_path);   lua_setfield(_lua, -2, "uni.path");
  lua_pushcfunction(_lua, _lua_load_time);   lua_setfield(_lua, -2, "uni.time");
  lua_pushcfunction(_lua, _lua_load_config); lua_setfield(_lua, -2, "uni.config");
  lua_pop(_lua, 2);

  // Sprite metatable lives in the registry; attached to each sprite userdata.
  _setupSpriteMetatable();
}

int LuaEngine::_lua_load_lcd(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _lcd_clear);         lua_setfield(L, -2, "clear");
  lua_pushcfunction(L, _lcd_fillScreen);    lua_setfield(L, -2, "fillScreen");
  lua_pushcfunction(L, _lcd_print);         lua_setfield(L, -2, "print");
  lua_pushcfunction(L, _lcd_rect);          lua_setfield(L, -2, "rect");
  lua_pushcfunction(L, _lcd_line);          lua_setfield(L, -2, "line");
  lua_pushcfunction(L, _lcd_circle);        lua_setfield(L, -2, "circle");
  lua_pushcfunction(L, _lcd_fillCircle);    lua_setfield(L, -2, "fillCircle");
  lua_pushcfunction(L, _lcd_roundRect);     lua_setfield(L, -2, "roundRect");
  lua_pushcfunction(L, _lcd_fillRoundRect); lua_setfield(L, -2, "fillRoundRect");
  lua_pushcfunction(L, _lcd_color);         lua_setfield(L, -2, "color");
  lua_pushcfunction(L, _lcd_textSize);      lua_setfield(L, -2, "textSize");
  lua_pushcfunction(L, _lcd_textColor);     lua_setfield(L, -2, "textColor");
  lua_pushcfunction(L, _lcd_textDatum);     lua_setfield(L, -2, "textDatum");
  lua_pushcfunction(L, _lcd_textWidth);     lua_setfield(L, -2, "textWidth");
  lua_pushcfunction(L, _lcd_w);             lua_setfield(L, -2, "w");
  lua_pushcfunction(L, _lcd_h);             lua_setfield(L, -2, "h");
  lua_pushcfunction(L, _lcd_sprite);        lua_setfield(L, -2, "sprite");
  return 1;
}

int LuaEngine::_lua_load_sd(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _sd_read);   lua_setfield(L, -2, "read");
  lua_pushcfunction(L, _sd_write);  lua_setfield(L, -2, "write");
  lua_pushcfunction(L, _sd_append); lua_setfield(L, -2, "append");
  lua_pushcfunction(L, _sd_exists); lua_setfield(L, -2, "exists");
  lua_pushcfunction(L, _sd_list);   lua_setfield(L, -2, "list");
  lua_pushcfunction(L, _sd_remove); lua_setfield(L, -2, "remove");
  lua_pushcfunction(L, _sd_rename); lua_setfield(L, -2, "rename");
  lua_pushcfunction(L, _sd_mkdir);  lua_setfield(L, -2, "mkdir");
  lua_pushcfunction(L, _sd_size);   lua_setfield(L, -2, "size");
  return 1;
}

int LuaEngine::_lua_load_nav(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _nav_btn);       lua_setfield(L, -2, "btn");
  lua_pushcfunction(L, _nav_touchX);    lua_setfield(L, -2, "touchX");
  lua_pushcfunction(L, _nav_touchY);    lua_setfield(L, -2, "touchY");
  lua_pushcfunction(L, _nav_isTouched); lua_setfield(L, -2, "isTouched");
  return 1;
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
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return 0;
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
  LuaEngine* eng = _fromState(L);
  uint32_t c = (uint32_t)luaL_checknumber(L, 1);
  if (eng) eng->_textFg = c;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    uint32_t bg = (uint32_t)luaL_checknumber(L, 2);
    Uni.Lcd.setTextColor(c, bg);
  } else {
    Uni.Lcd.setTextColor(c);
  }
  return 0;
}

int LuaEngine::_lcd_textDatum(lua_State* L) {
  Uni.Lcd.setTextDatum((uint8_t)luaL_checknumber(L, 1));
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
  if (!s->exists(path)) { lua_pushnil(L); return 1; }
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

int LuaEngine::_sd_remove(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  lua_pushboolean(L, s->deleteFile(luaL_checkstring(L, 1)) ? 1 : 0);
  return 1;
}

int LuaEngine::_sd_rename(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  const char* from = luaL_checkstring(L, 1);
  const char* to   = luaL_checkstring(L, 2);
  lua_pushboolean(L, s->renameFile(from, to) ? 1 : 0);
  return 1;
}

int LuaEngine::_sd_mkdir(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushboolean(L, 0); return 1; }
  lua_pushboolean(L, s->makeDir(luaL_checkstring(L, 1)) ? 1 : 0);
  return 1;
}

int LuaEngine::_sd_size(lua_State* L) {
  IStorage* s = _storage();
  if (!s) { lua_pushnumber(L, -1); return 1; }
  const char* path = luaL_checkstring(L, 1);
  if (!s->exists(path)) { lua_pushnumber(L, -1); return 1; }
  fs::File f = s->open(path, "r");
  if (!f) { lua_pushnumber(L, -1); return 1; }
  size_t sz = f.size();
  f.close();
  lua_pushnumber(L, (lua_Number)sz);
  return 1;
}

// ── uni.lcd.* additions ───────────────────────────────────────────────

int LuaEngine::_lcd_fillScreen(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  uint32_t c = (uint32_t)luaL_checknumber(L, 1);
  Uni.Lcd.fillRect(eng->_bx, eng->_by, eng->_bw, eng->_bh, c);
  return 0;
}

int LuaEngine::_lcd_textWidth(lua_State* L) {
  const char* s = luaL_checkstring(L, 1);
  lua_pushnumber(L, (lua_Number)Uni.Lcd.textWidth(s));
  return 1;
}

int LuaEngine::_lcd_circle(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  uint32_t c = (uint32_t)luaL_checknumber(L, 4);
  Uni.Lcd.drawCircle(eng->_bx + x, eng->_by + y, r, c);
  return 0;
}

int LuaEngine::_lcd_fillCircle(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  uint32_t c = (uint32_t)luaL_checknumber(L, 4);
  Uni.Lcd.fillCircle(eng->_bx + x, eng->_by + y, r, c);
  return 0;
}

int LuaEngine::_lcd_roundRect(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  uint32_t c = (uint32_t)luaL_checknumber(L, 6);
  Uni.Lcd.drawRoundRect(eng->_bx + x, eng->_by + y, w, h, r, c);
  return 0;
}

int LuaEngine::_lcd_fillRoundRect(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  uint32_t c = (uint32_t)luaL_checknumber(L, 6);
  Uni.Lcd.fillRoundRect(eng->_bx + x, eng->_by + y, w, h, r, c);
  return 0;
}

// ── uni.lcd.sprite (off-screen buffer for flicker-free composing) ─────

namespace {
  // Userdata payload for sprite handles — held by Lua GC.
  struct SpriteHandle { Sprite* sp; };
  constexpr const char* kSpriteMeta = "uni.lcd.sprite";

  SpriteHandle* _checkSpriteHandle(lua_State* L, int idx) {
    return (SpriteHandle*)luaL_checkudata(L, idx, kSpriteMeta);
  }
  Sprite* _checkSprite(lua_State* L, int idx) {
    SpriteHandle* h = _checkSpriteHandle(L, idx);
    if (!h->sp) luaL_error(L, "sprite has been freed");
    return h->sp;
  }
}

void LuaEngine::_setupSpriteMetatable() {
  // Registers the sprite metatable under kSpriteMeta in the registry once per
  // engine init. Each lua_close() drops it; init() re-creates next run.
  luaL_newmetatable(_lua, kSpriteMeta);
  lua_pushcfunction(_lua, _sprite_gc); lua_setfield(_lua, -2, "__gc");

  lua_newtable(_lua);
  lua_pushcfunction(_lua, _sprite_push);          lua_setfield(_lua, -2, "push");
  lua_pushcfunction(_lua, _sprite_fill);          lua_setfield(_lua, -2, "fill");
  lua_pushcfunction(_lua, _sprite_rect);          lua_setfield(_lua, -2, "rect");
  lua_pushcfunction(_lua, _sprite_line);          lua_setfield(_lua, -2, "line");
  lua_pushcfunction(_lua, _sprite_circle);        lua_setfield(_lua, -2, "circle");
  lua_pushcfunction(_lua, _sprite_fillCircle);    lua_setfield(_lua, -2, "fillCircle");
  lua_pushcfunction(_lua, _sprite_roundRect);     lua_setfield(_lua, -2, "roundRect");
  lua_pushcfunction(_lua, _sprite_fillRoundRect); lua_setfield(_lua, -2, "fillRoundRect");
  lua_pushcfunction(_lua, _sprite_print);         lua_setfield(_lua, -2, "print");
  lua_pushcfunction(_lua, _sprite_textColor);     lua_setfield(_lua, -2, "textColor");
  lua_pushcfunction(_lua, _sprite_textSize);      lua_setfield(_lua, -2, "textSize");
  lua_pushcfunction(_lua, _sprite_textDatum);     lua_setfield(_lua, -2, "textDatum");
  lua_pushcfunction(_lua, _sprite_textWidth);     lua_setfield(_lua, -2, "textWidth");
  lua_pushcfunction(_lua, _sprite_w);             lua_setfield(_lua, -2, "w");
  lua_pushcfunction(_lua, _sprite_h);             lua_setfield(_lua, -2, "h");
  lua_pushcfunction(_lua, _sprite_free);          lua_setfield(_lua, -2, "free");
  lua_setfield(_lua, -2, "__index");

  lua_pop(_lua, 1);
}

int LuaEngine::_lcd_sprite(lua_State* L) {
  int w = (int)luaL_checknumber(L, 1);
  int h = (int)luaL_checknumber(L, 2);
  if (w <= 0 || h <= 0) { lua_pushnil(L); return 1; }

  Sprite* sp = new Sprite(&Uni.Lcd);
  if (!sp) { lua_pushnil(L); return 1; }
  if (!sp->createSprite(w, h)) {
    delete sp;
    lua_pushnil(L);
    return 1;
  }
  sp->setTextSize(1);
  sp->setTextDatum(TL_DATUM);

  auto* handle = (SpriteHandle*)lua_newuserdata(L, sizeof(SpriteHandle));
  handle->sp = sp;
  luaL_getmetatable(L, kSpriteMeta);
  lua_setmetatable(L, -2);
  return 1;
}

int LuaEngine::_sprite_gc(lua_State* L) {
  auto* h = _checkSpriteHandle(L, 1);
  if (h->sp) {
    h->sp->deleteSprite();
    delete h->sp;
    h->sp = nullptr;
  }
  return 0;
}

int LuaEngine::_sprite_free(lua_State* L) {
  auto* h = _checkSpriteHandle(L, 1);
  if (h->sp) {
    h->sp->deleteSprite();
    delete h->sp;
    h->sp = nullptr;
  }
  return 0;
}

int LuaEngine::_sprite_push(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int bx = eng ? eng->_bx : 0;
  int by = eng ? eng->_by : 0;
  if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
    uint32_t transp = (uint32_t)luaL_checknumber(L, 4);
    sp->pushSprite(bx + x, by + y, transp);
  } else {
    sp->pushSprite(bx + x, by + y);
  }
  return 0;
}

int LuaEngine::_sprite_fill(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  uint32_t c = (uint32_t)luaL_checknumber(L, 2);
  sp->fillSprite(c);
  return 0;
}

int LuaEngine::_sprite_rect(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int w = (int)luaL_checknumber(L, 4);
  int h = (int)luaL_checknumber(L, 5);
  uint32_t c = (uint32_t)luaL_checknumber(L, 6);
  sp->fillRect(x, y, w, h, c);
  return 0;
}

int LuaEngine::_sprite_line(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x0 = (int)luaL_checknumber(L, 2);
  int y0 = (int)luaL_checknumber(L, 3);
  int x1 = (int)luaL_checknumber(L, 4);
  int y1 = (int)luaL_checknumber(L, 5);
  uint32_t c = (uint32_t)luaL_checknumber(L, 6);
  sp->drawLine(x0, y0, x1, y1, c);
  return 0;
}

int LuaEngine::_sprite_circle(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int r = (int)luaL_checknumber(L, 4);
  uint32_t c = (uint32_t)luaL_checknumber(L, 5);
  sp->drawCircle(x, y, r, c);
  return 0;
}

int LuaEngine::_sprite_fillCircle(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int r = (int)luaL_checknumber(L, 4);
  uint32_t c = (uint32_t)luaL_checknumber(L, 5);
  sp->fillCircle(x, y, r, c);
  return 0;
}

int LuaEngine::_sprite_roundRect(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int w = (int)luaL_checknumber(L, 4);
  int h = (int)luaL_checknumber(L, 5);
  int r = (int)luaL_checknumber(L, 6);
  uint32_t c = (uint32_t)luaL_checknumber(L, 7);
  sp->drawRoundRect(x, y, w, h, r, c);
  return 0;
}

int LuaEngine::_sprite_fillRoundRect(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x = (int)luaL_checknumber(L, 2);
  int y = (int)luaL_checknumber(L, 3);
  int w = (int)luaL_checknumber(L, 4);
  int h = (int)luaL_checknumber(L, 5);
  int r = (int)luaL_checknumber(L, 6);
  uint32_t c = (uint32_t)luaL_checknumber(L, 7);
  sp->fillRoundRect(x, y, w, h, r, c);
  return 0;
}

int LuaEngine::_sprite_print(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  int x         = (int)luaL_checknumber(L, 2);
  int y         = (int)luaL_checknumber(L, 3);
  const char* s = luaL_checkstring(L, 4);
  sp->drawString(s, x, y);
  return 0;
}

int LuaEngine::_sprite_textColor(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  uint32_t c = (uint32_t)luaL_checknumber(L, 2);
  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    uint32_t bg = (uint32_t)luaL_checknumber(L, 3);
    sp->setTextColor(c, bg);
  } else {
    sp->setTextColor(c);
  }
  return 0;
}

int LuaEngine::_sprite_textSize(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  sp->setTextSize((uint8_t)luaL_checknumber(L, 2));
  return 0;
}

int LuaEngine::_sprite_textDatum(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  sp->setTextDatum((uint8_t)luaL_checknumber(L, 2));
  return 0;
}

int LuaEngine::_sprite_textWidth(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  const char* s = luaL_checkstring(L, 2);
  lua_pushnumber(L, (lua_Number)sp->textWidth(s));
  return 1;
}

int LuaEngine::_sprite_w(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  lua_pushnumber(L, (lua_Number)sp->width());
  return 1;
}

int LuaEngine::_sprite_h(lua_State* L) {
  Sprite* sp = _checkSprite(L, 1);
  lua_pushnumber(L, (lua_Number)sp->height());
  return 1;
}

// ── uni.nav.* ─────────────────────────────────────────────────────────

int LuaEngine::_nav_btn(lua_State* L) {
  if (!Uni.Nav || !Uni.Nav->wasPressed()) { lua_pushstring(L, "none"); return 1; }
  switch (Uni.Nav->readDirection()) {
    case INavigation::DIR_UP:    lua_pushstring(L, "up");    break;
    case INavigation::DIR_DOWN:  lua_pushstring(L, "down");  break;
    case INavigation::DIR_LEFT:  lua_pushstring(L, "left");  break;
    case INavigation::DIR_RIGHT: lua_pushstring(L, "right"); break;
    case INavigation::DIR_PRESS: lua_pushstring(L, "ok");    break;
    case INavigation::DIR_BACK:  lua_pushstring(L, "back");  break;
    default:                     lua_pushstring(L, "none");  break;
  }
  return 1;
}

int LuaEngine::_nav_touchX(lua_State* L) {
  lua_pushnumber(L, Uni.Nav ? (lua_Number)Uni.Nav->lastTouchX() : -1);
  return 1;
}

int LuaEngine::_nav_touchY(lua_State* L) {
  lua_pushnumber(L, Uni.Nav ? (lua_Number)Uni.Nav->lastTouchY() : -1);
  return 1;
}

int LuaEngine::_nav_isTouched(lua_State* L) {
  lua_pushboolean(L, (Uni.Nav && Uni.Nav->isPressed()) ? 1 : 0);
  return 1;
}

// ── Lazy loaders for tier-2 modules ───────────────────────────────────

int LuaEngine::_lua_load_input(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _input_text);   lua_setfield(L, -2, "text");
  lua_pushcfunction(L, _input_number); lua_setfield(L, -2, "number");
  lua_pushcfunction(L, _input_hex);    lua_setfield(L, -2, "hex");
  lua_pushcfunction(L, _input_ip);     lua_setfield(L, -2, "ip");
  return 1;
}

int LuaEngine::_lua_load_dialog(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _dialog_confirm); lua_setfield(L, -2, "confirm");
  lua_pushcfunction(L, _dialog_select);  lua_setfield(L, -2, "select");
  return 1;
}

int LuaEngine::_lua_load_notify(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _notify_show); lua_setfield(L, -2, "show");
  return 1;
}

int LuaEngine::_lua_load_json(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _json_encode); lua_setfield(L, -2, "encode");
  lua_pushcfunction(L, _json_decode); lua_setfield(L, -2, "decode");
  return 1;
}

int LuaEngine::_lua_load_path(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _path_join);     lua_setfield(L, -2, "join");
  lua_pushcfunction(L, _path_basename); lua_setfield(L, -2, "basename");
  lua_pushcfunction(L, _path_dirname);  lua_setfield(L, -2, "dirname");
  lua_pushcfunction(L, _path_ext);      lua_setfield(L, -2, "ext");
  return 1;
}

int LuaEngine::_lua_load_time(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _time_now); lua_setfield(L, -2, "now");
  return 1;
}

int LuaEngine::_lua_load_config(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _config_get); lua_setfield(L, -2, "get");
  return 1;
}

// ── Popup bridge ──────────────────────────────────────────────────────
//
// Lua task fills a request slot on the engine and waits. The loop task drains
// it via servicePendingPopup() — calling popup() on the loop task is the only
// way to keep the firmware action's internal Uni.update() / Uni.Lcd flow on
// the same task that already drives them. From the Lua task we'd race the
// M5Unified mutexes and assert.

int LuaEngine::_runPopupAndPushResult(lua_State* L, bool stringResult) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  // Spin until the loop task clears the slot. Honour exit so a script kill
  // doesn't strand the Lua task forever inside a popup wait.
  while (eng->_popupType != POPUP_NONE) {
    if (eng->_exitRequested) {
      eng->_popupType      = POPUP_NONE;
      eng->_popupCancelled = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  if (eng->_popupCancelled) {
    lua_pushnil(L);
  } else if (stringResult) {
    lua_pushstring(L, eng->_popupResultStr.c_str());
  } else {
    lua_pushnumber(L, (lua_Number)eng->_popupResultInt);
  }
  return 1;
}

void LuaEngine::servicePendingPopup() {
  if (_popupType == POPUP_NONE) return;
  PopupType type = _popupType;

  switch (type) {
    case POPUP_TEXT: {
      String r = InputTextAction::popup(_popupTitle.c_str(), _popupDefaultStr,
                                        InputTextAction::INPUT_TEXT);
      _popupCancelled = InputTextAction::wasCancelled();
      _popupResultStr = _popupCancelled ? String("") : r;
      break;
    }
    case POPUP_HEX: {
      String r = InputTextAction::popup(_popupTitle.c_str(), _popupDefaultStr,
                                        InputTextAction::INPUT_HEX);
      _popupCancelled = InputTextAction::wasCancelled();
      _popupResultStr = _popupCancelled ? String("") : r;
      break;
    }
    case POPUP_IP: {
      String r = InputTextAction::popup(_popupTitle.c_str(), _popupDefaultStr,
                                        InputTextAction::INPUT_IP_ADDRESS);
      _popupCancelled = InputTextAction::wasCancelled();
      _popupResultStr = _popupCancelled ? String("") : r;
      break;
    }
    case POPUP_NUMBER: {
      int r = InputNumberAction::popup(_popupTitle.c_str(), _popupMin, _popupMax,
                                       _popupDefaultInt);
      _popupCancelled = InputNumberAction::wasCancelled();
      _popupResultInt = r;
      break;
    }
    case POPUP_CONFIRM: {
      static constexpr InputSelectAction::Option opts[] = {
        {"No",  "no"},
        {"Yes", "yes"},
      };
      const char* sel = InputSelectAction::popup(_popupTitle.c_str(), opts, 2, "no");
      _popupCancelled = (sel == nullptr);
      _popupResultStr = (sel && strcmp(sel, "yes") == 0) ? "yes" : "no";
      break;
    }
    case POPUP_SELECT: {
      InputSelectAction::Option opts[kMaxSelectOptions];
      int n = _popupOptionCount;
      if (n > kMaxSelectOptions) n = kMaxSelectOptions;
      for (int i = 0; i < n; i++) {
        opts[i].label = _popupOptions[i].c_str();
        opts[i].value = _popupOptions[i].c_str();
      }
      const char* sel = InputSelectAction::popup(_popupTitle.c_str(), opts, n);
      _popupCancelled = (sel == nullptr);
      _popupResultStr = sel ? String(sel) : String("");
      break;
    }
    default: break;
  }

  _popupType = POPUP_NONE;   // releases the Lua task's spin-wait
}

// ── uni.input.* ───────────────────────────────────────────────────────

int LuaEngine::_input_text(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  eng->_popupTitle      = luaL_checkstring(L, 1);
  eng->_popupDefaultStr = luaL_optstring(L, 2, "");
  eng->_popupCancelled  = false;
  eng->_popupResultStr  = "";
  eng->_popupType       = POPUP_TEXT;
  return _runPopupAndPushResult(L, /*stringResult=*/true);
}

int LuaEngine::_input_hex(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  eng->_popupTitle      = luaL_checkstring(L, 1);
  eng->_popupDefaultStr = luaL_optstring(L, 2, "");
  eng->_popupCancelled  = false;
  eng->_popupResultStr  = "";
  eng->_popupType       = POPUP_HEX;
  return _runPopupAndPushResult(L, true);
}

int LuaEngine::_input_ip(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  eng->_popupTitle      = luaL_checkstring(L, 1);
  eng->_popupDefaultStr = luaL_optstring(L, 2, "");
  eng->_popupCancelled  = false;
  eng->_popupResultStr  = "";
  eng->_popupType       = POPUP_IP;
  return _runPopupAndPushResult(L, true);
}

int LuaEngine::_input_number(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  eng->_popupTitle      = luaL_checkstring(L, 1);
  eng->_popupMin        = (int)luaL_optnumber(L, 2, INT_MIN);
  eng->_popupMax        = (int)luaL_optnumber(L, 3, INT_MAX);
  eng->_popupDefaultInt = (int)luaL_optnumber(L, 4, 0);
  eng->_popupCancelled  = false;
  eng->_popupResultInt  = 0;
  eng->_popupType       = POPUP_NUMBER;
  return _runPopupAndPushResult(L, /*stringResult=*/false);
}

// ── uni.dialog.* ──────────────────────────────────────────────────────

int LuaEngine::_dialog_confirm(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushboolean(L, 0); return 1; }
  eng->_popupTitle     = luaL_checkstring(L, 1);
  eng->_popupCancelled = false;
  eng->_popupResultStr = "";
  eng->_popupType      = POPUP_CONFIRM;
  while (eng->_popupType != POPUP_NONE) {
    if (eng->_exitRequested) {
      eng->_popupType      = POPUP_NONE;
      eng->_popupCancelled = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  // Confirm returns boolean: true = yes, false = no/cancelled.
  bool yes = !eng->_popupCancelled && eng->_popupResultStr == "yes";
  lua_pushboolean(L, yes ? 1 : 0);
  return 1;
}

int LuaEngine::_dialog_select(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) { lua_pushnil(L); return 1; }
  eng->_popupTitle = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  int n = (int)lua_objlen(L, 2);
  if (n > kMaxSelectOptions) n = kMaxSelectOptions;
  eng->_popupOptionCount = n;
  for (int i = 0; i < n; i++) {
    lua_rawgeti(L, 2, i + 1);
    eng->_popupOptions[i] = luaL_checkstring(L, -1);
    lua_pop(L, 1);
  }
  eng->_popupCancelled = false;
  eng->_popupResultStr = "";
  eng->_popupType      = POPUP_SELECT;
  return _runPopupAndPushResult(L, /*stringResult=*/true);
}

// ── uni.notify.* ──────────────────────────────────────────────────────
//
// Runs entirely on the Lua task: ShowStatusAction with positive duration just
// paints + sleeps + wipes (no Uni.update / Nav reads), so it's safe to call
// here without going through the popup bridge.

int LuaEngine::_notify_show(lua_State* L) {
  const char* msg = luaL_checkstring(L, 1);
  int ms          = (int)luaL_optnumber(L, 2, 800);
  if (ms < 0) ms = 0;
  ShowStatusAction::show(msg, ms > 0 ? ms : 0);
  return 0;
}

// ── uni.json.* ────────────────────────────────────────────────────────
//
// Recursive cJSON ↔ Lua marshalling. Tables with a sequential 1..N integer
// keyset map to JSON arrays; everything else maps to JSON objects.

static void _cjsonToLua(lua_State* L, const cJSON* node);
static cJSON* _luaToCjson(lua_State* L, int idx);

static void _cjsonToLua(lua_State* L, const cJSON* node) {
  if (!node || cJSON_IsNull(node)) { lua_pushnil(L); return; }
  if (cJSON_IsBool(node))   { lua_pushboolean(L, cJSON_IsTrue(node) ? 1 : 0); return; }
  if (cJSON_IsNumber(node)) { lua_pushnumber(L, node->valuedouble); return; }
  if (cJSON_IsString(node)) { lua_pushstring(L, node->valuestring ? node->valuestring : ""); return; }
  if (cJSON_IsArray(node)) {
    lua_newtable(L);
    int i = 1;
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, node) {
      _cjsonToLua(L, it);
      lua_rawseti(L, -2, i++);
    }
    return;
  }
  if (cJSON_IsObject(node)) {
    lua_newtable(L);
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, node) {
      _cjsonToLua(L, it);
      lua_setfield(L, -2, it->string ? it->string : "");
    }
    return;
  }
  lua_pushnil(L);
}

static bool _luaTableIsArray(lua_State* L, int idx, int* lenOut) {
  // 1..N integer keys with no holes → array. Empty tables encode as objects
  // (`{}` round-trips that way; arrays should have an explicit element).
  size_t n = lua_objlen(L, idx);
  *lenOut = (int)n;
  if (n == 0) return false;
  for (size_t i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, (int)i);
    bool ok = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!ok) return false;
  }
  return true;
}

static cJSON* _luaToCjson(lua_State* L, int idx) {
  int t = lua_type(L, idx);
  switch (t) {
    case LUA_TNIL:     return cJSON_CreateNull();
    case LUA_TBOOLEAN: return cJSON_CreateBool(lua_toboolean(L, idx) ? 1 : 0);
    case LUA_TNUMBER:  return cJSON_CreateNumber(lua_tonumber(L, idx));
    case LUA_TSTRING:  return cJSON_CreateString(lua_tostring(L, idx));
    case LUA_TTABLE: {
      int absIdx = idx > 0 ? idx : lua_gettop(L) + idx + 1;
      int n = 0;
      if (_luaTableIsArray(L, absIdx, &n)) {
        cJSON* arr = cJSON_CreateArray();
        for (int i = 1; i <= n; i++) {
          lua_rawgeti(L, absIdx, i);
          cJSON* child = _luaToCjson(L, -1);
          if (child) cJSON_AddItemToArray(arr, child);
          lua_pop(L, 1);
        }
        return arr;
      }
      cJSON* obj = cJSON_CreateObject();
      lua_pushnil(L);
      while (lua_next(L, absIdx) != 0) {
        // key at -2, value at -1; cJSON keys must be strings.
        if (lua_type(L, -2) == LUA_TSTRING) {
          const char* key = lua_tostring(L, -2);
          cJSON* child = _luaToCjson(L, -1);
          if (child) cJSON_AddItemToObject(obj, key, child);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
          char buf[32];
          snprintf(buf, sizeof(buf), "%g", lua_tonumber(L, -2));
          cJSON* child = _luaToCjson(L, -1);
          if (child) cJSON_AddItemToObject(obj, buf, child);
        }
        lua_pop(L, 1);
      }
      return obj;
    }
    default: return cJSON_CreateNull();
  }
}

int LuaEngine::_json_decode(lua_State* L) {
  const char* str = luaL_checkstring(L, 1);
  cJSON* root = cJSON_Parse(str);
  if (!root) { lua_pushnil(L); return 1; }
  _cjsonToLua(L, root);
  cJSON_Delete(root);
  return 1;
}

int LuaEngine::_json_encode(lua_State* L) {
  cJSON* root = _luaToCjson(L, 1);
  if (!root) { lua_pushnil(L); return 1; }
  char* str = cJSON_PrintUnformatted(root);
  if (!str) { cJSON_Delete(root); lua_pushnil(L); return 1; }
  lua_pushstring(L, str);
  cJSON_free(str);
  cJSON_Delete(root);
  return 1;
}

// ── uni.path.* ────────────────────────────────────────────────────────

int LuaEngine::_path_join(lua_State* L) {
  String out = luaL_checkstring(L, 1);
  int n = lua_gettop(L);
  for (int i = 2; i <= n; i++) {
    const char* part = luaL_checkstring(L, i);
    if (!*part) continue;
    if (out.length() > 0 && out[out.length() - 1] != '/' && part[0] != '/') {
      out += '/';
    } else if (out.length() > 0 && out[out.length() - 1] == '/' && part[0] == '/') {
      out += (part + 1);
      continue;
    }
    out += part;
  }
  lua_pushstring(L, out.c_str());
  return 1;
}

int LuaEngine::_path_basename(lua_State* L) {
  const char* p = luaL_checkstring(L, 1);
  const char* slash = strrchr(p, '/');
  lua_pushstring(L, slash ? slash + 1 : p);
  return 1;
}

int LuaEngine::_path_dirname(lua_State* L) {
  const char* p = luaL_checkstring(L, 1);
  const char* slash = strrchr(p, '/');
  if (!slash) { lua_pushstring(L, ""); return 1; }
  if (slash == p) { lua_pushstring(L, "/"); return 1; }
  lua_pushlstring(L, p, (size_t)(slash - p));
  return 1;
}

int LuaEngine::_path_ext(lua_State* L) {
  const char* p = luaL_checkstring(L, 1);
  const char* slash = strrchr(p, '/');
  const char* base  = slash ? slash + 1 : p;
  const char* dot   = strrchr(base, '.');
  if (!dot || dot == base) { lua_pushstring(L, ""); return 1; }
  lua_pushstring(L, dot + 1);
  return 1;
}

// ── uni.time.* ────────────────────────────────────────────────────────

int LuaEngine::_time_now(lua_State* L) {
  time_t now = time(nullptr);
  struct tm tmv = {};
  // localtime_r can return NULL on uninitialised tz data (early boot) — leave
  // tmv zeroed in that case so the script gets 1900-01-01 instead of garbage.
  localtime_r(&now, &tmv);
  lua_newtable(L);
  lua_pushnumber(L, tmv.tm_year + 1900); lua_setfield(L, -2, "year");
  lua_pushnumber(L, tmv.tm_mon + 1);     lua_setfield(L, -2, "month");
  lua_pushnumber(L, tmv.tm_mday);        lua_setfield(L, -2, "day");
  lua_pushnumber(L, tmv.tm_hour);        lua_setfield(L, -2, "hour");
  lua_pushnumber(L, tmv.tm_min);         lua_setfield(L, -2, "min");
  lua_pushnumber(L, tmv.tm_sec);         lua_setfield(L, -2, "sec");
  lua_pushnumber(L, tmv.tm_wday);        lua_setfield(L, -2, "wday");
  lua_pushnumber(L, (lua_Number)now);    lua_setfield(L, -2, "epoch");
  return 1;
}

// ── uni.config.* ──────────────────────────────────────────────────────
//
// Read-only access to ConfigManager values. Returns the raw stored string for
// most keys; "theme_color" returns the resolved RGB565 number so scripts can
// match the device theme without re-parsing the colour name.

int LuaEngine::_config_get(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  if (strcmp(key, "theme_color") == 0) {
    lua_pushnumber(L, (lua_Number)Config.getThemeColor());
    return 1;
  }
  String val = Config.get(key, "");
  lua_pushstring(L, val.c_str());
  return 1;
}
