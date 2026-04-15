#include "BLESpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEMenuScreen.h"

// NimBLE internal API to set the static random address
extern "C" int ble_hs_id_set_rnd(const uint8_t *addr);

static constexpr const char kCharset[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
static constexpr int kCharsetLen = sizeof(kCharset) - 1;

// ── Lifecycle ──────────────────────────────────────────────────────────────

BLESpamScreen::~BLESpamScreen()
{
  _stop();
}

void BLESpamScreen::onInit()
{
  int n = Achievement.inc("ble_spam_first");
  if (n == 1) Achievement.unlock("ble_spam_first");
  _spamStartMs  = millis();
  _spam1minFired = false;

  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  _pAdv = NimBLEDevice::getAdvertising();
  _spam();
}

void BLESpamScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      Screen.setScreen(new BLEMenuScreen());
      return;
    }
  }

  uint32_t now = millis();
  if (!_spam1minFired && now - _spamStartMs >= 60000) {
    _spam1minFired = true;
    int n = Achievement.inc("ble_spam_1min");
    if (n == 1) Achievement.unlock("ble_spam_1min");
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

void BLESpamScreen::onRender()
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

void BLESpamScreen::_spam()
{
  if (_pAdv) _pAdv->stop();

  // Random 15-char device name
  char name[16];
  for (int i = 0; i < 15; i++) name[i] = kCharset[random(0, kCharsetLen)];
  name[15] = '\0';

  // Random static-random BLE address (top 2 bits = 11)
  uint8_t addr[6];
  esp_fill_random(addr, 6);
  addr[5] |= 0xC0;
  ble_hs_id_set_rnd(addr);

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);  // General discoverable | BR/EDR not supported
  advData.setName(name);

  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void BLESpamScreen::_stop()
{
  if (_pAdv) {
    _pAdv->stop();
    _pAdv = nullptr;
  }
  NimBLEDevice::deinit(true);
}