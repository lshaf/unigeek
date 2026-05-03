#include "RandomLineViewerScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_random.h>

static constexpr uint32_t kMaxFileSize  = 32768; // per-file limit
static constexpr uint32_t kMaxTotalSize = 65536; // merged limit

// ── Constructor ───────────────────────────────────────────────────────────────

RandomLineViewerScreen::RandomLineViewerScreen(const String* paths, uint8_t count)
  : _pathCount(count < kMaxPaths ? count : kMaxPaths) {
  for (uint8_t i = 0; i < _pathCount; i++) _paths[i] = paths[i];

  if (_pathCount == 1) {
    int slash = _paths[0].lastIndexOf('/');
    String name = (slash >= 0) ? _paths[0].substring(slash + 1) : _paths[0];
    strncpy(_titleBuf, name.c_str(), sizeof(_titleBuf) - 1);
  } else {
    snprintf(_titleBuf, sizeof(_titleBuf), "%u files", _pathCount);
  }
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void RandomLineViewerScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    _goBack();
    return;
  }

  // Merge content from all selected files
  _content = "";
  for (uint8_t i = 0; i < _pathCount; i++) {
    fs::File f = Uni.Storage->open(_paths[i].c_str(), "r");
    if (!f) continue;
    size_t sz = f.size();
    f.close();
    if (sz == 0 || sz > kMaxFileSize) continue;
    if (_content.length() + sz > kMaxTotalSize) break;

    String chunk = Uni.Storage->readFile(_paths[i].c_str());
    if (chunk.length() == 0) continue;
    if (_content.length() > 0) _content += '\n';
    _content += chunk;
  }

  if (_content.length() == 0) {
    ShowStatusAction::show("No readable content", 1500);
    _goBack();
    return;
  }

  _parseLines();
  if (_lineCount == 0) {
    ShowStatusAction::show("No lines found", 1500);
    _goBack();
    return;
  }

  _order = (uint16_t*)malloc(_lineCount * sizeof(uint16_t));
  if (!_order) {
    ShowStatusAction::show("Out of memory", 1500);
    _goBack();
    return;
  }
  for (uint16_t i = 0; i < _lineCount; i++) _order[i] = i;
  _shuffle();
  _current     = 0;
  _chromeDrawn = false;
}

void RandomLineViewerScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    _goBack();
    return;
  }
  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) {
    if (_current > 0) { _current--; render(); }
    return;
  }
  if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
    if (_current < _lineCount - 1) { _current++; render(); }
    return;
  }
  // DIR_PRESS: intentionally ignored
}

void RandomLineViewerScreen::onRender() {
  _renderDisplay();
}

// ── Private ──────────────────────────────────────────────────────────────────

void RandomLineViewerScreen::_parseLines() {
  _lineCount = 0;
  _lines = (char**)malloc(kMaxLines * sizeof(char*));
  if (!_lines) return;

  char* buf   = const_cast<char*>(_content.c_str());
  char* start = buf;

  for (char* p = buf; ; p++) {
    if (*p == '\n' || *p == '\0') {
      bool done = (*p == '\0');
      if (p > start && *(p - 1) == '\r') *(p - 1) = '\0';
      *p = '\0';
      if (*start != '\0' && _lineCount < kMaxLines) {
        _lines[_lineCount++] = start;
      }
      if (done) break;
      start = p + 1;
    }
  }
}

void RandomLineViewerScreen::_shuffle() {
  for (uint16_t i = _lineCount - 1; i > 0; i--) {
    uint16_t j = (uint16_t)(esp_random() % (uint32_t)(i + 1));
    uint16_t tmp = _order[i];
    _order[i]    = _order[j];
    _order[j]    = tmp;
  }
}

void RandomLineViewerScreen::_renderDisplay() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();

  static constexpr int TOP_H  = 14;
  static constexpr int BOT_H  = 12;
  static constexpr int PAD    = 4;
  static constexpr int FONT_W = 6;
  static constexpr int LINE_H = 10;

  int midY = TOP_H + PAD;
  int midH = h - TOP_H - BOT_H - PAD * 2;

  // 1 ── Counter: "X / Y" — sprite self-clears its fixed region
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u / %u", _current + 1, _lineCount);
    Sprite sp(&lcd);
    sp.createSprite(w, TOP_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.setTextDatum(TC_DATUM);
    sp.setTextSize(1);
    sp.drawString(buf, w / 2, 3);
    sp.pushSprite(x, y);
    sp.deleteSprite();
  }

  // 2 ── Text: word-wrapped, vertically centered — sprite always covers full midH
  if (_lines && _lineCount > 0) {
    static constexpr uint8_t kWrapLines = 8;
    static constexpr uint8_t kWrapLen   = 56;
    char wbuf[kWrapLines][kWrapLen];
    uint8_t wcount = 0;

    int maxChars = max(1, (w - 8) / FONT_W);
    char curLine[kWrapLen] = "";
    int  curLen = 0;

    const char* rawText = _lines[_order[_current]];
    int tLen = strlen(rawText);
    int pos  = 0;

    while (pos <= tLen && wcount < kWrapLines) {
      int wstart = pos;
      while (pos < tLen && rawText[pos] != ' ') pos++;
      int wlen = pos - wstart;
      pos++;

      if (wlen == 0) continue;

      bool fits = (curLen == 0)
        ? (wlen < maxChars)
        : (curLen + 1 + wlen <= maxChars);

      if (fits || curLen == 0) {
        if (curLen > 0) curLine[curLen++] = ' ';
        int copy = min(wlen, (int)kWrapLen - curLen - 1);
        memcpy(curLine + curLen, rawText + wstart, copy);
        curLen += copy;
        curLine[curLen] = '\0';
      } else {
        strncpy(wbuf[wcount], curLine, kWrapLen - 1);
        wbuf[wcount++][kWrapLen - 1] = '\0';
        int copy = min(wlen, (int)kWrapLen - 1);
        memcpy(curLine, rawText + wstart, copy);
        curLen = copy;
        curLine[curLen] = '\0';
      }
    }
    if (curLen > 0 && wcount < kWrapLines) {
      strncpy(wbuf[wcount], curLine, kWrapLen - 1);
      wbuf[wcount++][kWrapLen - 1] = '\0';
    }

    int totalH  = wcount * LINE_H;
    int offsetY = midH > totalH ? (midH - totalH) / 2 : 0;

    Sprite sp(&lcd);
    sp.createSprite(w, max(midH, LINE_H));
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(TC_DATUM);
    sp.setTextSize(1);
    for (uint8_t i = 0; i < wcount; i++) {
      sp.drawString(wbuf[i], w / 2, offsetY + i * LINE_H);
    }
    sp.pushSprite(x, y + midY);
    sp.deleteSprite();
  }

  // 3 ── Help bar — static, drawn once
  if (!_chromeDrawn) {
    Sprite sp(&lcd);
    sp.createSprite(w, BOT_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.setTextDatum(BC_DATUM);
    sp.setTextSize(1);
    sp.drawString("Up=Prev  Dn=Next  Back=Exit", w / 2, BOT_H - 1);
    sp.pushSprite(x, y + h - BOT_H);
    sp.deleteSprite();
    _chromeDrawn = true;
  }
}

void RandomLineViewerScreen::_goBack() {
  if (_lines) { free(_lines); _lines = nullptr; }
  if (_order) { free(_order); _order = nullptr; }
  Screen.goBack();
}
