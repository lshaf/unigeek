#include "M5RF433Screen.h"
#include "core/AchievementManager.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

// Pinout is fixed per board at build time. M5 RF433T/R Units plug into the
// Grove port — default to GROVE_SDA (TX) / GROVE_SCL (RX). A board can override
// either with -DM5RF433_TX_PIN / -DM5RF433_RX_PIN in pins_arduino.h.
#if defined(M5RF433_TX_PIN)
  static constexpr int8_t kDefaultTxPin = (int8_t)M5RF433_TX_PIN;
#elif defined(GROVE_SDA)
  static constexpr int8_t kDefaultTxPin = (int8_t)GROVE_SDA;
#else
  static constexpr int8_t kDefaultTxPin = -1;
#endif

#if defined(M5RF433_RX_PIN)
  static constexpr int8_t kDefaultRxPin = (int8_t)M5RF433_RX_PIN;
#elif defined(GROVE_SCL)
  static constexpr int8_t kDefaultRxPin = (int8_t)GROVE_SCL;
#else
  static constexpr int8_t kDefaultRxPin = -1;
#endif

void M5RF433Screen::onInit() {
  _txPin = kDefaultTxPin;
  _rxPin = kDefaultRxPin;

  if (_txPin < 0 && _rxPin < 0) {
    ShowStatusAction::show("M5 RF433 not supported");
    Screen.goBack();
    return;
  }

  _rf.begin(_txPin, _rxPin);
  _showMenu();
}

void M5RF433Screen::onUpdate() {
  if (_state == STATE_SIGNAL_INFO) {
    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      onBack();
      return;
    }
    if (_textView.onNav(dir) && Uni.Speaker) Uni.Speaker->beep();
    return;
  }

  if (_state == STATE_RECEIVING) {
    if (_capturedCount < kMaxCapture) {
      M5RF433Util::Signal sig;
      if (_rf.pollReceive(sig) && !_isDuplicate(sig)) {
        _capturedSignals[_capturedCount] = sig;
        _capturedTimes[_capturedCount]   = _generateTimestampName();
        _capturedSaved[_capturedCount]   = false;
        _capturedCount++;
        if (Uni.Speaker) Uni.Speaker->playNotification();
        if (_capturedCount == 1) {
          int n = Achievement.inc("rf_receive_first");
          if (n == 1) Achievement.unlock("rf_receive_first");
        }
        if (_capturedCount >= kMaxCapture) {
          _rf.endReceive();
          snprintf(_titleBuf, sizeof(_titleBuf), "M5 RF433 Full");
        }
        _showReceiveList();
      }
    }

    // Hold-PRESS (500 ms) toggles the filter — fallback for 2-way devices
    // (sticks, T-Display, T-Embed, CYD touch) where LEFT/RIGHT aren't emitted.
    if (!_holdFired && Uni.Nav->isPressed() &&
        Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
        Uni.Nav->heldDuration() >= 500) {
      _holdFired = true;
      _toggleRxFilter();
      render();
      return;
    }
    if (_holdFired) {
      if (Uni.Nav->wasPressed()) {
        Uni.Nav->readDirection();
        _holdFired = false;
      }
      return;
    }

    // Nav: RIGHT/LEFT toggles filter; UP/DOWN/PRESS/BACK navigate the captured list.
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
        _toggleRxFilter();
        render();
      } else if (dir == INavigation::DIR_BACK) {
        onBack();
      } else if (_capturedCount > 0) {
        if (dir == INavigation::DIR_UP) {
          _selectedIndex = (_selectedIndex == 0) ? _capturedCount - 1 : _selectedIndex - 1;
          setCount(_capturedCount);
          render();
        } else if (dir == INavigation::DIR_DOWN) {
          _selectedIndex = (_selectedIndex >= _capturedCount - 1) ? 0 : _selectedIndex + 1;
          setCount(_capturedCount);
          render();
        } else if (dir == INavigation::DIR_PRESS) {
          onItemSelected(_selectedIndex);
        }
      }
      return;
    }

    if (_capturedCount < kMaxCapture && millis() - _lastRender > 500) {
      _lastRender = millis();
      render();
    }
    return;
  }

  if (_state == STATE_JAMMING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _rf.stopJam();
        _showMenu();
        return;
      }
    }

    _rf.jamBurst();
    yield();

    if (millis() - _jamStart > 500) {
      render();
      _jamStart = millis();
    }
    return;
  }

  ListScreen::onUpdate();
}

void M5RF433Screen::onRender() {
  if (_state == STATE_SIGNAL_INFO) {
    _textView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }

  if (_state == STATE_RECEIVING) {
    if (_capturedCount == 0) {
      auto& lcd = Uni.Lcd;
      if (_chromeDrawn) return;
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextSize(1);
      lcd.setTextDatum(MC_DATUM);
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.drawString("433.92 MHz", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 20);
      lcd.drawString("Waiting for signal...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
      lcd.fillRect(bodyX(), bodyY() + bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
      const char* filterLabel = (_rf.getRxFilter() == M5RF433Util::RX_FILTER_CODE)
                                ? "> Filter: Code" : "> Filter: RAW";
      lcd.setTextDatum(ML_DATUM);
      #ifdef DEVICE_HAS_KEYBOARD
        lcd.drawString("BACK: Stop", bodyX() + 4, bodyY() + bodyH() - 8);
      #else
        lcd.drawString("< Stop", bodyX() + 4, bodyY() + bodyH() - 8);
      #endif
      lcd.setTextDatum(MR_DATUM);
      lcd.drawString(filterLabel, bodyX() + bodyW() - 4, bodyY() + bodyH() - 8);
      _chromeDrawn = true;
    } else {
      ListScreen::onRender();
    }
    return;
  }

  if (_state == STATE_JAMMING) {
    if (_chromeDrawn) return;
    auto& lcd = Uni.Lcd;
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);

    lcd.drawString("433.92 MHz", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 20);
    lcd.drawString("Jamming...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);

    #ifdef DEVICE_HAS_KEYBOARD
      lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 10);
    #else
      lcd.fillRect(bodyX(), bodyY() + bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
      lcd.drawString("< Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
    #endif
    _chromeDrawn = true;
    return;
  }

  ListScreen::onRender();
}

void M5RF433Screen::onBack() {
  if (_state == STATE_SIGNAL_INFO) {
    uint8_t idx = _infoIdx;
    if (_infoSource == INFO_FROM_CAPTURE) {
      _state = STATE_RECEIVING;
      _showReceiveList();
      _handleCaptureSelection(idx);
    } else {
      _state = STATE_SEND_BROWSE;
      _loadBrowseDir(_browsePath);
      _showBrowseOptions(idx);
    }
    return;
  }
  if (_state == STATE_MENU) {
    _rf.end();
    Screen.goBack();
  } else if (_state == STATE_RECEIVING) {
    _rf.endReceive();
    _showMenu();
  } else if (_state == STATE_JAMMING) {
    _rf.stopJam();
    _showMenu();
  } else if (_state == STATE_SEND_BROWSE) {
    if (_browsePath == kRootPath) {
      _showMenu();
    } else {
      int slash = _browsePath.lastIndexOf('/');
      if (slash > 0) _loadBrowseDir(_browsePath.substring(0, slash));
      else _showMenu();
    }
  }
}

void M5RF433Screen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0: { // Receive
        if (_rxPin < 0) {
          ShowStatusAction::show("RX pin not available");
          render();
          return;
        }
        _capturedCount = 0;
        _lastRender = 0;
        _state = STATE_RECEIVING;
        _chromeDrawn = false;
        snprintf(_titleBuf, sizeof(_titleBuf), "M5 RF433 RX (0/%d)", kMaxCapture);
        _rf.beginReceive();
        setItems(_capturedItems, 0);
        break;
      }
      case 1: { // Send
        if (_txPin < 0) {
          ShowStatusAction::show("TX pin not available");
          render();
          return;
        }
        _loadBrowseDir(kRootPath);
        break;
      }
      case 2: { // Jammer
        if (_txPin < 0) {
          ShowStatusAction::show("TX pin not available");
          render();
          return;
        }
        _rf.startJam();
        _state = STATE_JAMMING;
        _chromeDrawn = false;
        _jamStart = millis();
        strcpy(_titleBuf, "M5 RF433 Jam");
        {
          int n = Achievement.inc("rf_jammer_first");
          if (n == 1) Achievement.unlock("rf_jammer_first");
        }
        render();
        break;
      }
      default:
        return;
    }
    return;
  }

  if (_state == STATE_RECEIVING) {
    if (index < _capturedCount) {
      _handleCaptureSelection(index);
      if (_capturedCount < kMaxCapture) _rf.beginReceive();
    }
    return;
  }

  if (_state == STATE_SEND_BROWSE) {
    if (index >= _browser.count()) return;
    if (_browser.entry(index).isDir) {
      _loadBrowseDir(_browser.entry(index).path);
      return;
    }
    _showBrowseOptions(index);
    return;
  }
}

// ── Browse helpers ────────────────────────────────────────────────────────

void M5RF433Screen::_sendBrowseFile(uint8_t index) {
  const auto& e = _browser.entry(index);
  String content = Uni.Storage->readFile(e.path.c_str());
  if (content.length() == 0) {
    ShowStatusAction::show("Failed to read file");
    render();
    return;
  }
  M5RF433Util::Signal sig;
  if (!CC1101Util::loadFile(content, sig)) {
    ShowStatusAction::show("Invalid .sub file");
    render();
    return;
  }
  ProgressView::init();
  ProgressView::progress(("Sending " + e.name).c_str(), 50);
  _rf.sendSignal(sig);
  {
    int n = Achievement.inc("rf_send_first");
    if (n == 1) Achievement.unlock("rf_send_first");
  }
  ShowStatusAction::show(("Sent: " + e.name).c_str(), 1000);
  render();
}

void M5RF433Screen::_showBrowseFileInfo(uint8_t index) {
  String content = Uni.Storage->readFile(_browser.entry(index).path.c_str());
  M5RF433Util::Signal sig;
  if (!CC1101Util::loadFile(content, sig)) {
    ShowStatusAction::show("Invalid .sub file");
    render();
    return;
  }
  _showSignalInfo(sig, INFO_FROM_BROWSE, index);
}

void M5RF433Screen::_showBrowseOptions(uint8_t index) {
  static constexpr InputSelectAction::Option fileOpts[] = {
    {"Send",   "send"},
    {"Info",   "info"},
    {"Rename", "rename"},
    {"Delete", "delete"},
  };
  const char* choice = InputSelectAction::popup("Options", fileOpts, 4, "send");
  if (!choice) { render(); return; }

  if (strcmp(choice, "send") == 0) {
    _sendBrowseFile(index);

  } else if (strcmp(choice, "info") == 0) {
    _showBrowseFileInfo(index);

  } else if (strcmp(choice, "rename") == 0) {
    String curName = _browser.entry(index).name;
    if (curName.endsWith(".sub")) curName = curName.substring(0, curName.length() - 4);
    String newName = InputTextAction::popup("Rename", curName.c_str());
    if (newName.length() == 0) { render(); return; }
    String content = Uni.Storage->readFile(_browser.entry(index).path.c_str());
    String newPath = _makeUniquePath(newName);
    if (Uni.Storage->writeFile(newPath.c_str(), content.c_str())) {
      Uni.Storage->deleteFile(_browser.entry(index).path.c_str());
      ShowStatusAction::show("Renamed", 1000);
      _loadBrowseDir(_browsePath);
    } else {
      ShowStatusAction::show("Rename failed");
      render();
    }

  } else if (strcmp(choice, "delete") == 0) {
    if (Uni.Storage->deleteFile(_browser.entry(index).path.c_str())) {
      ShowStatusAction::show("Deleted", 1000);
      _loadBrowseDir(_browsePath);
    } else {
      ShowStatusAction::show("Delete failed");
      render();
    }
  }
}

// ── Menu ──────────────────────────────────────────────────────────────────

void M5RF433Screen::_showMenu() {
  _state = STATE_MENU;
  _chromeDrawn = false;
  strcpy(_titleBuf, "M5 RF433");
  setItems(_menuItems, kMenuCount);
}

void M5RF433Screen::_toggleRxFilter() {
  auto cur = _rf.getRxFilter();
  _rf.setRxFilter(cur == M5RF433Util::RX_FILTER_CODE
                  ? M5RF433Util::RX_FILTER_RAW
                  : M5RF433Util::RX_FILTER_CODE);
  _chromeDrawn = false;  // redraw waiting-screen footer with new label
  if (Uni.Speaker) Uni.Speaker->beep();
}

// ── Captured List ──────────────────────────────────────────────────────────

void M5RF433Screen::_showReceiveList() {
  snprintf(_titleBuf, sizeof(_titleBuf), "M5 RF433 RX (%d/%d)", _capturedCount, kMaxCapture);
  _rebuildCapturedItems();
  setCount(_capturedCount);
  render();
}

void M5RF433Screen::_handleCaptureSelection(uint8_t index) {
  if (index >= _capturedCount) return;

  static constexpr InputSelectAction::Option captureOpts[] = {
    {"Info",   "info"},
    {"Replay", "replay"},
    {"Save",   "save"},
    {"Delete", "delete"},
  };
  const char* choice = InputSelectAction::popup("Options", captureOpts, 4);
  if (!choice) { render(); return; }

  if (strcmp(choice, "info") == 0) {
    _showSignalInfo(_capturedSignals[index], INFO_FROM_CAPTURE, index);
    return;
  }

  if (strcmp(choice, "replay") == 0) {
    _sendCapturedSignal(index);

  } else if (strcmp(choice, "save") == 0) {
    if (_capturedSaved[index]) {
      ShowStatusAction::show("Already saved");
      render();
      return;
    }
    String name = InputTextAction::popup("Save As", _capturedTimes[index].c_str());
    if (name.length() == 0) { render(); return; }
    _saveSignal(index, name);
    _rebuildCapturedItems();
    render();

  } else if (strcmp(choice, "delete") == 0) {
    for (uint8_t i = index; i + 1 < _capturedCount; i++) {
      _capturedSignals[i] = _capturedSignals[i + 1];
      _capturedTimes[i]   = _capturedTimes[i + 1];
      _capturedSaved[i]   = _capturedSaved[i + 1];
    }
    _capturedCount--;
    _showReceiveList();
  }
}

void M5RF433Screen::_showSignalInfo(const M5RF433Util::Signal& sig, InfoSource src, uint8_t idx) {
  _infoSource = src;
  _infoIdx    = idx;
  strcpy(_titleBuf, "Signal Info");
  _state = STATE_SIGNAL_INFO;
  _textView.setContent(CC1101Util::signalInfoText(sig));
  render();
}

void M5RF433Screen::_rebuildCapturedItems() {
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const M5RF433Util::Signal& sig = _capturedSignals[i];
    if (_capturedSaved[i]) {
      _capturedSubLabels[i] = "Saved";
    } else if (sig.protocol == "RcSwitch") {
      char buf[32];
      snprintf(buf, sizeof(buf), "0x%llX P%s %db",
               (unsigned long long)sig.key, sig.preset.c_str(), sig.bit);
      _capturedSubLabels[i] = buf;
    } else {
      int pulses = 0;
      for (char c : sig.rawData) { if (c == ' ') pulses++; }
      pulses++;
      char buf[24];
      snprintf(buf, sizeof(buf), "RAW %d pulses", pulses);
      _capturedSubLabels[i] = buf;
    }
    _capturedItems[i] = {_capturedTimes[i].c_str(), _capturedSubLabels[i].c_str()};
  }
}

void M5RF433Screen::_sendCapturedSignal(uint8_t index) {
  if (index >= _capturedCount) return;
  if (_txPin < 0) {
    ShowStatusAction::show("Set M5 RF433T (TX) pin first");
    render();
    return;
  }
  ProgressView::init();
  ProgressView::progress(("Replaying " + _capturedTimes[index]).c_str(), 50);
  _rf.sendSignal(_capturedSignals[index]);
  {
    int n = Achievement.inc("rf_send_first");
    if (n == 1) Achievement.unlock("rf_send_first");
  }
  ShowStatusAction::show("Replayed", 1000);
  render();
}

void M5RF433Screen::_saveSignal(uint8_t index, const String& name) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage");
    return;
  }
  Uni.Storage->makeDir(kRootPath);
  String path    = _makeUniquePath(name);
  String content = CC1101Util::saveToString(_capturedSignals[index]);
  if (Uni.Storage->writeFile(path.c_str(), content.c_str())) {
    _capturedSaved[index] = true;
    _capturedSubLabels[index] = "Saved";
    _capturedItems[index].sublabel = _capturedSubLabels[index].c_str();
    int lastSlash = path.lastIndexOf('/');
    String fname = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
    ShowStatusAction::show(fname.c_str(), 1200);
    {
      int n = Achievement.inc("rf_signal_saved");
      if (n == 1)  Achievement.unlock("rf_signal_saved");
      if (n == 5)  Achievement.unlock("rf_signal_saved_5");
      if (n == 20) Achievement.unlock("rf_signal_saved_20");
    }
  } else {
    ShowStatusAction::show("Save failed");
  }
}

bool M5RF433Screen::_isDuplicate(const M5RF433Util::Signal& sig) const {
  if (sig.protocol != "RcSwitch") return false;
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const M5RF433Util::Signal& s = _capturedSignals[i];
    if (s.protocol == "RcSwitch" &&
        s.key    == sig.key    &&
        s.preset == sig.preset &&
        s.bit    == sig.bit) {
      return true;
    }
  }
  return false;
}

String M5RF433Screen::_generateTimestampName() {
  uint32_t s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "rf_%05lu", (unsigned long)(s % 100000));
  return String(buf);
}

// ── File Browser ──────────────────────────────────────────────────────────

String M5RF433Screen::_makeUniquePath(const String& name) {
  String base = String(kRootPath) + "/" + name + ".sub";
  if (!Uni.Storage || !Uni.Storage->exists(base.c_str())) return base;
  for (int n = 2; n < 1000; n++) {
    String candidate = String(kRootPath) + "/" + name + "_(" + n + ").sub";
    if (!Uni.Storage->exists(candidate.c_str())) return candidate;
  }
  return base;
}

void M5RF433Screen::_loadBrowseDir(const String& path) {
  _browsePath = path;
  _state = STATE_SEND_BROWSE;

  int lastSlash = path.lastIndexOf('/');
  String folderName = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  snprintf(_titleBuf, sizeof(_titleBuf), "RF: %s", folderName.c_str());

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    Uni.Storage->makeDir(path.c_str());
  }

  uint8_t n = _browser.load(this, path, ".sub");
  setItems(_browser.items(), n);
}
