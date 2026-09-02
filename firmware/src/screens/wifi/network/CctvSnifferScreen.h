#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/cctv/CctvScanUtil.h"
#include "utils/cctv/CctvStreamUtil.h"
#include "utils/network/IpScanUtil.h"

class CctvSnifferScreen : public ListScreen {
public:
  const char* title()        override { return "IP Cameras"; }
  bool inhibitPowerSave()    override { return _state == STATE_SCANNING || _state == STATE_STREAMING; }
  bool inhibitPowerOff()     override { return _state == STATE_SCANNING || _state == STATE_STREAMING; }

  void onInit() override;
  void onUpdate() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,
    STATE_SCANNING,
    STATE_CAMERA_LIST,
    STATE_CAMERA_MENU,
    STATE_STREAMING,
  };

  enum ScanMode { MODE_RANGE, MODE_TARGETS };

  enum ConfigAction {
    CFG_MODE,
    CFG_TARGET_1,
    CFG_TARGET_2,
    CFG_TARGET_3,
    CFG_TARGET_4,
    CFG_START_IP,
    CFG_END_IP,
    CFG_START_SCAN,
  };

  State    _state    = STATE_CONFIG;
  ScanMode _scanMode = MODE_RANGE;

  static constexpr uint8_t MAX_TARGETS = 4;
  String _targets[MAX_TARGETS];
  int    _startIp = 1;
  int    _endIp   = 254;

  String _targetSubs[MAX_TARGETS];
  String _startIpSub;
  String _endIpSub;

  static constexpr uint8_t MAX_CONFIG_ITEMS = 7;
  static constexpr uint8_t MAX_FOUND_HOSTS  = 64;
  static constexpr uint8_t MAX_FOUND        = 32;

  ListItem     _configItems[MAX_CONFIG_ITEMS];
  ConfigAction _configActions[MAX_CONFIG_ITEMS];
  uint8_t      _configCount = 0;

  IpScanUtil::Host _hosts[MAX_FOUND_HOSTS];

  CctvScanUtil::Camera _cameras[MAX_FOUND];
  uint8_t  _cameraCount = 0;
  char     _cameraLabels[MAX_FOUND][40];
  ListItem _cameraItems[MAX_FOUND];

  uint8_t _selectedCamera = 0;
  String  _usernameSub;
  String  _passwordSub;
  String  _username;
  String  _password;
  ListItem _menuItems[4];

  CctvStreamUtil _stream;
  unsigned long  _lastFrame = 0;
  int            _frameCount = 0;
  float          _fps = 0;
  static CctvSnifferScreen* _instance;

  void _showConfig(uint8_t selectedIndex = 0);
  void _startScan();
  void _scanTarget(const char* ip, const char* label);
  bool _validIp(const String& ip) const;
  bool _hasTargets() const;
  void _editTarget(uint8_t targetIndex, uint8_t selectedIndex);
  void _scanRange();
  void _scanHost(const char* ip, const char* label);
  void _showCameraList();
  void _showCameraMenu(uint8_t camIdx);
  void _startStream();
  void _stopStream();
  void _drawFrame(const uint8_t* jpegBuf, size_t jpegLen);
  String _networkPrefix() const;

  static bool _onFrame(const uint8_t* jpegBuf, size_t jpegLen, void* userData);
  static bool _tjpgCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
};
