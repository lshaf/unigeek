#include "ChameleonT5577CleanerScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonLFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

// Common T5577 passwords brute-forced in turn. Writes a zero-UID EM410X with
// each candidate as an "old" password; if the write succeeds, the password is
// cleared (now 0x51243648 / default).
static constexpr uint8_t kPasswords[][4] = {
  { 0x51, 0x24, 0x36, 0x48 }, // default
  { 0x00, 0x00, 0x00, 0x00 },
  { 0xAA, 0xAA, 0xAA, 0xAA },
  { 0x55, 0x55, 0x55, 0x55 },
  { 0x12, 0x34, 0x56, 0x78 },
  { 0xFF, 0xFF, 0xFF, 0xFF },
  { 0x19, 0x92, 0x04, 0x27 }, // HID factory
  { 0x01, 0x23, 0x45, 0x67 },
  { 0xAB, 0xCD, 0xEF, 0x01 },
  { 0xC6, 0xB6, 0xF9, 0x2E }, // Doorking
};
static constexpr uint8_t kPwCount = sizeof(kPasswords) / 4;

void ChameleonT5577CleanerScreen::onInit() {
  _done = false;
  _log.clear();
  _log.addLine("T5577 Password Cleaner", TFT_CYAN);
  _log.addLine("Place T5577 card", TFT_DARKGREY);
  _log.addLine("[OK] start  [Hold] back", TFT_DARKGREY);
  _needsDraw = true;
}

void ChameleonT5577CleanerScreen::onUpdate() {
  if (_running) return;

  if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    Screen.setScreen(new ChameleonLFMenuScreen());
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.setScreen(new ChameleonLFMenuScreen());
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (!_done) _run();
      else Screen.setScreen(new ChameleonLFMenuScreen());
    }
  }
}

void ChameleonT5577CleanerScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonT5577CleanerScreen::_run() {
  _running = true;

  // Known-pattern UID so we can tell if the write took.
  static const uint8_t dummyUid[5] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
  static const uint8_t newPw[4]    = { 0x51, 0x24, 0x36, 0x48 }; // default

  auto& c = ChameleonClient::get();
  c.setMode(1);

  bool success = false;
  char msg[48];
  for (uint8_t i = 0; i < kPwCount; i++) {
    snprintf(msg, sizeof(msg), "Try pw %02X%02X%02X%02X",
             kPasswords[i][0], kPasswords[i][1],
             kPasswords[i][2], kPasswords[i][3]);
    _log.addLine(msg, TFT_WHITE);
    _needsDraw = true; onRender();

    if (c.writeEM410XToT5577(dummyUid, newPw, kPasswords[i], 1)) {
      snprintf(msg, sizeof(msg), "Cleared w/ pw #%d", i);
      _log.addLine(msg, TFT_GREEN);
      success = true;
      break;
    }
  }

  if (!success) _log.addLine("All passwords failed", TFT_RED);
  else {
    int n = Achievement.inc("chameleon_t5577_clean");
    if (n == 1) Achievement.unlock("chameleon_t5577_clean");
  }

  _running = false;
  _done    = true;
  _needsDraw = true; onRender();
}
