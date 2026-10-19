#include "ui/views/ScrollbarRenderer.h"
#include "FileViewerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "screens/utility/FileManagerScreen.h"
#include "ui/actions/ShowStatusAction.h"

static constexpr uint16_t MAX_LINES = 3000;
static constexpr uint32_t MAX_FILE_SIZE = 110 * 1024; // 110KB max (fits 100KB wiki text); larger files rejected to avoid OOM
static constexpr uint8_t LINE_HEIGHT = 10;
static constexpr uint8_t FONT = 1;

void FileViewerScreen::onInit() {
  // Title from filename
  int slash = _path.lastIndexOf('/');
  String name = (slash >= 0) ? _path.substring(slash + 1) : _path;
  strncpy(_titleBuf, name.c_str(), sizeof(_titleBuf) - 1);
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';

  // Plain-text files get word-wrapped; everything else keeps raw line view.
  String lower = name; lower.toLowerCase();
  _wrap = lower.endsWith(".txt");

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    render();
    ShowStatusAction::show("Storage not available");
    _goBack();
    return;
  }

  fs::File f = Uni.Storage->open(_path.c_str(), "r");
  if (!f) {
    render();
    ShowStatusAction::show("Failed to open file");
    _goBack();
    return;
  }
  size_t fileSize = f.size();
  f.close();

  if (fileSize == 0) {
    render();
    ShowStatusAction::show("Empty file");
    _goBack();
    return;
  }

  if (fileSize > MAX_FILE_SIZE) {
    render();
    ShowStatusAction::show("File too large (>110 KB)");
    _goBack();
    return;
  }

  _content = Uni.Storage->readFile(_path.c_str());
  if (_content.length() == 0) {
    render();
    ShowStatusAction::show("Failed to read file");
    _goBack();
    return;
  }

  if (_wrap) _parseLinesWrapped();
  else       _parseLines();
  _visibleLines = bodyH() / LINE_HEIGHT;

  int n = Achievement.inc("fileview_first");
  if (n == 1) Achievement.unlock("fileview_first");
}

void FileViewerScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
    _goBack();
    return;
  }

  uint16_t maxScroll = (_lineCount > _visibleLines) ? _lineCount - _visibleLines : 0;

  if (dir == INavigation::DIR_UP && _scrollOffset > 0) {
    _scrollOffset--;
    render();
  } else if (dir == INavigation::DIR_DOWN && _scrollOffset < maxScroll) {
    _scrollOffset++;
    render();
  } else if (dir == INavigation::DIR_LEFT && _scrollOffset > 0) {
    uint16_t jump = _visibleLines > 1 ? _visibleLines - 1 : 1;
    _scrollOffset = (_scrollOffset > jump) ? _scrollOffset - jump : 0;
    render();
  } else if (dir == INavigation::DIR_RIGHT && _scrollOffset < maxScroll) {
    uint16_t jump = _visibleLines > 1 ? _visibleLines - 1 : 1;
    _scrollOffset = (_scrollOffset + jump > maxScroll) ? maxScroll : _scrollOffset + jump;
    render();
  }
}

void FileViewerScreen::onRender() {
  _renderContent();
}

void FileViewerScreen::_parseLines() {
  _lineCount = 0;
  _lines = (const char**)malloc(MAX_LINES * sizeof(const char*));
  if (!_lines) return;

  char* buf = const_cast<char*>(_content.c_str());
  char* line = buf;

  for (char* p = buf; ; p++) {
    if (*p == '\n' || *p == '\0') {
      bool end = (*p == '\0');
      *p = '\0';
      if (_lineCount < MAX_LINES) {
        _lines[_lineCount++] = line;
      }
      if (end) break;
      line = p + 1;
    }
  }
}

// Word-wrap the file in place. Font 1 at size 1 is fixed 6 px/char, so a line
// budget is just a column count — no per-string textWidth needed. Wrap points
// (the chosen space, or a '\n') are turned into '\0' so each wrapped line is a
// zero-copy null-terminated slice of _content, exactly like _parseLines().
// Over-long words with no break opportunity overflow and clip (same as raw view).
void FileViewerScreen::_parseLinesWrapped() {
  _lineCount = 0;
  _lines = (const char**)malloc(MAX_LINES * sizeof(const char*));
  if (!_lines) return;

  // Leave room for the scrollbar (3 px); 6 px per glyph in font 1.
  int textW = (int)bodyW() - 3;
  int cols  = (textW > 6) ? textW / 6 : 1;
  if (cols < 1) cols = 1;

  char* buf = const_cast<char*>(_content.c_str());
  int   n   = (int)_content.length();

  int lineStart = 0;
  int lastSpace = -1;   // index of last space in the current line (wrap candidate)
  int col       = 0;

  for (int i = 0; i <= n && _lineCount < MAX_LINES; i++) {
    char c = (i < n) ? buf[i] : '\n';   // treat EOF as a final newline to flush
    if (c == '\r') continue;

    if (c == '\n') {
      buf[i] = '\0';
      _lines[_lineCount++] = &buf[lineStart];
      lineStart = i + 1;
      lastSpace = -1;
      col = 0;
      continue;
    }

    if (c == ' ') lastSpace = i;
    col++;

    if (col > cols && lastSpace >= lineStart) {
      buf[lastSpace] = '\0';
      _lines[_lineCount++] = &buf[lineStart];
      lineStart = lastSpace + 1;
      lastSpace = -1;
      col = i - lineStart + 1;
    }
  }
}

void FileViewerScreen::_renderContent() {
  auto& lcd = Uni.Lcd;

  if (!_lines || _lineCount == 0) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.drawString("(empty)", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
    return;
  }

  bool hasScrollbar = _lineCount > _visibleLines;
  uint16_t textW = hasScrollbar ? bodyW() - 3 : bodyW();

  // Render per-line sprites — each push atomically replaces the old line
  Sprite spr(&lcd);
  spr.createSprite(textW, LINE_HEIGHT);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);

  for (uint16_t i = 0; i < _visibleLines; i++) {
    spr.fillSprite(TFT_BLACK);
    if ((_scrollOffset + i) < _lineCount) {
      spr.drawString(_lines[_scrollOffset + i], 1, 0);
    }
    spr.pushSprite(bodyX(), bodyY() + i * LINE_HEIGHT);
  }
  spr.deleteSprite();

  // Clear fractional pixels below the last line slot
  int usedH = _visibleLines * LINE_HEIGHT;
  if (usedH < bodyH()) {
    lcd.fillRect(bodyX(), bodyY() + usedH, textW, bodyH() - usedH, TFT_BLACK);
  }

  // Scrollbar last — always on top
  if (hasScrollbar) {
    ScrollbarRenderer::draw(bodyX() + bodyW() - 2, bodyY(), 2, bodyH(),
                            _lineCount, _visibleLines, _scrollOffset,
                            4, 0x2104, Config.getThemeColor());
  }
}

void FileViewerScreen::_goBack() {
  if (_lines) { free(_lines); _lines = nullptr; }
  Screen.goBack();
}
