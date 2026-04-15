//
// WiFi Marauder v7 (ESP32, 4MB flash)
// ILI9341 240x320 display, 5 buttons active LOW with external pull-ups.
// GPIOs 34-39 are input-only on ESP32 — no internal pull-up, use INPUT mode.
// Display uses VSPI (SCK=18, MISO=19, MOSI=23); SD uses dedicated HSPI (SCK=14, MISO=12, MOSI=13).
//

#pragma once
#include <stdint.h>

// ─── Display SPI bus (VSPI: SCK=18, MISO=19, MOSI=23) ────
#define SPI_MOSI_PIN  23
#define SPI_MISO_PIN  19
#define SPI_SCK_PIN   18

static const uint8_t SS   = 21;  // SD CS
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (Grove) ──────────────────────────────────────────
#define GROVE_SDA  33
#define GROVE_SCL  25

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS   17
#define LCD_DC   16
#define LCD_BL   32   // backlight: HIGH = on, PWM capable

// ─── SD Card (dedicated HSPI: SCK=14, MISO=12, MOSI=13) ──
#define SDCARD_CS    21
#define SDCARD_SCK   14
#define SDCARD_MISO  12
#define SDCARD_MOSI  13
#define SD_CS  SDCARD_CS   // alias used by Device::initStorage()

// ─── Navigation buttons (active LOW, external pull-ups) ───
// GPIO 34/35/36/39: input-only, no internal pull-up — use INPUT mode
#define BTN_SEL    34   // OK / Select  (RTC_GPIO4, usable for ext0 wakeup)
#define BTN_UP     36   // Up
#define BTN_DOWN   35   // Down
#define BTN_RIGHT  39   // Right
#define BTN_LEFT   26   // Left / Back

// ─── App feature flags ────────────────────────────────────
#define APP_MENU_POWER_OFF   // deep sleep via BTN_SEL wakeup

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1
#define TFT_INVERSION_ON

#define ILI9341_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_MISO    SPI_MISO_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     5
#define TFT_BL      LCD_BL
#define TFT_BACKLIGHT_ON  1
#define TFT_DEFAULT_ORIENTATION  0
#define TOUCH_CS    -1
#define SMOOTH_FONT 1
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  16000000

// ─── App feature flags ────────────────────────────────────
// Device has dedicated up/down/left/right buttons in addition to select.
// Enables row-based grid navigation in MainMenuScreen (UP/DOWN move between
// rows, LEFT/RIGHT move between columns). Without this flag, UP maps to LEFT
// and DOWN maps to RIGHT so 2-button devices can still traverse the grid.
#define DEVICE_HAS_4WAY_NAV