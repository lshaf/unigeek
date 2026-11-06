#include "BLETagRingScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"

void BLETagRingScreen::_ring(bool start) {
  _busy = true;
  _draw();  // "Ringing..." / "Stopping..."
  _result = start ? BleTagRing::play(_address) : BleTagRing::stop(_address);
  _playing = start && _result == BleTagRing::Result::Playing;
  _busy = false;
  _draw();
}

void BLETagRingScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  const auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (dir == INavigation::DIR_PRESS) {
    // Playing -> Stop; otherwise -> Replay.
    _ring(!_playing);
  }
}

void BLETagRingScreen::_draw() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  lcd.fillRect(x, y, w, h, TFT_BLACK);

  // Centre the two lines in the area above the footer.
  static constexpr int kFooter = 14;
  const int cy = y + (h - kFooter) / 2;
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(_label, x + w / 2, cy - 10);

  if (_busy) {
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(_playing ? "Stopping..." : "Playing...", x + w / 2, cy + 10);
    return;  // no hint while busy
  } else {
    const bool ok = _result == BleTagRing::Result::Playing;
    lcd.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    lcd.drawString(BleTagRing::resultText(_result), x + w / 2, cy + 10);
  }

  // Footer as elsewhere: a separator line, the hint on the lower line. Stop
  // while playing, else Replay; Back is obvious (no hint).
  static constexpr int kFooterH = 14;
  lcd.drawFastHLine(x, y + h - kFooterH, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString(_playing ? "[Press] Stop" : "[Press] Replay", x + w / 2, y + h - 1);
  lcd.setTextDatum(TL_DATUM);
}
