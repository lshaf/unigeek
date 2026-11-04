#include "ToolScreen.h"

#include <WiFi.h>
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

void ToolScreen::onInit() {
  _view.setWrapMode(TextScrollView::WRAP_CHARACTER);
  if (_startsImmediately()) _start();
  else                      _showSetup();
}

// ── Setup ────────────────────────────────────────────────────────────────────

void ToolScreen::_addRow(const char* label, const String& value, uint8_t id) {
  if (_count >= MAX_ROWS) return;
  _values[_count] = value;
  _items[_count] = {label, _values[_count].c_str()};
  _ids[_count] = id;
  ++_count;
}

void ToolScreen::_showSetup() {
  _state = STATE_SETUP;
  _count = 0;
  _buildRows();
  _items[_count] = {"Start"};
  _ids[_count] = ID_START;
  const uint8_t total = _count + 1;
  if (_selected >= total) _selected = total - 1;
  setItems(_items, total, _selected);
}

void ToolScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_SETUP || index > _count) return;
  _selected = index;
  if (_ids[index] == ID_START) {
    _start();
    return;
  }
  _editRow(_ids[index]);
  _showSetup();
}

void ToolScreen::onBack() {
  if (_state == STATE_SETUP) Screen.goBack();
}

// ── Run ──────────────────────────────────────────────────────────────────────

void ToolScreen::_start() {
  _log = "";
  _view.setContent("");
  _chromeDrawn = false;
  _state = STATE_RUNNING;
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  render();
  if (!_begin()) {
    _end();
    if (_startsImmediately()) { Screen.goBack(); return; }
    _showSetup();
    return;
  }
  render();
}

void ToolScreen::_finish(const char* note) {
  if (note) _print(note);
  _end();
  _state = STATE_DONE;
  _renderFooter();
}

void ToolScreen::_print(const String& line) {
  if (_log.length()) _log += '\n';
  _log += line;
  if (_log.length() > kMaxLog) {
    const int cut = _log.indexOf('\n', _log.length() - kMaxLog);
    _log.remove(0, cut < 0 ? _log.length() - kMaxLog : cut + 1);
  }
  _view.updateContent(_log, true);
  if (_state != STATE_SETUP) _renderOutput();
}

void ToolScreen::onUpdate() {
  if (_state == STATE_SETUP) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      if (_state == STATE_RUNNING) _end();
      if (_startsImmediately()) { Screen.goBack(); return; }
      _showSetup();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (_state == STATE_RUNNING) { if (_allowsStop()) _finish("Stopped."); }
      else if (_allowsRestart())   _start();
      return;
    }
    if (_view.onNav(dir)) _renderOutput();
  }

  if (_state == STATE_RUNNING && !_step()) _finish(nullptr);
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void ToolScreen::onRender() {
  if (_state == STATE_SETUP) {
    ListScreen::onRender();
    return;
  }
  _renderOutput();
  _renderFooter();
}

void ToolScreen::_renderOutput() {
  _view.render(bodyX(), bodyY(), bodyW(), bodyH() - _footerH());
}

void ToolScreen::_renderFooter() {
  if (_footerH() == 0) return;
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  const int top = y + h - kFooterH;
  lcd.fillRect(x, top, w, kFooterH, TFT_BLACK);
  lcd.drawFastHLine(x, top, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  const char* hint = _state == STATE_RUNNING ? (_allowsStop() ? "[Press] Stop" : nullptr)
                   : _allowsRestart()         ? "[Press] Restart"
                                              : nullptr;
  if (hint) lcd.drawString(hint, x + w / 2, y + h - 1);
  lcd.setTextDatum(TL_DATUM);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

String ToolScreen::_editHost(const char* title, const String& current) {
  String initial = current;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }
#ifdef DEVICE_HAS_KEYBOARD
  const auto mode = InputTextAction::INPUT_TEXT;
#else
  const auto mode = InputTextAction::INPUT_IP_ADDRESS;
#endif
  String value = InputTextAction::popup(title, initial, mode);
  if (InputTextAction::wasCancelled() || value.length() == 0) return current;
  value.trim();
  return value;
}

bool ToolScreen::_resolve(const String& host, IPAddress& out) {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return false;
  }
  if (host.length() == 0) {
    ShowStatusAction::show("Host not set");
    return false;
  }
  if (out.fromString(host)) return true;
  _print("Resolving " + host + "...");
  if (WiFi.hostByName(host.c_str(), out) == 1 && out != IPAddress(0, 0, 0, 0)) return true;
  ShowStatusAction::show("Cannot resolve host", 1500);
  return false;
}
