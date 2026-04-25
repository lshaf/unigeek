//
// Created by L Shaf on 2026-03-25.
//

#pragma once
#include <Arduino.h>
#include <map>
#include "IStorage.h"

// ─── Pin config keys & defaults ──────────────────────────────────────────────
#define PIN_CONFIG_EXT_SDA          "ext_sda"
#define PIN_CONFIG_EXT_SCL          "ext_scl"

#ifdef GROVE_SDA
  #define PIN_CONFIG_EXT_SDA_DEFAULT  String(GROVE_SDA)
#else
  #define PIN_CONFIG_EXT_SDA_DEFAULT  "-1"
#endif

#ifdef GROVE_SCL
  #define PIN_CONFIG_EXT_SCL_DEFAULT  String(GROVE_SCL)
#else
  #define PIN_CONFIG_EXT_SCL_DEFAULT  "-1"
#endif

#ifdef GPS_TX
  #define PIN_CONFIG_GPS_TX_DEFAULT   String(GPS_TX)
#else
  #define PIN_CONFIG_GPS_TX_DEFAULT   "1"
#endif

#ifdef GPS_RX
  #define PIN_CONFIG_GPS_RX_DEFAULT   String(GPS_RX)
#else
  #define PIN_CONFIG_GPS_RX_DEFAULT   "2"
#endif

// ─── IR pin config ──────────────────────────────────────────────────────────
#define PIN_CONFIG_IR_TX            "ir_tx"
#define PIN_CONFIG_IR_RX            "ir_rx"

#if defined(IR_TX)
  #define PIN_CONFIG_IR_TX_DEFAULT    String(IR_TX)
#elif defined(IR_TX_PIN)
  #define PIN_CONFIG_IR_TX_DEFAULT    String(IR_TX_PIN)
#else
  #define PIN_CONFIG_IR_TX_DEFAULT    "-1"
#endif

#if defined(IR_RX_PIN)
  #define PIN_CONFIG_IR_RX_DEFAULT    String(IR_RX_PIN)
#else
  #define PIN_CONFIG_IR_RX_DEFAULT    "-1"
#endif

// ─── CC1101 pin config ──────────────────────────────────────────────────────
#define PIN_CONFIG_CC1101_CS        "cc1101_cs"
#define PIN_CONFIG_CC1101_GDO0      "cc1101_gdo0"

#ifdef CC1101_CS_PIN
  #define PIN_CONFIG_CC1101_CS_DEFAULT   String(CC1101_CS_PIN)
#else
  #define PIN_CONFIG_CC1101_CS_DEFAULT   "-1"
#endif

#ifdef CC1101_GDO0_PIN
  #define PIN_CONFIG_CC1101_GDO0_DEFAULT String(CC1101_GDO0_PIN)
#else
  #define PIN_CONFIG_CC1101_GDO0_DEFAULT "-1"
#endif

// ─── NRF24L01 pin config ────────────────────────────────────────────────────
#define PIN_CONFIG_NRF24_CE         "nrf24_ce"
#define PIN_CONFIG_NRF24_CSN        "nrf24_csn"

#ifdef NRF24_CE_PIN
  #define PIN_CONFIG_NRF24_CE_DEFAULT  String(NRF24_CE_PIN)
#else
  #define PIN_CONFIG_NRF24_CE_DEFAULT  "-1"
#endif

#ifdef NRF24_CSN_PIN
  #define PIN_CONFIG_NRF24_CSN_DEFAULT String(NRF24_CSN_PIN)
#else
  #define PIN_CONFIG_NRF24_CSN_DEFAULT "-1"
#endif

// ─── GPS pin config ─────────────────────────────────────────────────────────
#define PIN_CONFIG_GPS_TX           "gps_tx"
#define PIN_CONFIG_GPS_RX           "gps_rx"
#define PIN_CONFIG_GPS_BAUD         "gps_baud"
#ifdef GPS_BAUD
  #define PIN_CONFIG_GPS_BAUD_DEFAULT String(GPS_BAUD)
#else
  #define PIN_CONFIG_GPS_BAUD_DEFAULT "9600"
#endif

// ─── PN532 (HSU/UART) pin config ────────────────────────────────────────────
#define PIN_CONFIG_PN532_TX         "pn532_tx"
#define PIN_CONFIG_PN532_RX         "pn532_rx"
#define PIN_CONFIG_PN532_BAUD       "pn532_baud"

#ifdef PN532_TX
  #define PIN_CONFIG_PN532_TX_DEFAULT   String(PN532_TX)
#else
  #define PIN_CONFIG_PN532_TX_DEFAULT   "-1"
#endif

#ifdef PN532_RX
  #define PIN_CONFIG_PN532_RX_DEFAULT   String(PN532_RX)
#else
  #define PIN_CONFIG_PN532_RX_DEFAULT   "-1"
#endif

#define PIN_CONFIG_PN532_BAUD_DEFAULT "115200"

// ─── M5 CoreS3 (bare) Grove Port A 5V direction ─────────────────────────────
// Values: "output" (default — AXP2101 → Grove 5V) or "input" (Grove 5V → AXP2101 charge)
#define PIN_CONFIG_CORES3_GROVE_5V         "cores3_grove_5v"
#define PIN_CONFIG_CORES3_GROVE_5V_DEFAULT "output"

class PinConfigManager
{
public:
  static PinConfigManager& getInstance() {
    static PinConfigManager instance;
    return instance;
  }

  void load(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    String content = storage->readFile("/unigeek/pin_config");
    if (content.length() == 0) return;
    _data.clear();
    int start = 0;
    while (start < (int)content.length()) {
      int nl = content.indexOf('\n', start);
      if (nl < 0) nl = content.length();
      String line = content.substring(start, nl);
      line.trim();
      int sep = line.indexOf('=');
      if (sep > 0) _data[line.substring(0, sep)] = line.substring(sep + 1);
      start = nl + 1;
    }
  }

  void save(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    storage->makeDir("/unigeek");
    String content;
    for (auto& kv : _data) content += kv.first + "=" + kv.second + "\n";
    storage->writeFile("/unigeek/pin_config", content.c_str());
  }

  String get(const String& key, const String& def = "") const {
    auto it = _data.find(key);
    return it != _data.end() ? it->second : def;
  }

  int getInt(const String& key, const String& def = "0") const {
    return get(key, def).toInt();
  }

  void set(const String& key, const String& value) {
    _data[key] = value;
  }

  PinConfigManager(const PinConfigManager&)            = delete;
  PinConfigManager& operator=(const PinConfigManager&) = delete;

private:
  PinConfigManager() = default;
  std::map<String, String> _data;
};

#define PinConfig PinConfigManager::getInstance()