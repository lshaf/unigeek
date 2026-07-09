//
// IR Remote Screen
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "utils/ir/IRUtil.h"

class IRScreen : public ListScreen
{
public:
  IRScreen() = default;
  // Open straight into the Send signal-list for a specific .ir file (used by the
  // File Manager "Send" action on a long-pressed .ir file).
  explicit IRScreen(const String& sendFile) : _pendingSendFile(sendFile) {}

  const char* title() override { return _titleBuf; }

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
    STATE_SEND_LIST,
    STATE_TVBGONE,
  } _state = STATE_MENU;

  IRUtil _ir;
  int8_t _txPin = -1;
  int8_t _rxPin = -1;
  char _titleBuf[32] = "IR Remote";

  // Menu — IR TX/RX pins are configured under Settings > Pin Setting.
  ListItem _menuItems[3] = {
    {"Receive"},
    {"Send"},
    {"TV-B-Gone"},
  };

  void _showMenu();

  // Receive state
  IRUtil::Signal _captured[IRUtil::MAX_SIGNALS];
  uint8_t _capturedCount = 0;
  ListItem _recvItems[IRUtil::MAX_SIGNALS + 1]; // +1 for "Save Remote"
  String _recvLabels[IRUtil::MAX_SIGNALS];
  String _recvSublabels[IRUtil::MAX_SIGNALS];
  void _showReceiveList();
  void _onRecvItemAction(uint8_t index);
  bool _isDuplicate(const IRUtil::Signal& sig);
  static String _signalFingerprint(const IRUtil::Signal& sig);
  static String _signalSublabel(const IRUtil::Signal& sig);

  // Send — file browser
  static constexpr const char* kRootPath = "/unigeek/ir";
  String _browsePath;
  BrowseFileView _browser;
  void _loadBrowseDir(const String& path);

  // Send — signal list from loaded file
  IRUtil::Signal _sendSignals[IRUtil::MAX_SIGNALS];
  uint8_t _sendCount = 0;
  ListItem _sendItems[IRUtil::MAX_SIGNALS + 1]; // +1 for "Save Update"
  String _sendLabels[IRUtil::MAX_SIGNALS];
  String _sendSublabels[IRUtil::MAX_SIGNALS];
  String _sendFilePath;
  bool _sendDirty = false;
  bool _holdFired = false;
  String _pendingSendFile;  // set via ctor: jump into Send list for this file
  void _openPendingSendFile();
  void _loadAndShowSignals(const String& filePath);
  void _refreshSendList();
  void _onSendItemAction(uint8_t index);
  void _saveSendFile();

  // TV-B-Gone
  static void _tvbProgressCb(uint8_t current, uint8_t total);
  static bool _tvbCancelCb();
  static IRScreen* _activeInstance;
  bool _tvbCancelled = false;

  // Rendering
  unsigned long _lastRender = 0;
};
