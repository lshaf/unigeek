#pragma once
#include <stdint.h>


// ─── SPI Bus ──────────────────────────────────────────────
#define SPI_SS_PIN    5
#define SPI_MOSI_PIN  15
#define SPI_MISO_PIN  36
#define SPI_SCK_PIN   13

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (AXP192 + IMU) ───────────────────────────────────
#define GROVE_SDA  32
#define GROVE_SCL  33

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS  5
#define LCD_DC  23
#define LCD_RST 18
#define LCD_BL  -1   // AXP192 controlled, GPIO just for init

// ─── Buttons ──────────────────────────────────────────────
#define BTN_A  37   // front button
#define BTN_B  39   // side button

// ─── IMU (MPU6886) ────────────────────────────────────────
#define IMU_I2C_ADDRESS  0x68
#define IMU_SDA          GROVE_SDA
#define IMU_SCL          GROVE_SCL

// ─── TFT_eSPI config (USER_SETUP_LOADED in platformio.ini) ─
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_DRIVER
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     LCD_RST
#define TFT_BL      LCD_BL
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION 3
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY   5000000

// PERMISSION MENU
#define APP_MENU_POWER_OFF