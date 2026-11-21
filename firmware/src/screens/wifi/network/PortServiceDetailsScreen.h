#pragma once
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/network/ServiceProbeUtil.h"

class PortServiceDetailsScreen : public BaseScreen {
public:
  const char* title() override { return "Service Details"; }
  void configure(const PortScanUtil::Result& selected,
                 const ServiceProbeUtil::Result& probe) {
    _endpoint = selected.label;
    _service = selected.service;
    _probe = probe;
  }
  void onInit() override {
    _rows[0] = {"IP:Port", _endpoint};
    _rows[1] = {"Service (guess)", _service};
    _rows[2] = {"Protocol", _probe.protocol.length() ? _probe.protocol : "Unknown"};
    _rows[3] = {"Product", _probe.product.length() ? _probe.product : "Unknown"};
    _rows[4] = {"Version", _probe.version.length() ? _probe.version : "Unknown"};
    _rows[5] = {"Banner", _probe.banner.length() ? _probe.banner : "No banner"};
    for (size_t i = 1; i < 6; ++i) {
      if (_rows[i].value.length() > 24)
        _rows[i].value = _rows[i].value.substring(0, 21) + "...";
    }
    _list.setRows(_rows, 6);
    _list.resetScroll();
  }
  void onRender() override { _list.render(bodyX(), bodyY(), bodyW(), bodyH()); }
  void onUpdate() override {
    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
#ifndef DEVICE_HAS_KEYBOARD
    if (dir == INavigation::DIR_PRESS) { Screen.goBack(); return; }
#endif
    _list.onNav(dir);
  }
private:
  String _endpoint, _service;
  ServiceProbeUtil::Result _probe;
  ScrollListView::Row _rows[6] = {};
  ScrollListView _list;
};
