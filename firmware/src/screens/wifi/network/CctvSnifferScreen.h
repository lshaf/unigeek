#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/cctv/CctvScanUtil.h"
#include "utils/cctv/CctvStreamUtil.h"

class CctvSnifferScreen : public ListScreen {
public:
  const char* title()        override { return "CCTV Sniffer"; }
  bool inhibitPowerSave()    override { return _state == STATE_SCANNING || _state == STATE_STREAMING; }
  bool inhibitPowerOff()     override { return _state == STATE_SCANNING || _state == STATE_STREAMING; }

  void onInit() override;
  void onUpdate() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,       // Mode, IP, File, Start
    STATE_SCANNING,     // Log view during scan
    STATE_CAMERA_LIST,  // List of found cameras
    STATE_CAMERA_MENU,  // Selected camera: Username, Password, Stream
    STATE_STREAMING,    // MJPEG viewer
  };

  enum Mode { MODE_LAN, MODE_SINGLE_IP, MODE_FILE_IP };

  State _state = STATE_CONFIG;
  Mode  _mode  = MODE_LAN;

  // Config menu
  String   _modeSub;
  String   _ipSub;
  String   _fileSub;
  String   _targetIp;
  String   _targetFile;
  ListItem _configItems[3];

  // Scanning log
  static constexpr int MAX_LOG = 30;
  char _logLines[MAX_LOG][60];
  int  _logCount = 0;

  // Found cameras
  static constexpr uint8_t MAX_FOUND = 32;
  CctvScanUtil::Camera _cameras[MAX_FOUND];
  uint8_t _cameraCount = 0;
  char    _cameraLabels[MAX_FOUND][40];
  ListItem _cameraItems[MAX_FOUND];

  // Selected camera menu
  uint8_t  _selectedCamera = 0;
  String   _usernameSub;
  String   _passwordSub;
  String   _username;
  String   _password;
  ListItem _menuItems[4]; // Username, Password, Stream, Back

  // Streaming
  CctvStreamUtil _stream;
  unsigned long  _lastFrame = 0;
  int            _frameCount = 0;
  float          _fps = 0;
  static CctvSnifferScreen* _instance;  // for TJpgDec callback

  void _showConfig();
  void _startScan();
  void _scanLAN();
  void _scanSingleIP();
  void _scanFileIP();
  void _scanHost(const char* ip);
  void _addLog(const char* msg);
  void _drawLog();
  void _showCameraList();
  void _showCameraMenu(uint8_t camIdx);
  void _startStream();
  void _stopStream();
  void _drawFrame(const uint8_t* jpegBuf, size_t jpegLen);

  static bool _onFrame(const uint8_t* jpegBuf, size_t jpegLen, void* userData);
  static bool _tjpgCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
};
