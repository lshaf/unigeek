#pragma once
#include "ui/templates/BaseScreen.h"

class ClaudeBuddyScreen : public BaseScreen {
public:
  ~ClaudeBuddyScreen();
  const char* title() override { return "Claude Buddy"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  struct _State {
    uint8_t  total       = 0;
    uint8_t  running     = 0;
    uint8_t  waiting     = 0;
    uint8_t  nLines      = 0;
    bool     connected   = false;
    char     promptId[40]   = {};
    char     promptTool[24] = {};
    char     promptHint[48] = {};
    uint32_t lastUpdated    = 0;
  };

  _State   _st;

  // Screen-sized dynamic text buffers (allocated in onInit from actual bodyW/bodyH)
  uint8_t  _cpl        = 0;     // chars per line that fit in dialog width
  uint8_t  _maxLines   = 0;     // max storable lines that fit in dialog height
  char*    _msg        = nullptr;    // _cpl+1 bytes — current status message
  char*    _lines      = nullptr;    // _maxLines * (_cpl+1) bytes — activity entries
  char*    _msgSnap    = nullptr;    // render-side snapshot of _msg
  char*    _linesSnap  = nullptr;    // render-side snapshot of _lines

  char     _lineBuf[512] = {};
  uint16_t _lineBufLen   = 0;
  bool     _responseSent   = false;
  bool     _selectedYes    = true;
  uint8_t  _animTick       = 0;
  uint32_t _lastAnimMs     = 0;
  uint32_t _lastRenderMs   = 0;

  TaskHandle_t       _bleTask        = nullptr;
  SemaphoreHandle_t  _stMutex        = nullptr;
  volatile bool      _bleRunning         = false;
  volatile bool      _notifyPending      = false;
  volatile bool      _connectedPending   = false;

  static void _bleTaskFn(void* arg);
  void _pollBle();
  void _applyJson(const char* json);
  void _sendRaw(const char* json);
};
