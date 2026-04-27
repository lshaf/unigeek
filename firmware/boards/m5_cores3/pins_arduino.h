//
// M5Stack CoreS3 — ESP32-S3, 16MB flash, 8MB OPI PSRAM.
// ILI9342C 320×240, FT6336U touch, AXP2101 power, AW9523B GPIO expander.
// AW88298 I2S speaker amp (enabled via AW9523B P0.2).
//
// Uses the same include-guard name as the framework variant so that
// the framework's variants/m5stack_cores3/pins_arduino.h is suppressed.
//

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// ─── Internal I2C (AXP2101 + FT6336U + AW9523B) ──────────────
#define INTERNAL_SDA  12
#define INTERNAL_SCL  11

static const uint8_t SDA = INTERNAL_SDA;
static const uint8_t SCL = INTERNAL_SCL;

// ─── Grove I2C ────────────────────────────────────────────────
#define GROVE_SDA  2
#define GROVE_SCL  1

// ─── GPS (Grove port default) ─────────────────────────────────
#define GPS_TX    2
#define GPS_RX    1
#define GPS_BAUD  115200

// ─── LCD (ILI9342C, shared SPI with SD) ───────────────────────
#define LCD_CS  3

// ─── SD Card (shares SPI with LCD) ────────────────────────────
#define SD_CS  4

// ─── SPI Bus (shared LCD + SD) ────────────────────────────────
#define SPI_SCK_PIN   36
#define SPI_MISO_PIN  35
#define SPI_MOSI_PIN  37
#define SPI_SS_PIN    SD_CS

static const uint8_t SS   = SPI_SS_PIN;
static const uint8_t MOSI = SPI_MOSI_PIN;
static const uint8_t MISO = SPI_MISO_PIN;
static const uint8_t SCK  = SPI_SCK_PIN;

// ─── Speaker (AW88298 I2S amp, enabled via AW9523B) ──────────
#define SPK_BCLK      34
#define SPK_WCLK      33
#define SPK_DOUT      13
#define SPK_I2S_PORT  I2S_NUM_1

// ─── Touch (FT6336U on internal I2C) ─────────────────────────
#define TOUCH_INT  21

// ─── TFT_eSPI config ─────────────────────────────────────────
#define DISABLE_ALL_LIBRARY_WARNINGS 1
#define USER_SETUP_LOADED 1

#define ILI9342_DRIVER
#define TFT_INVERSION_ON
// TFT_WIDTH/TFT_HEIGHT omitted — ILI9342 driver defaults to 320×240
#define TFT_MOSI    SPI_MOSI_PIN
#define TFT_SCLK    SPI_SCK_PIN
#define TFT_CS      LCD_CS
#define TFT_DC      SPI_MISO_PIN
#define TFT_RST     -1
#define TFT_MISO    -1
#define TFT_BL      -1
#define TFT_BACKLIGHT_ON HIGH
#define TOUCH_CS    -1
#define SMOOTH_FONT
#define USE_HSPI_PORT
#define TFT_DEFAULT_ORIENTATION 6
#define SPI_FREQUENCY        10000000
#define SPI_READ_FREQUENCY   10000000

// ─── Firmware Feature Flags ───────────────────────────────────
#define DEVICE_HAS_SOUND              // AW88298 I2S speaker
#define DEVICE_HAS_VOLUME_CONTROL     // I2S amp supports setVolume()
#define DEVICE_HAS_USB_HID            // ESP32-S3 native USB HID
#define DEVICE_HAS_WEBAUTHN           // FIDO2/WebAuthn over USB HID
#define APP_MENU_POWER_OFF            // AXP2101 power-off
#define DEVICE_HAS_TOUCH_NAV          // touch-only navigation (no physical buttons)
#define DEVICE_HAS_SCREEN_ORIENT        // display rotates 180° for right-hand mode

#endif // Pins_Arduino_h
