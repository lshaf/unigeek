#include "CastBombScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

const CastBombScreen::VideoPreset CastBombScreen::PRESETS[] = {
  {"Rick Astley",   "dQw4w9WgXcQ"},
  {"All Star",      "L_jWHffIx5E"},
  {"Trololo",       "2Z4m4lnjxkY"},
  {"Sandstorm",     "y6120QOlsfU"},
  {"He's a Pirate", "cFVUEUfZyDU"},
};
const uint8_t CastBombScreen::PRESET_COUNT =
  sizeof(PRESETS) / sizeof(PRESETS[0]);

CastBombScreen::CastBombScreen() {
  memset(_devices,     0, sizeof(_devices));
  memset(_devItems,    0, sizeof(_devItems));
  memset(_configItems, 0, sizeof(_configItems));
}

void CastBombScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
    Screen.goBack();
    return;
  }
  _showConfig();
}

void CastBombScreen::onBack() {
  if (_state == STATE_DEVICES) { _showConfig(); return; }
  Screen.goBack();
}

void CastBombScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_CONFIG) {
    switch (index) {
      case 0: _pickPreset(); break;
      case 1: _setCustom();  break;
      case 2: _discover();   break;
    }
  } else if (_state == STATE_DEVICES) {
    _cast(index);
  }
}

const char* CastBombScreen::_currentVideoId() const {
  if (!_customId.isEmpty()) return _customId.c_str();
  return PRESETS[_presetIdx].videoId;
}

const char* CastBombScreen::_currentVideoLabel() const {
  if (!_customId.isEmpty()) return "Custom";
  return PRESETS[_presetIdx].label;
}

void CastBombScreen::_showConfig() {
  _state = STATE_CONFIG;
  _videoSub = _currentVideoLabel();
  _configItems[0] = {"Video",          _videoSub.c_str()};
  _configItems[1] = {"Custom Video"};
  _configItems[2] = {"Discover & Cast"};
  setItems(_configItems, 3);
}

void CastBombScreen::_pickPreset() {
  _presetIdx = (_presetIdx + 1) % PRESET_COUNT;
  _customId = "";
  _showConfig();
}

void CastBombScreen::_setCustom() {
  String prev = _customId.isEmpty() ? String(PRESETS[_presetIdx].videoId) : _customId;
  String entered = InputTextAction::popup("YouTube ID", prev);
  if (!entered.isEmpty()) _customId = entered;
  _showConfig();
}

void CastBombScreen::_discover() {
  _state = STATE_DISCOVERING;
  _devCount = 0;
  memset(_devices, 0, sizeof(_devices));

  int n1 = Achievement.inc("wifi_cast_bomb_first");
  if (n1 == 1) Achievement.unlock("wifi_cast_bomb_first");

  ProgressView::init();
  _devCount = CastBombUtil::discover(
    _devices, CastBombUtil::MAX_DEVICES,
    [](uint8_t pct) { ProgressView::progress("Searching cast targets...", pct); }
  );
  ProgressView::finish();

  _showDevices();
}

void CastBombScreen::_showDevices() {
  _state = STATE_DEVICES;

  if (_devCount == 0) {
    ShowStatusAction::show("No cast devices found", 1500);
    _showConfig();
    return;
  }

  for (uint8_t i = 0; i < _devCount; i++) {
    _devSubs[i]  = _devices[i].ip;
    _devItems[i] = { _devices[i].name, _devSubs[i].c_str() };
  }
  uint8_t total = _devCount;
  if (_devCount > 1) {
    _devItems[_devCount] = { "Cast All" };
    total = _devCount + 1;
  }
  setItems(_devItems, total);
}

void CastBombScreen::_cast(uint8_t index) {
  _state = STATE_CASTING;

  const char* vid = _currentVideoId();
  if (!vid || !*vid) {
    ShowStatusAction::show("No video selected", 1500);
    _showDevices();
    return;
  }

  bool anyHit = false;

  if (index < _devCount) {
    String msg = "Casting to ";
    msg += _devices[index].name;
    msg += "...";
    ShowStatusAction::show(msg.c_str(), 0);
    bool ok = CastBombUtil::launchYouTube(_devices[index], vid);
    ShowStatusAction::show(ok ? "Cast launched" : "Cast failed", 1200);
    anyHit = ok;
  } else {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < _devCount; i++) {
      String msg = "Casting (";
      msg += String(i + 1);
      msg += "/";
      msg += String(_devCount);
      msg += ")...";
      ShowStatusAction::show(msg.c_str(), 0);
      if (CastBombUtil::launchYouTube(_devices[i], vid)) hits++;
    }
    String done = "Cast ";
    done += String(hits);
    done += "/";
    done += String(_devCount);
    done += " ok";
    ShowStatusAction::show(done.c_str(), 1500);
    anyHit = hits > 0;
  }

  if (anyHit) {
    int n2 = Achievement.inc("wifi_cast_bomb_hit");
    if (n2 == 1) Achievement.unlock("wifi_cast_bomb_hit");
  }

  _showDevices();
}
