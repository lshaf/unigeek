//
// Created by L Shaf on 2026-02-23.
//

#pragma once
#include <stdint.h>

// ─── SPI Bus ──────────────────────────────────────────────
#define SPI_SS_PIN    21
#define SPI_MOSI_PIN  34
#define SPI_MISO_PIN  33
#define SPI_SCK_PIN   35

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (shared by keyboard, RTC, sensor, audio, touch) ──
#define GROVE_SDA  3
#define GROVE_SCL  2

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS  38
#define LCD_DC  37
#define LCD_BL  42

// ─── SD Card ──────────────────────────────────────────────
#define SD_CS  21

// ─── Keyboard (TCA8418) ───────────────────────────────────
#define KB_INT  6
#define KB_BL   46

// ─── Rotary Encoder ───────────────────────────────────────
#define ENCODER_A    40
#define ENCODER_B    41
#define ENCODER_BTN   7

// ─── RTC (PCF85063A) ──────────────────────────────────────
#define RTC_INT       1
#define DEVICE_HAS_RTC
#define RTC_I2C_ADDR  0x51
#define RTC_REG_BASE  0x04  // PCF85063A: seconds register at 0x04

// ─── NFC (ST25R3916) ──────────────────────────────────────
#define NFC_CS   39
#define NFC_INT   5

// ─── AI Sensor (BHI260AP) ─────────────────────────────────
#define SENSOR_INT  8

// ─── LoRa (SX1262) ────────────────────────────────────────
#define LORA_CS    36
#define LORA_RST   47
#define LORA_IRQ   14
#define LORA_BUSY  48

// ─── GPS (MIA-M10Q) ───────────────────────────────────────
#define GPS_TX   12
#define GPS_RX    4
#define GPS_PPS  13

// ─── Audio Codec (ES8311) ─────────────────────────────────
#define AUDIO_WS    18
#define AUDIO_SCK   11
#define AUDIO_MCLK  10
#define AUDIO_DOUT  45
#define AUDIO_DIN   17

// ─── Speaker (I2S via ES8311 codec + NS4150B amp) ─────────
#define SPK_BCLK      AUDIO_SCK   // 11
#define SPK_WCLK      AUDIO_WS    // 18
#define SPK_DOUT      AUDIO_DOUT  // 45
#define SPK_MCLK      AUDIO_MCLK  // 10
#define SPK_I2S_PORT  I2S_NUM_0

// ─── XL9555 I2C GPIO Expander ─────────────────────────────
#define EXPANDS_AMP_EN   1  // port 0 bit 1 — NS4150B amp enable
#define EXPANDS_GNSS_EN  4  // port 0 bit 4 — GNSS power supply enable

// ─── UART (external 12-pin socket) ────────────────────────
#define UART1_TX  43
#define UART1_RX  44

// ─── Custom free pin (external 12-pin socket) ─────────────
#define CUSTOM_PIN  9

// ─── Boot button ──────────────────────────────────────────
#define BTN_BOOT  0

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7796_DRIVER
#define TFT_WIDTH   222
#define TFT_HEIGHT  480
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_MISO    SPI_MISO_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     -1
#define TFT_BL      LCD_BL
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION 3
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_KEYBOARD       // keyboard attached — enables keyboard input paths
#define KB_QWERT_NUM_REMAP        // remap q-p top row to digits 1-0 in number input (no dedicated numrow)
#define DEVICE_HAS_SOUND          // speaker attached — enables audio paths and sound settings
#define DEVICE_HAS_VOLUME_CONTROL // I2S amp supports setVolume() — shows Volume slider in Settings
#define DEVICE_HAS_USB_HID        // ESP32-S3 native USB OTG — enables USB HID keyboard
#define DEVICE_HAS_GPS            // built-in MIA-M10Q GPS module
#define APP_MENU_POWER_OFF        // show Power Off in main menu (hardware power cut via BQ25896)