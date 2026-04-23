#include "BLEAnalyzerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

static String _toHex(const std::string& data)
{
  String result;
  for (uint8_t b : data) {
    if (b < 0x10) result += '0';
    result += String(b, HEX);
  }
  return result;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLEAnalyzerScreen::~BLEAnalyzerScreen()
{
  if (_bleScan != nullptr) {
    _bleScan->stop();
    NimBLEDevice::deinit(true);
    _bleScan = nullptr;
  }
}

void BLEAnalyzerScreen::onInit()
{
  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  _startScan();
}

void BLEAnalyzerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_LIST) {
    if (index < (uint8_t)_scanResults.getCount()) {
      _selectedDeviceIdx = index;
      _showInfo();
    }
  } else if (_state == STATE_INFO) {
    NimBLEAdvertisedDevice dev = _scanResults.getDevice(_selectedDeviceIdx);
    if (index == 5 && dev.getServiceUUIDCount() > 0)
      _showDetail(STATE_SERVICE_UUID);
    else if (index == 6 && dev.getManufacturerDataCount() > 0)
      _showDetail(STATE_MANUFACTURE_DATA);
    else if (index == 7 && dev.getServiceDataCount() > 0)
      _showDetail(STATE_SERVICE_DATA);
  }
}

void BLEAnalyzerScreen::onBack()
{
  if (_state == STATE_INFO) {
    _selectedDeviceIdx = -1;
    _showList();
  } else if (_state == STATE_SERVICE_UUID ||
             _state == STATE_SERVICE_DATA  ||
             _state == STATE_MANUFACTURE_DATA) {
    _showInfo();
  } else {
    Screen.goBack();
  }
}

// ── Private ────────────────────────────────────────────────────────────────

void BLEAnalyzerScreen::_startScan()
{
  _state = STATE_SCAN;
  _selectedDeviceIdx = -1;
  ShowStatusAction::show("Scanning...", 0);
  _bleScan->clearResults();
  _scanResults = _bleScan->start(5, false);

  int ns = Achievement.inc("ble_analyzer_scan");
  if (ns == 1) Achievement.unlock("ble_analyzer_scan");

  _showList();
}

void BLEAnalyzerScreen::_showList()
{
  _state = STATE_LIST;
  _devCount = 0;

  int count = min((int)_scanResults.getCount(), (int)kMaxDevices);

  if ((int)_scanResults.getCount() >= 20) {
    int n20 = Achievement.inc("ble_analyzer_20");
    if (n20 == 1) Achievement.unlock("ble_analyzer_20");
  }
  for (int i = 0; i < count; i++) {
    NimBLEAdvertisedDevice dev = _scanResults.getDevice(i);
    _devLabel[i] = dev.getAddress().toString().c_str();
    _devSub[i]   = dev.getName().c_str();
    _devItems[i] = {_devLabel[i].c_str(),
                    _devSub[i].length() > 0 ? _devSub[i].c_str() : nullptr};
    _devCount++;
  }

  // Ensure non-empty list so DIR_BACK always works
  if (_devCount == 0) {
    _devLabel[0] = "No devices found";
    _devItems[0] = {_devLabel[0].c_str()};
    _devCount    = 1;
  }

  setItems(_devItems, _devCount);
}

void BLEAnalyzerScreen::_showInfo()
{
  if (_selectedDeviceIdx < 0) return;
  _state = STATE_INFO;

  int nd = Achievement.inc("ble_analyzer_detail");
  if (nd == 1) Achievement.unlock("ble_analyzer_detail");

  NimBLEAdvertisedDevice dev = _scanResults.getDevice(_selectedDeviceIdx);

  const char* addrType;
  switch (dev.getAddressType()) {
    case BLE_ADDR_PUBLIC:    addrType = "Public";    break;
    case BLE_ADDR_RANDOM:    addrType = "Random";    break;
    case BLE_ADDR_PUBLIC_ID: addrType = "Public ID"; break;
    case BLE_ADDR_RANDOM_ID: addrType = "Random ID"; break;
    default:                 addrType = "Unknown";   break;
  }

  _infoVal[0] = dev.getName().c_str();
  _infoVal[1] = dev.getAddress().toString().c_str();
  _infoVal[2] = addrType;
  _infoVal[3] = String(dev.getRSSI());
  _infoVal[4] = dev.isConnectable() ? "Yes" : "No";
  _infoVal[5] = String(dev.getServiceUUIDCount());
  _infoVal[6] = String(dev.getManufacturerDataCount());
  _infoVal[7] = String(dev.getServiceDataCount());
  _infoVal[8] = dev.getURI().c_str();

  static constexpr const char* kLabels[] = {
    "Name", "Address", "Addr Type", "RSSI",
    "Connectable",
    "Svc UUIDs", "Mfr Data", "Svc Data", "URI",
  };
  for (int i = 0; i < 9; i++) {
    _infoItems[i] = {kLabels[i],
                     _infoVal[i].length() > 0 ? _infoVal[i].c_str() : nullptr};
  }
  setItems(_infoItems, 9);
}

void BLEAnalyzerScreen::_showDetail(State state)
{
  _state = state;
  _detailCount = 0;

  NimBLEAdvertisedDevice dev = _scanResults.getDevice(_selectedDeviceIdx);

  if (state == STATE_SERVICE_UUID) {
    int count = min((int)dev.getServiceUUIDCount(), (int)kMaxDetail);
    for (int i = 0; i < count; i++) {
      _detailLabel[i] = dev.getServiceUUID(i).toString().c_str();
      _detailItems[i] = {_detailLabel[i].c_str()};
      _detailCount++;
    }
  } else if (state == STATE_MANUFACTURE_DATA) {
    int count = min((int)dev.getManufacturerDataCount(), (int)kMaxDetail);
    for (int i = 0; i < count; i++) {
      _detailLabel[i] = _toHex(dev.getManufacturerData(i));
      _detailItems[i] = {_detailLabel[i].c_str()};
      _detailCount++;
    }
  } else if (state == STATE_SERVICE_DATA) {
    int count = min((int)dev.getServiceDataCount(), (int)kMaxDetail);
    for (int i = 0; i < count; i++) {
      _detailLabel[i] = _toHex(dev.getServiceData(i));
      _detailItems[i] = {_detailLabel[i].c_str()};
      _detailCount++;
    }
  }

  setItems(_detailItems, _detailCount);
}