#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfcAdvancedScreen : public ListScreen {
public:
  const char* title() override { return _showingMemory ? "Read Memory" : "Advanced"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[4];
  bool _showingMemory = false;
  ScrollListView _view;
  ScrollListView::Row _rows[260];
  String _labels[260];
  String _values[260];
  uint16_t _rowCount = 0;
  void _readMemory();
  void _editMemory();
  void _editUidGen3();
  void _lockUidGen3();
  void _addRow(const String& label, const String& value);
};
