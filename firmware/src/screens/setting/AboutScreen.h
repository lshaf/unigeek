#pragma once

#include "ui/templates/ListViewScreen.h"

class AboutScreen : public ListViewScreen
{
public:
  const char* title() override { return "About"; }

  void onInit() override;

protected:
  void onBack() override;

private:
  static constexpr uint8_t MAX_ROWS = 15;
  Row     _rows[MAX_ROWS] = {};
  uint8_t _rowCount = 0;
};