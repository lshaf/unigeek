#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

// Network > Information: this connection's details (IP, DNS, gateway, SSID,
// password, signal, MAC...). [Press] Options offers Connectivity Check and the
// network's QR Code; arrows scroll; [Back] returns to the Network menu.
class NetworkInformationScreen : public BaseScreen {
public:
  const char* title() override { return "Information"; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr int kFooterH = 14;  // one hint line

  ScrollListView      _scrollView;
  ScrollListView::Row _rows[11];

  void _load();
  void _renderFooter();
  void _openOptions();
  void _showQRCode();
};
