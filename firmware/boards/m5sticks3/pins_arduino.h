//
// M5StickS3 — ESP32-S3, 8MB flash, ST7789 135×240, ES8311 I2S codec.
// M5PM1 (PY32) power IC at I2C 0x6E. SD/CC1101/NRF24/PN532 share FSPI bus.
//

#pragma once
#include <stdint.h>

// ─── Peripheral SPI Bus (FSPI — TFT uses its own HSPI bus) ─
// Shared by SD card, CC1101, NRF24L01+, PN532 — exposed on the
// bottom HAT connector. CS pins below; only one device active at a time.
#define SPI_SCK_PIN   5
#define SPI_MISO_PIN  4
#define SPI_MOSI_PIN  6
#define SPI_SS_PIN    7

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── SD Card ──────────────────────────────────────────────
#define SD_CS  7

// ─── CC1101 Sub-GHz ──────────────────────────────────────
#define CC1101_CS_PIN    2
#define CC1101_GDO0_PIN  3

// ─── NRF24L01+ ───────────────────────────────────────────
#define NRF24_CE_PIN    1
#define NRF24_CSN_PIN   8

// ─── PN532 NFC ───────────────────────────────────────────
#define PN532_CS_PIN   43
#define PN532_TX        1
#define PN532_RX        0

// ─── I2C (Grove) ─────────────────────────────────────────
#define GROVE_SDA       9
#define GROVE_SCL       10
#define GROVE_5V_OUTPUT true   // true = Grove 5V rail sources power; false = input (charging)

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── Internal I2C (M5PM1 power IC) ───────────────────────
#define INTERNAL_SDA  47
#define INTERNAL_SCL  48

// ─── LCD ──────────────────────────────────────────────────
#define LCD_BL     38
#define LCD_BL_CH   7

// ─── Buttons ──────────────────────────────────────────────
// BTN_A (GPIO 11): SELECT
// BTN_B (GPIO 12): long-press=BACK, double-press=UP, single-press=DOWN
#define BTN_A  11
#define BTN_B  12

// ─── IR Transmitter / Receiver ───────────────────────────
#define IR_TX_PIN  46
#define IR_RX_PIN  42

// ─── Speaker (ES8311 I2S codec) ───────────────────────────
#define SPK_BCLK      17
#define SPK_WCLK      15
#define SPK_DOUT      14
#define SPK_MCLK      18
#define SPK_I2S_PORT  I2S_NUM_0

// ─── TFT_eSPI config ─────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER  1
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
#define TFT_MOSI    39
#define TFT_SCLK    40
#define TFT_CS      41
#define TFT_DC      45
#define TFT_RST     21
#define TFT_MISO    -1
#define TFT_BL      38
#define TFT_BACKLIGHT_ON HIGH
#define TFT_INVERSION_ON
#define USE_HSPI_PORT
#define TFT_DEFAULT_ORIENTATION 3
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY        20000000
#define SPI_READ_FREQUENCY   20000000

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_SOUND              // ES8311 I2S codec
#define DEVICE_HAS_VOLUME_CONTROL     // I2S amp supports setVolume()
#define DEVICE_HAS_USB_HID            // ESP32-S3 native USB HID
#define DEVICE_HAS_SCREEN_ORIENT        // screen rotation + UP/DOWN swap for left/right hand orientation
#define APP_MENU_POWER_OFF            // M5PM1 power-off
