#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonEmvScreen : public BaseScreen {
public:
  const char* title() override { return "EMV Details"; }
  bool inhibitPowerOff() override { return _reading; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
private:
  bool _reading = false;
  bool _hasResult = false;
  static constexpr uint8_t kMaxRows = 28;
  ScrollListView _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String _labels[kMaxRows];
  String _values[kMaxRows];
  uint8_t _rowCount = 0;
  void _read();
  void _push(const String& label, const String& value);
  void _pushWrapped(const String& label, const String& value);
};
