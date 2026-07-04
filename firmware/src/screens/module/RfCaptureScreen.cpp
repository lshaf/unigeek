#include "RfCaptureScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/rf/KeeloqUtil.h"

// ── Lifecycle hooks ──────────────────────────────────────────────────────────

void RfCaptureScreen::onUpdate() {
  if (_onUpdateExtra()) return;

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
      Signal sig;
      if (_radioPollReceive(sig) && !_isDuplicate(sig)) {
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
          _radioEndReceive();
          snprintf(_titleBuf, sizeof(_titleBuf), "%s Full", _titlePrefix());
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
        Uni.Nav->readDirection();  // swallow the release
        _holdFired = false;
      }
      return;
    }

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

    // Blink title indicator while still listening
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
        _radioStopJam();
        _showMenu();
        return;
      }
    }

    _radioJamBurst();
    yield();

    if (millis() - _jamStart > 500) {
      render();
      _jamStart = millis();
    }
    return;
  }

  // STATE_MENU and STATE_SEND_BROWSE use ListScreen's default nav handling.
  ListScreen::onUpdate();
}

void RfCaptureScreen::onRender() {
  if (_onRenderExtra()) return;

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
      char freqStr[24];
      _radioFreqLabel(freqStr, sizeof(freqStr));
      lcd.drawString(freqStr, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 20);
      lcd.drawString("Waiting for signal...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
      lcd.fillRect(bodyX(), bodyY() + bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
      const char* filterLabel = (_radioGetRxFilter() == CC1101Util::RX_FILTER_CODE)
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

    char freqStr[24];
    _radioFreqLabel(freqStr, sizeof(freqStr));
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

void RfCaptureScreen::onBack() {
  if (_onBackExtra()) return;

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
    _radioShutdown();
    Screen.goBack();
  } else if (_state == STATE_RECEIVING) {
    _radioEndReceive();
    _showMenu();
  } else if (_state == STATE_JAMMING) {
    _radioStopJam();
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

void RfCaptureScreen::onItemSelected(uint8_t index) {
  if (_onItemSelectedExtra(index)) return;

  if (_state == STATE_MENU) {
    _onMenuSelected(index);
    return;
  }

  if (_state == STATE_RECEIVING) {
    if (index < _capturedCount) {
      _handleCaptureSelection(index);
      // If a deletion freed a slot and receive was stopped, restart it.
      if (_capturedCount < kMaxCapture) _radioBeginReceive();
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

// ── State transitions ────────────────────────────────────────────────────────

void RfCaptureScreen::_enterReceiveMode() {
  _capturedCount = 0;
  _lastRender    = 0;
  _state         = STATE_RECEIVING;
  _chromeDrawn   = false;
  snprintf(_titleBuf, sizeof(_titleBuf), "%s RX (0/%d)", _titlePrefix(), kMaxCapture);
  _radioBeginReceive();
  setItems(_capturedItems, 0);
}

void RfCaptureScreen::_enterJammingMode() {
  _state       = STATE_JAMMING;
  _chromeDrawn = false;
  _jamStart    = millis();
  snprintf(_titleBuf, sizeof(_titleBuf), "%s Jam", _titlePrefix());
  int n = Achievement.inc("rf_jammer_first");
  if (n == 1) Achievement.unlock("rf_jammer_first");
  render();
}

// ── Receive list helpers ─────────────────────────────────────────────────────

void RfCaptureScreen::_showReceiveList() {
  snprintf(_titleBuf, sizeof(_titleBuf), "%s RX (%d/%d)",
           _titlePrefix(), _capturedCount, kMaxCapture);
  _rebuildCapturedItems();
  setCount(_capturedCount);
  // When the list empties (e.g. all signals deleted), force a chrome redraw so
  // the "Waiting for signal" screen returns instead of a stale/black list.
  if (_capturedCount == 0) {
    _chromeDrawn   = false;
    _selectedIndex = 0;
  } else if (_selectedIndex >= _capturedCount) {
    _selectedIndex = _capturedCount - 1;
  }
  render();
}

void RfCaptureScreen::_handleCaptureSelection(uint8_t index) {
  if (index >= _capturedCount) return;

  // Show "Replay +1" only when KeeLoq decoded AND the manufacturer key is
  // still in the keystore — step() would no-op otherwise.
  const Signal& sig = _capturedSignals[index];
  bool keeloqStep = (sig.protocol == "RcSwitch" && sig.preset == "23" &&
                    sig.mf_name.length() > 0);

  InputSelectAction::Option captureOpts[5] = {
    {"Info",   "info"},
    {"Replay", "replay"},
  };
  uint8_t optCount = 2;
  if (keeloqStep) captureOpts[optCount++] = {"Replay +1", "replay_step"};
  captureOpts[optCount++] = {"Save",   "save"};
  captureOpts[optCount++] = {"Delete", "delete"};

  const char* choice = InputSelectAction::popup("Options", captureOpts, optCount);
  if (!choice) { render(); return; }

  if (strcmp(choice, "info") == 0) {
    _showSignalInfo(_capturedSignals[index], INFO_FROM_CAPTURE, index);
    return;
  }

  if (strcmp(choice, "replay_step") == 0) {
    _replayStepKeeloqSignal(index);

  } else if (strcmp(choice, "replay") == 0) {
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

void RfCaptureScreen::_rebuildCapturedItems() {
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const Signal& sig = _capturedSignals[i];
    if (_capturedSaved[i]) {
      _capturedSubLabels[i] = "Saved";
    } else if (sig.protocol == "RcSwitch" && sig.preset == "23" && sig.mf_name.length() > 0) {
      // KeeLoq with manufacturer identified — show counter so Replay +1
      // advance is visible in the list sublabel.
      char buf[40];
      snprintf(buf, sizeof(buf), "%s cnt=%u", sig.mf_name.c_str(), sig.cnt);
      _capturedSubLabels[i] = buf;
    } else if (sig.protocol == "RcSwitch") {
      // Show the chip/remote name when known (e.g. "HT6P20B"), else "Pxx".
      const char* pname = CC1101Util::rcSwitchProtoName(sig.preset.toInt());
      char buf[48];
      if (pname)
        snprintf(buf, sizeof(buf), "%s 0x%llX %db",
                 pname, (unsigned long long)sig.key, sig.bit);
      else
        snprintf(buf, sizeof(buf), "P%s 0x%llX %db",
                 sig.preset.c_str(), (unsigned long long)sig.key, sig.bit);
      _capturedSubLabels[i] = buf;
    } else if (sig.protocol != "RAW" && sig.bit > 0) {
      // Brand-decoded (CAME, Holtek, Linear, ...) — show the protocol name + key.
      char buf[40];
      snprintf(buf, sizeof(buf), "%s 0x%llX %db",
               sig.protocol.c_str(), (unsigned long long)sig.key, sig.bit);
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

void RfCaptureScreen::_sendCapturedSignal(uint8_t index) {
  if (index >= _capturedCount) return;
  ProgressView::init();
  ProgressView::progress(("Replaying " + _capturedTimes[index]).c_str(), 50);
  _radioSendCaptured(_capturedSignals[index]);
  int n = Achievement.inc("rf_send_first");
  if (n == 1) Achievement.unlock("rf_send_first");
  ShowStatusAction::show("Replayed", 1000);
  render();
}

void RfCaptureScreen::_replayStepKeeloqSignal(uint8_t index) {
  if (index >= _capturedCount) return;

  Signal& sig = _capturedSignals[index];
  if (!KeeloqUtil::step(sig)) {
    ShowStatusAction::show("Manufacturer key missing");
    render();
    return;
  }

  ProgressView::init();
  char buf[48];
  snprintf(buf, sizeof(buf), "Replay %s cnt=%u", sig.mf_name.c_str(), sig.cnt);
  ProgressView::progress(buf, 50);
  _radioSendCaptured(sig);
  _rebuildCapturedItems();

  int nk = Achievement.inc("rf_keeloq_step_replay");
  if (nk == 1) Achievement.unlock("rf_keeloq_step_replay");
  int n = Achievement.inc("rf_send_first");
  if (n == 1) Achievement.unlock("rf_send_first");

  ShowStatusAction::show("Replayed +1", 1000);
  render();
}

void RfCaptureScreen::_saveSignal(uint8_t index, const String& name) {
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
    int n = Achievement.inc("rf_signal_saved");
    if (n == 1)  Achievement.unlock("rf_signal_saved");
    if (n == 5)  Achievement.unlock("rf_signal_saved_5");
    if (n == 20) Achievement.unlock("rf_signal_saved_20");
  } else {
    ShowStatusAction::show("Save failed");
  }
}

bool RfCaptureScreen::_isDuplicate(const Signal& sig) const {
  if (sig.protocol != "RcSwitch") return false;  // RAW: timing jitter makes dedup unreliable
  for (uint8_t i = 0; i < _capturedCount; i++) {
    const Signal& s = _capturedSignals[i];
    if (s.protocol == "RcSwitch" &&
        s.key    == sig.key    &&
        s.preset == sig.preset &&
        s.bit    == sig.bit) {
      return true;
    }
  }
  return false;
}

String RfCaptureScreen::_generateTimestampName() {
  uint32_t s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "rf_%05lu", (unsigned long)(s % 100000));
  return String(buf);
}

// ── Signal info view ─────────────────────────────────────────────────────────

void RfCaptureScreen::_showSignalInfo(const Signal& sig, InfoSource src, uint8_t idx) {
  _infoSource = src;
  _infoIdx    = idx;
  strcpy(_titleBuf, "Signal Info");
  _state = STATE_SIGNAL_INFO;
  _textView.setContent(CC1101Util::signalInfoText(sig));
  render();
}

void RfCaptureScreen::_toggleRxFilter() {
  auto cur = _radioGetRxFilter();
  _radioSetRxFilter(cur == CC1101Util::RX_FILTER_CODE
                    ? CC1101Util::RX_FILTER_RAW
                    : CC1101Util::RX_FILTER_CODE);
  _chromeDrawn = false;  // redraw waiting-screen footer with new label
  if (Uni.Speaker) Uni.Speaker->beep();
}

// ── File browser ─────────────────────────────────────────────────────────────

String RfCaptureScreen::_makeUniquePath(const String& name) {
  String base = String(kRootPath) + "/" + name + ".sub";
  if (!Uni.Storage || !Uni.Storage->exists(base.c_str())) return base;
  for (int n = 2; n < 1000; n++) {
    String candidate = String(kRootPath) + "/" + name + "_(" + n + ").sub";
    if (!Uni.Storage->exists(candidate.c_str())) return candidate;
  }
  return base;
}

void RfCaptureScreen::_loadBrowseDir(const String& path) {
  _browsePath = path;
  _state = STATE_SEND_BROWSE;

  int lastSlash = path.lastIndexOf('/');
  String folderName = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  snprintf(_titleBuf, sizeof(_titleBuf), "RF: %s", folderName.c_str());

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    Uni.Storage->makeDir(path.c_str());
  }

  // Let the picker climb the whole card: a ".." row is shown whenever the
  // loaded dir differs from root. Rooting at "/" means even the default
  // kRootPath ("/unigeek/rf") gets a ".." so other .sub files on the SD card
  // are reachable, not just this folder.
  _browser.root = "/";
  uint8_t n = _browser.load(this, path, ".sub");
  setItems(_browser.items(), n);
}

void RfCaptureScreen::_sendBrowseFile(uint8_t index) {
  const auto& e = _browser.entry(index);
  // Stream the file (parse line-by-line) instead of slurping it into a String —
  // long RAW captures are tens of KB and two full copies (content + rawData) OOM.
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Failed to read file");
    render();
    return;
  }
  Signal sig;
  bool ok = CC1101Util::loadFromStream(f, sig, f.size());
  f.close();
  if (!ok) {
    ShowStatusAction::show("Invalid .sub file");
    render();
    return;
  }
  ProgressView::init();
  ProgressView::progress(("Sending " + e.name).c_str(), 50);
  if (!_radioSendFromBrowse(sig)) {
    ShowStatusAction::show("Send failed");
    render();
    return;
  }
  int n = Achievement.inc("rf_send_first");
  if (n == 1) Achievement.unlock("rf_send_first");
  ShowStatusAction::show(("Sent: " + e.name).c_str(), 1000);
  render();
}

void RfCaptureScreen::_showBrowseFileInfo(uint8_t index) {
  fs::File f = Uni.Storage->open(_browser.entry(index).path.c_str(), "r");
  Signal sig;
  bool ok = f && CC1101Util::loadFromStream(f, sig, f.size());
  if (f) f.close();
  if (!ok) {
    ShowStatusAction::show("Invalid .sub file");
    render();
    return;
  }
  _showSignalInfo(sig, INFO_FROM_BROWSE, index);
}

void RfCaptureScreen::_showBrowseOptions(uint8_t index) {
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
