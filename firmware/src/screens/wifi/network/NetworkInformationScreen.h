#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

// Network > Network Info: this connection's details (IP, DNS, gateway, SSID,
// password, signal, MAC...). [Press] Options offers Connectivity Check, the
// network's QR Code and its IP Settings; arrows scroll; [Back] returns to
// the Network menu.
class NetworkInformationScreen : public BaseScreen {
public:
  const char* title() override { return "Network Info"; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override { _load(); }  // Settings may have changed the address

private:
  static constexpr int kFooterH = 14;  // one hint line

  ScrollListView      _scrollView;
  ScrollListView::Row _rows[11];

  void _load();
  void _renderFooter();
  void _openOptions();
  void _showQRCode();
};
