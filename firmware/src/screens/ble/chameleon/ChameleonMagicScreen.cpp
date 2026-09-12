#include "ChameleonMagicScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

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
    if (dir == INavigation::DIR_PRESS && !_done) _run();
  }
}

void ChameleonMagicScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonMagicScreen::_run() {
  _running = true;

  _log.addLine("Scanning tag...", TFT_WHITE);
  _needsDraw = true;
  onRender();

  auto& c = ChameleonClient::get();
  const MagicCardType magic = c.detectMagicType();

  _log.addLine("Magic type:", TFT_CYAN);
  _log.addLine(magicCardTypeName(magic),
               magic == MagicCardType::NONE ? TFT_DARKGREY : TFT_GREEN);

  if (magic != MagicCardType::NONE) {
    int n = Achievement.inc("chameleon_magic_detect");
    if (n == 1) Achievement.unlock("chameleon_magic_detect");
  }

  _running = false;
  _done = true;
  _needsDraw = true;
  onRender();
}
