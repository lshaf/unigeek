#include "ChameleonMagicScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

// Chameleon raw-cmd option bitmask:
//   bit 7 (128) activateRfField
//   bit 6  (64) waitResponse
//   bit 5  (32) appendCrc
//   bit 4  (16) autoSelect
//   bit 3   (8) keepRfField
//   bit 2   (4) checkResponseCrc

void ChameleonMagicScreen::onInit() {
  _done = false;
  _log.clear();
  _log.addLine("Detect Magic", TFT_CYAN);
  _log.addLine("[Press] Start", TFT_DARKGREY);
  _needsDraw = true;
}

void ChameleonMagicScreen::onUpdate() {
  if (_running) return;

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS && !_done) {
      _run();
    }
  }
}

void ChameleonMagicScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonMagicScreen::_run() {
  _running = true;

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  _log.addLine("Scanning tag...", TFT_WHITE);
  _needsDraw = true; onRender();

  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  if (!c.scan14A(uid, &uidLen, atqa, &sak)) {
    _log.addLine("No tag detected", TFT_DARKGREY);
  } else if (sak != 0x01 && sak != 0x08 && sak != 0x18) {
    _log.addLine("Not MIFARE Classic", TFT_DARKGREY);
  } else {
    _log.addLine("Checking Magic type...", TFT_WHITE);
    _needsDraw = true; onRender();

    const MagicCardType magic = c.detectMagicType();
    _log.addLine("Magic type:", TFT_CYAN);
    _log.addLine(magicCardTypeName(magic),
                 magic == MagicCardType::NONE ? TFT_DARKGREY : TFT_GREEN);

    if (magic != MagicCardType::NONE) {
      int n = Achievement.inc("chameleon_magic_detect");
      if (n == 1) Achievement.unlock("chameleon_magic_detect");
    }
  }

  if (restoreMode) c.setMode(previousMode);

  _running = false;
  _done    = true;
  _needsDraw = true; onRender();
}
