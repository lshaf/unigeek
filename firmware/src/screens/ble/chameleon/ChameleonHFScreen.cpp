#include "ChameleonHFMenuScreen.h"
#include "ChameleonMfcMenuScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfuScreen.h"
#include "ChameleonMfuMenuScreen.h"
#include "ChameleonHFScreen.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/TagPrompt.h"
#include "utils/ble/ChameleonClient.h"

const char* ChameleonHFScreen::_inferType(uint8_t sak, const uint8_t atqa[2]) {
  if (sak == 0x09) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) {
    if (atqa[0] == 0x03) return "MIFARE DESFire";
    return "ISO14443-4";
  }
  if (sak == 0x00) return "Ultralight / NTAG";
  return "ISO14443A";
}

void ChameleonHFScreen::_draw() {
  _needsDraw = false;
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  TagPrompt::show("Waiting for tag...", bx, by, bw, bh);
}

void ChameleonHFScreen::_doScan() {
  _scanning = true;
  _dispatchRead = false;
  _familyMenu = 0;

  render();

  auto& c = ChameleonClient::get();

  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1); // reader mode

  bool found = c.scan14A(_uid, &_uidLen, _atqa, &_sak);
  _tagType = 0;
  _familyMenu = 0;
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
        _tagType = ChameleonClient::MFU_UNKNOWN;
      }
    }
  }

  // Scan is a temporary reader operation. Restore the CU mode so the HF field
  // (and white reader LED) is not left active after returning the result.
  if (restoreMode) c.setMode(previousMode);

  _scanning  = false;

  if (found) {
    if (_readMode) {
      if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18 || _sak == 0x00) {
        _dispatchRead = true;
        _needsDraw = true;
        return;
      }
      ShowStatusAction::show("Read Tag not supported", 1200);
      Screen.goBack();
      return;
    }
    if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) _familyMenu = 1;
    else if (_sak == 0x00) _familyMenu = 2;
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

    if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) {
      const MagicCardType magic = c.detectMagicType();
      _rowLabels[_rowCount] = "Magic";
      _rowValues[_rowCount] = magicCardTypeName(magic);
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;
    }

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

    _rowLabels[_rowCount] = "[Press]";
    _rowValues[_rowCount] = "Scan again";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
    if (_familyMenu) {
      _rowLabels[_rowCount] = "[Hold]";
      _rowValues[_rowCount] = _familyMenu == 1 ? "MIFARE Classic Menu" : "Ultralight / NTAG Menu";
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;
    }

    _scrollView.setRows(_rows, _rowCount);

    int n = Achievement.inc("chameleon_hf_read");
    if (n == 1)  Achievement.unlock("chameleon_hf_read");
    if (n == 5)  Achievement.unlock("chameleon_hf_read_5");
    if (n == 10) Achievement.unlock("chameleon_hf_read_10");
  } else {
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Tag not detected", 1200);
    Screen.goBack();
    return;
  }

  _needsDraw = true;
  render();
}

void ChameleonHFScreen::onRestore() {
  if (_returnToMenu) {
    _returnToMenu = false;
    Screen.goBack();
  }
}

void ChameleonHFScreen::onInit() {
  _state     = STATE_IDLE;
  _needsDraw = true;
  _doScan();
}

void ChameleonHFScreen::onUpdate() {
  if (_scanning) return;
  if (_dispatchRead) {
    _dispatchRead = false;
    if (_sak == 0x00) Screen.push(new ChameleonMfuScreen(_uid, _uidLen));
    else Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_READ_TAG, _uid, _uidLen));
    _returnToMenu = true;
    return;
  }

  if (_state == STATE_RESULT && _familyMenu &&
      Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 700) {
    Uni.Nav->suppressCurrentPress();
    if (_familyMenu == 1) Screen.replace(new ChameleonMfcMenuScreen());
    else Screen.replace(new ChameleonMfuMenuScreen());
    return;
  }
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS && _state == STATE_RESULT) {
      if (Uni.Nav->pressDuration() < 700) _doScan();
      return;
    }
    if (_state == STATE_RESULT) _scrollView.onNav(dir);
  }
}

void ChameleonHFScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_needsDraw) _draw();
}
