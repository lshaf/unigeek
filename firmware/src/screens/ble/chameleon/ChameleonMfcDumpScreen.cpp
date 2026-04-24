#include "ChameleonMfcDumpScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

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
      if (_state == STATE_IDLE) _run();
      else Screen.goBack();
    }
  }
}

void ChameleonMfcDumpScreen::onRender() {
  if (!_needsDraw) return;
  _needsDraw = false;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

bool ChameleonMfcDumpScreen::_loadKeys(const char* path,
                                        uint8_t (*keyA)[6], uint8_t (*keyB)[6],
                                        bool* hasA, bool* hasB, int* recovered) {
  *recovered = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content = Uni.Storage->readFile(path);
  if (content.length() == 0) return false;

  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    // Parse lines written by _saveKeys(): "S%02d A/B %02X%02X%02X%02X%02X%02X"
    if (line.length() >= 10 && line[0] == 'S') {
      int s = line.substring(1, 3).toInt();
      if (s >= 0 && s < 40) {
        char type = line[4];
        String hexStr = line.substring(6);
        hexStr.trim();
        if (hexStr.length() == 12) {
          uint8_t key[6];
          bool valid = true;
          for (int i = 0; i < 6 && valid; i++) {
            char hex[3] = { hexStr[i * 2], hexStr[i * 2 + 1], 0 };
            char* end = nullptr;
            key[i] = (uint8_t)strtoul(hex, &end, 16);
            if (*end != 0) valid = false;
          }
          if (valid) {
            if (type == 'A' && !hasA[s]) {
              memcpy(keyA[s], key, 6);
              hasA[s] = true;
              (*recovered)++;
            } else if (type == 'B' && !hasB[s]) {
              memcpy(keyB[s], key, 6);
              hasB[s] = true;
              (*recovered)++;
            }
          }
        }
      }
    }
    start = nl + 1;
  }
  return true;
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

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidHex, h);
  }

  uint8_t keyA[40][6] = {}, keyB[40][6] = {};
  bool    hasA[40] = {}, hasB[40] = {};
  int     recovered = 0;

  char keyPath[64];
  snprintf(keyPath, sizeof(keyPath), "/unigeek/nfc/keys/%s.txt", uidHex);

  bool fileFound = _loadKeys(keyPath, keyA, keyB, hasA, hasB, &recovered);

  if (!fileFound || recovered == 0) {
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    ShowStatusAction::show("Run Dict Attack first", 1500);
    _needsDraw = true; onRender();
    return;
  }

  char msg[48];
  snprintf(msg, sizeof(msg), "Keys: %d", recovered);
  _log.addLine(msg, TFT_GREEN);
  _needsDraw = true; onRender();

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    _log.addLine("No storage", TFT_RED);
    _state = STATE_DONE; _running = false;
    _needsDraw = true; onRender();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

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
    uint8_t s = (block < 128) ? (uint8_t)(block / 4)
                              : (uint8_t)(32 + (block - 128) / 16);

    uint8_t data[16] = {};
    bool ok = false;
    if (hasA[s] && c.mf1ReadBlock(block, 0x60, keyA[s], data)) ok = true;
    else if (hasB[s] && c.mf1ReadBlock(block, 0x61, keyB[s], data)) ok = true;

    if (!ok) memset(data, 0, 16);

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