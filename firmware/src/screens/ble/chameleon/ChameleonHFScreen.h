#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonHFScreen : public BaseScreen {
public:
  const char* title() override { return "HF Reader"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT, STATE_CLONED, STATE_ERROR };

  State _state     = STATE_IDLE;
  bool  _scanning  = false;
  bool  _needsDraw = true;
  bool  _holdFired = false;

  uint8_t _uid[7]     = {};
  uint8_t _uidLen     = 0;
  uint8_t _atqa[2]    = {};
  uint8_t _sak        = 0;
  uint8_t _activeSlot = 0;

  static constexpr int kMaxRows = 10;
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String              _rowLabels[kMaxRows];
  String              _rowValues[kMaxRows];
  uint8_t             _rowCount = 0;

  static const char* _inferType(uint8_t sak, const uint8_t atqa[2]);
  void _draw();
  void _doScan();
  void _doClone();
};
