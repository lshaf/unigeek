#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include <NimBLEDevice.h>

class BLEAnalyzerScreen : public ListScreen {
public:
  const char* title() override
  {
    return _state == STATE_DETAIL ? _detailTitle.c_str() : "BLE Analyzer";
  }

  ~BLEAnalyzerScreen() override;
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State {
    STATE_SCAN,
    STATE_LIST,
    STATE_INFO,
    STATE_DETAIL,
  } _state = STATE_SCAN;

  static constexpr uint8_t  kMaxDevices = 40;
  static constexpr uint8_t  kInfoRows   = 16;
  static constexpr uint32_t kScanCycleSeconds = 3;     // async scan window per cycle
  static constexpr unsigned long kScanCycleGapMs = 300; // rest between cycles
  static constexpr time_t   kStaleTimeoutSec = 20;      // device not re-advertised -> dropped

  NimBLEScan*       _bleScan           = nullptr;
  NimBLEScanResults _scanResults;
  int               _selectedDeviceIdx = -1;

  // Device list storage
  String   _devLabel[kMaxDevices];
  String   _devSub[kMaxDevices];
  ListItem _devItems[kMaxDevices];
  uint8_t  _devCount = 0;

  // Live/rolling scan — kept accumulating (is_continue=true) across restarts
  // so NimBLEScan's own results vector already merges by address and updates
  // RSSI in place; we only add periodic staleness pruning (via erase()) and
  // the restart cadence. The completion callback runs on the NimBLE host
  // task, so it only flips a lock-protected flag — all the real work (
  // reading results, pruning, rebuilding rows) happens on the main loop.
  bool          _scanInFlight = false;
  unsigned long _nextScanAt   = 0;
  unsigned long _lastPruneAt  = 0;

  static portMUX_TYPE _scanLock;
  static volatile bool _scanCycleDone;
  static void _onScanComplete(NimBLEScanResults results);

  void _startLiveScan();
  void _pollLiveScan();
  void _pruneStale();
  void _rebuildDevItems();
  void _stopLiveScan();

  // Info view (16 fixed rows)
  String   _infoVal[kInfoRows];
  ListItem _infoItems[kInfoRows];

  // Detail view — wrapped scrolling text for clickable info-row drill-downs.
  TextScrollView _textView;
  String         _detailTitle;

  void _startScan();
  void _showList();
  void _showInfo();
  void _showDetail(const char* titleText, const String& content);
};