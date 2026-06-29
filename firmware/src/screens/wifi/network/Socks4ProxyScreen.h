#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/LogView.h"
#include "utils/network/Socks4Proxy.h"

// SOCKS4 proxy server screen — start a tunnel on <device-ip>:1080 and watch
// connections. Lives under WiFi → Network (requires an active STA connection).
class Socks4ProxyScreen : public BaseScreen {
public:
  const char* title()    override { return "SOCKS4 Proxy"; }
  bool inhibitPowerOff() override { return true; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  Socks4Proxy   _proxy;
  LogView       _log;
  bool          _running  = false;
  unsigned long _lastDraw = 0;

  static void _logCb(void* ctx, const char* msg, uint16_t color);
  static void _statusCb(Sprite& sp, int barY, int width, void* userData);
};
