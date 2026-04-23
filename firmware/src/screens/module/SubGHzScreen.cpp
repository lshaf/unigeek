#include "SubGHzScreen.h"
#include "core/AchievementManager.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/PinConfigManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

void SubGHzScreen::onInit() {
  _csPin   = PinConfig.get(PIN_CONFIG_CC1101_CS,   PIN_CONFIG_CC1101_CS_DEFAULT).toInt();
  _gdo0Pin = PinConfig.get(PIN_CONFIG_CC1101_GDO0, PIN_CONFIG_CC1101_GDO0_DEFAULT).toInt();

  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CC1101 pins first");
    Screen.goBack();
    return;
  }

  ProgressView::init();
  ProgressView::progress("Detecting CC1101...", 30);
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found!");
    Screen.goBack();
    return;
  }
  _rf.end();

  _showMenu();
}

void SubGHzScreen::onUpdate() {
  if (_state == STATE_SCANNING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _rf.endScan();
        _rf.end();
        _showMenu();
        return;
      }
    }
    _rf.stepScan();
    if (!_rfDetectFired && _rf.getScanRssi() > CC1101Util::RSSI_THRESHOLD) {
      _rfDetectFired = true;
      int n = Achievement.inc("rf_detect_freq");
      if (n == 1) Achievement.unlock("rf_detect_freq");
    }
    render();
    return;
  }

  if (_state == STATE_RECEIVING) {
    if (_capturedCount < kMaxCapture) {
      CC1101Util::Signal sig;
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
          snprintf(_titleBuf, sizeof(_titleBuf), "Sub-GHz Full");
        }
        _showReceiveList();
      }
    }
    // Blink title indicator while still listening
    if (_capturedCount < kMaxCapture && millis() - _lastRender > 500) {
      _lastRender = millis();
      render();
    }
  }

  if (_state == STATE_JAMMING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        digitalWrite(_gdo0Pin, LOW);
        _rf.end();
        _showMenu();
        return;
      }
    }

    for (int i = 0; i < 50; i++) {
      uint32_t pw  = 5 + (micros() % 46);
      uint32_t gap = 5 + (micros() % 96);
      digitalWrite(_gdo0Pin, HIGH); delayMicroseconds(pw);
      digitalWrite(_gdo0Pin, LOW);  delayMicroseconds(gap);
    }
    yield();

    if (millis() - _jamStart > 500) {
      render();
      _jamStart = millis();
    }
    return;
  }

  if (_state == STATE_SEND_BROWSE) {
    if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
      _holdFired = true;
      _pendingHoldIdx = _selectedIndex;
      // Show popup immediately — don't wait for release
      if (_pendingHoldIdx < _browseCount && !_browseIsDir[_pendingHoldIdx])
        _showBrowseOptions(_pendingHoldIdx);
      else
        render();
      return;
    }
  }
  if (_holdFired) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();  // consume the release so it doesn't trigger onItemSelected
      _holdFired = false;
    }
    return;
  }

  ListScreen::onUpdate();
}

void SubGHzScreen::onRender() {
  if (_state == STATE_RECEIVING) {
    if (_capturedCount == 0) {
      auto& lcd = Uni.Lcd;
      // Waiting view is fully static — paint once per state entry.
      if (_chromeDrawn) return;
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextSize(1);
      lcd.setTextDatum(MC_DATUM);
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      char freqStr[24];
      snprintf(freqStr, sizeof(freqStr), "%.2f MHz", _rf.getFrequency());
      lcd.drawString(freqStr, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 20);
      lcd.drawString("Waiting for signal...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
      lcd.fillRect(bodyX(), bodyY() + bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
      #ifdef DEVICE_HAS_KEYBOARD
        lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
      #else
        lcd.drawString("< Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
      #endif
      _chromeDrawn = true;
    } else {
      ListScreen::onRender();
    }
    return;
  }

  if (_state == STATE_SCANNING) {
    auto& lcd = Uni.Lcd;

    static constexpr int kRssiFloor   = -110;
    static constexpr int kRssiCeiling = -30;
    static constexpr int kRssiRange   = kRssiCeiling - kRssiFloor; // 80

    const int footerH = 16;
    const int infoH   = 26;
    const int contentH = bodyH() - footerH;   // info + chart area
    const int chartY   = infoH;                // inside sprite
    const int chartH   = contentH - infoH;

    // Footer chrome painted once.
    if (!_chromeDrawn) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextSize(1);
      lcd.setTextDatum(MC_DATUM);
      lcd.fillRect(bodyX(), bodyY() + bodyH() - footerH, bodyW(), footerH, Config.getThemeColor());
      lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
      #ifdef DEVICE_HAS_KEYBOARD
        lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
      #else
        lcd.drawString("< Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
      #endif
      _chromeDrawn = true;
    }

    uint8_t n = _rf.getScanCount();
    int barW  = bodyW() / (n ? n : 1);
    if (barW < 1) barW = 1;

    uint8_t bestIdx  = 0;
    int     bestRssi = -120;
    for (uint8_t i = 0; i < n; i++) {
      int r = _rf.getScanRssiAt(i);
      if (r > bestRssi) { bestRssi = r; bestIdx = i; }
    }

    Sprite sp(&lcd);
    sp.createSprite(bodyW(), contentH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);

    // Bars
    for (uint8_t i = 0; i < n; i++) {
      int rssi    = _rf.getScanRssiAt(i);
      int clamped = constrain(rssi, kRssiFloor, kRssiCeiling);
      int barH    = (clamped - kRssiFloor) * chartH / kRssiRange;
      int x       = i * barW;
      int y       = chartY + chartH - barH;

      uint16_t color;
      if (i == bestIdx && rssi > CC1101Util::RSSI_THRESHOLD)     color = TFT_YELLOW;
      else if (rssi > CC1101Util::RSSI_THRESHOLD)                color = TFT_GREEN;
      else if (rssi > kRssiFloor + 10)                           color = 0x2945;
      else                                                       color = TFT_DARKGREY;

      if (barH > 0) sp.fillRect(x, y, barW - 1, barH, color);
    }

    // Cursor
    for (uint8_t i = 0; i < n; i++) {
      if (fabsf(_rf.getScanFreqAt(i) - _rf.getScanFreq()) < 0.01f) {
        sp.drawFastVLine(i * barW + barW / 2, chartY, chartH, TFT_WHITE);
        break;
      }
    }

    // Info
    sp.setTextDatum(ML_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    char freqBuf[20];
    snprintf(freqBuf, sizeof(freqBuf), "%.3f MHz", _rf.getScanFreq());
    sp.drawString(freqBuf, 2, 7);

    sp.setTextDatum(MR_DATUM);
    char rssiBuf[16];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", _rf.getScanRssi());
    uint16_t rssiColor = (_rf.getScanRssi() > CC1101Util::RSSI_THRESHOLD) ? TFT_GREEN : TFT_CYAN;
    sp.setTextColor(rssiColor, TFT_BLACK);
    sp.drawString(rssiBuf, bodyW() - 2, 7);

    sp.setTextDatum(ML_DATUM);
    if (bestRssi > CC1101Util::RSSI_THRESHOLD) {
      sp.setTextColor(TFT_YELLOW, TFT_BLACK);
      char bestBuf[28];
      snprintf(bestBuf, sizeof(bestBuf), "> %.3f MHz %ddBm", _rf.getScanFreqAt(bestIdx), bestRssi);
      sp.drawString(bestBuf, 2, 19);
    } else {
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("No signal", 2, 19);
    }

    sp.pushSprite(bodyX(), bodyY());
    sp.deleteSprite();
    return;
  }

  if (_state == STATE_JAMMING) {
    // Fully static — paint once per state entry.
    if (_chromeDrawn) return;
    auto& lcd = Uni.Lcd;
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);

    char freqStr[16];
    snprintf(freqStr, sizeof(freqStr), "%.2f MHz", _rf.getFrequency());
    lcd.drawString(freqStr, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 20);
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

void SubGHzScreen::onBack() {
  if (_state == STATE_MENU) {
    _rf.end();
    Screen.goBack();
  } else if (_state == STATE_RECEIVING) {
    _rf.end();
    _showMenu();
  } else if (_state == STATE_SCANNING) {
    _rf.endScan();
    _rf.end();
    _showMenu();
  } else if (_state == STATE_JAMMING) {
    digitalWrite(_gdo0Pin, LOW);
    _rf.end();
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

void SubGHzScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0: { // Frequency
        _selectFrequency();
        return;
      }
      case 1: { // Detect Freq
        _startScan();
        return;
      }
      case 2: { // Receive
        if (_csPin < 0 || _gdo0Pin < 0) {
          ShowStatusAction::show("Set CS and GDO0 pins first");
          render();
          return;
        }
        if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
          ShowStatusAction::show("CC1101 not found");
          render();
          return;
        }
        _capturedCount = 0;
        _lastRender = 0;
        _state = STATE_RECEIVING;
        _chromeDrawn = false;
        snprintf(_titleBuf, sizeof(_titleBuf), "Sub-GHz RX (0/%d)", kMaxCapture);
        _rf.beginReceive();
        setItems(_capturedItems, 0);  // prime pointer once; _showReceiveList uses setCount after this
        break;
      }
      case 3: { // Send
        if (_csPin < 0) {
          ShowStatusAction::show("Set CS pin first");
          render();
          return;
        }
        _loadBrowseDir(kRootPath);
        break;
      }
      case 4: { // Jammer
        if (_csPin < 0 || _gdo0Pin < 0) {
          ShowStatusAction::show("Set CS and GDO0 pins first");
          render();
          return;
        }
        if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
          ShowStatusAction::show("CC1101 not found");
          render();
          return;
        }
        _rf.startTx();
        _state = STATE_JAMMING;
        _chromeDrawn = false;
        _jamStart = millis();
        strcpy(_titleBuf, "Sub-GHz Jam");
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
      // If a deletion freed a slot and receive was stopped, restart it
      if (_capturedCount < kMaxCapture) _rf.beginReceive();
    }
    return;
  }

  if (_state == STATE_SEND_BROWSE) {
    if (index >= _browseCount) return;
    if (_browseIsDir[index]) {
      _loadBrowseDir(_browsePaths[index]);
      return;
    }
    _sendBrowseFile(index);
    return;
  }
}

// ── Browse helpers ────────────────────────────────────────────────────────

void SubGHzScreen::_sendBrowseFile(uint8_t index) {
  String content = Uni.Storage->readFile(_browsePaths[index].c_str());
  if (content.length() == 0) {
    ShowStatusAction::show("Failed to read file");
    render();
    return;
  }
  CC1101Util::Signal sig;
  if (!CC1101Util::loadFile(content, sig)) {
    ShowStatusAction::show("Invalid .sub file");
    render();
    return;
  }
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  ProgressView::init();
  ProgressView::progress(("Sending " + _browseNames[index]).c_str(), 50);
  _rf.sendSignal(sig);
  _rf.end();
  {
    int n = Achievement.inc("rf_send_first");
    if (n == 1) Achievement.unlock("rf_send_first");
  }
  ShowStatusAction::show(("Sent: " + _browseNames[index]).c_str(), 1000);
  render();
}

void SubGHzScreen::_showBrowseOptions(uint8_t index) {
  static constexpr InputSelectAction::Option fileOpts[] = {
    {"Send",   "send"},
    {"Rename", "rename"},
    {"Delete", "delete"},
  };
  const char* choice = InputSelectAction::popup("Options", fileOpts, 3, "send");
  if (!choice) { render(); return; }

  if (strcmp(choice, "send") == 0) {
    _sendBrowseFile(index);

  } else if (strcmp(choice, "rename") == 0) {
    String curName = _browseNames[index];
    if (curName.endsWith(".sub")) curName = curName.substring(0, curName.length() - 4);
    String newName = InputTextAction::popup("Rename", curName.c_str());
    if (newName.length() == 0) { render(); return; }
    String content = Uni.Storage->readFile(_browsePaths[index].c_str());
    String newPath = _makeUniquePath(newName);
    if (Uni.Storage->writeFile(newPath.c_str(), content.c_str())) {
      Uni.Storage->deleteFile(_browsePaths[index].c_str());
      ShowStatusAction::show("Renamed", 1000);
      _loadBrowseDir(_browsePath);
    } else {
      ShowStatusAction::show("Rename failed");
      render();
    }

  } else if (strcmp(choice, "delete") == 0) {
    if (Uni.Storage->deleteFile(_browsePaths[index].c_str())) {
      ShowStatusAction::show("Deleted", 1000);
      _loadBrowseDir(_browsePath);
    } else {
      ShowStatusAction::show("Delete failed");
      render();
    }
  }
}

// ── Menu ──────────────────────────────────────────────────────────────────

void SubGHzScreen::_showMenu() {
  _state = STATE_MENU;
  _chromeDrawn = false;
  strcpy(_titleBuf, "Sub-GHz");
  _updateSublabels();
  setItems(_menuItems, kMenuCount);
}

void SubGHzScreen::_updateSublabels() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%.2f MHz", _rf.getFrequency());
  _freqSub = buf;
  _menuItems[0].sublabel = _freqSub.c_str();
}

void SubGHzScreen::_startScan() {
  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CS and GDO0 pins first");
    render();
    return;
  }
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  _rfDetectFired = false;
  _rf.beginScan();
  _state = STATE_SCANNING;
  _chromeDrawn = false;
  strcpy(_titleBuf, "Detect Freq");
  render();
}

void SubGHzScreen::_selectFrequency() {
  static constexpr InputSelectAction::Option freqOpts[] = {
    {"300 MHz",    "300"},
    {"315 MHz",    "315"},
    {"345 MHz",    "345"},
    {"390 MHz",    "390"},
    {"433.92 MHz", "433.92"},
    {"434 MHz",    "434"},
    {"868 MHz",    "868"},
    {"915 MHz",    "915"},
    {"Custom",     "custom"},
  };

  char curBuf[12];
  snprintf(curBuf, sizeof(curBuf), "%.2f", _rf.getFrequency());

  const char* choice = InputSelectAction::popup("Frequency", freqOpts, 9, curBuf);
  if (!choice) { render(); return; }

  float mhz;
  if (strcmp(choice, "custom") == 0) {
    int val = InputNumberAction::popup("MHz (280-928)", 280, 928, (int)_rf.getFrequency());
    if (InputNumberAction::wasCancelled()) { render(); return; }
    mhz = (float)val;
  } else {
    mhz = atof(choice);
  }

  if (!_rf.setFrequency(mhz)) {
    ShowStatusAction::show("Invalid frequency");
  }
  _updateSublabels();
  render();
}

// ── Captured List ──────────────────────────────────────────────────────────

void SubGHzScreen::_showReceiveList() {
  snprintf(_titleBuf, sizeof(_titleBuf), "Sub-GHz RX (%d/%d)", _capturedCount, kMaxCapture);
  _rebuildCapturedItems();   // update array in-place (SettingScreen pattern)
  setCount(_capturedCount);  // update count, clamp selection, adjust scroll — no render
  render();                  // one render at the current selection
}

void SubGHzScreen::_handleCaptureSelection(uint8_t index) {
  if (index >= _capturedCount) return;

  static constexpr InputSelectAction::Option captureOpts[] = {
    {"Replay", "replay"},
    {"Save",   "save"},
    {"Delete", "delete"},
  };
  const char* choice = InputSelectAction::popup("Options", captureOpts, 3);
  if (!choice) { render(); return; }

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

void SubGHzScreen::_rebuildCapturedItems() {
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const CC1101Util::Signal& sig = _capturedSignals[i];
    if (_capturedSaved[i]) {
      _capturedSubLabels[i] = "Saved";
    } else if (sig.protocol == "RcSwitch") {
      char buf[32];
      snprintf(buf, sizeof(buf), "0x%llX P%s %db",
               (unsigned long long)sig.key, sig.preset.c_str(), sig.bit);
      _capturedSubLabels[i] = buf;
    } else {
      // Count pulses in RAW data as a proxy for complexity
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

void SubGHzScreen::_sendCapturedSignal(uint8_t index) {
  if (index >= _capturedCount) return;
  if (_csPin < 0) {
    ShowStatusAction::show("Set CS pin first");
    render();
    return;
  }
  // Replay flow: stop receive → init/send/end transfer → start receive.
  // Chip is already initialised by STATE_RECEIVING. We must detach the RX
  // ISR before TX or every pulse on GDO0 we drive for transmission would
  // re-fire the receive interrupt (corrupts RX buffer + adds pulse jitter).
  // The caller (onItemSelected STATE_RECEIVING) re-arms RX via beginReceive().
  _rf.endReceive();
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

void SubGHzScreen::_saveSignal(uint8_t index, const String& name) {
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

bool SubGHzScreen::_isDuplicate(const CC1101Util::Signal& sig) const {
  if (sig.protocol != "RcSwitch") return false; // RAW: accept all, timing jitter makes dedup unreliable
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const CC1101Util::Signal& s = _capturedSignals[i];
    if (s.protocol == "RcSwitch" &&
        s.key    == sig.key    &&
        s.preset == sig.preset &&
        s.bit    == sig.bit) {
      return true;
    }
  }
  return false;
}

String SubGHzScreen::_generateTimestampName() {
  uint32_t s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "rf_%05lu", (unsigned long)(s % 100000));
  return String(buf);
}

// ── File Browser ──────────────────────────────────────────────────────────

String SubGHzScreen::_makeUniquePath(const String& name) {
  String base = String(kRootPath) + "/" + name + ".sub";
  if (!Uni.Storage || !Uni.Storage->exists(base.c_str())) return base;
  for (int n = 2; n < 1000; n++) {
    String candidate = String(kRootPath) + "/" + name + "_(" + n + ").sub";
    if (!Uni.Storage->exists(candidate.c_str())) return candidate;
  }
  return base;
}

void SubGHzScreen::_loadBrowseDir(const String& path) {
  _browsePath  = path;
  _browseCount = 0;
  _state = STATE_SEND_BROWSE;

  int lastSlash = path.lastIndexOf('/');
  String folderName = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  snprintf(_titleBuf, sizeof(_titleBuf), "RF: %s", folderName.c_str());

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    setItems(_browseItems, 0);
    return;
  }

  Uni.Storage->makeDir(path.c_str());

  IStorage::DirEntry entries[kMaxBrowse];
  uint8_t total = Uni.Storage->listDir(path.c_str(), entries, kMaxBrowse);

  // Sort: dirs first, then alphabetical
  for (uint8_t i = 0; i < total; i++) {
    for (uint8_t j = i + 1; j < total; j++) {
      bool swap = false;
      if (entries[j].isDir && !entries[i].isDir) swap = true;
      else if (entries[i].isDir == entries[j].isDir &&
               strcasecmp(entries[j].name.c_str(), entries[i].name.c_str()) < 0) swap = true;
      if (swap) {
        IStorage::DirEntry tmp = entries[i];
        entries[i] = entries[j];
        entries[j] = tmp;
      }
    }
  }

  for (uint8_t i = 0; i < total && _browseCount < kMaxBrowse; i++) {
    const String& name = entries[i].name;
    bool isDir = entries[i].isDir;
    if (!isDir && !name.endsWith(".sub")) continue;

    _browseNames[_browseCount]  = name;
    _browsePaths[_browseCount]  = path + "/" + name;
    _browseIsDir[_browseCount]  = isDir;
    _browseItems[_browseCount]  = {_browseNames[_browseCount].c_str(),
                                   isDir ? ">" : nullptr};
    _browseCount++;
  }

  setItems(_browseItems, _browseCount);
}