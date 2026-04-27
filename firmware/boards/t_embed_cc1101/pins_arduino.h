//
// LilyGO T-Embed CC1101 (ESP32-S3, 16MB flash, 8MB OPI PSRAM)
//

#pragma once
#include <stdint.h>

// ─── SPI Bus ──────────────────────────────────────────────
#define SPI_MOSI_PIN  9
#define SPI_MISO_PIN  10
#define SPI_SCK_PIN   11

// ─── SD Card ──────────────────────────────────────────────
#define SD_CS  13

static const uint8_t SS   = SD_CS;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (power, fuel gauge) ──────────────────────────────
#define GROVE_SDA  8
#define GROVE_SCL  18

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS  41
#define LCD_DC  16
#define LCD_BL  21
#define LCD_BL_CH  7

// ─── IR ───────────────────────────────────────────────────
#define IR_TX_PIN  2
#define IR_RX_PIN  1

// ─── CC1101 Sub-GHz ───────────────────────────────────────
#define CC1101_CS_PIN    12
#define CC1101_GDO0_PIN   3

// ─── NRF24L01+ (QWIIC port) ──────────────────────────────
#define NRF24_CE_PIN     43
#define NRF24_CSN_PIN    44

// ─── Rotary Encoder ───────────────────────────────────────
#define ENCODER_A    4
#define ENCODER_B    5
#define ENCODER_BTN  0   // encoder push (active LOW)
#define ENCODER_BK   6   // dedicated back button (active LOW)

// ─── Power ────────────────────────────────────────────────
#define PIN_POWER_ON  15  // keep HIGH to stay powered

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_DRIVER
#define TFT_WIDTH   170
#define TFT_HEIGHT  320
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_MISO    SPI_MISO_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     -1     // shared with SPK_WCLK (40) — use software reset
#define TFT_BL      LCD_BL
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION 3
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000

// ─── Speaker (NS4168 I2S amp) ─────────────────────────────
#define SPK_BCLK      46
#define SPK_WCLK      40
#define SPK_DOUT      7
#define SPK_I2S_PORT  I2S_NUM_0

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_SOUND          // NS4168 I2S speaker
#define DEVICE_HAS_VOLUME_CONTROL // I2S amp supports setVolume()
#define DEVICE_HAS_USB_HID        // ESP32-S3 native USB OTG
#define APP_MENU_POWER_OFF        // BQ25896 can power off the device
