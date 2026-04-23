#include "ChameleonMagicScreen.h"
#include "utils/chameleon/ChameleonClient.h"
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
  _log.addLine("Magic Card Detect", TFT_CYAN);
  _log.addLine("[OK] start  [Hold] back", TFT_DARKGREY);
  _needsDraw = true;
}

void ChameleonMagicScreen::onUpdate() {
  if (_running) return;

  if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    Screen.goBack();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (!_done) _run();
      else Screen.goBack();
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
  c.setMode(1);
  bool anyMagic = false;

  // gen1a probe: 40 (7 bits, activateRf + waitResp + keepRf), expect 0x0A ACK
  {
    uint8_t resp[16] = {};
    uint16_t respLen = 0;
    uint8_t data = 0x40;
    _log.addLine("gen1a probe (0x40)...", TFT_WHITE);
    _needsDraw = true; onRender();
    bool ok = c.hf14ARaw(128 | 64 | 8, 200, 7, &data, 1, resp, &respLen, sizeof(resp));
    if (ok && respLen >= 1 && resp[0] == 0x0A) {
      // second half
      uint8_t data2 = 0x43;
      bool ok2 = c.hf14ARaw(64 | 8, 200, 8, &data2, 1, resp, &respLen, sizeof(resp));
      if (ok2 && respLen >= 1 && resp[0] == 0x0A) {
        _log.addLine("gen1a: YES", TFT_GREEN);
        anyMagic = true;
      } else {
        _log.addLine("gen1a: ACK only", TFT_YELLOW);
      }
    } else {
      _log.addLine("gen1a: no", TFT_DARKGREY);
    }
  }

  // gen3 probe: `30 00` with autoSelect + appendCrc + waitResp + checkCrc
  {
    uint8_t resp[32] = {};
    uint16_t respLen = 0;
    uint8_t data[2] = { 0x30, 0x00 };
    _log.addLine("gen3 probe (30 00)...", TFT_WHITE);
    _needsDraw = true; onRender();
    bool ok = c.hf14ARaw(128 | 64 | 32 | 16 | 4, 500, 16, data, 2, resp, &respLen, sizeof(resp));
    if (ok && respLen >= 16) {
      _log.addLine("gen3 candidate", TFT_YELLOW);
      // Response should be 18 bytes (16 block + 2 CRC) if writable magic
      if (respLen >= 18) {
        _log.addLine("gen3: likely YES", TFT_GREEN);
        anyMagic = true;
      }
    } else {
      _log.addLine("gen3: no", TFT_DARKGREY);
    }
  }

  _log.addLine(anyMagic ? "Magic detected" : "No magic signature",
               anyMagic ? TFT_GREEN : TFT_DARKGREY);

  if (anyMagic) {
    int n = Achievement.inc("chameleon_magic_detect");
    if (n == 1) Achievement.unlock("chameleon_magic_detect");
  }

  _running = false;
  _done    = true;
  _needsDraw = true; onRender();
}
