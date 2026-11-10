#include "MitmReverseDnsScreen.h"
#include <WiFi.h>
#include "core/Device.h"
#include "core/ScreenManager.h"

void MitmReverseDnsScreen::onInit() {
  // Start immediately against the network's resolver.
  _running = _query.start(WiFi.dnsIP(), _ip, DnsQuery::PTR);
  _status = _running ? DnsQuery::Status::Pending : DnsQuery::Status::Error;
  _draw();
}

void MitmReverseDnsScreen::onUpdate() {
  if (_running) {
    _status = _query.poll();
    if (_status != DnsQuery::Status::Pending) { _running = false; _query.stop(); _draw(); }
  }
  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }  // back to paused preview
  }
}

void MitmReverseDnsScreen::onRender() { _draw(); }

void MitmReverseDnsScreen::_draw() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextSize(1);
  const int fh = lcd.fontHeight();
  int yy = y + 2;

  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(TL_DATUM);
  lcd.drawString("PTR", x + 2, yy);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextDatum(TR_DATUM);
  lcd.drawString(_ip, x + w - 2, yy);
  yy += fh + 4;
  lcd.drawFastHLine(x, yy, w, TFT_DARKGREY);
  yy += 4;

  lcd.setTextDatum(TL_DATUM);
  if (_status == DnsQuery::Status::Pending) {
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("Looking up...", x + 2, yy);
    return;
  }
  if (_status == DnsQuery::Status::Timeout) {
    lcd.setTextColor(TFT_RED, TFT_BLACK); lcd.drawString("No answer (timeout)", x + 2, yy); return;
  }
  if (_status == DnsQuery::Status::Error || _query.rcode() == 3) {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(_query.rcode() == 3 ? "No PTR record" : "Lookup failed", x + 2, yy);
    return;
  }
  if (_query.count() == 0) {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK); lcd.drawString("No PTR record", x + 2, yy); return;
  }
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  for (uint8_t i = 0; i < _query.count() && yy + fh < y + h; ++i) {
    lcd.drawString(_query.record(i).text, x + 2, yy);
    yy += fh + 2;
  }
}
