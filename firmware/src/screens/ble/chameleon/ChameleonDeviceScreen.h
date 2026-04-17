#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonDeviceScreen : public BaseScreen {
public:
  const char* title() override { return "Device Info"; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr int kFields = 6;

  ScrollListView      _scrollView;
  ScrollListView::Row _rows[kFields];
  String              _rowLabels[kFields];
  String              _rowValues[kFields];

  void _load();
};
