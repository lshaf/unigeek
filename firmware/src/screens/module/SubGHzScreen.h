//
// Sub-GHz (CC1101) Screen
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/rf/CC1101Util.h"

class SubGHzScreen : public ListScreen {
public:
  const char* title() override { return _titleBuf; }
  bool inhibitPowerSave() override { return _state == STATE_RECEIVING || _state == STATE_SCANNING; }
  bool inhibitPowerOff() override { return _state == STATE_RECEIVING || _state == STATE_JAMMING || _state == STATE_SCANNING; }

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
    STATE_SCANNING,
  } _state = STATE_MENU;

  CC1101Util _rf;
  int8_t _csPin   = -1;
  int8_t _gdo0Pin = -1;
  char _titleBuf[32] = "Sub-GHz";

  // Menu (5 items)
  static constexpr uint8_t kMenuCount = 5;
  ListItem _menuItems[kMenuCount] = {
    {"Frequency"},
    {"Detect Freq"},
    {"Receive"},
    {"Send"},
    {"Jammer"},
  };
  String _freqSub;
  void _showMenu();
  void _updateSublabels();
  void _selectFrequency();
  void _startScan();

  // Receive — captured signal buffer
  static constexpr uint8_t kMaxCapture = 15;
  CC1101Util::Signal _capturedSignals[kMaxCapture];
  String _capturedTimes[kMaxCapture];
  bool   _capturedSaved[kMaxCapture];
  String _capturedSubLabels[kMaxCapture];
  ListItem _capturedItems[kMaxCapture];
  uint8_t _capturedCount = 0;
  uint32_t _lastRender   = 0;     // for blink indicator while waiting
  void _showReceiveList();        // rebuild items + setItems
  void _handleCaptureSelection(uint8_t index);  // replay/save/delete popup
  void _rebuildCapturedItems();
  void _sendCapturedSignal(uint8_t index);
  void _saveSignal(uint8_t index, const String& name);
  bool _isDuplicate(const CC1101Util::Signal& sig) const;
  String _generateTimestampName();

  // Jammer state
  uint32_t _jamStart = 0;

  // Send — file browser
  static constexpr uint8_t kMaxBrowse = 30;
  static constexpr const char* kRootPath = "/unigeek/rf";
  String _browsePath;
  String _browseNames[kMaxBrowse];
  String _browsePaths[kMaxBrowse];
  bool   _browseIsDir[kMaxBrowse];
  ListItem _browseItems[kMaxBrowse];
  uint8_t _browseCount = 0;
  bool    _holdFired = false;
  uint8_t _pendingHoldIdx = 0;
  void _loadBrowseDir(const String& path);
  void _sendBrowseFile(uint8_t index);
  void _showBrowseOptions(uint8_t index);
  String _makeUniquePath(const String& name);
};

