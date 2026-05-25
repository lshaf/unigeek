#pragma once
#include <Arduino.h>

extern "C" {
  #include "lua.h"
  #include "lauxlib.h"
  #include "lualib.h"
}

class CC1101Util;
class M5RF433Util;

class LuaEngine {
public:
  // Unique address used as a sentinel to distinguish clean exit() from errors.
  static char exitSentinel;

  bool init();
  void deinit();

  // Hand the engine a script to run. Takes ownership of `src` (must be
  // allocated via heap_caps_malloc); the engine frees it after compile on
  // the Lua task. Returns false only if the engine is not ready.
  // Compile and execution both happen on the Lua task spawned by stepLoop().
  bool loadScript(char* src, size_t len, String& errOut);

  // Drive the script. First call spawns the Lua task (which compiles then
  // runs). Returns false when done (exit() called, error, or requestExit()).
  bool stepLoop(String& errOut);

  // Drain any pending popup the Lua task has parked on the engine. MUST be
  // called from the main loop task — the firmware actions it dispatches
  // call Uni.update() / draw to Uni.Lcd, both of which expect the loop task.
  // Returns immediately when there's nothing to do.
  void servicePendingPopup();

  void requestExit()             { _exitRequested = true; }
  bool isExitRequested()   const { return _exitRequested; }

  // Body rect in screen coordinates — set once by the runner screen.
  void setBodyRect(int x, int y, int w, int h) {
    _bx = x; _by = y; _bw = w; _bh = h;
  }

private:
  enum Status : uint8_t {
    STATUS_IDLE,
    STATUS_RUNNING,
    STATUS_DONE_OK,
    STATUS_DONE_EXIT,
    STATUS_DONE_ERR,
  };

  lua_State* _lua      = nullptr;
  int        _chunkRef = LUA_NOREF;
  bool       _exitRequested = false;
  int        _bx = 0, _by = 0, _bw = 240, _bh = 200;
  uint32_t   _textFg = 0xFFFF;

  // Pending source: owned by the engine, freed by _taskEntry after compile.
  char*  _pendingSrc    = nullptr;
  size_t _pendingSrcLen = 0;

  // Popup bridge — Lua task → loop task. The Lua task fills the request
  // fields and spins on _popupType going back to POPUP_NONE; the loop task
  // sees POPUP_NONE != _popupType in servicePendingPopup() and runs the
  // matching firmware action, then clears the slot.
  enum PopupType : uint8_t {
    POPUP_NONE = 0,
    POPUP_TEXT,
    POPUP_HEX,
    POPUP_IP,
    POPUP_NUMBER,
    POPUP_CONFIRM,
    POPUP_SELECT,
  };
  static constexpr int kMaxSelectOptions = 16;
  volatile PopupType _popupType        = POPUP_NONE;
  String             _popupTitle;
  String             _popupDefaultStr;
  String             _popupResultStr;
  int                _popupMin         = 0;
  int                _popupMax         = 0;
  int                _popupDefaultInt  = 0;
  int                _popupResultInt   = 0;
  bool               _popupCancelled   = false;
  String             _popupOptions[kMaxSelectOptions];
  int                _popupOptionCount = 0;

  void*             _task        = nullptr;  // TaskHandle_t (opaque to avoid header pulls)
  volatile Status   _status      = STATUS_IDLE;
  String            _taskErrOut;

  static void  _taskEntry(void* arg);
  static void* _alloc(void* ud, void* ptr, size_t osize, size_t nsize);
  static void  _countHook(lua_State* L, lua_Debug* ar);
  static LuaEngine* _fromState(lua_State* L);
  void _registerBindings();

  // uni.*
  static int _uni_debug(lua_State* L);
  static int _uni_delay(lua_State* L);
  static int _uni_heap(lua_State* L);
  static int _uni_millis(lua_State* L);
  static int _uni_beep(lua_State* L);
  static int _lua_exit(lua_State* L);

  // Override for Lua's stock math.randomseed — folds the user-supplied seed
  // into RandomSeed's entropy chain and re-applies it to both Arduino's
  // random() and Lua's math.random().
  static int _math_randomseed(lua_State* L);

  // uni.lcd / uni.sd / uni.nav lazy loaders (registered in package.preload)
  static int _lua_load_lcd(lua_State* L);
  static int _lua_load_sd(lua_State* L);
  static int _lua_load_nav(lua_State* L);

  // uni.lcd.*
  static int _lcd_clear(lua_State* L);
  static int _lcd_fillScreen(lua_State* L);
  static int _lcd_print(lua_State* L);
  static int _lcd_rect(lua_State* L);
  static int _lcd_line(lua_State* L);
  static int _lcd_circle(lua_State* L);
  static int _lcd_fillCircle(lua_State* L);
  static int _lcd_roundRect(lua_State* L);
  static int _lcd_fillRoundRect(lua_State* L);
  static int _lcd_color(lua_State* L);
  static int _lcd_textSize(lua_State* L);
  static int _lcd_textColor(lua_State* L);
  static int _lcd_textDatum(lua_State* L);
  static int _lcd_textWidth(lua_State* L);
  static int _lcd_w(lua_State* L);
  static int _lcd_h(lua_State* L);

  // uni.lcd.sprite — userdata + methods via metatable
  static int _lcd_sprite(lua_State* L);
  static int _sprite_push(lua_State* L);
  static int _sprite_fill(lua_State* L);
  static int _sprite_rect(lua_State* L);
  static int _sprite_line(lua_State* L);
  static int _sprite_circle(lua_State* L);
  static int _sprite_fillCircle(lua_State* L);
  static int _sprite_roundRect(lua_State* L);
  static int _sprite_fillRoundRect(lua_State* L);
  static int _sprite_print(lua_State* L);
  static int _sprite_textColor(lua_State* L);
  static int _sprite_textSize(lua_State* L);
  static int _sprite_textDatum(lua_State* L);
  static int _sprite_textWidth(lua_State* L);
  static int _sprite_w(lua_State* L);
  static int _sprite_h(lua_State* L);
  static int _sprite_free(lua_State* L);
  static int _sprite_gc(lua_State* L);
  void _setupSpriteMetatable();

  // uni.sd.*
  static int _sd_read(lua_State* L);
  static int _sd_write(lua_State* L);
  static int _sd_append(lua_State* L);
  static int _sd_exists(lua_State* L);
  static int _sd_list(lua_State* L);
  static int _sd_remove(lua_State* L);
  static int _sd_rename(lua_State* L);
  static int _sd_mkdir(lua_State* L);
  static int _sd_size(lua_State* L);

  // uni.nav.*
  static int _nav_btn(lua_State* L);
  static int _nav_touchX(lua_State* L);
  static int _nav_touchY(lua_State* L);
  static int _nav_isTouched(lua_State* L);

  // Lazy loaders + bindings for tier-2 modules.
  static int _lua_load_input(lua_State* L);
  static int _lua_load_dialog(lua_State* L);
  static int _lua_load_notify(lua_State* L);
  static int _lua_load_json(lua_State* L);
  static int _lua_load_path(lua_State* L);
  static int _lua_load_time(lua_State* L);
  static int _lua_load_config(lua_State* L);
  static int _lua_load_wifi(lua_State* L);
  static int _lua_load_http(lua_State* L);

  // uni.wifi.* — block-until-connected + status helpers.
  static int _wifi_status(lua_State* L);
  static int _wifi_ssid(lua_State* L);
  static int _wifi_ip(lua_State* L);
  static int _wifi_connect(lua_State* L);
  static int _wifi_disconnect(lua_State* L);

  // uni.http.* — blocking GET/POST. Reuse WiFiClientSecure with setInsecure.
  static int _http_get(lua_State* L);
  static int _http_post(lua_State* L);

  // True if the script's wifi.connect() actually brought the radio up — used
  // by _cleanupNetwork() to disconnect on exit only when we're the originator.
  bool _scriptStartedWifi = false;

  // Tear down anything the script touched on the network side: WiFi (only if
  // the script started it) and any cached HTTP transport state.
  void _cleanupNetwork();

  // uni.subghz — single facade over CC1101 (SPI) and M5 RF433 (GPIO bit-bang).
  // The backend is decided lazily on the first call to _subghzEnsureOpen():
  // CC1101 is tried first when its CS/GDO0 pins are configured and SPI is
  // available; falls back to M5 RF433 when the board exposes Grove pins.
  enum SubGhzBackend : uint8_t {
    SUBGHZ_NONE   = 0,
    SUBGHZ_CC1101 = 1,
    SUBGHZ_RF433  = 2,
  };
  SubGhzBackend _subghzBackend   = SUBGHZ_NONE;
  CC1101Util*   _subghzCc        = nullptr;
  M5RF433Util*  _subghzRf        = nullptr;
  bool          _subghzReceiving = false;
  bool          _subghzScanning  = false;
  bool          _subghzJamming   = false;

  bool _subghzEnsureOpen();
  void _cleanupSubghz();

  // Lazy loader + bindings for uni.subghz.
  static int _lua_load_subghz(lua_State* L);
  static int _subghz_info(lua_State* L);
  static int _subghz_setFrequency(lua_State* L);
  static int _subghz_getFrequency(lua_State* L);
  static int _subghz_setRxFilter(lua_State* L);
  static int _subghz_beginReceive(lua_State* L);
  static int _subghz_pollReceive(lua_State* L);
  static int _subghz_endReceive(lua_State* L);
  static int _subghz_send(lua_State* L);
  static int _subghz_beginScan(lua_State* L);
  static int _subghz_stepScan(lua_State* L);
  static int _subghz_endScan(lua_State* L);
  static int _subghz_getScanFreq(lua_State* L);
  static int _subghz_getScanRssi(lua_State* L);
  static int _subghz_startJam(lua_State* L);
  static int _subghz_jamBurst(lua_State* L);
  static int _subghz_stopJam(lua_State* L);
  static int _subghz_parseSub(lua_State* L);
  static int _subghz_formatSub(lua_State* L);
  static int _subghz_close(lua_State* L);

  // uni.input.* — popup-bridge to firmware Input*Action classes.
  static int _input_text(lua_State* L);
  static int _input_number(lua_State* L);
  static int _input_hex(lua_State* L);
  static int _input_ip(lua_State* L);

  // uni.dialog.* — popup-bridge to InputSelectAction.
  static int _dialog_confirm(lua_State* L);
  static int _dialog_select(lua_State* L);

  // uni.notify.* — runs directly on the Lua task (just LCD writes + sleep).
  static int _notify_show(lua_State* L);

  // uni.json.* — cJSON wrappers.
  static int _json_encode(lua_State* L);
  static int _json_decode(lua_State* L);

  // uni.path.* — string helpers.
  static int _path_join(lua_State* L);
  static int _path_basename(lua_State* L);
  static int _path_dirname(lua_State* L);
  static int _path_ext(lua_State* L);

  // uni.time.* — RTC.
  static int _time_now(lua_State* L);

  // uni.config.* — read-only ConfigManager.
  static int _config_get(lua_State* L);

  // Helper used by _input_*: park request, wait for loop task to clear it.
  static int _runPopupAndPushResult(lua_State* L, bool stringResult);
};
