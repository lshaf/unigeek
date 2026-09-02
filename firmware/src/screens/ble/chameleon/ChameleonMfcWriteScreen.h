#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/ble/ChameleonClient.h"
#include <Arduino.h>

class ChameleonMfcWriteScreen : public BaseScreen {
public:
  explicit ChameleonMfcWriteScreen(const String& path) : _source(SOURCE_FILE), _path(path) {}
  explicit ChameleonMfcWriteScreen(uint8_t slot) : _source(SOURCE_SLOT), _slot(slot) {}
  ChameleonMfcWriteScreen(const uint8_t* dump, uint16_t dumpLen);

  const char* title() override { return "Write to Tag"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum Source { SOURCE_FILE, SOURCE_SLOT, SOURCE_MEMORY };
  Source _source;
  String _path;
  uint8_t _slot = 0;
  bool _busy = false;

  uint8_t* _dump = nullptr;
  uint16_t _dumpLen = 0;

  uint8_t _previousSlot = 0;
  bool _restoreSlot = false;
  uint8_t _previousMode = 0;
  bool _restoreMode = false;

  static constexpr uint8_t kMaxRows = 18;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _labels[kMaxRows];
  String _values[kMaxRows];
  uint8_t _rowCount = 0;

  bool _loadSource();
  bool _loadFile();
  bool _loadSlot();
  bool _loadMemory();
  void _buildSourcePreview();
  bool _extractNdef(uint8_t** ndef, size_t* ndefLen) const;
  bool _resolveTargetKeys(uint8_t keysA[16][6], bool foundA[16],
                          uint8_t keysB[16][6], bool foundB[16]);
  bool _writeTarget(const uint8_t keysA[16][6], const bool foundA[16],
                    const uint8_t keysB[16][6], const bool foundB[16]);
  void _write();
  void _freeDump();
  void _restoreContext();
  void _addRow(const char* label, const String& value);
  static String _uidString(const uint8_t* uid, uint8_t len);
};
