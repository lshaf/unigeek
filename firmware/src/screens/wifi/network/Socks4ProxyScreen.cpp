#ifdef HAS_NET_TOOLS
#include "Socks4ProxyScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include <WiFi.h>
#include <cstdio>

void Socks4ProxyScreen::_logCb(void* ctx, const char* msg, uint16_t color) {
  static_cast<Socks4ProxyScreen*>(ctx)->_log.addLine(msg, color);
}

void Socks4ProxyScreen::_statusCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<Socks4ProxyScreen*>(userData);
  char buf[40];
  snprintf(buf, sizeof(buf), "Conns:%lu  %s",
           (unsigned long)self->_proxy.connections(),
           self->_proxy.tunnelActive() ? "TUNNEL" : "idle");
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(self->_proxy.tunnelActive() ? TFT_GREEN : TFT_DARKGREY);
  sp.drawString(buf, 2, barY);

  char kb[24];
  snprintf(kb, sizeof(kb), "U:%luk D:%luk",
           (unsigned long)(self->_proxy.bytesUp() / 1024),
           (unsigned long)(self->_proxy.bytesDown() / 1024));
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(kb, width - 2, barY);
}

void Socks4ProxyScreen::onInit() {
  _log.clear();
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to WiFi first.");
    Screen.goBack();
    return;
  }
  _proxy.setLog(&Socks4ProxyScreen::_logCb, this);
  _running = _proxy.begin(1080);

  char buf[48];
  snprintf(buf, sizeof(buf), "Proxy %s:1080", WiFi.localIP().toString().c_str());
  _log.addLine(buf, TFT_GREEN);
  _log.addLine("Set laptop SOCKS4 proxy here.", TFT_DARKGREY);
  _log.addLine("Waiting for clients...", TFT_DARKGREY);
  render();
}

void Socks4ProxyScreen::onUpdate() {
  if (_running) _proxy.poll();

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _proxy.stop();
      Screen.goBack();
      return;
    }
  }
  // Throttle redraws so sprite churn doesn't starve the relay.
  if (millis() - _lastDraw > 150) { _lastDraw = millis(); render(); }
}

void Socks4ProxyScreen::onRender() {
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _statusCb, this);
}
#endif // HAS_NET_TOOLS
