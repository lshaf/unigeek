#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"
#include <HardwareSerial.h>

class UartTerminalScreen : public ListScreen {
public:
  const char* title() override;
  ~UartTerminalScreen();

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onBack()                      override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_CONFIG, STATE_RUNNING };
  State _state = STATE_CONFIG;

  // ── config values ─────────────────────────────────────────
  int    _baud = 115200;
  int    _rxPin = -1;  // resolved from Grove SDA in onInit
  int    _txPin = -1;  // resolved from Grove SCL in onInit
  String _saveFilename;

  // ── config list sublabel buffers ──────────────────────────
  char _baudLabel[8]   = {};
  char _rxLabel[6]     = {};
  char _txLabel[6]     = {};
  char _saveLabel[32]  = {};
  ListItem _configItems[5];

  // ── terminal state ────────────────────────────────────────
  HardwareSerial    _serial{ 2 };
  bool              _serialRunning = false;
  bool              _hexMode       = false;

  TaskHandle_t      _taskHandle  = nullptr;
  SemaphoreHandle_t _mutex       = nullptr;
  String            _rxSharedBuf;

  LogView  _log;
  String   _rxLineBuf;
  String   _saveBuffer;
  int      _saveLineCount = 0;
  uint32_t _lastDrawMs    = 0;

  static constexpr int MAX_SAVE_LINES = 200;

  static void _statusBarCb(Sprite& sp, int barY, int w, void* user);
  static void _serialTask(void* arg);

  void _updateLabels();
  void _configBaud();
  void _configRx();
  void _configTx();
  void _configSaveFile();
  void _startTerminal();
  void _sendCommand();
  void _drainShared();
  void _saveLog();
};
