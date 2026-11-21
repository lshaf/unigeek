#include "WolMenuScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "WakeOnLanScreen.h"

void WolMenuScreen::onInit() { _showMenu(); }

void WolMenuScreen::onRestore() {
  if (_state == STATE_SAVED) { _showSaved(); if (_hosts.empty()) _showMenu(1); }
  else render();
}

void WolMenuScreen::_showMenu(uint8_t sel) {
  _state = STATE_MENU;
  setItems(_menu, 2, sel);
  render();
}

void WolMenuScreen::_showSaved() {
  _hosts = SavedHosts::load(DIR);
  if (_hosts.size() > MAX_SAVED) _hosts.resize(MAX_SAVED);
  for (size_t i = 0; i < _hosts.size(); ++i) {
    _details[i] = _hosts[i].get("mac");
    _items[i] = {_hosts[i].name.c_str(), _details[i].c_str()};
  }
  _state = STATE_SAVED;
  setItems(_items, (uint8_t)_hosts.size(), 0);
  render();
}

void WolMenuScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) { _open(nullptr); return; }
    _showSaved();
    if (_hosts.empty()) { ShowStatusAction::show("No saved targets", 1500); _showMenu(1); }
    return;
  }
  if (index < _hosts.size()) _open(&_hosts[index]);
}

void WolMenuScreen::onBack() {
  if (_state == STATE_SAVED) { _showMenu(1); return; }
  Screen.goBack();
}

void WolMenuScreen::_open(const SavedHost* host) {
  auto* wol = new WakeOnLanScreen();
  if (host) wol->loadSaved(*host);
  Screen.push(wol);
}
