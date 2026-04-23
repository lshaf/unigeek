//
// IR Remote Screen
//

#include "IRScreen.h"

#if defined(DEVICE_M5STICK_S3)
#include <M5PM1.h>
#include <Wire.h>
extern M5PM1 pm1;
static void _irAmpEnable(bool on) {
  // Class-D amp enable via M5PM1 GPIO3
  pm1.gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, on ? 1 : 0,
              M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
}
static void _irBoostSet(bool on) {
  pm1.setBoostEnable(on);
}
#endif

#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/IStorage.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/views/ProgressView.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"

IRScreen* IRScreen::_activeInstance = nullptr;

void IRScreen::onInit() {
  _txPin = (int8_t)PinConfig.getInt(PIN_CONFIG_IR_TX, PIN_CONFIG_IR_TX_DEFAULT);
  _rxPin = (int8_t)PinConfig.getInt(PIN_CONFIG_IR_RX, PIN_CONFIG_IR_RX_DEFAULT);
  _showMenu();
}

void IRScreen::_updatePinSublabels() {
  _txPinSub = (_txPin >= 0) ? String("GPIO ") + String(_txPin) : "Not set";
  _rxPinSub = (_rxPin >= 0) ? String("GPIO ") + String(_rxPin) : "Not set";
  _menuItems[0] = {"TX Pin", _txPinSub.c_str()};
  _menuItems[1] = {"RX Pin", _rxPinSub.c_str()};
}

void IRScreen::_showMenu() {
  _state = STATE_MENU;
  _ir.end();
  #if defined(DEVICE_M5STICK_S3)
  _irBoostSet(false);
  _irAmpEnable(true);
  #endif
  strncpy(_titleBuf, "IR Remote", sizeof(_titleBuf));
  _updatePinSublabels();
  setItems(_menuItems);
}

void IRScreen::onUpdate() {
  if (_state == STATE_RECEIVING) {
    IRUtil::Signal sig;
    if (_ir.receive(sig)) {
      if (_capturedCount < IRUtil::MAX_SIGNALS && !_isDuplicate(sig)) {
        sig.name = "Signal " + String(_capturedCount + 1);
        _captured[_capturedCount] = sig;
        _capturedCount++;
        #if defined(DEVICE_M5STICK_S3) && defined(IR_RX_PIN)
        if (Uni.Speaker && _rxPin != IR_RX_PIN) Uni.Speaker->playNotification();
        #else
        if (Uni.Speaker) Uni.Speaker->playNotification();
        #endif
        if (_capturedCount == 1) {
          int n = Achievement.inc("ir_receive_first");
          if (n == 1) Achievement.unlock("ir_receive_first");
        }
        _showReceiveList();
      }
      _ir.resumeReceive();
    }

    // Blink indicator
    if (millis() - _lastRender > 500) {
      _lastRender = millis();
      render();
    }
  }

  // Long-press to open action menu on send list signals
  if (_state == STATE_SEND_LIST && !_holdFired &&
      Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    _holdFired = true;
    if (_selectedIndex < _sendCount) {
      _onSendItemAction(_selectedIndex);
    }
    return;
  }

  if (_holdFired) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      _holdFired = false;
    }
    return;
  }

  ListScreen::onUpdate();
}

void IRScreen::onRender() {
  ListScreen::onRender();
}

void IRScreen::onBack() {
  if (_state == STATE_MENU) {
    _ir.end();
    Screen.goBack();
  } else if (_state == STATE_RECEIVING) {
    _ir.end();
    _showMenu();
  } else if (_state == STATE_SEND_BROWSE) {
    // Navigate up or back to menu
    if (_browsePath == kRootPath) {
      _showMenu();
    } else {
      int slash = _browsePath.lastIndexOf('/');
      String parent = (slash > 0) ? _browsePath.substring(0, slash) : String(kRootPath);
      // Don't go above root
      if (parent.length() < String(kRootPath).length()) parent = kRootPath;
      _loadBrowseDir(parent);
    }
  } else if (_state == STATE_SEND_LIST) {
    _loadBrowseDir(_browsePath);
  } else {
    _showMenu();
  }
}

void IRScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0: { // TX Pin
        int pin = InputNumberAction::popup("TX Pin", -1, 48, _txPin);
        if (!InputNumberAction::wasCancelled()) {
          _txPin = (int8_t)pin;
          PinConfig.set(PIN_CONFIG_IR_TX, String(_txPin));
          PinConfig.save(Uni.Storage);
        }
        _updatePinSublabels();
        render();
        break;
      }
      case 1: { // RX Pin
        int pin = InputNumberAction::popup("RX Pin", -1, 48, _rxPin);
        if (!InputNumberAction::wasCancelled()) {
          _rxPin = (int8_t)pin;
          PinConfig.set(PIN_CONFIG_IR_RX, String(_rxPin));
          PinConfig.save(Uni.Storage);
        }
        _updatePinSublabels();
        render();
        break;
      }
      case 2: { // Receive
        if (_rxPin < 0) {
          ShowStatusAction::show("Set RX pin first");
          render();
          return;
        }
        #if defined(DEVICE_M5STICK_S3)
        if (_rxPin == IR_RX_PIN || _txPin == IR_TX_PIN) {
          _irBoostSet(true);
          if (_rxPin == IR_RX_PIN)
          {
            _irAmpEnable(false);
            ShowStatusAction::show("Starting...", 2000);
          }
        }
        #endif
        _ir.beginRx(_rxPin);
        if (_txPin >= 0) _ir.beginTx(_txPin);
        _capturedCount = 0;
        _state = STATE_RECEIVING;
        setItems(_recvItems, 0);  // prime pointer once; _showReceiveList uses setCount after this
        break;
      }
      case 3: { // Send
        if (_txPin < 0) {
          ShowStatusAction::show("Set TX pin first");
          render();
          return;
        }
        #if defined(DEVICE_M5STICK_S3)
        if (_txPin == IR_TX_PIN) _irBoostSet(true);
        #endif
        _ir.beginTx(_txPin);
        Uni.Storage->makeDir(kRootPath);
        _loadBrowseDir(kRootPath);
        break;
      }
      case 4: { // TV-B-Gone
        if (_txPin < 0) {
          ShowStatusAction::show("Set TX pin first");
          render();
          return;
        }

        static constexpr InputSelectAction::Option regionOpts[] = {
          {"North America", "na"},
          {"Europe", "eu"},
        };
        const char* sel = InputSelectAction::popup("Region", regionOpts, 2);
        if (!sel) { render(); return; }
        render();

        #if defined(DEVICE_M5STICK_S3)
        if (_txPin == IR_TX_PIN) _irBoostSet(true);
        #endif
        _ir.beginTx(_txPin);
        _tvbCancelled = false;
        _activeInstance = this;
        _state = STATE_TVBGONE;
        {
          int n = Achievement.inc("ir_tvbgone");
          if (n == 1) Achievement.unlock("ir_tvbgone");
        }

        uint8_t region = (strcmp(sel, "na") == 0) ? 1 : 0;
        ProgressView::init();
        _ir.startTvBGone(region, _tvbProgressCb, _tvbCancelCb);

        _ir.end();
        _activeInstance = nullptr;

        if (_tvbCancelled) {
          ShowStatusAction::show("Stopped", 1000);
        } else {
          ShowStatusAction::show("All codes sent!", 1500);
          int n = Achievement.inc("ir_tvbgone_complete");
          if (n == 1) Achievement.unlock("ir_tvbgone_complete");
        }
        _showMenu();
        break;
      }
    }
    return;
  }

  if (_state == STATE_RECEIVING) {
    // Last item is "Save Remote"
    if (index == _capturedCount) {
      if (_capturedCount == 0) {
        ShowStatusAction::show("No signals captured");
        render();
        return;
      }
      String filename = InputTextAction::popup("File Name", "remote");
      if (filename.length() == 0) { render(); return; }

      String content = IRUtil::saveToString(_captured, _capturedCount);
      String path = String(kRootPath) + "/" + filename + ".ir";
      Uni.Storage->makeDir(kRootPath);
      if (Uni.Storage->writeFile(path.c_str(), content.c_str())) {
        ShowStatusAction::show("Saved!");
        {
          int n = Achievement.inc("ir_signal_saved");
          if (n == 1)  Achievement.unlock("ir_signal_saved");
          if (n == 5)  Achievement.unlock("ir_signal_saved_5");
          if (n == 20) Achievement.unlock("ir_signal_saved_20");
        }
        if (_capturedCount >= 20) {
          int n = Achievement.inc("ir_remote_collection");
          if (n == 1) Achievement.unlock("ir_remote_collection");
        }
      } else {
        ShowStatusAction::show("Save failed");
      }
      _ir.end();
      _showMenu();
      return;
    }

    _onRecvItemAction(index);
    return;
  }

  if (_state == STATE_SEND_BROWSE) {
    if (index < _browseCount) {
      if (_browseIsDir[index]) {
        _loadBrowseDir(_browsePaths[index]);
      } else {
        _loadAndShowSignals(_browsePaths[index]);
      }
    }
    return;
  }

  if (_state == STATE_SEND_LIST) {
    if (_sendDirty && index == _sendCount) {
      _saveSendFile();
      return;
    }
    if (index < _sendCount) {
      ShowStatusAction::show("Sending...", 0);
      _ir.sendSignal(_sendSignals[index]);
      delay(100);
      {
        int n = Achievement.inc("ir_send_first");
        if (n == 1) Achievement.unlock("ir_send_first");
      }
      ShowStatusAction::show(("Sent: " + _sendSignals[index].name).c_str(), 800);
      render();
    }
    return;
  }
}

// ── Receive helpers ─────────────────────────────────────────────────────────

void IRScreen::_showReceiveList() {
  for (uint8_t i = 0; i < _capturedCount; i++) {
    _recvLabels[i] = _captured[i].name;
    _recvSublabels[i] = _signalSublabel(_captured[i]);
    _recvItems[i] = {_recvLabels[i].c_str(), _recvSublabels[i].c_str()};
  }
  _recvItems[_capturedCount] = {">> Save Remote", nullptr};
  setCount(_capturedCount + 1);  // update count, clamp selection — no render
  render();                      // one render at the current selection
}

bool IRScreen::_isDuplicate(const IRUtil::Signal& sig) {
  String fp = _signalFingerprint(sig);
  for (uint8_t i = 0; i < _capturedCount; i++) {
    if (_signalFingerprint(_captured[i]) == fp) return true;
  }
  return false;
}

String IRScreen::_signalFingerprint(const IRUtil::Signal& sig) {
  if (!sig.isRaw && sig.protocol.length() > 0) {
    return sig.protocol + ":" + String(sig.address, HEX) + ":" + String(sig.command, HEX);
  }
  return sig.rawData;
}

String IRScreen::_signalSublabel(const IRUtil::Signal& sig) {
  if (!sig.isRaw && sig.protocol.length() > 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %04lX:%04lX", sig.protocol.c_str(),
             (unsigned long)sig.address, (unsigned long)sig.command);
    return String(buf);
  }
  // For raw, show frequency and data length
  int count = 1;
  for (int i = 0; i < (int)sig.rawData.length(); i++) {
    if (sig.rawData[i] == ' ') count++;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "RAW %uHz %dpts", sig.frequency, count);
  return String(buf);
}

void IRScreen::_onRecvItemAction(uint8_t index) {
  if (index >= _capturedCount) return;

  static constexpr InputSelectAction::Option actionOpts[] = {
    {"Replay", "replay"},
    {"Rename", "rename"},
    {"Delete", "delete"},
  };
  const char* sel = InputSelectAction::popup(_captured[index].name.c_str(), actionOpts, 3);

  if (!sel) {
    render();
    return;
  }

  if (strcmp(sel, "replay") == 0) {
    if (_txPin < 0) {
      ShowStatusAction::show("Set TX pin first");
    } else {
      #if defined(DEVICE_M5STICK_S3)
      if (_txPin == IR_TX_PIN) _irBoostSet(true);
      #endif
      _ir.beginTx(_txPin);
      ShowStatusAction::show("Sending...", 0);
      _ir.sendSignal(_captured[index]);
      delay(100);
      {
        int n = Achievement.inc("ir_send_first");
        if (n == 1) Achievement.unlock("ir_send_first");
      }
      ShowStatusAction::show("Sent!", 800);
    }
  } else if (strcmp(sel, "rename") == 0) {
    String newName = InputTextAction::popup("New Name", _captured[index].name);
    if (newName.length() > 0) {
      _captured[index].name = newName;
    }
  } else if (strcmp(sel, "delete") == 0) {
    for (uint8_t i = index; i < _capturedCount - 1; i++) {
      _captured[i] = _captured[i + 1];
    }
    _capturedCount--;
  }

  _showReceiveList();
}

// ── Send — file browser ────────────────────────────────────────────────────

void IRScreen::_loadBrowseDir(const String& path) {
  _state = STATE_SEND_BROWSE;
  _browsePath = path;
  _browseCount = 0;

  // Update title to show current folder name
  int lastSlash = path.lastIndexOf('/');
  String folderName = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  if (path == kRootPath) folderName = "IR Files";
  snprintf(_titleBuf, sizeof(_titleBuf), "%s", folderName.c_str());

  IStorage::DirEntry entries[kMaxBrowse];
  uint8_t count = Uni.Storage->listDir(path.c_str(), entries, kMaxBrowse);

  // Sort: directories first, then alphabetical
  for (uint8_t i = 1; i < count; i++) {
    IStorage::DirEntry tmp = entries[i];
    int j = i - 1;
    while (j >= 0) {
      bool swap = false;
      if (tmp.isDir && !entries[j].isDir) swap = true;
      else if (tmp.isDir == entries[j].isDir &&
               strcasecmp(tmp.name.c_str(), entries[j].name.c_str()) < 0) swap = true;
      if (!swap) break;
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = tmp;
  }

  for (uint8_t i = 0; i < count && _browseCount < kMaxBrowse; i++) {
    // Show .ir files and directories only
    if (!entries[i].isDir && !entries[i].name.endsWith(".ir")) continue;
    _browseNames[_browseCount] = entries[i].name;
    _browsePaths[_browseCount] = path + "/" + entries[i].name;
    _browseIsDir[_browseCount] = entries[i].isDir;
    _browseItems[_browseCount] = {
      _browseNames[_browseCount].c_str(),
      entries[i].isDir ? "DIR" : nullptr
    };
    _browseCount++;
  }

  if (_browseCount == 0 && path == kRootPath) {
    ShowStatusAction::show("No IR files found in /unigeek/ir/");
    _showMenu();
    return;
  }

  setItems(_browseItems, _browseCount);
}

void IRScreen::_loadAndShowSignals(const String& filePath) {
  String content = Uni.Storage->readFile(filePath.c_str());
  if (content.length() == 0) {
    ShowStatusAction::show("Failed to read file");
    render();
    return;
  }

  _sendCount = IRUtil::loadFile(content, _sendSignals, IRUtil::MAX_SIGNALS);
  if (_sendCount == 0) {
    ShowStatusAction::show("No signals in file");
    render();
    return;
  }

  _sendFilePath = filePath;
  _sendDirty = false;

  // Update title to filename
  int lastSlash = filePath.lastIndexOf('/');
  String fileName = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
  snprintf(_titleBuf, sizeof(_titleBuf), "%s", fileName.c_str());

  _state = STATE_SEND_LIST;
  _refreshSendList();
}

void IRScreen::_refreshSendList() {
  for (uint8_t i = 0; i < _sendCount; i++) {
    _sendLabels[i] = _sendSignals[i].name;
    _sendSublabels[i] = _signalSublabel(_sendSignals[i]);
    _sendItems[i] = {_sendLabels[i].c_str(), _sendSublabels[i].c_str()};
  }
  uint8_t total = _sendCount;
  if (_sendDirty) {
    _sendItems[_sendCount] = {">> Save Update", nullptr};
    total++;
  }
  setItems(_sendItems, total);
}

void IRScreen::_onSendItemAction(uint8_t index) {
  if (index >= _sendCount) return;

  static constexpr InputSelectAction::Option opts[] = {
    {"Replay", "replay"},
    {"Rename", "rename"},
    {"Delete", "delete"},
  };
  const char* sel = InputSelectAction::popup(_sendSignals[index].name.c_str(), opts, 3);
  if (!sel) { render(); return; }

  if (strcmp(sel, "replay") == 0) {
    ShowStatusAction::show("Sending...", 0);
    _ir.sendSignal(_sendSignals[index]);
    delay(100);
    {
      int n = Achievement.inc("ir_send_first");
      if (n == 1) Achievement.unlock("ir_send_first");
    }
    ShowStatusAction::show("Sent!", 800);
  } else if (strcmp(sel, "rename") == 0) {
    String newName = InputTextAction::popup("New Name", _sendSignals[index].name);
    if (newName.length() > 0) {
      _sendSignals[index].name = newName;
      _sendDirty = true;
    }
  } else if (strcmp(sel, "delete") == 0) {
    for (uint8_t i = index; i < _sendCount - 1; i++) {
      _sendSignals[i] = _sendSignals[i + 1];
    }
    _sendCount--;
    _sendDirty = true;
  }

  _refreshSendList();
}

void IRScreen::_saveSendFile() {
  String content = IRUtil::saveToString(_sendSignals, _sendCount);
  if (Uni.Storage->writeFile(_sendFilePath.c_str(), content.c_str())) {
    ShowStatusAction::show("Saved!");
    _sendDirty = false;
  } else {
    ShowStatusAction::show("Save failed");
  }
  _refreshSendList();
}

// ── TV-B-Gone callbacks ─────────────────────────────────────────────────────

void IRScreen::_tvbProgressCb(uint8_t current, uint8_t total) {
  if (!_activeInstance) return;
  int pct = (int)((uint32_t)current * 100 / total);
  char msg[32];
  snprintf(msg, sizeof(msg), "Sending %u / %u", current, total);
  ProgressView::progress(msg, pct);
}

bool IRScreen::_tvbCancelCb() {
  if (!_activeInstance) return true;
  Uni.update();
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _activeInstance->_tvbCancelled = true;
      return true;
    }
  }
  return false;
}
