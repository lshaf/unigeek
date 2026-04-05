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

// ─── IR Transmitter ──────────────────────────────────────
#define IR_TX_PIN  9

// ─── CC1101 Sub-GHz (Grove port SPI) ─────────────────────
#define CC1101_CS_PIN    26
#define CC1101_GDO0_PIN  25
#define CC1101_SCK_PIN   0
#define CC1101_MISO_PIN  33
#define CC1101_MOSI_PIN  32

// ─── Speaker (buzzer, LEDC PWM) ───────────────────────────
#define SPK_PIN  2

// ─── Internal I2C (AXP192 + BM8563) ───────────────────────
#define INTERNAL_SDA  21
#define INTERNAL_SCL  22

// ─── RTC (BM8563) ─────────────────────────────────────────
#define DEVICE_HAS_RTC
#define RTC_I2C_ADDR  0x51
#define RTC_REG_BASE  0x02  // BM8563: seconds register at 0x02
#define RTC_WIRE      Wire1  // shares internal I2C bus with AXP192

// ─── GPS (external, default pins) ─────────────────────────
#define GPS_TX    32
#define GPS_RX    33
#define GPS_BAUD  115200

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_SOUND          // buzzer attached — enables audio paths and sound settings
                                  // NOTE: no DEVICE_HAS_VOLUME_CONTROL — piezo buzzer has no real volume control
#define DEVICE_HAS_NAV_MODE_SWITCH // supports switching between default (buttons) and encoder navigation
#define APP_MENU_POWER_OFF        // show Power Off in main menu (hardware power cut via AXP192)