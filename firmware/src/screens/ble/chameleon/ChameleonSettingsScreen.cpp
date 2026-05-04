#include "ChameleonSettingsScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

const char* ChameleonSettingsScreen::_animName(uint8_t v) {
  switch (v) {
    case 0: return "Full";
    case 1: return "Minimal";
    case 2: return "None";
    case 3: return "Symmetric";
    default: return "?";
  }
}

const char* ChameleonSettingsScreen::_btnName(uint8_t v) {
  switch (v) {
    case 0: return "Off";
    case 1: return "Cycle Fwd";
    case 2: return "Cycle Bk";
    case 3: return "Clone UID";
    case 4: return "Battery";
    default: return "?";
  }
}

void ChameleonSettingsScreen::_load() {
  auto& c = ChameleonClient::get();
  ChameleonClient::DeviceSettings s{};
  if (c.getDeviceSettings(&s)) {
    _anim       = s.animation;
    _btnAShort  = s.btnAShort;
    _btnBShort  = s.btnBShort;
    _btnALong   = s.btnALong;
    _btnBLong   = s.btnBLong;
    _blePairing = s.blePairingEnabled != 0;
  }
  _rebuildLabels();
}

void ChameleonSettingsScreen::_rebuildLabels() {
  snprintf(_labels[0], sizeof(_labels[0]), "Animation");
  snprintf(_subs[0],   sizeof(_subs[0]),   "%s", _animName(_anim));
  snprintf(_labels[1], sizeof(_labels[1]), "Btn A short");
  snprintf(_subs[1],   sizeof(_subs[1]),   "%s", _btnName(_btnAShort));
  snprintf(_labels[2], sizeof(_labels[2]), "Btn B short");
  snprintf(_subs[2],   sizeof(_subs[2]),   "%s", _btnName(_btnBShort));
  snprintf(_labels[3], sizeof(_labels[3]), "Btn A long");
  snprintf(_subs[3],   sizeof(_subs[3]),   "%s", _btnName(_btnALong));
  snprintf(_labels[4], sizeof(_labels[4]), "Btn B long");
  snprintf(_subs[4],   sizeof(_subs[4]),   "%s", _btnName(_btnBLong));
  snprintf(_labels[5], sizeof(_labels[5]), "BLE pairing");
  snprintf(_subs[5],   sizeof(_subs[5]),   "%s", _blePairing ? "On" : "Off");
  snprintf(_labels[6], sizeof(_labels[6]), "Save settings");
  _subs[6][0] = 0;
  snprintf(_labels[7], sizeof(_labels[7]), "Reset / Clear bonds");
  _subs[7][0] = 0;

  for (int i = 0; i < kCount; i++) {
    _items[i].label    = _labels[i];
    _items[i].sublabel = _subs[i][0] ? _subs[i] : nullptr;
  }
}

void ChameleonSettingsScreen::onInit() {
  _load();
  setItems(_items);

  int n = Achievement.inc("chameleon_settings_viewed");
  if (n == 1) Achievement.unlock("chameleon_settings_viewed");
}

void ChameleonSettingsScreen::onBack() {
  Screen.goBack();
}

void ChameleonSettingsScreen::_editAnim() {
  static const InputSelectAction::Option opts[] = {
    {"Full",      "0"},
    {"Minimal",   "1"},
    {"None",      "2"},
    {"Symmetric", "3"},
  };
  char def[2] = { (char)('0' + _anim), 0 };
  const char* r = InputSelectAction::popup("Animation", opts, 4, def);
  if (!r) { render(); return; }
  uint8_t v = r[0] - '0';
  if (ChameleonClient::get().setAnimation(v)) _anim = v;
  _rebuildLabels();
  render();
}

void ChameleonSettingsScreen::_editButton(bool isB, bool longPress) {
  static const InputSelectAction::Option opts[] = {
    {"Disable",       "0"},
    {"Cycle Forward", "1"},
    {"Cycle Backward","2"},
    {"Clone UID",     "3"},
    {"Battery Status","4"},
  };
  uint8_t cur = isB ? (longPress ? _btnBLong : _btnBShort)
                    : (longPress ? _btnALong : _btnAShort);
  char def[2] = { (char)('0' + cur), 0 };
  const char* title = longPress
    ? (isB ? "Btn B Long" : "Btn A Long")
    : (isB ? "Btn B Short" : "Btn A Short");
  const char* r = InputSelectAction::popup(title, opts, 5, def);
  if (!r) { render(); return; }
  uint8_t v = r[0] - '0';
  uint8_t idx = isB ? 'B' : 'A';
  if (ChameleonClient::get().setButtonConfig(idx, longPress, v)) {
    if (isB) (longPress ? _btnBLong : _btnBShort) = v;
    else     (longPress ? _btnALong : _btnAShort) = v;
  }
  _rebuildLabels();
  render();
}

void ChameleonSettingsScreen::_togglePairing() {
  bool next = !_blePairing;
  if (ChameleonClient::get().setBlePairingEnabled(next)) _blePairing = next;
  _rebuildLabels();
  render();
}

void ChameleonSettingsScreen::_save() {
  bool ok = ChameleonClient::get().saveSettings();
  ShowStatusAction::show(ok ? "Settings saved" : "Save failed", 1200);
  if (ok) {
    int n = Achievement.inc("chameleon_settings_saved");
    if (n == 1) Achievement.unlock("chameleon_settings_saved");
  }
  render();
}

void ChameleonSettingsScreen::_reset() {
  static const InputSelectAction::Option opts[] = {
    {"Reset defaults",  "reset"},
    {"Clear BLE bonds", "bonds"},
  };
  const char* r = InputSelectAction::popup("Maintenance", opts, 2, nullptr);
  if (!r) { render(); return; }
  auto& c = ChameleonClient::get();
  if (strcmp(r, "reset") == 0) {
    bool ok = c.resetSettings();
    ShowStatusAction::show(ok ? "Defaults restored" : "Reset failed", 1200);
    if (ok) _load();
  } else {
    bool ok = c.clearBleBonds();
    ShowStatusAction::show(ok ? "Bonds cleared" : "Clear failed", 1200);
    if (ok) {
      int n = Achievement.inc("chameleon_bonds_cleared");
      if (n == 1) Achievement.unlock("chameleon_bonds_cleared");
    }
  }
  render();
}

void ChameleonSettingsScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _editAnim(); break;
    case 1: _editButton(false, false); break;
    case 2: _editButton(true,  false); break;
    case 3: _editButton(false, true);  break;
    case 4: _editButton(true,  true);  break;
    case 5: _togglePairing(); break;
    case 6: _save(); break;
    case 7: _reset(); break;
  }
}
