#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"
#include "utils/interpreter/LuaEngine.h"

class LuaRunnerScreen : public ListScreen {
public:
  const char* title()          override { return _state == STATE_BROWSE ? "Lua Runner" : nullptr; }
  bool isFullScreen()          override { return _state != STATE_BROWSE; }
  bool inhibitPowerSave()      override { return _state == STATE_RUNNING; }

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onBack()                      override;
  void onItemSelected(uint8_t index) override;

private:
  static constexpr const char* ROOT_DIR = "/unigeek/lua";

  enum State { STATE_BROWSE, STATE_RUNNING, STATE_DONE };
  State _state = STATE_BROWSE;

  BrowseFileView _browser;
  LuaEngine      _engine;
  LogView        _log;
  String         _errBuf;
  String         _scriptSrc;
  String         _currentDir;

  void _loadDir(const String& path);
  void _startScript(const String& path);
  void _handleDone(bool isError);
  void _drawRunning();
  void _drawDone();
};
