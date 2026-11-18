#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonHFScreen : public BaseScreen {
public:
  explicit ChameleonHFScreen(bool readMode = false) : _readMode(readMode) {}
  const char* title() override { return _readMode ? "Read Tag" : "Scan Tag"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };

  bool _readMode = false;
  bool _dispatchRead = false;
  bool _returnToMenu = false;
  State _state     = STATE_IDLE;
  bool  _scanning  = false;
  bool  _needsDraw = true;
  uint8_t _familyMenu = 0;

  uint8_t _uid[7]     = {};
  uint8_t _uidLen     = 0;
  uint8_t _atqa[2]    = {};
  uint8_t _sak        = 0;
  uint16_t _tagType    = 0;

  static constexpr int kMaxRows = 10;
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String              _rowLabels[kMaxRows];
  String              _rowValues[kMaxRows];
  uint8_t             _rowCount = 0;

  static const char* _inferType(uint8_t sak, const uint8_t atqa[2]);
  void _draw();
  void _doScan();
};
