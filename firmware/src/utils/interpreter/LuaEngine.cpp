#include "utils/interpreter/LuaEngine.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ConfigManager.h"
#include "core/PinConfigManager.h"
#include "core/RandomSeed.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/rf/CC1101Util.h"
#include "utils/rf/M5RF433Util.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <cJSON.h>
#include <stdlib.h>
#include <math.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Mirror M5RF433Screen.cpp's pin-fallback logic for the GPIO bit-bang backend.
// Per-board pins_arduino.h is auto-included by PlatformIO via the variant.
#if defined(M5RF433_TX_PIN)
  static constexpr int8_t kLuaRf433DefaultTx = (int8_t)M5RF433_TX_PIN;
#elif defined(GROVE_SDA)
  static constexpr int8_t kLuaRf433DefaultTx = (int8_t)GROVE_SDA;
#else
  static constexpr int8_t kLuaRf433DefaultTx = -1;
#endif
#if defined(M5RF433_RX_PIN)
  static constexpr int8_t kLuaRf433DefaultRx = (int8_t)M5RF433_RX_PIN;
#elif defined(GROVE_SCL)
  static constexpr int8_t kLuaRf433DefaultRx = (int8_t)GROVE_SCL;
#else
  static constexpr int8_t kLuaRf433DefaultRx = -1;
#endif

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

  // Reseed both Arduino's random() and Lua's math.random() before the script
  // starts so naïve scripts that skip math.randomseed still get a fresh
  // sequence every run. Hardware entropy via esp_random() (mixed with MAC,
  // RTC, micros, persisted rolling chain) lives in RandomSeed::reseed().
  uint32_t s = RandomSeed::reseed();
  srand(s);

  // Replace Lua's stock math.randomseed with our binding so a script's seed
  // is folded into the same RandomSeed chain as the rest of the firmware.
  lua_getglobal(_lua, "math");
  if (lua_istable(_lua, -1)) {
    lua_pushcfunction(_lua, _math_randomseed);
    lua_setfield(_lua, -2, "randomseed");
  }
  lua_pop(_lua, 1);

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
  // Tear down WiFi if the script was the one that brought it up, and clear
  // any HTTP transport state so the next script starts on a clean network.
  _cleanupNetwork();
  // Release the radio if the script opened uni.subghz — leaving SPI or the
  // RX ISR live across scripts would race the next launch.
  _cleanupSubghz();
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

// Lua execution lives on its own FreeRTOS task. Stack size is board-conditional:
//   - PSRAM boards (cores3, sticks3): 32 KB — comfortable headroom for the
//     recursive parser + deep call chains; plenty of internal SRAM to spare.
//   - No-PSRAM boards (cardputer, stickc plus, t-display, …): 8 KB — keeps
//     the bulk of internal SRAM available for WiFi DMA. WARNING:
//     luaL_loadbuffer is a recursive-descent parser; large scripts
//     (≳ 8 KB source) may overflow this stack at compile time.
// The first stepLoop() spawns the task; later calls poll its status. The task
// self-deletes after pcall returns.
void LuaEngine::_taskEntry(void* arg) {
  LuaEngine* eng = (LuaEngine*)arg;
  lua_State* L   = eng->_lua;

  // Compile on this task — the parser is recursive. On no-PSRAM boards the
  // 8 KB stack is tight; large scripts may overflow here.
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
#ifdef BOARD_HAS_PSRAM
    constexpr size_t kLuaTaskStack = 32 * 1024;
#else
    constexpr size_t kLuaTaskStack = 8 * 1024;
#endif
    BaseType_t ok = xTaskCreatePinnedToCore(
      _taskEntry, "lua", kLuaTaskStack, this, 1, &handle, 1);
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
  lua_pushcfunction(_lua, _lua_load_wifi);   lua_setfield(_lua, -2, "uni.wifi");
  lua_pushcfunction(_lua, _lua_load_http);   lua_setfield(_lua, -2, "uni.http");
  lua_pushcfunction(_lua, _lua_load_subghz); lua_setfield(_lua, -2, "uni.subghz");
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

int LuaEngine::_math_randomseed(lua_State* L) {
  // Lua 5.1 calls math.randomseed with a single numeric arg. Fold it into the
  // RandomSeed chain (hardware RNG + rolling chain + MAC + time + caller seed)
  // and apply the result to both Arduino's random() and Lua's math.random().
  uint32_t userSeed = (uint32_t)luaL_checknumber(L, 1);
  uint32_t mixed    = RandomSeed::reseed(userSeed);
  srand(mixed);
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

// ── uni.wifi / uni.http ───────────────────────────────────────────────
//
// The script is responsible for asking. We only disconnect on exit when
// wifi.connect() was the one that actually brought the radio up — if the
// user had WiFi connected before launching the script (Web File Manager,
// Wardrive, …) we leave it alone.
//
// HTTP doesn't keep persistent state between calls. Each request gets a
// fresh local WiFiClientSecure + HTTPClient and the response body is read
// into a Lua string, capped at kHttpMaxBody bytes to keep a stray URL from
// OOM'ing the VM.

int LuaEngine::_lua_load_wifi(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _wifi_status);     lua_setfield(L, -2, "status");
  lua_pushcfunction(L, _wifi_ssid);       lua_setfield(L, -2, "ssid");
  lua_pushcfunction(L, _wifi_ip);         lua_setfield(L, -2, "ip");
  lua_pushcfunction(L, _wifi_connect);    lua_setfield(L, -2, "connect");
  lua_pushcfunction(L, _wifi_disconnect); lua_setfield(L, -2, "disconnect");
  return 1;
}

int LuaEngine::_lua_load_http(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _http_get);  lua_setfield(L, -2, "get");
  lua_pushcfunction(L, _http_post); lua_setfield(L, -2, "post");
  return 1;
}

int LuaEngine::_wifi_status(lua_State* L) {
  const char* s;
  switch (WiFi.status()) {
    case WL_CONNECTED:       s = "connected";    break;
    case WL_IDLE_STATUS:     s = "connecting";   break;
    case WL_NO_SSID_AVAIL:   s = "no_ssid";      break;
    case WL_CONNECT_FAILED:  s = "failed";       break;
    case WL_CONNECTION_LOST: s = "lost";         break;
    case WL_DISCONNECTED:    s = "disconnected"; break;
    default:                 s = "off";          break;
  }
  lua_pushstring(L, s);
  return 1;
}

int LuaEngine::_wifi_ssid(lua_State* L) {
  String s = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  lua_pushstring(L, s.c_str());
  return 1;
}

int LuaEngine::_wifi_ip(lua_State* L) {
  String s = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  lua_pushstring(L, s.c_str());
  return 1;
}

int LuaEngine::_wifi_connect(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  const char* ssid     = luaL_checkstring(L, 1);
  const char* pass     = luaL_optstring(L, 2, "");
  uint32_t    timeoutMs = (uint32_t)luaL_optnumber(L, 3, 10000);

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
    lua_pushboolean(L, 1);  // already connected to the requested AP
    return 1;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (eng && eng->_exitRequested) break;
    if (millis() - start >= timeoutMs) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok && eng) eng->_scriptStartedWifi = true;
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int LuaEngine::_wifi_disconnect(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  // WiFi.disconnect() (no args) — disassociates from the AP without calling
  // esp_wifi_deinit(). Passing `true` would deinit the driver, after which a
  // subsequent WiFi.begin() can't re-init under heap pressure ("Expected to
  // init 4 rx buffer, actual is 0"). Re-association is fine; full driver
  // teardown is not.
  WiFi.disconnect();
  if (eng) eng->_scriptStartedWifi = false;
  return 0;
}

static constexpr size_t kHttpMaxBody = 256 * 1024;  // 256 KB cap

static int _http_request(lua_State* L, const char* method, const char* url,
                         const char* body, size_t bodyLen) {
  // No upfront WiFi.status() gate — the Arduino-ESP32 status tracker is
  // event-driven and can lag for a tick right after connect, even though the
  // netif already has an IP. HTTPClient::begin()/GET() surfaces a real
  // transport error if WiFi is genuinely down (we just propagate the code).

  // Compact the Lua VM before the SSL handshake. mbedTLS needs ~30 KB
  // contiguous internal SRAM for buffers + session state; a freshly-loaded
  // VM often leaves only ~20 KB free between script-side garbage. A full GC
  // typically reclaims 5–10 KB — enough to land the handshake.
  lua_gc(L, LUA_GCCOLLECT, 0);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    lua_pushnil(L);
    lua_pushinteger(L, -2);
    lua_pushstring(L, "begin() failed (bad URL?)");
    return 3;
  }
  http.addHeader("User-Agent", "ESP32-UniGeek");

  int code;
  if (strcmp(method, "POST") == 0) {
    code = http.POST((uint8_t*)body, bodyLen);
  } else {
    code = http.GET();
  }

  if (code <= 0) {
    String err = HTTPClient::errorToString(code);
    http.end();
    lua_pushnil(L);
    lua_pushinteger(L, code);
    lua_pushstring(L, err.c_str());
    return 3;
  }

  int len = http.getSize();
  if (len > 0 && (size_t)len > kHttpMaxBody) {
    http.end();
    lua_pushnil(L);
    lua_pushinteger(L, -3);   // too large
    lua_pushstring(L, "response too large");
    return 3;
  }
  String resp = http.getString();
  http.end();

  if (resp.length() > kHttpMaxBody) {
    lua_pushnil(L);
    lua_pushinteger(L, -3);
    lua_pushstring(L, "response too large");
    return 3;
  }

  lua_pushlstring(L, resp.c_str(), resp.length());
  lua_pushinteger(L, code);
  lua_pushstring(L, "");
  return 3;
}

int LuaEngine::_http_get(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  return _http_request(L, "GET", url, nullptr, 0);
}

int LuaEngine::_http_post(lua_State* L) {
  const char* url = luaL_checkstring(L, 1);
  size_t      blen;
  const char* body = luaL_optlstring(L, 2, "", &blen);
  return _http_request(L, "POST", url, body, blen);
}

void LuaEngine::_cleanupNetwork() {
  // Only tear down WiFi if the script itself brought it up — otherwise
  // we'd disconnect the Web File Manager or whatever else is using it.
  // Disassociate without deiniting the driver (see _wifi_disconnect note).
  if (_scriptStartedWifi) {
    WiFi.disconnect();
    _scriptStartedWifi = false;
  }
  // HTTPClient state isn't cached — each call uses a local instance that's
  // destroyed when the binding returns. Nothing else to clean here.
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

// ── uni.subghz ────────────────────────────────────────────────────────
//
// Unified facade over the two RF backends in this firmware:
//   - CC1101Util  — SPI module, variable 280–928 MHz, supports scan
//   - M5RF433Util — Grove bit-bang, fixed 433.92 MHz, no scan
// Backend is picked lazily on the first operation that needs the radio
// (CC1101 preferred when the chip responds; M5 RF433 as fallback). The
// Signal struct is shared between both (M5RF433Util typedefs it), so the
// Lua-side signal table marshals identically for either path. Cleanup is
// driven from deinit() so a crashing script can't strand SPI / RX ISR.

namespace {

void _signalFromLuaTable(lua_State* L, int idx, CC1101Util::Signal& out) {
  int abs = idx > 0 ? idx : lua_gettop(L) + idx + 1;
  lua_getfield(L, abs, "frequency"); if (lua_isnumber(L, -1)) out.frequency = (float)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "preset");    if (lua_isstring(L, -1)) out.preset    = lua_tostring(L, -1);       lua_pop(L, 1);
  lua_getfield(L, abs, "protocol");  if (lua_isstring(L, -1)) out.protocol  = lua_tostring(L, -1);       lua_pop(L, 1);
  lua_getfield(L, abs, "rawData");   if (lua_isstring(L, -1)) out.rawData   = lua_tostring(L, -1);       lua_pop(L, 1);
  lua_getfield(L, abs, "key");       if (lua_isnumber(L, -1)) out.key       = (uint64_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "te");        if (lua_isnumber(L, -1)) out.te        = (int)lua_tonumber(L, -1);  lua_pop(L, 1);
  lua_getfield(L, abs, "bit");       if (lua_isnumber(L, -1)) out.bit       = (int)lua_tonumber(L, -1);  lua_pop(L, 1);
  lua_getfield(L, abs, "mf_name");   if (lua_isstring(L, -1)) out.mf_name   = lua_tostring(L, -1);       lua_pop(L, 1);
  lua_getfield(L, abs, "serial");    if (lua_isnumber(L, -1)) out.serial    = (uint32_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "btn");       if (lua_isnumber(L, -1)) out.btn       = (uint8_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "cnt");       if (lua_isnumber(L, -1)) out.cnt       = (uint16_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "fix");       if (lua_isnumber(L, -1)) out.fix       = (uint32_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "encrypted"); if (lua_isnumber(L, -1)) out.encrypted = (uint32_t)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, abs, "hop");       if (lua_isnumber(L, -1)) out.hop       = (uint32_t)lua_tonumber(L, -1); lua_pop(L, 1);
}

void _signalToLuaTable(lua_State* L, const CC1101Util::Signal& sig) {
  lua_newtable(L);
  lua_pushnumber(L, sig.frequency);         lua_setfield(L, -2, "frequency");
  lua_pushstring(L, sig.preset.c_str());    lua_setfield(L, -2, "preset");
  lua_pushstring(L, sig.protocol.c_str());  lua_setfield(L, -2, "protocol");
  lua_pushstring(L, sig.rawData.c_str());   lua_setfield(L, -2, "rawData");
  // KeeLoq fits in 32 bits; uint64_t key is rarely larger, but lua_Number is
  // double so values above 2^53 lose precision. Acceptable for current decoders.
  lua_pushnumber(L, (lua_Number)sig.key);   lua_setfield(L, -2, "key");
  lua_pushnumber(L, sig.te);                lua_setfield(L, -2, "te");
  lua_pushnumber(L, sig.bit);               lua_setfield(L, -2, "bit");
  lua_pushstring(L, sig.mf_name.c_str());   lua_setfield(L, -2, "mf_name");
  lua_pushnumber(L, sig.serial);            lua_setfield(L, -2, "serial");
  lua_pushnumber(L, sig.btn);               lua_setfield(L, -2, "btn");
  lua_pushnumber(L, sig.cnt);               lua_setfield(L, -2, "cnt");
  lua_pushnumber(L, sig.fix);               lua_setfield(L, -2, "fix");
  lua_pushnumber(L, sig.encrypted);         lua_setfield(L, -2, "encrypted");
  lua_pushnumber(L, sig.hop);               lua_setfield(L, -2, "hop");
}

}  // namespace

bool LuaEngine::_subghzEnsureOpen() {
  if (_subghzBackend != SUBGHZ_NONE) return true;

  // CC1101 first: read pin config (board default flows through PIN_CONFIG_*_DEFAULT).
  // begin() does a SPI chip-ID handshake — false means no chip on the bus.
  int8_t cs   = (int8_t)PinConfig.get(PIN_CONFIG_CC1101_CS,   PIN_CONFIG_CC1101_CS_DEFAULT).toInt();
  int8_t gdo0 = (int8_t)PinConfig.get(PIN_CONFIG_CC1101_GDO0, PIN_CONFIG_CC1101_GDO0_DEFAULT).toInt();
  if (cs >= 0 && gdo0 >= 0 && Uni.Spi != nullptr) {
    if (!_subghzCc) _subghzCc = new CC1101Util();
    if (_subghzCc && _subghzCc->begin(Uni.Spi, cs, gdo0)) {
      _subghzBackend = SUBGHZ_CC1101;
      return true;
    }
    if (_subghzCc) _subghzCc->end();
  }

  // Fallback: M5 RF433 — pin defaults follow M5RF433Screen.cpp (M5RF433_TX_PIN
  // override, else GROVE_SDA/SCL). There's no chip ID to probe, so we only
  // gate on pins being defined.
  if (kLuaRf433DefaultTx >= 0 || kLuaRf433DefaultRx >= 0) {
    if (!_subghzRf) _subghzRf = new M5RF433Util();
    if (_subghzRf && _subghzRf->begin(kLuaRf433DefaultTx, kLuaRf433DefaultRx)) {
      _subghzBackend = SUBGHZ_RF433;
      return true;
    }
  }
  return false;
}

void LuaEngine::_cleanupSubghz() {
  if (_subghzCc) {
    if (_subghzReceiving) _subghzCc->endReceive();
    if (_subghzScanning)  _subghzCc->endScan();
    _subghzCc->end();
    delete _subghzCc;
    _subghzCc = nullptr;
  }
  if (_subghzRf) {
    if (_subghzReceiving) _subghzRf->endReceive();
    if (_subghzJamming)   _subghzRf->stopJam();
    _subghzRf->end();
    delete _subghzRf;
    _subghzRf = nullptr;
  }
  _subghzBackend   = SUBGHZ_NONE;
  _subghzReceiving = false;
  _subghzScanning  = false;
  _subghzJamming   = false;
}

int LuaEngine::_lua_load_subghz(lua_State* L) {
  lua_newtable(L);
  lua_pushcfunction(L, _subghz_info);         lua_setfield(L, -2, "info");
  lua_pushcfunction(L, _subghz_setFrequency); lua_setfield(L, -2, "setFrequency");
  lua_pushcfunction(L, _subghz_getFrequency); lua_setfield(L, -2, "getFrequency");
  lua_pushcfunction(L, _subghz_setRxFilter);  lua_setfield(L, -2, "setRxFilter");
  lua_pushcfunction(L, _subghz_beginReceive); lua_setfield(L, -2, "beginReceive");
  lua_pushcfunction(L, _subghz_pollReceive);  lua_setfield(L, -2, "pollReceive");
  lua_pushcfunction(L, _subghz_endReceive);   lua_setfield(L, -2, "endReceive");
  lua_pushcfunction(L, _subghz_send);         lua_setfield(L, -2, "send");
  lua_pushcfunction(L, _subghz_beginScan);    lua_setfield(L, -2, "beginScan");
  lua_pushcfunction(L, _subghz_stepScan);     lua_setfield(L, -2, "stepScan");
  lua_pushcfunction(L, _subghz_endScan);      lua_setfield(L, -2, "endScan");
  lua_pushcfunction(L, _subghz_getScanFreq);  lua_setfield(L, -2, "getScanFreq");
  lua_pushcfunction(L, _subghz_getScanRssi);  lua_setfield(L, -2, "getScanRssi");
  lua_pushcfunction(L, _subghz_startJam);     lua_setfield(L, -2, "startJam");
  lua_pushcfunction(L, _subghz_jamBurst);     lua_setfield(L, -2, "jamBurst");
  lua_pushcfunction(L, _subghz_stopJam);      lua_setfield(L, -2, "stopJam");
  lua_pushcfunction(L, _subghz_parseSub);     lua_setfield(L, -2, "parseSub");
  lua_pushcfunction(L, _subghz_formatSub);    lua_setfield(L, -2, "formatSub");
  lua_pushcfunction(L, _subghz_close);        lua_setfield(L, -2, "close");
  return 1;
}

int LuaEngine::_subghz_info(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  bool open = eng && eng->_subghzEnsureOpen();
  lua_newtable(L);
  if (!open || !eng) {
    lua_pushnil(L);        lua_setfield(L, -2, "backend");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "canTune");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "canScan");
    return 1;
  }
  if (eng->_subghzBackend == SUBGHZ_CC1101) {
    lua_pushstring(L, "cc1101");                       lua_setfield(L, -2, "backend");
    lua_pushboolean(L, 1);                             lua_setfield(L, -2, "canTune");
    lua_pushboolean(L, 1);                             lua_setfield(L, -2, "canScan");
    lua_pushnumber(L, eng->_subghzCc->getFrequency()); lua_setfield(L, -2, "freq");
  } else {
    lua_pushstring(L, "rf433");                        lua_setfield(L, -2, "backend");
    lua_pushboolean(L, 0);                             lua_setfield(L, -2, "canTune");
    lua_pushboolean(L, 0);                             lua_setfield(L, -2, "canScan");
    lua_pushnumber(L, M5RF433Util::FIXED_FREQ);        lua_setfield(L, -2, "freq");
  }
  return 1;
}

int LuaEngine::_subghz_setFrequency(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  if (eng->_subghzBackend != SUBGHZ_CC1101) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "unsupported on rf433");
    return 2;
  }
  float mhz = (float)luaL_checknumber(L, 1);
  lua_pushboolean(L, eng->_subghzCc->setFrequency(mhz) ? 1 : 0);
  return 1;
}

int LuaEngine::_subghz_getFrequency(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushnumber(L, 0); return 1; }
  if (eng->_subghzBackend == SUBGHZ_CC1101) {
    lua_pushnumber(L, eng->_subghzCc->getFrequency());
  } else {
    lua_pushnumber(L, M5RF433Util::FIXED_FREQ);
  }
  return 1;
}

int LuaEngine::_subghz_setRxFilter(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  const char* mode = luaL_checkstring(L, 1);
  auto f = (strcmp(mode, "raw") == 0) ? CC1101Util::RX_FILTER_RAW
                                      : CC1101Util::RX_FILTER_CODE;
  if (eng->_subghzBackend == SUBGHZ_CC1101)     eng->_subghzCc->setRxFilter(f);
  else if (eng->_subghzBackend == SUBGHZ_RF433) eng->_subghzRf->setRxFilter(f);
  lua_pushboolean(L, 1);
  return 1;
}

int LuaEngine::_subghz_beginReceive(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  bool ok = false;
  if (eng->_subghzBackend == SUBGHZ_CC1101)     ok = eng->_subghzCc->beginReceive();
  else if (eng->_subghzBackend == SUBGHZ_RF433) ok = eng->_subghzRf->beginReceive();
  eng->_subghzReceiving = ok;
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int LuaEngine::_subghz_pollReceive(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || eng->_subghzBackend == SUBGHZ_NONE) { lua_pushnil(L); return 1; }
  CC1101Util::Signal sig;
  bool got = false;
  if (eng->_subghzBackend == SUBGHZ_CC1101)     got = eng->_subghzCc->pollReceive(sig);
  else if (eng->_subghzBackend == SUBGHZ_RF433) got = eng->_subghzRf->pollReceive(sig);
  if (!got) { lua_pushnil(L); return 1; }
  _signalToLuaTable(L, sig);
  return 1;
}

int LuaEngine::_subghz_endReceive(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  if (eng->_subghzBackend == SUBGHZ_CC1101 && eng->_subghzCc) eng->_subghzCc->endReceive();
  if (eng->_subghzBackend == SUBGHZ_RF433  && eng->_subghzRf) eng->_subghzRf->endReceive();
  eng->_subghzReceiving = false;
  return 0;
}

int LuaEngine::_subghz_send(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  luaL_checktype(L, 1, LUA_TTABLE);
  CC1101Util::Signal sig;
  _signalFromLuaTable(L, 1, sig);
  // RX must be torn down while TX runs: every pulse driven on GDO0/TX would
  // otherwise re-fire the receive ISR and corrupt the buffer. Re-arm RX
  // afterwards if the script was listening before send().
  bool wasRx = eng->_subghzReceiving;
  if (eng->_subghzBackend == SUBGHZ_CC1101) {
    if (sig.frequency > 0 &&
        fabsf(sig.frequency - eng->_subghzCc->getFrequency()) > 0.01f) {
      eng->_subghzCc->setFrequency(sig.frequency);
    }
    if (wasRx) eng->_subghzCc->endReceive();
    eng->_subghzCc->sendSignal(sig);
    if (wasRx) eng->_subghzCc->beginReceive();
  } else {
    if (wasRx) eng->_subghzRf->endReceive();
    eng->_subghzRf->sendSignal(sig);
    if (wasRx) eng->_subghzRf->beginReceive();
  }
  lua_pushboolean(L, 1);
  return 1;
}

int LuaEngine::_subghz_beginScan(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  if (eng->_subghzBackend != SUBGHZ_CC1101) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "unsupported on rf433");
    return 2;
  }
  bool ok = eng->_subghzCc->beginScan();
  eng->_subghzScanning = ok;
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int LuaEngine::_subghz_stepScan(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || eng->_subghzBackend != SUBGHZ_CC1101 || !eng->_subghzCc) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, eng->_subghzCc->stepScan() ? 1 : 0);
  return 1;
}

int LuaEngine::_subghz_endScan(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (eng && eng->_subghzBackend == SUBGHZ_CC1101 && eng->_subghzCc) {
    eng->_subghzCc->endScan();
  }
  if (eng) eng->_subghzScanning = false;
  return 0;
}

int LuaEngine::_subghz_getScanFreq(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (eng && eng->_subghzBackend == SUBGHZ_CC1101 && eng->_subghzCc) {
    lua_pushnumber(L, eng->_subghzCc->getScanFreq());
  } else {
    lua_pushnumber(L, 0);
  }
  return 1;
}

int LuaEngine::_subghz_getScanRssi(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (eng && eng->_subghzBackend == SUBGHZ_CC1101 && eng->_subghzCc) {
    lua_pushnumber(L, eng->_subghzCc->getScanRssi());
  } else {
    lua_pushnumber(L, -120);
  }
  return 1;
}

int LuaEngine::_subghz_startJam(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzEnsureOpen()) { lua_pushboolean(L, 0); return 1; }
  if (eng->_subghzBackend == SUBGHZ_CC1101) {
    eng->_subghzCc->startTx();
  } else {
    eng->_subghzRf->startJam();
  }
  eng->_subghzJamming = true;
  lua_pushboolean(L, 1);
  return 1;
}

int LuaEngine::_subghz_jamBurst(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng || !eng->_subghzJamming) { lua_pushboolean(L, 0); return 1; }
  if (eng->_subghzBackend == SUBGHZ_RF433 && eng->_subghzRf) {
    eng->_subghzRf->jamBurst();
  } else if (eng->_subghzBackend == SUBGHZ_CC1101) {
    // Mirror SubGHzScreen::_radioJamBurst — CC1101Util parks the chip in TX
    // mode via startTx(); the caller pulses GDO0 directly to drive the carrier.
    int8_t gdo0 = (int8_t)PinConfig.get(PIN_CONFIG_CC1101_GDO0,
                                        PIN_CONFIG_CC1101_GDO0_DEFAULT).toInt();
    if (gdo0 < 0) { lua_pushboolean(L, 0); return 1; }
    for (int i = 0; i < 50; i++) {
      uint32_t pw  = 5 + (micros() % 46);
      uint32_t gap = 5 + (micros() % 96);
      digitalWrite(gdo0, HIGH); delayMicroseconds(pw);
      digitalWrite(gdo0, LOW);  delayMicroseconds(gap);
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

int LuaEngine::_subghz_stopJam(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (!eng) return 0;
  if (eng->_subghzBackend == SUBGHZ_CC1101 && eng->_subghzCc) {
    int8_t gdo0 = (int8_t)PinConfig.get(PIN_CONFIG_CC1101_GDO0,
                                        PIN_CONFIG_CC1101_GDO0_DEFAULT).toInt();
    if (gdo0 >= 0) digitalWrite(gdo0, LOW);
  } else if (eng->_subghzBackend == SUBGHZ_RF433 && eng->_subghzRf) {
    eng->_subghzRf->stopJam();
  }
  eng->_subghzJamming = false;
  // Match SubGHzScreen::_radioStopJam — full teardown so the chip starts
  // each subsequent op from a clean state.
  eng->_cleanupSubghz();
  return 0;
}

int LuaEngine::_subghz_parseSub(lua_State* L) {
  const char* content = luaL_checkstring(L, 1);
  CC1101Util::Signal sig;
  if (!CC1101Util::loadFile(String(content), sig)) { lua_pushnil(L); return 1; }
  _signalToLuaTable(L, sig);
  return 1;
}

int LuaEngine::_subghz_formatSub(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  CC1101Util::Signal sig;
  _signalFromLuaTable(L, 1, sig);
  String out = CC1101Util::saveToString(sig);
  lua_pushstring(L, out.c_str());
  return 1;
}

int LuaEngine::_subghz_close(lua_State* L) {
  LuaEngine* eng = _fromState(L);
  if (eng) eng->_cleanupSubghz();
  return 0;
}
