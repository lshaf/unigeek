#include "ChameleonHIDProxScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonLFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/InputSelectOption.h"

void ChameleonHIDProxScreen::_draw() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  if (_state == STATE_IDLE) {
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString("HID Prox Scanner", bw / 2, bh / 2 - 20);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("Place HID Prox card", bw / 2, bh / 2);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString("[OK] Scan  [Hold] Back", bw / 2, bh / 2 + 20);
  } else if (_state == STATE_RESULT) {
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.drawString("HID Prox detected", bw / 2, bh / 2 - 30);
    char hex[40] = {};
    for (uint8_t i = 0; i < _payloadLen && i * 2 + 2 < (int)sizeof(hex); i++) {
      char h[4]; snprintf(h, sizeof(h), "%02X", _payload[i]); strcat(hex, h);
    }
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(hex, bw / 2, bh / 2 - 10);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("[OK] Action  [Hold] Back", bw / 2, bh / 2 + 20);
  } else if (_state == STATE_CLONED) {
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.drawString("Success!", bw / 2, bh / 2 - 10);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString("[Back] Menu", bw / 2, bh / 2 + 14);
  } else {
    sp.setTextColor(TFT_RED, TFT_BLACK);
    sp.drawString("No card / failed", bw / 2, bh / 2 - 4);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("[OK] Retry", bw / 2, bh / 2 + 14);
  }
  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void ChameleonHIDProxScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
}

void ChameleonHIDProxScreen::onRender() {
  if (_needsDraw) _draw();
}

void ChameleonHIDProxScreen::_doScan() {
  _scanning = true;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Scanning HID Prox...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  c.setMode(1);
  bool ok = c.scanHIDProx(_payload, &_payloadLen);
  _scanning = false;
  _state = ok ? STATE_RESULT : STATE_ERROR;

  if (ok) {
    int n = Achievement.inc("chameleon_hid_scan");
    if (n == 1) Achievement.unlock("chameleon_hid_scan");
  }
  _needsDraw = true;
  render();
}

void ChameleonHIDProxScreen::_doCloneSlot() {
  auto& c = ChameleonClient::get();
  bool ok = c.setHIDProxSlot(_payload, _payloadLen);
  if (ok) {
    c.setMode(0);
    int n = Achievement.inc("chameleon_clone");
    if (n == 1) Achievement.unlock("chameleon_clone");
    if (n == 3) Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
  }
  _state = ok ? STATE_CLONED : STATE_ERROR;
  _needsDraw = true;
  render();
}

void ChameleonHIDProxScreen::_doT5577() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Writing T5577...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  bool ok = c.writeHIDProxToT5577(_payload, _payloadLen, nullptr, nullptr, 0);
  if (ok) {
    int n = Achievement.inc("chameleon_t5577_write");
    if (n == 1) Achievement.unlock("chameleon_t5577_write");
  }
  _state = ok ? STATE_CLONED : STATE_ERROR;
  _needsDraw = true;
  render();
}

void ChameleonHIDProxScreen::onUpdate() {
  if (!_scanning && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    Screen.goBack();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      if (_state == STATE_RESULT) {
        _state = STATE_IDLE;
        _needsDraw = true;
        render();
      } else {
        Screen.goBack();
      }
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (_state == STATE_IDLE || _state == STATE_ERROR) {
        _doScan();
      } else if (_state == STATE_RESULT) {
        static const InputSelectAction::Option opts[] = {
          {"Clone to slot",  "slot"},
          {"Write to T5577", "t5577"},
        };
        const char* r = InputSelectAction::popup("HID Action", opts, 2, nullptr);
        if (r && strcmp(r, "slot") == 0)  _doCloneSlot();
        else if (r && strcmp(r, "t5577") == 0) _doT5577();
        else render();
      } else if (_state == STATE_CLONED) {
        Screen.goBack();
      }
    }
  }
}
