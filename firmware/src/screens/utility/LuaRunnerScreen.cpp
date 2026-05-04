#include "screens/utility/LuaRunnerScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "screens/utility/UtilityMenuScreen.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

void LuaRunnerScreen::_loadDir(const String& path) {
  _currentDir = path;
  _browser.load(this, path, ".lua");
  setItems(_browser.items(), _browser.count());
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void LuaRunnerScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    Screen.goBack();
    return;
  }
  _loadDir(ROOT_DIR);
}

// ── Update ────────────────────────────────────────────────────────────────────

void LuaRunnerScreen::onUpdate() {
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

  _errBuf = "";
  bool keepGoing = _engine.stepLoop(_errBuf);
  if (!keepGoing) _handleDone(!_errBuf.isEmpty());
}

// ── Render ────────────────────────────────────────────────────────────────────

void LuaRunnerScreen::onRender() {
  if (_state == STATE_BROWSE) { ListScreen::onRender(); return; }
  if (_state == STATE_RUNNING) { _drawRunning(); return; }
  _drawDone();
}

void LuaRunnerScreen::onBack() {
  if (_state != STATE_BROWSE) return;
  if (_currentDir == ROOT_DIR) {
    Screen.goBack();
    return;
  }
  // Navigate up one level
  int slash = _currentDir.lastIndexOf('/');
  String parent = (slash > 0) ? _currentDir.substring(0, slash) : ROOT_DIR;
  if (parent.length() == 0) parent = ROOT_DIR;
  _loadDir(parent);
}

void LuaRunnerScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_BROWSE) return;
  const auto& entry = _browser.entry(index);
  if (entry.isDir) {
    _loadDir(entry.path);
  } else {
    _startScript(entry.path);
  }
}

// ── Script start ──────────────────────────────────────────────────────────────

void LuaRunnerScreen::_startScript(const String& path) {
  _log.clear();
  _errBuf = "";

  _scriptSrc = Uni.Storage->readFile(path.c_str());
  if (_scriptSrc.isEmpty()) {
    Serial.println("[lua] empty or missing: " + path);
    _log.addLine("[error] empty or missing file", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }

  if (!_engine.init()) {
    Serial.println("[lua] lua_newstate failed (OOM?)");
    _log.addLine("[error] lua_newstate failed (OOM?)", TFT_RED);
    _state = STATE_DONE;
    render();
    return;
  }

  _engine.setBodyRect(0, 0, Uni.Lcd.width(), Uni.Lcd.height());

  String compileErr;
  if (!_engine.loadScript(_scriptSrc.c_str(), compileErr)) {
    Serial.println("[lua] syntax: " + compileErr);
    _log.addLine(("[syntax] " + compileErr).c_str(), TFT_RED);
    _engine.deinit();
    _state = STATE_DONE;
    render();
    return;
  }

  Uni.Lcd.setTextDatum(TL_DATUM);
  Uni.Lcd.fillScreen(TFT_BLACK);
  _state = STATE_RUNNING;
}

// ── Done state ────────────────────────────────────────────────────────────────

void LuaRunnerScreen::_handleDone(bool isError) {
  if (isError) {
    _log.addLine(("[error] " + _errBuf).c_str(), TFT_RED);
    Serial.println("[lua] " + _errBuf);
  } else {
    _log.addLine("[done]", TFT_DARKGREY);
  }
  _state = STATE_DONE;
  Uni.Lcd.fillScreen(TFT_BLACK);
  render();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void LuaRunnerScreen::_drawRunning() {
  int w = Uni.Lcd.width(), h = Uni.Lcd.height();
  _log.draw(Uni.Lcd, 0, 0, w, h);
}

void LuaRunnerScreen::_drawDone() {
  int w = Uni.Lcd.width(), h = Uni.Lcd.height();
  _log.draw(Uni.Lcd, 0, 0, w, h - 12);
  Uni.Lcd.setTextSize(1);
  Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  Uni.Lcd.drawString("BACK/OK: return", 2, h - 10);
}
