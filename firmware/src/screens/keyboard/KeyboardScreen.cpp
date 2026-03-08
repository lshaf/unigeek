#include "KeyboardScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/keyboard/KeyboardMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/StatusBar.h"
#ifdef DEVICE_HAS_USB_HID
#include "utils/keyboard/USBKeyboardUtil.h"  // must come before BLEKeyboardUtil — TinyUSB hid.h enum must be processed before NimBLE HIDTypes.h macro
#endif
#include "utils/keyboard/BLEKeyboardUtil.h"
#include "utils/keyboard/DuckScriptUtil.h"

// ── Constructor / Destructor ────────────────────────────────────────────────

KeyboardScreen::KeyboardScreen(int mode) : _mode(mode)
{
#ifdef DEVICE_HAS_USB_HID
  if (mode == MODE_USB) {
    _keyboard = new USBKeyboardUtil();
  } else {
    _keyboard = new BLEKeyboardUtil();
  }
#else
  _keyboard = new BLEKeyboardUtil();
#endif
}

KeyboardScreen::~KeyboardScreen()
{
  if (_keyboard) {
    _keyboard->releaseAll();
    _keyboard->end();
    delete _keyboard;
    _keyboard = nullptr;
  }
  StatusBar::bleConnected() = false;
  Uni.Nav->setSuppressKeys(false);
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void KeyboardScreen::onInit()
{
  _keyboard->begin();
  _goMenu();
}

void KeyboardScreen::onUpdate()
{
  if (_mode == MODE_BLE)
    StatusBar::bleConnected() = _keyboard->isConnected();

  if (_state == STATE_KEYBOARD) {
    _handleKeyboardRelay();
  } else if (_state == STATE_RUNNING_SCRIPT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS)
        _goMenu();
    }
  } else {
    ListScreen::onUpdate();
  }
}

void KeyboardScreen::onRender()
{
  if (_state == STATE_KEYBOARD) {
    _renderConnected();
  } else if (_state == STATE_RUNNING_SCRIPT) {
    _renderScript();
  } else {
    ListScreen::onRender();
  }
}

void KeyboardScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_MENU && _state != STATE_SELECT_FILE) return;

  if (_state == STATE_MENU) {
    // Resolve which item was actually selected
    // Items are: [0]=Keyboard (if HAS_KEYBOARD), then DuckyScript, then Reset Pair (BLE)
    uint8_t idx = 0;
#ifdef DEVICE_HAS_KEYBOARD
    if (index == idx++) {
      // Keyboard relay
      _keyboard->releaseAll();
      delay(300);
      if (!_keyboard->isConnected() && _mode == MODE_BLE) {
        ShowStatusAction::show("Not connected...", 1500);
        render();
        return;
      }
      _goConnected();
      return;
    }
#endif
    if (index == idx++) {
      // Ducky Script
      _showFiles(kDuckyBase);
      return;
    }
    if (_mode == MODE_BLE && index == idx) {
      // Reset Pair
      _keyboard->resetPair();
      ShowStatusAction::show("Pairing reset", 1500);
      render();
    }
  } else if (_state == STATE_SELECT_FILE) {
    if (index >= _fileCount) return;
    String path = _filePath[index];
    if (path.endsWith("/")) {
      // Directory — navigate into it
      _showFiles(path.substring(0, path.length() - 1));
    } else {
      if (!_keyboard->isConnected() && _mode == MODE_BLE) {
        ShowStatusAction::show("Not connected...", 1500);
        render();
        return;
      }
      _runDuckyScript(path);
    }
  }
}

void KeyboardScreen::onBack()
{
  if (_state == STATE_SELECT_FILE) {
    int slash = _curPath.lastIndexOf('/');
    if (slash > 0 && _curPath != kDuckyBase) {
      _showFiles(_curPath.substring(0, slash));
    } else {
      _goMenu();
    }
  } else {
    Screen.setScreen(new KeyboardMenuScreen());
  }
}

// ── Private ─────────────────────────────────────────────────────────────────

void KeyboardScreen::_goMenu()
{
  _state      = STATE_MENU;
  _menuCount  = 0;
  StatusBar::bleConnected() = false;
  Uni.Nav->setSuppressKeys(false);

#ifdef DEVICE_HAS_KEYBOARD
  _menuItems[_menuCount++] = {"Keyboard", nullptr};
#endif
  _menuItems[_menuCount++] = {"Ducky Script", nullptr};
  if (_mode == MODE_BLE)
    _menuItems[_menuCount++] = {"Reset Pair", nullptr};

  setItems(_menuItems, _menuCount);
}

void KeyboardScreen::_goConnected()
{
  _state = STATE_KEYBOARD;
  Uni.Nav->setSuppressKeys(true);
  render();
}

void KeyboardScreen::_showFiles(const String& path)
{
  if (!Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }

  _curPath   = path;
  _fileCount = 0;
  _state     = STATE_SELECT_FILE;

  IStorage::DirEntry entries[kMaxFiles];
  uint8_t count = Uni.Storage->listDir(path.c_str(), entries, kMaxFiles);

  if (count == 0) {
    ShowStatusAction::show("No files found", 1500);
    _goMenu();
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    _fileLabel[i] = entries[i].name;
    _filePath[i]  = entries[i].isDir
                      ? (path + "/" + entries[i].name + "/")
                      : (path + "/" + entries[i].name);
    _fileItems[i] = {_fileLabel[i].c_str(), entries[i].isDir ? "DIR" : nullptr};
  }
  _fileCount = count;

  setItems(_fileItems, _fileCount);
}

void KeyboardScreen::_runDuckyScript(const String& path)
{
  _state           = STATE_RUNNING_SCRIPT;
  _scriptLineCount = 0;
  render();

  String content = Uni.Storage->readFile(path.c_str());
  if (content.isEmpty()) {
    ShowStatusAction::show("Cannot open file", 1500);
    _goMenu();
    return;
  }

  DuckScriptUtil ducky(_keyboard);
  int start = 0;
  while (start < (int)content.length()) {
    int end  = content.indexOf('\n', start);
    if (end < 0) end = content.length();
    String line = content.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.isEmpty()) continue;

    bool ok = ducky.runCommand(line);
    _addScriptLine(line, ok);
    render();
  }
}

void KeyboardScreen::_addScriptLine(const String& text, bool ok)
{
  if (_scriptLineCount < kMaxOutput) {
    _scriptLines[_scriptLineCount++] = {text, ok};
  } else {
    for (uint8_t i = 0; i < kMaxOutput - 1; i++)
      _scriptLines[i] = _scriptLines[i + 1];
    _scriptLines[kMaxOutput - 1] = {text, ok};
  }
}

void KeyboardScreen::_renderConnected()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  bool connected = _keyboard->isConnected() || _mode == MODE_USB;
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(connected ? TFT_GREEN : TFT_RED, TFT_BLACK);
  sp.drawString(connected ? "Connected" : "Waiting...", bodyW() / 2, bodyH() / 2 - 8);
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
#if defined(DEVICE_M5_CARDPUTER) || defined(DEVICE_M5_CARDPUTER_ADV)
  sp.drawString("G0: Stop", bodyW() / 2, bodyH());
#elif defined(DEVICE_T_LORA_PAGER)
  sp.drawString("Encoder press: Stop", bodyW() / 2, bodyH());
#else
  sp.drawString("BACK / ENTER: Stop", bodyW() / 2, bodyH());
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void KeyboardScreen::_renderScript()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);

  uint8_t lineH = sp.fontHeight() + 2;
  for (uint8_t i = 0; i < _scriptLineCount; i++) {
    sp.setTextColor(_scriptLines[i].ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    sp.drawString(_scriptLines[i].text, 0, i * lineH);
  }

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("BACK / ENTER: Close", bodyW() / 2, bodyH());

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void KeyboardScreen::_handleKeyboardRelay()
{
#ifdef DEVICE_HAS_KEYBOARD
#if defined(DEVICE_M5_CARDPUTER) || defined(DEVICE_M5_CARDPUTER_ADV)
  // Cardputer: BtnG0 (boot button) exits relay
  if (digitalRead(BTN_BOOT) == LOW) {
    _keyboard->releaseAll(); _goMenu(); return;
  }
#elif defined(DEVICE_T_LORA_PAGER)
  // T-Lora Pager: encoder button press exits relay
  if (Uni.Nav->wasPressed()) {
    if (Uni.Nav->readDirection() == INavigation::DIR_PRESS) {
      _keyboard->releaseAll(); _goMenu(); return;
    }
  }
#endif

  // Forward all remaining keyboard chars to HID
  while (Uni.Keyboard && Uni.Keyboard->available()) {
    char c = Uni.Keyboard->getKey();
    _keyboard->write((uint8_t)c);
  }

  _refreshBatteryLevel();
#else
  // M5StickC — no physical keyboard; any nav event exits
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _keyboard->releaseAll();
      _goMenu();
    }
  }
#endif
}

void KeyboardScreen::_refreshBatteryLevel()
{
  if (_mode != MODE_BLE) return;
  uint32_t now = millis();
  if (now - _lastBatMs < 60000) return;
  _lastBatMs = now;
  int level = Uni.Power.getBatteryPercentage();
  if (level >= 0) _keyboard->setBatteryLevel((uint8_t)level);
}
