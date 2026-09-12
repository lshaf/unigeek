#pragma once

#include "core/Device.h"
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class ListViewScreen : public BaseScreen
{
public:
  void onUpdate() override
  {
    if (!Uni.Nav->wasPressed()) return;

    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || (backOnPress() && dir == INavigation::DIR_PRESS)) {
      onBack();
      return;
    }
    if (_view.onNav(dir)) {
      render();
    }
  }

  void onRender() override
  {
    _view.render(bodyX(), bodyY(), bodyW(), bodyH());
  }

protected:
  using Row = ScrollListView::Row;

  void setRows(Row* rows, uint16_t count)
  {
    _view.setRows(rows, count);
  }

  virtual void onBack() = 0;
  virtual bool backOnPress() { return true; }

private:
  ScrollListView _view;
};

