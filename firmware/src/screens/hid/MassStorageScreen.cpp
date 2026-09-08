#include "MassStorageScreen.h"

#ifdef DEVICE_HAS_USB_HID

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/usb/UsbMscUtil.h"

namespace {
// Human-readable size into `out` (e.g. "29.7 GB", "512 MB").
void fmtSize(uint64_t bytes, char* out, size_t n)
{
  const char* unit = "B";
  double v = (double)bytes;
  if (v >= 1024.0) { v /= 1024.0; unit = "KB"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "MB"; }
  if (v >= 1024.0) { v /= 1024.0; unit = "GB"; }
  if (v >= 100.0) snprintf(out, n, "%.0f %s", v, unit);
  else            snprintf(out, n, "%.1f %s", v, unit);
}
}  // namespace

void MassStorageScreen::onInit()
{
  if (!Uni.Storage || !Uni.Storage->isBlockDevice()) {
    _state = ST_UNSUPPORTED;
    return;
  }

  if (!UsbMscUtil::instance().begin(Uni.Storage)) {
    _state = (UsbMscUtil::instance().failReason() == UsbMscUtil::FAIL_USB_BUSY)
                 ? ST_USB_BUSY
                 : ST_UNSUPPORTED;
    return;
  }

  _state = ST_MOUNTED;
}

void MassStorageScreen::onUpdate()
{
  if (_state != ST_MOUNTED) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) Screen.goBack();
    }
    return;
  }

  auto& msc = UsbMscUtil::instance();

  // Redraw counters / eject status only when something changed.
  if (msc.sectorReads() != _lastReads || msc.sectorWrites() != _lastWrites ||
      msc.hostEjected() != _lastEjected) {
    render();
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) _exit();
  }
}

void MassStorageScreen::onRender()
{
  auto& lcd = Uni.Lcd;
  const int cx = bodyX() + bodyW() / 2;

  if (_state == ST_UNSUPPORTED) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("No SD card", cx, bodyY() + bodyH() / 2 - 16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString("Internal flash can't be", cx, bodyY() + bodyH() / 2 + 4);
    lcd.drawString("exposed as a USB drive.", cx, bodyY() + bodyH() / 2 + 16);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    return;
  }

  if (_state == ST_USB_BUSY) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("USB busy", cx, bodyY() + bodyH() / 2 - 16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString("Another USB feature", cx, bodyY() + bodyH() / 2 + 4);
    lcd.drawString("claimed USB this boot.", cx, bodyY() + bodyH() / 2 + 16);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("Reboot, open Mass Storage first.", cx, bodyY() + bodyH() / 2 + 34);
    return;
  }

  auto& msc = UsbMscUtil::instance();
  const bool ejected = msc.hostEjected();

  static constexpr int FOOTER_H = 12;
  const int contentH = bodyH() - FOOTER_H - 1;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    const int footerY = bodyY() + bodyH() - FOOTER_H;
    lcd.drawFastHLine(bodyX(), footerY - 1, bodyW(), TFT_DARKGREY);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.drawString("[Back] Eject", cx, footerY + 7);
    _chromeDrawn = true;
  }
  _lastReads   = msc.sectorReads();
  _lastWrites  = msc.sectorWrites();
  _lastEjected = ejected;

  // Body redrawn into a sprite each refresh so counters update flicker-free.
  const int spH = 70;
  Sprite sp(&lcd);
  sp.createSprite(bodyW(), spH);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  sp.setTextSize(2);
  sp.setTextColor(ejected ? TFT_ORANGE : TFT_GREEN, TFT_BLACK);
  sp.drawString(ejected ? "Ejected" : "Mounted", bodyW() / 2, 12);

  char buf[40];
  fmtSize(msc.capacityBytes(), buf, sizeof(buf));
  sp.setTextSize(1);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, 36);

  snprintf(buf, sizeof(buf), "R: %lu  W: %lu",
           (unsigned long)msc.sectorReads(), (unsigned long)msc.sectorWrites());
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, 54);

  sp.pushSprite(bodyX(), bodyY() + (contentH - spH) / 2);
  sp.deleteSprite();
}

void MassStorageScreen::_exit()
{
  UsbMscUtil::instance().end();
  Screen.goBack();
}

#endif  // DEVICE_HAS_USB_HID
