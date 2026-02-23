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

// ─── I2C ──────────────────────────────────────────────────
#define GROVE_SDA  3
#define GROVE_SCL  2

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS  38
#define LCD_DC  37
#define LCD_BL  42

// ─── Keyboard ─────────────────────────────────────────────
#define KB_I2C_ADDRESS  0x34
#define KB_BL           46

// ─── Encoder ──────────────────────────────────────────────
#define ENCODER_A    40
#define ENCODER_B    41
#define ENCODER_BTN   7

// ─── LoRa ─────────────────────────────────────────────────
#define LORA_CS    36
#define LORA_RST   47
#define LORA_IRQ   14
#define LORA_BUSY  48

// ─── GPS ──────────────────────────────────────────────────
#define GPS_TX   12
#define GPS_RX    4
#define GPS_PPS  13

// ─── SD Card ──────────────────────────────────────────────
#define SDCARD_CS  21

// ─── NFC ──────────────────────────────────────────────────
#define NFC_CS   39
#define NFC_INT   5

// ─── Audio ────────────────────────────────────────────────
#define AUDIO_I2S_WS    18
#define AUDIO_I2S_SCK   11
#define AUDIO_I2S_MCLK  10
#define AUDIO_I2S_SDOUT 45
#define AUDIO_I2S_SDIN  17

// ─── Encoder extra ────────────────────────────────────────
#define ENCODER_KEY  7   // same as ENCODER_BTN alias

// ─── RTC ──────────────────────────────────────────────────
#define RTC_INT  1

// ─── Buttons ──────────────────────────────────────────────
#define BTN_BOOT  0   // BOOT button doubles as wakeup

// ─── TFT_eSPI config (USER_SETUP_LOADED in platformio.ini) ─
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
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000