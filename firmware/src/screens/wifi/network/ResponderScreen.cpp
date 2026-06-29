#ifdef HAS_NET_TOOLS
#include "ResponderScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include <WiFi.h>
#include <cstdio>

void ResponderScreen::_logCb(void* ctx, const char* msg, uint16_t color) {
  static_cast<ResponderScreen*>(ctx)->_log.addLine(msg, color);
}

void ResponderScreen::_hitCb(void* ctx) {
  (void)ctx;
  if (Uni.Speaker) Uni.Speaker->playNotification();
}

void ResponderScreen::_statusCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ResponderScreen*>(userData);
  char buf[28];
  snprintf(buf, sizeof(buf), "Hashes: %lu", (unsigned long)self->_resp.hashCount());
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(self->_resp.hashCount() > 0 ? TFT_MAGENTA : TFT_DARKGREY);
  sp.drawString(buf, 2, barY);

  String q = self->_resp.lastQuery();
  if (q.length()) {
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_CYAN);
    sp.drawString(q.c_str(), width - 2, barY);
  }
}

void ResponderScreen::onInit() {
  _log.clear();
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to WiFi first.");
    Screen.goBack();
    return;
  }
  _running = _resp.begin("UNIGEEK", "WORKGROUP", "unigeek.local",
                         &ResponderScreen::_logCb, &ResponderScreen::_hitCb, this);

  char buf[48];
  snprintf(buf, sizeof(buf), "Poisoning as %s", WiFi.localIP().toString().c_str());
  _log.addLine(buf, TFT_GREEN);
  _log.addLine("LLMNR + NBT-NS + SMB up", TFT_DARKGREY);
  _log.addLine("Waiting for queries...", TFT_DARKGREY);
  render();
}

void ResponderScreen::onUpdate() {
  if (_running) _resp.poll();

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _resp.stop();
      Screen.goBack();
      return;
    }
  }
  if (millis() - _lastDraw > 150) { _lastDraw = millis(); render(); }
}

void ResponderScreen::onRender() {
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _statusCb, this);
}
#endif // HAS_NET_TOOLS
