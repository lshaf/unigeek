//
// M5Stack Cardputer (ESP32-S3)
//

#pragma once
#include <stdint.h>

// ─── SD SPI Bus ───────────────────────────────────────────
#define SPI_SS_PIN    12
#define SPI_MOSI_PIN  14
#define SPI_MISO_PIN  39
#define SPI_SCK_PIN   40

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── I2C (Grove) ──────────────────────────────────────────
#define GROVE_SDA  2
#define GROVE_SCL  1

static const uint8_t SDA = 13;
static const uint8_t SCL = 15;

// ─── LCD (own SPI bus: MOSI=35, SCK=36) ──────────────────
#define LCD_CS   37
#define LCD_DC   34
#define LCD_RST  33
#define LCD_BL     38
#define LCD_BL_CH  7
#define BAT_ADC_PIN  10

// ─── SD Card ──────────────────────────────────────────────
#define SD_CS  12

// ─── Keyboard (74HC138 GPIO matrix) ───────────────────────

// ─── IR Emitter ───────────────────────────────────────────
#define IR_TX  44

// ─── CC1101 Sub-GHz ──────────────────────────────────────
#define CC1101_CS_PIN   1   // GROVE_SCL
#define CC1101_GDO0_PIN 2   // GROVE_SDA

// ─── NRF24L01+ ────────────────────────────────────────────
#define NRF24_CSN_PIN   1   // GROVE_SCL
#define NRF24_CE_PIN    2   // GROVE_SDA

// ─── RGB LED (SK6812) ─────────────────────────────────────
#define RGB_LED  21

// ─── Speaker (I2S) ────────────────────────────────────────
#define SPK_BCLK      41
#define SPK_WCLK      43
#define SPK_DOUT      42
#define SPK_I2S_PORT  I2S_NUM_1

// ─── Boot / shoulder button ───────────────────────────────
#define BTN_BOOT  0

// ─── TFT_eSPI config ──────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER  1
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
#define TFT_MOSI    35
#define TFT_SCLK    36
#define TFT_MISO    -1
#define TFT_CS      LCD_CS
#define TFT_DC      LCD_DC
#define TFT_RST     LCD_RST
#define TFT_BL      LCD_BL
#define TFT_BACKLIGHT_ON  1
#define TFT_INVERSION_ON
#define TFT_DEFAULT_ORIENTATION  1
#define USE_HSPI_PORT
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  20000000

// ─── GPS (external, default pins) ─────────────────────────
#define GPS_TX    2
#define GPS_RX    1
#define GPS_BAUD  115200

// ─── Firmware Feature Flags ───────────────────────────────
#define DEVICE_HAS_KEYBOARD       // keyboard attached — enables keyboard input paths
#define DEVICE_HAS_SOUND          // speaker attached — enables audio paths and sound settings
#define DEVICE_HAS_VOLUME_CONTROL // I2S amp supports setVolume() — shows Volume slider in Settings
#define DEVICE_HAS_USB_HID        // ESP32-S3 native USB OTG — enables USB HID keyboard
#define DEVICE_HAS_WEBAUTHN       // FIDO2/WebAuthn over USB HID
#define APP_MENU_POWER_OFF        // show Power Off in main menu (hardware power cut via GPIO 4 + deep sleep)
// Device has dedicated up/down/left/right navigation in addition to select.
// Enables row-based grid navigation in MainMenuScreen (UP/DOWN move between
// rows, LEFT/RIGHT move between columns). Without this flag, UP maps to LEFT
// and DOWN maps to RIGHT so 2-button devices can still traverse the grid.
#define DEVICE_HAS_4WAY_NAV
