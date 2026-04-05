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

// ─── I2C (Grove) ─────────────────────────────────────────
#define GROVE_SDA  32
#define GROVE_SCL  33

static const uint8_t SDA = GROVE_SDA;
static const uint8_t SCL = GROVE_SCL;

// ─── LCD ──────────────────────────────────────────────────
#define LCD_CS  5
#define LCD_DC  14
#define LCD_RST 12
#define LCD_BL      27
#define LCD_BL_CH   7

// ─── Buttons ──────────────────────────────────────────────
#define BTN_UP  35   // top side button (power/up)
#define BTN_A   37   // front button (select)
#define BTN_B   39   // bottom side button (down)

// ─── TFT_eSPI config (USER_SETUP_LOADED in platformio.ini) ─
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER   1
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     LCD_RST
#define TFT_BL      -1       // managed by Display.h via LEDC, not TFT_eSPI
#define TFT_BACKLIGHT_ON 1
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION 3
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  20000000

// ─── Speaker (buzzer, LEDC PWM) ───────────────────────────
#define SPK_PIN  2

// ─── Battery (ADC) ───────────────────────────────────────
#define BAT_ADC_PIN  38

// ─── Power keep-alive ────────────────────────────────────
#define PWR_HOLD_PIN 4

// ─── IR Transmitter ──────────────────────────────────────
#define IR_TX_PIN  19

// ─── CC1101 Sub-GHz (Grove port SPI) ─────────────────────
#define CC1101_CS_PIN    26
#define CC1101_GDO0_PIN  25
#define CC1101_SCK_PIN   0
#define CC1101_MISO_PIN  33
#define CC1101_MOSI_PIN  32

// ─── Internal I2C (BM8563 RTC) ───────────────────────────
#define INTERNAL_SDA  21
#define INTERNAL_SCL  22

// ─── RTC (BM8563) ─────────────────────────────────────────
#define DEVICE_HAS_RTC
#define RTC_I2C_ADDR  0x51
#define RTC_REG_BASE  0x02  // BM8563: seconds register at 0x02
#define RTC_WIRE      Wire1

// ─── GPS (external, default pins) ─────────────────────────
#define GPS_TX    32
#define GPS_RX    33
#define GPS_BAUD  115200

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_SOUND          // buzzer attached — enables audio paths and sound settings
                                  // NOTE: no DEVICE_HAS_VOLUME_CONTROL — piezo buzzer has no real volume control
#define DEVICE_HAS_NAV_MODE_SWITCH // supports switching between default (buttons) and encoder navigation
#define APP_MENU_POWER_OFF        // show Power Off in main menu (hardware power cut via GPIO 4 + deep sleep)
