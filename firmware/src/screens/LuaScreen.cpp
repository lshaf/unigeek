#include "screens/LuaScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include <esp_heap_caps.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

void LuaScreen::_loadDir(const String& path) {
  _currentDir = path;
  _browser.load(this, path, ".lua");
  setItems(_browser.items(), _browser.count());
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void LuaScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    Screen.goBack();
    return;
  }
  _loadDir(ROOT_DIR);
}

// ── Update ────────────────────────────────────────────────────────────────────

void LuaScreen::onUpdate() {
  if (_state == STATE_BROWSE) {
    ListScreen::onUpdate();
    return;
  }

  if (_state == STATE_DONE) {
    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _engine.deinit();
      _state = STATE_BROWSE;
      _loadDir(_currentDir);
      render();
    }
    return;
  }

  // Drain any popup the Lua task is waiting on (input/dialog) — must happen
  // on this loop task because the firmware popup actions own Uni.update().
  _engine.servicePendingPopup();

  _errBuf = "";
  bool keepGoing = _engine.stepLoop(_errBuf);
  if (!keepGoing) _handleDone(!_errBuf.isEmpty());
}

// ── Render ────────────────────────────────────────────────────────────────────

void LuaScreen::onRender() {
  if (_state == STATE_BROWSE) { ListScreen::onRender(); return; }
  if (_state == STATE_RUNNING) { _drawRunning(); return; }
  _drawDone();
}

void LuaScreen::onBack() {
  if (_state != STATE_BROWSE) return;
  if (_currentDir == ROOT_DIR) {
    Screen.goBack();
    return;
  }
  int slash = _currentDir.lastIndexOf('/');
  String parent = (slash > 0) ? _currentDir.substring(0, slash) : ROOT_DIR;
  if (parent.length() == 0) parent = ROOT_DIR;
  _loadDir(parent);
}

void LuaScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_BROWSE) return;
  const auto& entry = _browser.entry(index);
  if (entry.isDir) {
    _loadDir(entry.path);
  } else {
    _startScript(entry.path);
  }
}

// ── Script start ──────────────────────────────────────────────────────────────

void LuaScreen::_startScript(const String& path) {
  _log.clear();
  _errBuf = "";

  // Stream the file into a sized buffer. Big scripts spill to PSRAM (when the
  // board has it) so they don't hog scarce internal SRAM; the buffer is freed
  // right after compile since Lua keeps its own bytecode copy.
  fs::File f = Uni.Storage->open(path.c_str(), "r");
  if (!f) {
    Serial.println("[lua] missing: " + path);
    _log.addLine("[error] empty or missing file", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }
  size_t size = f.size();
  if (size == 0) {
    f.close();
    Serial.println("[lua] empty: " + path);
    _log.addLine("[error] empty or missing file", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }

  static constexpr size_t kPsramThreshold = 2048;
  uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#ifdef BOARD_HAS_PSRAM
  if (size >= kPsramThreshold) caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
  char* buf = (char*)heap_caps_malloc(size + 1, caps);
#ifdef BOARD_HAS_PSRAM
  // Fall back to internal if PSRAM allocation fails for any reason.
  if (!buf && (caps & MALLOC_CAP_SPIRAM)) {
    buf = (char*)heap_caps_malloc(size + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#endif
  if (!buf) {
    f.close();
    Serial.printf("[lua] alloc fail size=%u\n", (unsigned)size);
    _log.addLine("[error] out of memory", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }
  size_t n = f.readBytes(buf, size);
  f.close();
  buf[n] = '\0';

  if (!_engine.init()) {
    heap_caps_free(buf);
    Serial.println("[lua] lua_newstate failed (OOM?)");
    _log.addLine("[error] lua_newstate failed (OOM?)", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }

  _engine.setBodyRect(0, 0, Uni.Lcd.width(), Uni.Lcd.height());

  // loadScript takes ownership of `buf`; it'll be freed on the Lua task once
  // compile finishes (or now if the engine rejects it). Compile errors come
  // through stepLoop()'s error path.
  String queueErr;
  bool ok = _engine.loadScript(buf, n, queueErr);
  if (!ok) {
    Serial.println("[lua] queue: " + queueErr);
    _log.addLine(("[error] " + queueErr).c_str(), TFT_RED);
    _engine.deinit();
    _state = STATE_DONE;
    render();
    return;
  }

  Uni.Lcd.setTextDatum(TL_DATUM);
  Uni.Lcd.fillScreen(TFT_BLACK);
  // Silence the touch-overlay paint while Lua owns the screen: the Lua task
  // writes to the display SPI bus every frame, so a concurrent _paintZone()
  // from the loop task crosses the SPI mutex and asserts. Only touch boards
  // actually paint the overlay; on cardputer / t_lora_pager `suppressKeys`
  // additionally gates keyboard input, so we must NOT set it there.
#ifdef DEVICE_HAS_TOUCH_NAV
  if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif
  _state = STATE_RUNNING;
}

// ── Done state ────────────────────────────────────────────────────────────────

void LuaScreen::_handleDone(bool isError) {
  _engine.deinit();
#ifdef DEVICE_HAS_TOUCH_NAV
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  if (!isError) {
    _state = STATE_BROWSE;
    _loadDir(_currentDir);
    Uni.Lcd.fillScreen(TFT_BLACK);
    render();
    return;
  }
  _log.addLine(("[error] " + _errBuf).c_str(), TFT_RED);
  _state = STATE_DONE;
  Uni.Lcd.fillScreen(TFT_BLACK);
  render();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void LuaScreen::_drawRunning() {
  int w = Uni.Lcd.width(), h = Uni.Lcd.height();
  _log.draw(Uni.Lcd, 0, 0, w, h);
}

void LuaScreen::_drawDone() {
  int w = Uni.Lcd.width(), h = Uni.Lcd.height();
  _log.draw(Uni.Lcd, 0, 0, w, h - 12);
  Uni.Lcd.setTextSize(1);
  Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  Uni.Lcd.drawString("BACK/OK: return", 2, h - 10);
}