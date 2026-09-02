#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/ble/ChameleonClient.h"

class ChameleonMfuScreen : public BaseScreen {
public:
  const char* title() override { return "Ultralight / NTAG"; }
  bool inhibitPowerOff() override { return _busy; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT };

  State _state = STATE_IDLE;
  bool _busy = false;
  bool _needsDraw = true;

  ChameleonClient::MfuTagInfo _info = {};
  uint8_t* _dump = nullptr;
  uint16_t _dumpLen = 0;

  static constexpr int kMaxRows = 16;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _rowLabels[kMaxRows];
  String _rowValues[kMaxRows];
  uint8_t _rowCount = 0;

  void _drawIdle();
  void _read();
  void _buildResult();
  void _save();
  void _resultActions();
  void _freeDump();
};
