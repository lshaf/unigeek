//
// CYD — All variants:
//   2432W328R / 2432S024R  ILI9341 240×320, XPT2046 shared HSPI
//   2432S028 / 2432S028_2USB  ILI9341 240×320, XPT2046 dedicated SPI (bit-bang)
//   2432W328C / 2432W328C_2   ILI9341 240×320, CST816S capacitive I2C
//   3248S035R                  ST7796  480×320, XPT2046 dedicated SPI (bit-bang)
//   3248S035C                  ST7796  480×320, GT911 capacitive I2C
//
// Fixed across all variants:
//   Display SPI (HSPI): MOSI=13, MISO=12, SCK=14, CS=15, DC=2
//   SD card (VSPI):     MOSI=23, MISO=19, SCK=18, CS=5
//   IR TX: GPIO 22
//
// Per-variant overrides via build flags (see config.ini):
//   LCD_BL, GROVE_SDA, SPI_FREQUENCY, SPI_READ_FREQUENCY
//   TFT_WIDTH, TFT_HEIGHT, ST7796_DRIVER
//   TOUCH_SPI_MOSI/MISO/SCK, TOUCH_CST816S, TOUCH_GT911
//

#pragma once
#include <stdint.h>

// ─── SPI Bus (VSPI — SD card) ─────────────────────────────
#define SPI_SS_PIN    5
#define SPI_MOSI_PIN  23
#define SPI_MISO_PIN  19
#define SPI_SCK_PIN   18

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C ──────────────────────────────────────────────────
// Default: W328R/S024R/W328C (SDA=21). Override to 27 for S028 / 3248S035.
#ifndef GROVE_SDA
#define GROVE_SDA  21
#endif
#define GROVE_SCL  22

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── SD Card ──────────────────────────────────────────────
#define SD_CS  5

// ─── LCD Backlight ────────────────────────────────────────
// Default: W328R/S024R/W328C (GPIO 27). Override to 21 for S028.
#ifndef LCD_BL
#define LCD_BL  27
#endif
#define LCD_BL_CH  0

// ─── Touch ────────────────────────────────────────────────
// XPT2046 CS — only needed for resistive touch boards (not cap-touch).
// Suppressed when TOUCH_CST816S or TOUCH_GT911 is defined so TFT_eSPI
// doesn't try to initialise XPT2046 touch on those boards.
#if !defined(TOUCH_CST816S) && !defined(TOUCH_GT911)
#define TOUCH_CS  33
#endif

// Touch interrupt / capacitive INT pin.
// Default GPIO 36 (W328R, S024R, S028, W328C). Override for 3248S035C (25).
#ifndef TOUCH_IRQ
#define TOUCH_IRQ  36
#endif

// ─── IR Transmitter ──────────────────────────────────────
#define IR_TX_PIN  22

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

// Display driver. Override with -DST7796_DRIVER for 3248S035 boards.
#ifndef ST7796_DRIVER
#define ILI9341_2_DRIVER
#endif

// Physical panel dimensions (portrait-native).
// Default: 240×320 (2.8" ILI9341). Override: -DTFT_WIDTH=320 -DTFT_HEIGHT=480 for 3248S035.
#ifndef TFT_WIDTH
#define TFT_WIDTH   240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT  320
#endif

#define USE_HSPI_PORT           // display on HSPI bus
#define TFT_MISO    12
#define TFT_MOSI    13
#define TFT_SCLK    14
#define TFT_CS      15
#define TFT_DC      2
#define TFT_RST     -1
#define TFT_BL      -1          // backlight via LEDC (see LCD_BL)
#define TFT_BACKLIGHT_ON HIGH

// Default: landscape (rotation=1). Override with -DTFT_DEFAULT_ORIENTATION=0 for portrait.
#ifndef TFT_DEFAULT_ORIENTATION
#define TFT_DEFAULT_ORIENTATION  1
#endif

#define SMOOTH_FONT

// Display SPI clock. Default 55 MHz (W328R). Override for S028/3248S035 (40 MHz).
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY        55000000
#endif
#ifndef SPI_READ_FREQUENCY
#define SPI_READ_FREQUENCY   20000000
#endif
#define SPI_TOUCH_FREQUENCY  2500000

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_TOUCH_NAV
