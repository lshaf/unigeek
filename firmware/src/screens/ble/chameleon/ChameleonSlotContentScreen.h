#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

// Interpreted content preview for a Chameleon HF slot.
// Supports MIFARE Classic and MF0 / Ultralight / NTAG slot content.
class ChameleonSlotContentScreen : public BaseScreen {
public:
  explicit ChameleonSlotContentScreen(uint8_t slot) : _slot(slot) {}

  const char* title() override { return _title; }
  bool inhibitPowerOff() override { return _loading; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  uint8_t _slot;
  char    _title[24] = {};
  bool    _loading = true;

  uint8_t _previousSlot = 0;
  bool    _restoreSlot = false;

  uint8_t* _dump = nullptr;
  uint16_t _dumpLen = 0;
  uint16_t _blocks = 0;
  uint16_t _pages = 0;
  uint16_t _hfType = 0;

  static constexpr int MAX_ROWS = 20;
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[MAX_ROWS];
  String              _labels[MAX_ROWS];
  String              _values[MAX_ROWS];
  uint8_t             _rowCount = 0;

  void _run();
  void _buildPreview();
  void _buildMfuPreview();
  void _freeDump();
  void _restoreActiveSlot();
  void _addRow(const char* label, const String& value);
  bool _extractClassicNdef(uint8_t** ndef, size_t* ndefLen) const;
};
