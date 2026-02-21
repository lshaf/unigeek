#pragma once

#include "core/IScreen.h"
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
    // refresh status bar every second
    if (millis() - _lastStatusUpdate > 1000) {
      _renderStatusBar();
      _lastStatusUpdate = millis();
    }
    onUpdate();
  }

  void render() override {
    _renderChrome();
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

  virtual StatusBar::Status statusBarStatus() {
    // override to provide live data from your Power/radio classes
    return {
      Uni.Power.getBatteryPercentage(),
      false,
      false
    };
  }

  virtual void onInit()   {}
  virtual void onUpdate() {}
  virtual void onRender() {}

private:
  Header    _header;
  StatusBar _statusBar;
  uint32_t  _lastStatusUpdate = 0;

  void _renderChrome() {
    _header.render(title());
    _renderStatusBar();
  }

  void _renderStatusBar() {
    _statusBar.render(statusBarStatus());
  }
};