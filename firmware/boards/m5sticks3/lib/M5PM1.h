//
// Minimal M5PM1 power IC driver for M5StickS3.
// Register map and init sequence derived from official M5PM1 library source.
// I2C address 0x6E on Wire1 (SDA=47, SCL=48).
//
// Key registers (from M5PM1.h):
//   0x00  REG_DEVICE_ID   device identification
//   0x04  REG_PWR_SRC     [2:0]: 0=5VIN(USB), 1=5VINOUT, 2=BAT
//   0x09  REG_I2C_CFG     [4] SPD: 0=100K 1=400K; [3:0] SLP_TO: 0=off
//   0x0C  REG_SYS_CMD     [7:4] must be 0xA (key), [1:0]: 01=shutdown
//   0x22  REG_VBAT_L      battery voltage low byte (mV, 16-bit LE with 0x23)
//

#pragma once
#include <Wire.h>
#include <Arduino.h>

class M5PM1 {
public:
  // Initialise Wire1 and wake the PM1 chip.
  // Sequence mirrors official M5PM1::begin(TwoWire*, addr, sda, scl).
  void begin() {
    // Step 1: Init I2C at 100 kHz
    Wire1.end();
    delay(50);
    Wire1.begin(47, 48, 100000UL);
    delay(100);

    // Step 2: Wake PM1 — SDA falling edge triggers EXTI4 wakeup
    Wire1.beginTransmission(0x6E);
    Wire1.endTransmission();
    delay(10);

    // Step 3: Verify device is alive (read DEVICE_ID)
    if (_read(0x00) == 0) {
      // Retry after 800 ms (official library retry pattern)
      delay(800);
      Wire1.beginTransmission(0x6E);
      Wire1.endTransmission();
      delay(10);
    }

    // Step 4: Disable I2C sleep + set 400 kHz on PM1 side
    // Reg 0x09: [4]=SPD 1=400K, [3:0]=SLP_TO 0=disabled → 0x10
    _write(0x09, 0x10);

    // Step 5: Switch host I2C to 400 kHz (end+begin like official lib)
    Wire1.end();
    delay(10);
    Wire1.begin(47, 48, 400000UL);
    delay(50);
  }

  // Battery level 0–100 derived from cell voltage.
  // Level formula from Power_Class.cpp: (mv - 3300) * 100 / (4150 - 3350)
  uint8_t getBatteryLevel() {
    uint16_t mv = _read16(0x22);
    if (mv == 0) return 0;
    int level = (int)(mv - 3300) * 100 / (4150 - 3350);
    if (level < 0)   return 0;
    if (level > 100) return 100;
    return (uint8_t)level;
  }

  // PWR_SRC [2:0]: 0=5VIN, 1=5VINOUT → external power (charging); 2=BAT → not charging
  bool isCharging() {
    uint8_t src = _read(0x04) & 0x07;
    return src == 0 || src == 1;
  }

  // Shutdown: 120 ms settling, then write key|cmd to REG_SYS_CMD
  // Key = 0xA0, CMD_OFF = 0x01  → 0xA1
  void powerOff() {
    delay(120);
    _write(0x0C, 0xA1);
  }

private:
  uint8_t _read(uint8_t reg) {
    Wire1.beginTransmission(0x6E);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom((uint8_t)0x6E, (uint8_t)1);
    return Wire1.available() ? Wire1.read() : 0;
  }

  uint16_t _read16(uint8_t reg) {
    Wire1.beginTransmission(0x6E);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom((uint8_t)0x6E, (uint8_t)2);
    uint8_t lo = Wire1.available() ? Wire1.read() : 0;
    uint8_t hi = Wire1.available() ? Wire1.read() : 0;
    return (uint16_t)((hi << 8) | lo);
  }

  void _write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(0x6E);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
  }
};
