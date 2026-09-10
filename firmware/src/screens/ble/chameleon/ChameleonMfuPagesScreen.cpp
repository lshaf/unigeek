#include "ChameleonMfuPagesScreen.h"
#include "ChameleonMfuAuthUtils.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

namespace {
void pageReadProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...", (unsigned)done, (unsigned)total);
  ProgressView::progress(msg, total ? (int)((uint32_t)done * 100u / total) : 0);
}
}

void ChameleonMfuPagesScreen::_freeDump() {
  if (_dump) free(_dump);
  _dump = nullptr;
  _dumpLen = 0;
}

void ChameleonMfuPagesScreen::_read() {
  _busy = true;
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  if (!c.mfuDetect(&_info)) {
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    ShowStatusAction::show("Tag not supported", 1400);
    Screen.goBack();
    return;
  }

  const uint32_t bytes = (uint32_t)_info.pages * 4u;
  _dump = (uint8_t*)malloc(bytes);
  if (!_dump) {
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    ShowStatusAction::show("Out of memory", 1200);
    Screen.goBack();
    return;
  }

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, _info, 0, _info.pages - 1, true, pwd, usePwd)) {
    free(_dump); _dump = nullptr;
    if (restoreMode) c.setMode(previousMode);
    _busy = false;
    render();
    return;
  }

  ProgressView::init();
  uint16_t got = 0;
  const bool ok = c.mfuReadDump(_info, _dump, (uint16_t)bytes, &got, pageReadProgress,
                                usePwd ? pwd : nullptr);
  ProgressView::finish();
  if (restoreMode) c.setMode(previousMode);
  _busy = false;

  if (!ok) {
    _freeDump();
    ShowStatusAction::show("Read failed", 1200);
    Screen.goBack();
    return;
  }

  _dumpLen = got;
  _ready = true;
  _topPage = 0;
  render();
}

void ChameleonMfuPagesScreen::onInit() { _read(); }

void ChameleonMfuPagesScreen::onUpdate() {
  if (_busy) return;
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    _freeDump();
    Screen.goBack();
    return;
  }
  if (!_ready) return;
  if (dir == INavigation::DIR_UP && _topPage > 0) {
    --_topPage;
    render();
  } else if (dir == INavigation::DIR_DOWN && _topPage + 1 < _info.pages) {
    ++_topPage;
    render();
  }
}

void ChameleonMfuPagesScreen::onRender() {
  if (!_ready || !_dump) return;
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextSize(1);

  char head[48];
  snprintf(head, sizeof(head), "%s  %u pages",
           ChameleonClient::mfuTagTypeName(_info.type), (unsigned)_info.pages);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString(head, bx + 2, by + 2);

  const int lineH = 16;
  const int firstY = by + 20;
  const int visible = max(1, (bh - 22) / lineH);
  for (int row = 0; row < visible; ++row) {
    const uint16_t page = _topPage + row;
    if (page >= _info.pages) break;
    const uint8_t* d = _dump + page * 4u;
    char line[40];
    if (_info.type == ChameleonClient::MFU_ULTRALIGHT_C && page >= 44) {
      snprintf(line, sizeof(line), "P%-3u  Unreadable (key)", (unsigned)page);
    } else {
      snprintf(line, sizeof(line), "P%-3u  %02X %02X %02X %02X",
               (unsigned)page, d[0], d[1], d[2], d[3]);
    }
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(line, bx + 2, firstY + row * lineH);
  }
}
