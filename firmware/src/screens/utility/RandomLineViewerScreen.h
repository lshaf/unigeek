#pragma once

#include "ui/templates/BaseScreen.h"

class RandomLineViewerScreen : public BaseScreen {
public:
  RandomLineViewerScreen(const String* paths, uint8_t count);

  const char* title() override { return _titleBuf; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr uint8_t  kMaxPaths = 30;
  static constexpr uint16_t kMaxLines = 500;

  String    _paths[kMaxPaths];
  uint8_t   _pathCount   = 0;
  char      _titleBuf[32] = "Random";
  String    _content;
  char**    _lines       = nullptr;
  uint16_t* _order       = nullptr;
  uint16_t  _lineCount   = 0;
  uint16_t  _current     = 0;
  bool      _chromeDrawn = false;

  void _parseLines();
  void _shuffle();
  void _renderDisplay();
  void _goBack();
};
