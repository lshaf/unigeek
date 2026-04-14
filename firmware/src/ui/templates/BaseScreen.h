#pragma once

#include "core/IScreen.h"
#include "core/AchievementManager.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"

class BaseScreen : public IScreen
{
public:
  void init() override {
    Uni.Lcd.fillScreen(TFT_BLACK);
    onInit();
    render();
  }

  void update() override {
    onUpdate();
    Achievement.drawToastIfNeeded(bodyX(), bodyY(), bodyW(), bodyH());
    if (Uni.lcdOff) return;
    if (!isFullScreen() && millis() - _lastStatusUpdate > 1000) {
      StatusBar::refresh();
      _lastStatusUpdate = millis();
    }
  }

  void render() override {
    if (Uni.lcdOff) return;
    if (!isFullScreen()) _renderChrome();
    onRender();
  }

protected:
  // ─── Body area helpers ────────────────────────────────
  uint16_t bodyX() { return StatusBar::WIDTH; }
  uint16_t bodyY() { return title() ? Header::HEIGHT : 0; }
  uint16_t bodyW() { return Uni.Lcd.width() - StatusBar::WIDTH - 4; }
  uint16_t bodyH() { return Uni.Lcd.height() - bodyY() - 4; }

  // ─── Subclass overrides ───────────────────────────────
  virtual const char* title() { return nullptr; }  // nullptr = no header
  virtual bool isFullScreen() { return false; }   // true = skip header + status bar

  virtual void onInit()   {}
  virtual void onUpdate() {}
  virtual void onRender() {}

private:
  Header   _header;
  uint32_t _lastStatusUpdate = 0;

  void _renderChrome() {
    _header.render(title());
    StatusBar::refresh();
  }
};