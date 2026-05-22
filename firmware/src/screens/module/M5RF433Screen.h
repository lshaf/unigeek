//
// M5 RF433 (M5 RF433T/R Unit) Screen — single-pin RF, fixed 433.92 MHz
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/TextScrollView.h"
#include "utils/rf/M5RF433Util.h"

class M5RF433Screen : public ListScreen {
public:
  const char* title() override { return _titleBuf; }
  bool inhibitPowerSave() override { return _state == STATE_RECEIVING; }
  bool inhibitPowerOff() override { return _state == STATE_RECEIVING || _state == STATE_JAMMING; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_MENU,
    STATE_RECEIVING,
    STATE_SEND_BROWSE,
    STATE_JAMMING,
    STATE_SIGNAL_INFO,
  } _state = STATE_MENU;

  enum InfoSource { INFO_FROM_CAPTURE, INFO_FROM_BROWSE };
  TextScrollView _textView;
  InfoSource     _infoSource = INFO_FROM_CAPTURE;
  uint8_t        _infoIdx    = 0;
  void _showSignalInfo(const M5RF433Util::Signal& sig, InfoSource src, uint8_t idx);

  M5RF433Util _rf;
  int8_t _txPin = -1;
  int8_t _rxPin = -1;
  char _titleBuf[32] = "M5 RF433";
  bool _chromeDrawn = false;

  // Menu — Receive / Send / Jammer
  static constexpr uint8_t kMenuCount = 3;
  ListItem _menuItems[kMenuCount] = {
    {"Receive"},
    {"Send"},
    {"Jammer"},
  };
  void _showMenu();
  void _toggleRxFilter();

  // Receive — captured signal buffer
  static constexpr uint8_t kMaxCapture = 15;
  M5RF433Util::Signal _capturedSignals[kMaxCapture];
  String _capturedTimes[kMaxCapture];
  bool   _capturedSaved[kMaxCapture];
  String _capturedSubLabels[kMaxCapture];
  ListItem _capturedItems[kMaxCapture];
  uint8_t _capturedCount = 0;
  uint32_t _lastRender   = 0;
  void _showReceiveList();
  void _handleCaptureSelection(uint8_t index);
  void _rebuildCapturedItems();
  void _sendCapturedSignal(uint8_t index);
  void _saveSignal(uint8_t index, const String& name);
  bool _isDuplicate(const M5RF433Util::Signal& sig) const;
  String _generateTimestampName();

  // Jammer state
  uint32_t _jamStart = 0;

  // Send — file browser
  static constexpr const char* kRootPath = "/unigeek/rf";
  String _browsePath;
  BrowseFileView _browser;
  bool _holdFired = false;
  void _loadBrowseDir(const String& path);
  void _sendBrowseFile(uint8_t index);
  void _showBrowseOptions(uint8_t index);
  void _showBrowseFileInfo(uint8_t index);
  String _makeUniquePath(const String& name);
};
