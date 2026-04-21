//
// DIY Smoochie (ESP32-S3, 16MB flash)
//

#pragma once
#include <stdint.h>

// ─── RF module SPI bus (CC1101/NRF24) ────────────────────
#define SPI_MOSI_PIN  12
#define SPI_MISO_PIN  11
#define SPI_SCK_PIN   13
#define SPI_SS_PIN    46   // CC1101 CS

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (Grove) ──────────────────────────────────────────
#define GROVE_SDA  47
#define GROVE_SCL  48

static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

// ─── LCD (shares SPI bus with SD: MOSI=17, SCK=18, MISO=8) ─
#define LCD_CS   7
#define LCD_DC   15
#define LCD_RST  16
#define LCD_BL     6
#define LCD_BL_CH  7

// ─── SD Card (shares display SPI bus: SCK=18, MISO=8, MOSI=17) ───
#define SD_CS  3

// ─── Navigation buttons (active LOW) ──────────────────────
#define BTN_SEL    0   // OK / Select
#define BTN_UP    41   // Up
#define BTN_DOWN  40   // Down
#define BTN_RIGHT 38   // Right
#define BTN_LEFT  39   // Left / Back

// ─── IR Emitter ───────────────────────────────────────────
#define IR_TX  44

// ─── CC1101 Sub-GHz ──────────────────────────────────────
#define CC1101_CS_PIN   46
#define CC1101_GDO0_PIN 9

// ─── RGB LED (WS2812B) ────────────────────────────────────
#define RGB_LED  45

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER  TFT_RGB
#define TFT_WIDTH   170
#define TFT_HEIGHT  320
#define TFT_MOSI    17
#define TFT_SCLK    18
#define TFT_MISO    8
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     LCD_RST
#define TFT_BL      LCD_BL
#define TFT_BACKLIGHT_ON  1
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION  1
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT 1
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  16000000

// ─── App feature flags ────────────────────────────────────
// Device has dedicated up/down/left/right buttons in addition to select.
// Enables row-based grid navigation in MainMenuScreen (UP/DOWN move between
// rows, LEFT/RIGHT move between columns). Without this flag, UP maps to LEFT
// and DOWN maps to RIGHT so 2-button devices can still traverse the grid.
#define DEVICE_HAS_4WAY_NAV
