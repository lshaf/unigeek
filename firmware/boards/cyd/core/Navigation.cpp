//
// CYD navigation — four touch backends selected by build flags:
//
//   TOUCH_CST816S   CST816S capacitive I2C (addr 0x15, SDA=GROVE_SDA, SCL=GROVE_SCL,
//                   INT=TOUCH_IRQ). Used on: 2432W328C, 2432W328C_2.
//
//   TOUCH_GT911     GT911 capacitive I2C via lewisxhe/SensorLib.
//                   Build flags: GT911_SDA, GT911_SCL, GT911_INT, GT911_RST, GT911_I2C_ADDR.
//                   Used on: 3248S035C.
//
//   TOUCH_SPI_MOSI  XPT2046 bit-bang SPI on dedicated pins.
//                   Build flags: TOUCH_SPI_MOSI, TOUCH_SPI_MISO, TOUCH_SPI_SCK,
//                                CAL_X_MIN, CAL_X_MAX, CAL_Y_MIN, CAL_Y_MAX.
//                   Used on: 2432S028, 2432S028_2USB, 3248S035R.
//
//   (default)       XPT2046 shared HSPI via TFT_eSPI getTouch().
//                   Build flags: CAL_X_MIN, CAL_X_MAX, CAL_Y_MIN, CAL_Y_MAX, CAL_ORIENT.
//                   Used on: 2432W328R, 2432S024R.
//
// Touch zone layout (landscape, any CYD screen size):
//   Left  1/4 (x < SCREEN_W/4)                    BACK
//   Right 3/4 (x >= SCREEN_W/4), top third:        UP
//   Right 3/4 (x >= SCREEN_W/4), middle third:     SELECT
//   Right 3/4 (x >= SCREEN_W/4), bottom third:     DOWN
//

#include "Navigation.h"
#include "core/Device.h"
#include <Arduino.h>

// SCREEN_W/H derive from the portrait-native panel dimensions so the same
// zone math works for both 240×320 (ILI9341) and 320×480 (ST7796) panels.
static constexpr int16_t SCREEN_W = TFT_HEIGHT;  // long side in landscape
static constexpr int16_t SCREEN_H = TFT_WIDTH;   // short side in landscape
static constexpr int16_t BACK_END = SCREEN_W / 4;
static constexpr int16_t ZONE_H   = SCREEN_H / 3;

static constexpr uint8_t NO_TOUCH_THRESHOLD = 3;

// Runtime XY-swap flag — set via setTouchSwapXY() after config is loaded.
// Only has effect in the TOUCH_SPI_MOSI backend; all other backends are no-ops.
static bool _gSwapXY = false;

// Runtime right-hand flag — set via setRightHand() from applyOrientation().
// Flips both axes 180° after touch mapping so zones track the rotated display.
// Does NOT set INavigation::_rightHand, so orientDir() (UP↔DOWN) never fires.
static bool _gRightHand = false;

// ─── Touch backend ────────────────────────────────────────────────────────────

#ifdef TOUCH_CST816S

// 2432W328C: CST816S capacitive I2C touch.
// INT (TOUCH_IRQ) goes LOW when a finger is down; read 6 bytes from reg 0x01.

#include <Wire.h>
static constexpr uint8_t CST816S_ADDR = 0x15;

static bool _readTouch(uint16_t* tx, uint16_t* ty) {
  if (digitalRead(TOUCH_IRQ) != LOW) return false;
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(CST816S_ADDR, (uint8_t)6);
  Wire.read();                     // gesture ID
  uint8_t npts = Wire.read();      // finger count
  uint8_t xH   = Wire.read() & 0x0F;
  uint8_t xL   = Wire.read();
  uint8_t yH   = Wire.read() & 0x0F;
  uint8_t yL   = Wire.read();
  if (!npts) return false;
  *tx = ((uint16_t)xH << 8) | xL;
  *ty = ((uint16_t)yH << 8) | yL;
  return true;
}

void NavigationImpl::begin() {
  Wire.begin(GROVE_SDA, GROVE_SCL);
  pinMode(TOUCH_IRQ, INPUT);
}

#elif defined(TOUCH_GT911)

// 3248S035C: GT911 capacitive I2C touch via lewisxhe/SensorLib.
// Build flags: GT911_SDA, GT911_SCL, GT911_INT, GT911_RST, GT911_I2C_ADDR.

#include <Wire.h>
#include <TouchDrvGT911.hpp>

static TouchDrvGT911 _gt911;
static bool _gt911Ready = false;

static bool _readTouch(uint16_t* tx, uint16_t* ty) {
  if (!_gt911Ready) return false;
  int16_t x, y;
  if (_gt911.getPoint(&x, &y, 1) == 0) return false;
  *tx = (uint16_t)x;
  *ty = (uint16_t)y;
  return true;
}

void NavigationImpl::begin() {
  Wire.begin(GT911_SDA, GT911_SCL);
  _gt911.setPins(GT911_RST, GT911_INT);
  _gt911Ready = _gt911.begin(Wire, GT911_I2C_ADDR, GT911_SDA, GT911_SCL);
}

#elif defined(TOUCH_SPI_MOSI)

// 2432S028 / 3248S035R: XPT2046 on dedicated bit-bang SPI.
// XPT2046 CPOL=0 CPHA=0: data before rising edge, sampled on rising edge.

static uint16_t _xptTransact(uint8_t cmd) {
  uint16_t val = 0;
  for (int8_t i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_SPI_MOSI, (cmd >> i) & 1);
    digitalWrite(TOUCH_SPI_SCK, HIGH);
    digitalWrite(TOUCH_SPI_SCK, LOW);
  }
  for (int8_t i = 15; i >= 0; i--) {
    digitalWrite(TOUCH_SPI_SCK, HIGH);
    if (digitalRead(TOUCH_SPI_MISO)) val |= (1u << i);
    digitalWrite(TOUCH_SPI_SCK, LOW);
  }
  return val >> 3; // 12-bit result (1 null + 12 data + 3 pad bits)
}

static bool _readTouch(uint16_t* tx, uint16_t* ty) {
  if (digitalRead(TOUCH_IRQ) != LOW) return false;

  digitalWrite(TOUCH_CS, LOW);
  uint32_t xr = 0, yr = 0;
  for (uint8_t i = 0; i < 3; i++) {
    xr += _xptTransact(0xD0); // X+ channel, 12-bit differential
    yr += _xptTransact(0x90); // Y+ channel, 12-bit differential
  }
  digitalWrite(TOUCH_CS, HIGH);
  xr /= 3;
  yr /= 3;

  if (xr < CAL_X_MIN || xr > CAL_X_MAX || yr < CAL_Y_MIN || yr > CAL_Y_MAX) return false;

  if (_gSwapXY) {
    *tx = (uint16_t)map((long)yr, CAL_Y_MIN, CAL_Y_MAX, 0, SCREEN_W - 1);
    *ty = (uint16_t)map((long)xr, CAL_X_MIN, CAL_X_MAX, 0, SCREEN_H - 1);
  } else {
    *tx = (uint16_t)map((long)xr, CAL_X_MIN, CAL_X_MAX, 0, SCREEN_W - 1);
    *ty = (uint16_t)map((long)yr, CAL_Y_MIN, CAL_Y_MAX, 0, SCREEN_H - 1);
  }
  return true;
}

void NavigationImpl::begin() {
  pinMode(TOUCH_SPI_SCK,  OUTPUT);
  pinMode(TOUCH_SPI_MOSI, OUTPUT);
  pinMode(TOUCH_SPI_MISO, INPUT);
  pinMode(TOUCH_CS,       OUTPUT);
  pinMode(TOUCH_IRQ,      INPUT);
  digitalWrite(TOUCH_CS,      HIGH);
  digitalWrite(TOUCH_SPI_SCK, LOW);
}

#else

// 2432W328R / 2432S024R: XPT2046 on shared HSPI — use TFT_eSPI built-in touch.
static bool _readTouch(uint16_t* tx, uint16_t* ty) {
  return Uni.Lcd.getTouch(tx, ty);
}

void NavigationImpl::begin() {
  static uint16_t calData[5] = { CAL_X_MIN, CAL_X_MAX, CAL_Y_MIN, CAL_Y_MAX, CAL_ORIENT };
  Uni.Lcd.setTouch(calData);
}

#endif

void NavigationImpl::setRightHand(bool v)   { _gRightHand = v; }
void NavigationImpl::setTouchSwapXY(bool v) { _gSwapXY = v; }

// ─── Navigation update ────────────────────────────────────────────────────────

void NavigationImpl::update() {
  static uint32_t lastPoll = 0;
  uint32_t now = millis();

  if (now - lastPoll < 20) {
    updateState(_curDir);
    return;
  }
  lastPoll = now;

  uint16_t tx, ty;
  bool touched = _readTouch(&tx, &ty);

  if (!touched) {
    if (++_noTouchCnt < NO_TOUCH_THRESHOLD) {
      updateState(_curDir);
      return;
    }
    _curDir = DIR_NONE;
    updateState(DIR_NONE);
    return;
  }

  _noTouchCnt = 0;

  if (tx >= (uint16_t)SCREEN_W) tx = SCREEN_W - 1;
  if (ty >= (uint16_t)SCREEN_H) ty = SCREEN_H - 1;

  if (_gRightHand) {
    tx = (uint16_t)(SCREEN_W - 1) - tx;
    ty = (uint16_t)(SCREEN_H - 1) - ty;
  }

  Direction dir;
  if (tx < (uint16_t)BACK_END) {
    dir = DIR_BACK;
  } else {
    if      (ty < (uint16_t)ZONE_H)       dir = DIR_UP;
    else if (ty < (uint16_t)(ZONE_H * 2)) dir = DIR_PRESS;
    else                                   dir = DIR_DOWN;
  }

  _lastTouchX = (int16_t)tx;
  _lastTouchY = (int16_t)ty;
  _curDir = dir;
  updateState(_curDir);
}

// ─── Overlay rendering ────────────────────────────────────────────────────────

void NavigationImpl::_paintZone(Direction d, bool lit) {
  static constexpr uint16_t LIT_RED   = 0xF800;
  static constexpr uint16_t LIT_GREEN = 0x07E0;
  static constexpr uint16_t LIT_BLUE  = 0x001F;

  auto& lcd = Uni.Lcd;
  Sprite bar(&lcd);

  switch (d) {
    case DIR_BACK:
      bar.createSprite(2, SCREEN_H);
      bar.fillSprite(lit ? LIT_RED : TFT_BLACK);
      bar.pushSprite(0, 0);
      break;
    case DIR_UP:
      bar.createSprite(2, ZONE_H - 1);
      bar.fillSprite(lit ? LIT_GREEN : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, 0);
      break;
    case DIR_PRESS:
      bar.createSprite(2, ZONE_H - 1);
      bar.fillSprite(lit ? LIT_BLUE : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, ZONE_H);
      break;
    case DIR_DOWN:
      bar.createSprite(2, SCREEN_H - ZONE_H * 2);
      bar.fillSprite(lit ? LIT_GREEN : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, ZONE_H * 2);
      break;
    default:
      return;
  }
  bar.deleteSprite();
}

void NavigationImpl::_doDrawOverlay() {
  if (_curDir == _lastDir) return;
  if (_lastDir != DIR_NONE) _paintZone(_lastDir, false);
  if (_curDir  != DIR_NONE) _paintZone(_curDir,  true);
  _lastDir = _curDir;
}
