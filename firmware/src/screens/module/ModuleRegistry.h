#pragma once
//
// Single source of truth for the entries shown in the Modules menu.
//
// Module ids are positional and persisted as a bitmask in the
// APP_CONFIG_HIDDEN_MODULES config key, so entries must only ever be appended
// (never reordered) or existing hide-flags will shift onto the wrong module.
//
#include <Arduino.h>
#include "core/ConfigManager.h"

namespace ModuleRegistry
{
  enum : uint8_t {
    MOD_MFRC522_I2C = 0,
    MOD_PN532_UART,
    MOD_PN532_I2C,
    MOD_GPS,
    MOD_IR,
    MOD_SUBGHZ,
    MOD_M5_RF433,
    MOD_NRF24,
    MOD_PIN_SETTING,
    MOD_COUNT
  };

  static const char* const LABELS[MOD_COUNT] = {
    "MFRC522 I2C",
    "PN532 UART",
    "PN532 I2C",
    "GPS",
    "IR Remote",
    "Sub-GHz",
    "M5 RF433",
    "NRF24L01",
    "Pin Setting",
  };

  inline uint32_t hiddenMask()
  {
    return (uint32_t)Config.get(APP_CONFIG_HIDDEN_MODULES,
                                APP_CONFIG_HIDDEN_MODULES_DEFAULT).toInt();
  }

  inline bool isHidden(uint8_t id)
  {
    return id < MOD_COUNT && (hiddenMask() & (1UL << id));
  }

  inline void setHidden(uint8_t id, bool hidden)
  {
    if (id >= MOD_COUNT) return;
    uint32_t mask = hiddenMask();
    if (hidden) mask |= (1UL << id);
    else        mask &= ~(1UL << id);
    Config.set(APP_CONFIG_HIDDEN_MODULES, String((unsigned long)mask));
  }
}
