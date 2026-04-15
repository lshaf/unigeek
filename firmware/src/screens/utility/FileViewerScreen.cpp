#include "FileViewerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/FileManagerScreen.h"
#include "ui/actions/ShowStatusAction.h"

static constexpr uint16_t MAX_LINES = 500;
static constexpr uint32_t MAX_FILE_SIZE = 32768; // 32KB max to prevent OOM restart
static constexpr uint8_t LINE_HEIGHT = 10;
static constexpr uint8_t FONT = 1;

void FileViewerScreen::onInit() {
  // Title from filename
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
  size_t fileSize = f.size();
  f.close();

  if (fileSize == 0) {
    ShowStatusAction::show("Empty file");
    _goBack();
    return;
  }

  if (fileSize > MAX_FILE_SIZE) {
    ShowStatusAction::show("File too large (>32KB)");
    _goBack();
    return;
  }

  _content = Uni.Storage->readFile(_path.c_str());
  if (_content.length() == 0) {
    ShowStatusAction::show("Failed to read file");
    _goBack();
    return;
  }

  _parseLines();
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

void FileViewerScreen::_renderContent() {
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);

  if (!_lines || _lineCount == 0) {
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextDatum(MC_DATUM);
    sp.drawString("(empty)", bodyW() / 2, bodyH() / 2);
    sp.pushSprite(bodyX(), bodyY());
    sp.deleteSprite();
    return;
  }

  uint16_t maxScroll = (_lineCount > _visibleLines) ? _lineCount - _visibleLines : 0;

  // Draw scrollbar if content overflows
  if (_lineCount > _visibleLines) {
    uint16_t barX = bodyW() - 2;
    uint16_t barH = bodyH();
    uint16_t thumbH = max((uint16_t)4, (uint16_t)(barH * _visibleLines / _lineCount));
    uint16_t thumbY = (maxScroll > 0) ? (barH - thumbH) * _scrollOffset / maxScroll : 0;
    sp.fillRect(barX, 0, 2, barH, TFT_DARKGREY);
    sp.fillRect(barX, thumbY, 2, thumbH, TFT_WHITE);
  }

  sp.setTextColor(TFT_WHITE);

  for (uint16_t i = 0; i < _visibleLines && (_scrollOffset + i) < _lineCount; i++) {
    const char* line = _lines[_scrollOffset + i];
    sp.drawString(line, 1, i * LINE_HEIGHT);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void FileViewerScreen::_goBack() {
  if (_lines) { free(_lines); _lines = nullptr; }
  int slash = _path.lastIndexOf('/');
  String dir = (slash > 0) ? _path.substring(0, slash) : "/";
  Screen.setScreen(new FileManagerScreen(dir));
}
