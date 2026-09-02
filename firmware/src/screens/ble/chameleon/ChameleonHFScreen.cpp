#include "ChameleonHFScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"

const char* ChameleonHFScreen::_inferType(uint8_t sak, const uint8_t atqa[2]) {
  if (sak == 0x01) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) {
    if (atqa[0] == 0x03) return "MIFARE DESFire";
    return "ISO14443-4";
  }
  if (sak == 0x00) {
    if (atqa[1] == 0x44) return "MIFARE UL / NTAG";
    return "ISO14443A T2";
  }
  return "ISO14443A";
}

void ChameleonHFScreen::_draw() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  sp.setTextColor(TFT_CYAN, TFT_BLACK);
  sp.drawString("HF Card Reader", bw / 2, bh / 2 - 28);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Place ISO14443 card near", bw / 2, bh / 2 - 10);
  sp.drawString("Chameleon reader face", bw / 2, bh / 2 + 6);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString("[Press] Scan", bw / 2, bh / 2 + 24);

  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void ChameleonHFScreen::_doScan() {
  _scanning = true;

  // Draw scanning state directly before blocking BLE call (MFRC pattern)
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Scanning ISO14443A...", bx + bw / 2, by + bh / 2 - 8);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Hold card near reader", bx + bw / 2, by + bh / 2 + 8);

  auto& c = ChameleonClient::get();

  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1); // reader mode

  bool found = c.scan14A(_uid, &_uidLen, _atqa, &_sak);
  _tagType = 0;
  if (found) {
    _tagType = ChameleonClient::inferHFTagType(_sak, _atqa);
    if (_sak == 0x00) {
      ChameleonClient::MfuTagInfo info = {};
      if (c.mfuDetect(&info)) {
        _tagType = info.type;
        memcpy(_uid, info.uid, sizeof(_uid));
        _uidLen = info.uidLen;
        memcpy(_atqa, info.atqa, sizeof(_atqa));
        _sak = info.sak;
      } else {
        // SAK 00 alone cannot distinguish concrete Type-2 variants.
        _tagType = 0;
      }
    }
  }

  // Scan is a temporary reader operation. Restore the CU mode so the HF field
  // (and white reader LED) is not left active after returning the result.
  if (restoreMode) c.setMode(previousMode);

  _scanning  = false;

  if (found) {
    _state    = STATE_RESULT;
    _rowCount = 0;

    char uidStr[24] = {};
    for (uint8_t i = 0; i < _uidLen; i++) {
      char h[4];
      snprintf(h, sizeof(h), "%02X%s", _uid[i], (i + 1 < _uidLen) ? ":" : "");
      strcat(uidStr, h);
    }

    _rowLabels[_rowCount] = "UID";
    _rowValues[_rowCount] = uidStr;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "Type";
    if (_sak == 0x00 && _tagType != 0)
      _rowValues[_rowCount] = ChameleonClient::mfuTagTypeName(_tagType);
    else
      _rowValues[_rowCount] = _inferType(_sak, _atqa);
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02X:%02X", _atqa[0], _atqa[1]);
    _rowLabels[_rowCount] = "ATQA";
    _rowValues[_rowCount] = buf;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    snprintf(buf, sizeof(buf), "%02X", _sak);
    _rowLabels[_rowCount] = "SAK";
    _rowValues[_rowCount] = buf;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "UID Len";
    _rowValues[_rowCount] = String(_uidLen) + " bytes";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "Protocol";
    _rowValues[_rowCount] = "ISO14443A";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "[Press]"; _rowValues[_rowCount] = "Scan again";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _rowLabels[_rowCount] = "[Hold]"; _rowValues[_rowCount] = "Copy to slot";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;

    _scrollView.setRows(_rows, _rowCount);

    int n = Achievement.inc("chameleon_hf_read");
    if (n == 1)  Achievement.unlock("chameleon_hf_read");
    if (n == 5)  Achievement.unlock("chameleon_hf_read_5");
    if (n == 10) Achievement.unlock("chameleon_hf_read_10");
  } else {
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("No card found", 1200);
    render();
    return;
  }

  _needsDraw = true;
  render();
}

void ChameleonHFScreen::_doClone() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Cloning to slot...", bx + bw / 2, by + bh / 2 - 8);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Please wait", bx + bw / 2, by + bh / 2 + 8);

  auto& c = ChameleonClient::get();
  c.getActiveSlot(&_activeSlot);
  uint16_t tagType = _tagType ? _tagType : ChameleonClient::inferHFTagType(_sak, _atqa);
  bool ok = c.cloneHF(_activeSlot, tagType, _uid, _uidLen, _atqa, _sak);

  _state = STATE_RESULT;
  _needsDraw = true;
  render();

  if (ok) {
    int n = Achievement.inc("chameleon_clone");
    if (n == 1)  Achievement.unlock("chameleon_clone");
    if (n == 3)  Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
    ShowStatusAction::show("Clone OK", 1200);
  } else {
    ShowStatusAction::show("Clone failed", 1200);
  }
  render();
}

void ChameleonHFScreen::onInit() {
  _state     = STATE_IDLE;
  _needsDraw = true;
}

void ChameleonHFScreen::onUpdate() {
  if (_scanning) return;

  // Press-hold anywhere opens the action menu. Works on every board since it
  // only needs a single PRESS axis.
  if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
    _holdFired = true;
    Uni.Nav->suppressCurrentPress();
    if (_state == STATE_RESULT) _doClone();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      // Tap = scan (or re-scan). Hold = copy (handled above).
      _doScan();
      return;
    }
    if (_state == STATE_RESULT) _scrollView.onNav(dir);
  } else if (_holdFired && !Uni.Nav->isPressed()) {
    _holdFired = false;   // consume the release so it doesn't trigger a tap
  }
}

void ChameleonHFScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_needsDraw) _draw();
}
