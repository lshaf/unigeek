#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/keyboard/HIDKeyboardUtil.h"

class KeyboardScreen : public ListScreen {
public:
  static constexpr int MODE_BLE = 0;
  static constexpr int MODE_USB = 1;

  explicit KeyboardScreen(int mode);
  ~KeyboardScreen() override;

  const char* title()            override { return "HID"; }
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
    STATE_MOUSE_JIGGLE,
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
  static constexpr const char* kDuckyBase = "/unigeek/hid/duckyscript";

  // Menu (built dynamically)
  static constexpr uint8_t kMaxMenu = 5;
  ListItem _menuItems[kMaxMenu];
  uint8_t  _menuCount = 0;

  // Mouse Jiggle
  static constexpr uint32_t kJiggleIntervalMs = 30000;
  bool      _jiggleChromeDrawn = false;
  uint32_t  _jiggleStartMs     = 0;
  uint32_t  _jiggleNextMs      = 0;
  uint32_t  _jiggleCount       = 0;
  bool      _jiggleDirRight    = true;
  uint32_t  _jiggleLastPaintMs = 0;

  // Script output
  struct ScriptLine { String text; bool ok; };
  static constexpr uint8_t kMaxOutput = 11;
  ScriptLine _scriptLines[kMaxOutput];
  uint8_t    _scriptLineCount = 0;

  void _goMenu();
  void _goConnected();
  void _goMouseJiggle();
  void _showFiles(const String& path);
  void _runDuckyScript(const String& path);
  void _addScriptLine(const String& text, bool ok);

  void _renderConnected();
  void _renderScript();
  void _renderMouseJiggle();
  void _handleKeyboardRelay();
  void _handleMouseJiggle();
  void _refreshBatteryLevel();
};
