//
// M5Stack CoreS3 (M5GFX) — custom LGFX config for ILI9342C.
// Inherits lgfx::LGFX_Device via IDisplay; no M5GFX auto-detection.
// LCD reset is handled by M5Unified (via AW9523B) before display.init().
// Backlight is set via M5.Display.setBrightness() (routes through AXP2101 DLDO1).
// SPI bus shared with SD card (bus_shared=true); Arduino SPI.begin() inits SPI2
// first (Device::createInstance), then M5.begin() and our Bus_SPI both attach.
//

#pragma once

#include "core/IDisplay.h"
#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_ILI9342.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

class DisplayImpl : public IDisplay {
  lgfx::Panel_ILI9342  _panelImpl;
  lgfx::Bus_SPI        _busImpl;

public:
  DisplayImpl() {
    // ── SPI bus ───────────────────────────────────────────────────
    auto bcfg = _busImpl.config();
    bcfg.spi_host    = SPI2_HOST;       // HSPI on ESP32-S3
    bcfg.spi_mode    = 0;
    bcfg.freq_write  = 40000000;
    bcfg.freq_read   = 16000000;
    bcfg.spi_3wire   = false;
    bcfg.use_lock    = true;
    bcfg.dma_channel = SPI_DMA_CH_AUTO;
    bcfg.pin_sclk    = SPI_SCK_PIN;    // GPIO 36
    bcfg.pin_mosi    = SPI_MOSI_PIN;   // GPIO 37
    bcfg.pin_miso    = -1;             // LCD has no MISO; SD uses MISO=35
    bcfg.pin_dc      = TFT_DC;         // GPIO 35 (also SPI MISO for SD)
    _busImpl.config(bcfg);
    _panelImpl.setBus(&_busImpl);

    // ── Panel ─────────────────────────────────────────────────────
    auto pcfg = _panelImpl.config();
    pcfg.pin_cs          = LCD_CS;  // GPIO 3
    pcfg.pin_rst         = -1;      // reset done by M5Unified (AW9523B P1.5)
    pcfg.pin_busy        = -1;
    pcfg.panel_width     = 320;
    pcfg.panel_height    = 240;
    pcfg.offset_x        = 0;
    pcfg.offset_y        = 0;
    pcfg.offset_rotation = 1;      // setRotation(0) → landscape 320×240
    pcfg.dummy_read_pixel = 8;
    pcfg.dummy_read_bits  = 1;
    pcfg.readable        = false;
    pcfg.invert          = true;
    pcfg.rgb_order       = false;
    pcfg.dlen_16bit      = false;
    pcfg.bus_shared      = true;   // shared with SD card
    _panelImpl.config(pcfg);
    setPanel(&_panelImpl);
  }

  void setBrightness(uint8_t pct) override {
    // M5.Display controls the AXP2101 DLDO1 backlight rail.
    // Scale our 0-100 percent to 0-255.
    M5.Display.setBrightness((uint8_t)((uint32_t)pct * 255 / 100));
  }
};
