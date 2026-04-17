#include "ChameleonMfcDumpScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

static constexpr uint8_t kDumpKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
};
static constexpr uint8_t kDumpKeyCount = sizeof(kDumpKeys) / 6;

uint8_t ChameleonMfcDumpScreen::_trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

uint16_t ChameleonMfcDumpScreen::_totalBlocks(uint8_t sectors) {
  if (sectors == 5)  return 20;   // Mini
  if (sectors == 16) return 64;   // 1K
  if (sectors == 40) return 256;  // 4K
  return 64;
}

void ChameleonMfcDumpScreen::onInit() {
  _state = STATE_IDLE;
  _log.clear();
  _log.addLine("MF Classic Dump", TFT_CYAN);
  _log.addLine("[OK] start  [Hold] back", TFT_DARKGREY);
  _needsDraw = true;
}

void ChameleonMfcDumpScreen::onUpdate() {
  if (_state == STATE_RUNNING) return;

  if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    Screen.setScreen(new ChameleonHFMenuScreen());
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.setScreen(new ChameleonHFMenuScreen());
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (_state == STATE_IDLE) _run();
      else Screen.setScreen(new ChameleonHFMenuScreen());
    }
  }
}

void ChameleonMfcDumpScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonMfcDumpScreen::_run() {
  _state   = STATE_RUNNING;
  _running = true;

  auto& c = ChameleonClient::get();
  c.setMode(1);

  uint8_t atqa[2] = {}, sak = 0;
  _log.addLine("Scanning...", TFT_YELLOW);
  _needsDraw = true; onRender();

  if (!c.scan14A(_uid, &_uidLen, atqa, &sak)) {
    _log.addLine("No card", TFT_RED);
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    return;
  }

  uint8_t sectors = 16;
  if (sak == 0x18)      sectors = 40;
  else if (sak == 0x01) sectors = 5;
  uint16_t totalBlocks = _totalBlocks(sectors);

  char msg[48];
  snprintf(msg, sizeof(msg), "Detecting keys (%d sectors)", sectors);
  _log.addLine(msg, TFT_WHITE);
  _needsDraw = true; onRender();

  uint8_t keyA[40][6] = {}, keyB[40][6] = {};
  bool    hasA[40] = {}, hasB[40] = {};
  int recovered = 0;

  uint8_t flatKeys[32 * 6];
  memcpy(flatKeys, kDumpKeys, kDumpKeyCount * 6);

  for (uint8_t s = 0; s < sectors; s++) {
    uint8_t trailer = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType = (kt == 0) ? 0x60 : 0x61;
      uint8_t matched[6];
      if (!c.mf1CheckKeysOfBlock(trailer, keyType, flatKeys, kDumpKeyCount, matched)) continue;
      if (kt == 0) { memcpy(keyA[s], matched, 6); hasA[s] = true; }
      else         { memcpy(keyB[s], matched, 6); hasB[s] = true; }
      recovered++;
    }
  }

  snprintf(msg, sizeof(msg), "Keys: %d/%d", recovered, sectors * 2);
  _log.addLine(msg, TFT_GREEN);
  _needsDraw = true; onRender();

  if (recovered == 0) {
    _log.addLine("No keys — cannot dump", TFT_RED);
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    return;
  }

  // Read every block with a known key
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    _log.addLine("No storage", TFT_RED);
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidHex, h);
  }
  char path[80];
  snprintf(path, sizeof(path), "/unigeek/nfc/dumps/%s.bin", uidHex);
  fs::File f = Uni.Storage->open(path, "w");
  if (!f) {
    _log.addLine("Open file failed", TFT_RED);
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    return;
  }

  int blocksWritten = 0;
  for (uint16_t block = 0; block < totalBlocks; block++) {
    // Compute which sector this block belongs to
    uint8_t s = (block < 128) ? (uint8_t)(block / 4)
                              : (uint8_t)(32 + (block - 128) / 16);

    uint8_t data[16] = {};
    bool ok = false;
    if (hasA[s] && c.mf1ReadBlock(block, 0x60, keyA[s], data)) ok = true;
    else if (hasB[s] && c.mf1ReadBlock(block, 0x61, keyB[s], data)) ok = true;

    if (!ok) {
      memset(data, 0, 16);
    }

    // Overlay trailer key bytes if this is a trailer (so dump holds discovered keys)
    if (block == _trailerBlock(s)) {
      if (hasA[s]) memcpy(data, keyA[s], 6);
      if (hasB[s]) memcpy(data + 10, keyB[s], 6);
    }

    f.write(data, 16);
    blocksWritten++;

    if ((block & 0x07) == 0) {
      snprintf(msg, sizeof(msg), "Reading block %d/%d", block, totalBlocks);
      _log.addLine(msg, TFT_WHITE);
      _needsDraw = true; onRender();
    }
  }
  f.close();

  snprintf(msg, sizeof(msg), "Saved %d blocks", blocksWritten);
  _log.addLine(msg, TFT_GREEN);
  _log.addLine(path + strlen("/unigeek"), TFT_CYAN);

  int n = Achievement.inc("chameleon_mfc_dump");
  if (n == 1) Achievement.unlock("chameleon_mfc_dump");

  _state   = STATE_DONE;
  _running = false;
  _needsDraw = true; onRender();
}
