#pragma once

#include <NimBLEDevice.h>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class BLEWatchScreenBase : public BaseScreen {
public:
  enum class Mode : uint8_t { Watchdog, Watchcat };

  explicit BLEWatchScreenBase(Mode mode) : _mode(mode) {}
  ~BLEWatchScreenBase() override;

  const char* title() override;
  bool inhibitPowerOff() override { return _scanning; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override;

protected:
  Mode _mode;

private:
#if defined(DEVICE_M5_CARDPUTER) || defined(DEVICE_M5_CARDPUTER_ADV)
  static constexpr int MAX_ENTRIES = 24;
  static constexpr int MAX_ROWS = 32;
  static constexpr int MAX_ACTIVITY = 24;
#else
  static constexpr int MAX_ENTRIES = 40;
  static constexpr int MAX_ROWS = 40;
  static constexpr int MAX_ACTIVITY = 32;
#endif
  static constexpr unsigned long WINDOW_MS = 30000UL;
  static constexpr unsigned long SPAM_WINDOW_MS = 3000UL;
  static constexpr unsigned long RATE_WINDOW_MS = 2000UL;
  static constexpr uint16_t SPAM_THRESHOLD = 4;
  static constexpr uint16_t SUSPICIOUS_RATE_THRESHOLD = 25;

  enum View : uint8_t {
    VIEW_OVERALL,
    VIEW_CAT0,
    VIEW_CAT1,
    VIEW_CAT2,
    VIEW_CAT3
  };

  struct Entry {
    uint8_t category = 0;
    char key[32] = {};
    char label[32] = {};
    char mac[18] = {};
    int8_t rssi = -127;
    uint16_t count = 0;
    unsigned long firstSeen = 0;
    unsigned long lastSeen = 0;
    unsigned long burstStart = 0;
    uint16_t burstCount = 0;
    bool visible = true;
    bool hasDevice = false;
    NimBLEAdvertisedDevice device;  // last advertisement, for Details / Fox Hunt
  };

  struct Activity {
    char mac[18] = {};
    unsigned long windowStart = 0;
    unsigned long lastSeen = 0;
    uint16_t packets = 0;
    int8_t rssi = -127;
  };

  bool _scanning = false;
  NimBLEScan* _bleScan = nullptr;
  View _view = VIEW_OVERALL;
  uint8_t _gridSel = 0;
  int _prevGridSel = -1;
  int _holdCell = -1;
  int _prevCounts[4] = {-1, -1, -1, -1};
  unsigned long _lastUpdate = 0;
  SemaphoreHandle_t _stateMutex = nullptr;

  Entry _entries[MAX_ENTRIES] = {};
  int _entryCount = 0;
  Activity _activity[MAX_ACTIVITY] = {};
  int _activityCount = 0;

  ScrollListView _scroll;

  // Paused list (as in the Wi-Fi Watchdog / Watchcat): a frozen snapshot of
  // the category, one row per entry, so the selection can't move.
  bool _paused = false;
  bool _holdHandled = false;
  std::vector<Entry> _snap;
  NimBLEAdvertisedDevice* _currentDev = nullptr;  // set while classifying
  ScrollListView::Row _rows[MAX_ROWS] = {};
  char _labels[MAX_ROWS][32] = {};
  char _values[MAX_ROWS][20] = {};  // fits a MAC
  int _itemCount = 0;

  void _startScan();
  void _stopScan();
  void _restartScanIfNeeded();
  void _onDevice(NimBLEAdvertisedDevice* dev);
  void _classifyWatchdog(NimBLEAdvertisedDevice* dev, const char* mac);
  void _classifyWatchcat(NimBLEAdvertisedDevice* dev, const char* mac);

  void _addOrUpdate(uint8_t category, const char* key, const char* label,
                    const char* mac, int8_t rssi, bool visible = true,
                    bool burst = false);
  Entry* _findEntry(uint8_t category, const char* key);
  void _observeActivity(const char* mac, int8_t rssi, bool known);
  void _prune();

  void _enterView(View view);
  void _renderView();
  void _renderOverall();
  void _renderCategory(uint8_t category);
  void _drawGridCell(int idx, int count);
  void _drawBackButton();
  void _setListState(int count);
  void _collectCategory(uint8_t category, std::vector<Entry>& out);
  int  _listHeight();
  void _renderFooter();
  void _pause();
  void _resume();
  void _leaveList();
  void _openOptions();

  static bool _matchPattern(const char* pattern, const uint8_t* data, size_t len);
  static bool _containsNoCase(const std::string& text, const char* needle);
  static bool _isNamed(const std::string& text, const char* needle);
  static const char* _spamType(NimBLEAdvertisedDevice* dev);
  static bool _matchSpamType(NimBLEAdvertisedDevice* dev, const void* type);

  class ScanCallbacks;
  friend class ScanCallbacks;
  static BLEWatchScreenBase* _instance;
};
