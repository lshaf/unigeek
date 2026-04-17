#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/keyboard/HIDKeyboardUtil.h"

class KeyboardScreen : public ListScreen {
public:
  static constexpr int MODE_BLE = 0;
  static constexpr int MODE_USB = 1;

  explicit KeyboardScreen(int mode);
  ~KeyboardScreen() override;

  const char* title()            override { return "Keyboard"; }
  bool inhibitPowerOff()         override { return true; }

  void onInit()                  override;
  void onUpdate()                override;
  void onRender()                override;
  void onItemSelected(uint8_t index) override;
  void onBack()                  override;

private:
  enum State {
    STATE_MENU,
    STATE_KEYBOARD,
    STATE_SELECT_FILE,
    STATE_RUNNING_SCRIPT,
  } _state = STATE_MENU;

  int               _mode;
  HIDKeyboardUtil*  _keyboard    = nullptr;
  uint32_t          _lastBatMs   = 0;

  // Partial-redraw flags — chrome (black fill + static footer) painted once.
  bool _connectedChromeDrawn = false;
  bool _connectedLastStatus  = false;  // tracks prev Connected/Waiting to skip redraw
  bool _scriptChromeDrawn    = false;
  uint8_t _scriptLastRendered = 0;     // last _scriptLineCount drawn

  // File browser
  static constexpr uint8_t kMaxFiles = 40;
  String   _fileLabel[kMaxFiles];
  String   _filePath[kMaxFiles];
  ListItem _fileItems[kMaxFiles];
  uint8_t  _fileCount  = 0;
  String   _curPath;
  static constexpr const char* kDuckyBase = "/unigeek/keyboard/duckyscript";

  // Menu (built dynamically)
  static constexpr uint8_t kMaxMenu = 4;
  ListItem _menuItems[kMaxMenu];
  uint8_t  _menuCount = 0;

  // Script output
  struct ScriptLine { String text; bool ok; };
  static constexpr uint8_t kMaxOutput = 11;
  ScriptLine _scriptLines[kMaxOutput];
  uint8_t    _scriptLineCount = 0;

  void _goMenu();
  void _goConnected();
  void _showFiles(const String& path);
  void _runDuckyScript(const String& path);
  void _addScriptLine(const String& text, bool ok);

  void _renderConnected();
  void _renderScript();
  void _handleKeyboardRelay();
  void _refreshBatteryLevel();
};
