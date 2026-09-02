#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

// Shows the current emulator payload of a Chameleon slot:
//  - HF: reads MIFARE Classic blocks or MF0 / Ultralight / NTAG pages
//  - LF: reads the EM410X / HID / Viking UID from the LF slot getters
class ChameleonSlotViewScreen : public BaseScreen {
public:
  ChameleonSlotViewScreen(uint8_t slot, bool lf)
    : _slot(slot), _lf(lf) {}

  const char* title() override { return _title; }
  bool inhibitPowerOff() override { return _loading; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  uint8_t _slot;
  bool    _lf;
  char    _title[18] = {};
  bool    _loading   = true;
  bool    _ready     = false;
  uint8_t _previousSlot = 0;
  bool    _restoreSlot = false;

  static constexpr int MAX_ROWS = 270;      // up to 256 blocks/pages + headers + controls
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[MAX_ROWS];
  String              _labels[MAX_ROWS];
  String              _values[MAX_ROWS];
  uint16_t            _rowCount = 0;

  void _runHF();
  void _runLF();
  void _restoreActiveSlot();
  void _addRow(const char* label, const String& value);
};
