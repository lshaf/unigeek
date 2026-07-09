#include "BLESpamRunScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

BLESpamRunScreen::~BLESpamRunScreen()
{
  _spam.end();
}

void BLESpamRunScreen::onInit()
{
  int n = Achievement.inc("ble_spam_first");
  if (n == 1) Achievement.unlock("ble_spam_first");

  _spam.begin((BleSpamUtil::Attack)_attack, _device);
  _lastTickMs = _lastDrawMs = millis();
}

void BLESpamRunScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _spam.end();
      Screen.goBack();
      return;
    }
  }

  uint32_t now = millis();
  _spam.tick();  // engine self-times via advMs/gapMs — call every loop
  if (now - _lastDrawMs >= 1000) {
    _lastDrawMs = now;
    _spinIdx = (_spinIdx + 1) % 4;
    render();
  }
}

void BLESpamRunScreen::onRender()
{
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString(BleSpamUtil::attackLabel(_attack), bx + 4, by + 4, 1);

    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    const char* devName =
      (_device >= 0 && _device < BleSpamUtil::deviceCount(_attack))
        ? BleSpamUtil::deviceLabel(_attack, _device) : "Random / All";
    lcd.drawString(devName, bx + 4, by + 4 + bh / 6, 1);

    char cfg[40];
    snprintf(cfg, sizeof(cfg), "TX:%s  MAC:%s",
             BleSpamUtil::txLabel(BleSpamUtil::txPower),
             BleSpamUtil::macLabel(BleSpamUtil::macRand));
    lcd.drawString(cfg, bx + 4, by + 4 + bh / 6 * 2, 1);

    lcd.drawString("BACK / OK: Stop", bx + 4, by + bh - 12, 1);
    _chromeDrawn = true;
  }

  // Dynamic: spinner + packet count
  Sprite sp(&lcd);
  sp.createSprite(bw - 8, 14);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  char line[32];
  snprintf(line, sizeof(line), "[%c] Spamming  pkts:%lu",
           _spinner[_spinIdx], (unsigned long)_spam.packets());
  sp.drawString(line, 2, 0, 1);
  sp.pushSprite(bx + 4, by + 4 + bh / 6 * 3);
  sp.deleteSprite();
}
