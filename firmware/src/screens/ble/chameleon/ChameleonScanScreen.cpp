#include "ChameleonScanScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/components/StatusBar.h"
#include "screens/ble/BLEMenuScreen.h"
#include <NimBLEDevice.h>
#include <string.h>

ChameleonScanScreen* ChameleonScanScreen::_instance = nullptr;

static const NimBLEUUID kChameleonSvc("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

class ChameleonScanScreen::ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (_instance) _instance->_onDevice(dev);
  }
};

static void scanDoneCB(NimBLEScanResults) {}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool ChameleonScanScreen::_isChameleon(NimBLEAdvertisedDevice* dev) {
  if (dev->isAdvertisingService(kChameleonSvc)) return true;
  const std::string& name = dev->getName();
  return name.find("Chameleon") != std::string::npos;
}

void ChameleonScanScreen::_onDevice(NimBLEAdvertisedDevice* dev) {
  if (!_isChameleon(dev)) return;

  const char* addr = dev->getAddress().toString().c_str();

  for (int i = 0; i < _devCount; i++) {
    if (strcmp(_devices[i].addr, addr) == 0) {
      _devices[i].rssi = dev->getRSSI();
      _devChanged = true;
      return;
    }
  }
  if (_devCount >= kMaxDevices) return;

  int idx = _devCount;
  const std::string& nm = dev->getName();
  strncpy(_devices[idx].name, nm.empty() ? "ChameleonUltra" : nm.c_str(), sizeof(_devices[idx].name) - 1);
  _devices[idx].name[sizeof(_devices[idx].name) - 1] = 0;
  strncpy(_devices[idx].addr, addr, sizeof(_devices[idx].addr) - 1);
  _devices[idx].addr[sizeof(_devices[idx].addr) - 1] = 0;
  _devices[idx].rssi    = dev->getRSSI();
  _devices[idx].bleAddr = dev->getAddress();
  _devCount             = idx + 1;
  _devChanged           = true;
}

void ChameleonScanScreen::_startScan() {
  _bleScan = NimBLEDevice::getScan();
  _bleScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
  _bleScan->setActiveScan(true);
  _bleScan->setInterval(100);
  _bleScan->setWindow(80);
  _bleScan->start(0, scanDoneCB, false);
  _scanning = true;
}

void ChameleonScanScreen::_stopScan() {
  if (_bleScan) {
    _bleScan->stop();
    NimBLEDevice::getScan()->clearResults();
  }
  _scanning = false;
}

void ChameleonScanScreen::_rebuildList() {
  int count = _devCount;

  for (int i = 0; i < count; i++) {
    strncpy(_labels[i], _devices[i].addr, sizeof(_labels[i]) - 1);
    snprintf(_sublabels[i], sizeof(_sublabels[i]), "%ddBm", _devices[i].rssi);
    _items[i] = {_labels[i], _sublabels[i]};
  }

  if (count > 0) {
    _state = STATE_LISTED;
    setItems(_items, count);
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

ChameleonScanScreen::~ChameleonScanScreen() {
  _stopScan();
  if (_instance == this) _instance = nullptr;
}

void ChameleonScanScreen::onInit() {
  NimBLEDevice::init("UniGeek");
  _instance   = this;
  _devCount   = 0;
  _devChanged = false;
  _state      = STATE_EMPTY;
  _startScan();
  render();
}

void ChameleonScanScreen::onUpdate() {
  if (_state == STATE_LISTED) {
    ListScreen::onUpdate();
  } else {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        onBack();
        return;
      }
    }
  }

  if (_devChanged) {
    _devChanged = false;
    _rebuildList();
  }
}

void ChameleonScanScreen::onRender() {
  if (_state == STATE_LISTED) {
    ListScreen::onRender();
    return;
  }

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Looking for Chameleon Ultra...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
}

void ChameleonScanScreen::onItemSelected(uint8_t index) {
  int count = _devCount;
  if (index >= (uint8_t)count) return;

  _stopScan();

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Connecting...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 8);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(_devices[index].name, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 8);

  bool ok = ChameleonClient::get().connect(_devices[index].bleAddr);

  if (ok) {
    StatusBar::bleConnected() = true;
    int n = Achievement.inc("chameleon_connected");
    if (n == 1) Achievement.unlock("chameleon_connected");
    if (n == 5) Achievement.unlock("chameleon_connect_5");
    Screen.push(new ChameleonMenuScreen());
  } else {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("Connect failed, retrying...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
    delay(1200);
    _devCount   = 0;
    _devChanged = false;
    _state      = STATE_EMPTY;
    render();
    _startScan();
  }
}

void ChameleonScanScreen::onBack() {
  _stopScan();
  NimBLEDevice::deinit(true);
  Screen.goBack();
}
