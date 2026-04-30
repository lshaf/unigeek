#include "FileHexViewerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "screens/utility/FileManagerScreen.h"
#include "ui/actions/ShowStatusAction.h"

void FileHexViewerScreen::onInit()
{
  int slash = _path.lastIndexOf('/');
  String name = (slash >= 0) ? _path.substring(slash + 1) : _path;
  strncpy(_titleBuf, name.c_str(), sizeof(_titleBuf) - 1);
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available");
    _goBack();
    return;
  }

  fs::File f = Uni.Storage->open(_path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Cannot open file");
    _goBack();
    return;
  }
  _fileSize = f.size();
  f.close();

  if (_fileSize == 0) {
    ShowStatusAction::show("Empty file");
    _goBack();
    return;
  }

  int na = Achievement.inc("hexview_first");
  if (na == 1) Achievement.unlock("hexview_first");

  // Layout: each row = [XX ×n] [gap≥2chars] [c×n]
  // total chars = 3n + gap + n = 4n + gap, gap ≥ 2
  // solve for max n: n = (charsPerLine - 2) / 4
  uint8_t charsPerLine = (uint8_t)(bodyW() / kCharW);
  uint8_t n = (charsPerLine > 2) ? (charsPerLine - 2) / 4 : 1;
  if (n < 1) n = 1;
  if (n > kMaxBytesPerRow) n = kMaxBytesPerRow;
  _bytesPerRow = n;
  _visibleRows = (uint8_t)(bodyH() / kLineH);
}

void FileHexViewerScreen::onUpdate()
{
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
    _goBack();
    return;
  }

  uint32_t rowBytes  = _bytesPerRow;
  uint32_t totalRows = (_fileSize + rowBytes - 1) / rowBytes;
  uint32_t maxOffset = (totalRows > _visibleRows)
                         ? (totalRows - _visibleRows) * rowBytes
                         : 0;

  if (dir == INavigation::DIR_UP && _byteOffset >= rowBytes) {
    _byteOffset -= rowBytes;
    render();
  } else if (dir == INavigation::DIR_DOWN && _byteOffset < maxOffset) {
    _byteOffset += rowBytes;
    render();
  } else if (dir == INavigation::DIR_LEFT && _byteOffset > 0) {
    uint32_t jump = (uint32_t)(_visibleRows > 1 ? _visibleRows - 1 : 1) * rowBytes;
    _byteOffset = (_byteOffset > jump) ? _byteOffset - jump : 0;
    render();
  } else if (dir == INavigation::DIR_RIGHT && _byteOffset < maxOffset) {
    uint32_t jump = (uint32_t)(_visibleRows > 1 ? _visibleRows - 1 : 1) * rowBytes;
    uint32_t next = _byteOffset + jump;
    _byteOffset   = (next > maxOffset) ? maxOffset : next;
    render();
  }
}

void FileHexViewerScreen::onRender()
{
  _renderHex();
}

void FileHexViewerScreen::_renderHex()
{
  auto& lcd = Uni.Lcd;
  uint8_t n = _bytesPerRow;

  // Char area is right-aligned with 2px gap before the scrollbar
  uint16_t charAreaX = (uint16_t)(bodyW() - (uint16_t)n * kCharW - 4);

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    return;
  }

  fs::File f = Uni.Storage->open(_path.c_str(), "r");
  if (!f) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    return;
  }
  f.seek(_byteOffset);

  Sprite spr(&lcd);
  spr.createSprite(bodyW(), kLineH);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  uint8_t buf[kMaxBytesPerRow];

  for (uint8_t row = 0; row < _visibleRows; row++) {
    spr.fillSprite(TFT_BLACK);

    int bytesRead = (f && f.available()) ? (int)f.read(buf, n) : 0;

    if (bytesRead > 0) {
      // Hex section: "XX " per byte, left-aligned
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      for (int b = 0; b < bytesRead; b++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", buf[b]);
        spr.drawString(hex, (uint16_t)(b * 3 * kCharW), 0);
      }

      // Char section: printable or '.', right-aligned
      spr.setTextColor(TFT_GREEN, TFT_BLACK);
      for (int b = 0; b < bytesRead; b++) {
        uint8_t byte = buf[b];
        char ch[2] = { (byte >= 0x20 && byte < 0x7F) ? (char)byte : '.', '\0' };
        spr.drawString(ch, (uint16_t)(charAreaX + b * kCharW), 0);
      }
    }

    spr.pushSprite(bodyX(), bodyY() + (int)(row * kLineH));
  }
  spr.deleteSprite();
  f.close();

  // Clear any leftover pixels below the last row
  int usedH = _visibleRows * kLineH;
  if (usedH < bodyH()) {
    lcd.fillRect(bodyX(), bodyY() + usedH, bodyW(), bodyH() - usedH, TFT_BLACK);
  }

  // Scrollbar
  uint32_t totalRows  = (_fileSize + n - 1) / n;
  uint32_t currentRow = _byteOffset / n;
  if (totalRows > _visibleRows) {
    uint16_t sbX    = bodyX() + bodyW() - 2;
    uint16_t barH   = (uint16_t)bodyH();
    uint32_t maxRow = totalRows - _visibleRows;
    uint16_t thumbH = max((uint16_t)4,
                          (uint16_t)((uint32_t)barH * _visibleRows / totalRows));
    uint16_t thumbY = (maxRow > 0)
                        ? (uint16_t)((uint32_t)(barH - thumbH) * currentRow / maxRow)
                        : 0;
    lcd.fillRect(sbX, bodyY(), 2, barH, 0x2104);
    lcd.fillRect(sbX, bodyY() + thumbY, 2, thumbH, Config.getThemeColor());
  }
}

void FileHexViewerScreen::_goBack()
{
  Screen.goBack();
}