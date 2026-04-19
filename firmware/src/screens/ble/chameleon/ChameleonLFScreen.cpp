#include "ChameleonLFScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonLFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include <stdio.h>
#include <string.h>

void ChameleonLFScreen::_draw() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  switch (_state) {
    case STATE_IDLE:
      sp.setTextColor(TFT_CYAN, TFT_BLACK);
      sp.drawString("LF Card Reader", bw / 2, bh / 2 - 28);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("Place EM410X card near", bw / 2, bh / 2 - 10);
      sp.drawString("Chameleon reader", bw / 2, bh / 2 + 6);
      sp.setTextColor(TFT_WHITE, TFT_BLACK);
      sp.drawString("[Press] Scan", bw / 2, bh / 2 + 24);
      break;

    case STATE_ERROR:
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString("No card found", bw / 2, bh / 2 - 10);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("[Press] Retry", bw / 2, bh / 2 + 8);
      break;

    case STATE_CLONED:
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.drawString("Cloned!", bw / 2, bh / 2 - 20);
      sp.setTextColor(TFT_WHITE, TFT_BLACK);
      sp.drawString("Card loaded to slot.", bw / 2, bh / 2 - 4);
      sp.drawString("Emulating now.", bw / 2, bh / 2 + 12);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("[Press] Rescan", bw / 2, bh - 10);
      break;

    default: break;
  }

  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void ChameleonLFScreen::_doScan() {
  _scanning = true;

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Scanning EM410X...", bx + bw / 2, by + bh / 2 - 8);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Hold card near reader", bx + bw / 2, by + bh / 2 + 8);

  auto& c = ChameleonClient::get();
  c.setMode(1); // reader mode

  bool found = c.scanEM410X(_uid);
  _scanning  = false;

  if (found) {
    _state    = STATE_RESULT;
    _rowCount = 0;

    char hexStr[18] = {};
    for (int i = 0; i < 5; i++) {
      char h[4];
      snprintf(h, sizeof(h), "%02X%s", _uid[i], (i < 4) ? ":" : "");
      strcat(hexStr, h);
    }

    uint64_t dec = 0;
    for (int i = 0; i < 5; i++) dec = (dec << 8) | _uid[i];
    char decStr[20];
    snprintf(decStr, sizeof(decStr), "%llu", (unsigned long long)dec);

    _rowLabels[_rowCount] = "UID (Hex)";
    _rowValues[_rowCount] = hexStr;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "UID (Dec)";
    _rowValues[_rowCount] = decStr;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "Format";
    _rowValues[_rowCount] = "EM410X";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "Protocol";
    _rowValues[_rowCount] = "125 kHz";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "[Press]"; _rowValues[_rowCount] = "Scan again";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "[Hold]"; _rowValues[_rowCount] = "Copy to slot";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _scrollView.setRows(_rows, _rowCount);

    int n = Achievement.inc("chameleon_lf_read");
    if (n == 1)  Achievement.unlock("chameleon_lf_read");
    if (n == 5)  Achievement.unlock("chameleon_lf_read_5");
    if (n == 10) Achievement.unlock("chameleon_lf_read_10");
  } else {
    _state = STATE_ERROR;
  }

  _needsDraw = true;
  render();
}

void ChameleonLFScreen::_doT5577() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Writing T5577...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  bool ok = c.writeEM410XToT5577(_uid, nullptr, nullptr, 0);

  if (ok) {
    int n = Achievement.inc("chameleon_t5577_write");
    if (n == 1) Achievement.unlock("chameleon_t5577_write");
    _state = STATE_CLONED;
  } else {
    _state = STATE_ERROR;
  }
  _needsDraw = true;
  render();
}

void ChameleonLFScreen::_doClone() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Cloning...", bx + bw / 2, by + bh / 2);

  auto& c = ChameleonClient::get();
  bool ok = c.setEM410XSlot(_uid);

  if (ok) {
    c.setMode(0); // emulator mode
    int n = Achievement.inc("chameleon_clone");
    if (n == 1)  Achievement.unlock("chameleon_clone");
    if (n == 3)  Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
    _state = STATE_CLONED;
  } else {
    _state = STATE_ERROR;
  }
  _needsDraw = true;
  render();
}

void ChameleonLFScreen::onInit() {
  _state     = STATE_IDLE;
  _needsDraw = true;
}

void ChameleonLFScreen::onUpdate() {
  if (_scanning) return;

  if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
    _holdFired = true;
    Uni.Nav->suppressCurrentPress();
    if (_state == STATE_RESULT) _doClone();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.setScreen(new ChameleonLFMenuScreen());
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      _doScan();
      return;
    }
    if (_state == STATE_RESULT) _scrollView.onNav(dir);
  } else if (_holdFired && !Uni.Nav->isPressed()) {
    _holdFired = false;
  }
}

void ChameleonLFScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_needsDraw) _draw();
}
