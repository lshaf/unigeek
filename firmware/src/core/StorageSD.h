//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IStorage.h"
#include <SD.h>
#include <SPI.h>
#include <Arduino.h>
#include <esp32-hal-spi.h>  // spiAttachMISO()

class StorageSD : public IStorage
{
public:
  // dcPin: shared DC/MISO pin (e.g. CoreS3 GPIO35). Pass -1 if not shared.
  // When set, every SD operation:
  //   _miso() — re-routes pin to SPI MISO in GPIO matrix + enables input buffer
  //   _dc()   — switches pin back to GPIO output (HIGH = DC data signal)
  // The re-route step is needed because M5GFX / LovyanGFX calls
  // gpio_pad_select_gpio() on the DC pin during display init, which clears
  // the GPIO matrix SPI routing. spiAttachMISO() restores it each call.
  //
  // Background FatFS status checks (ff_sd_status → sdSelectCard) can run while
  // GPIO35 is in DC-output mode (input buffer disabled), setting the card to
  // STA_NOINIT. _recover() re-initialises the SD bus to clear that state.
  bool begin(uint8_t csPin, SPIClass& spi, uint32_t freq = 4000000, int8_t dcPin = -1) {
    _csPin = csPin;
    _freq  = freq;
    _dcPin = dcPin;
    _spi   = (dcPin >= 0) ? &spi : nullptr;
    _miso();
    _available = SD.begin(csPin, spi, freq);
    _dc();
    return _available;
  }

  bool     isAvailable() override { return _available; }

  uint64_t totalBytes() override {
    if (!_available) return 0;
    _miso(); auto r = SD.totalBytes(); _dc(); return r;
  }
  uint64_t usedBytes() override {
    if (!_available) return 0;
    _miso(); auto r = SD.usedBytes(); _dc(); return r;
  }
  uint64_t freeBytes() override {
    uint64_t t = totalBytes(), u = usedBytes();
    return (u < t) ? (t - u) : 0;
  }

  fs::File open(const char* path, const char* mode) override {
    if (!_available) return fs::File();
    _miso(); auto r = SD.open(path, mode); _dc(); return r;
  }

  bool exists(const char* path) override {
    if (!_available) return false;
    _miso(); auto r = SD.exists(path); _dc(); return r;
  }

  String readFile(const char* path) override {
    if (!_available) return "";
    _miso();
    File f = SD.open(path, FILE_READ);
    if (!f) { _dc(); return ""; }
    String content = f.readString();
    f.close();
    _dc();
    return content;
  }

  bool writeFile(const char* path, const char* content) override {
    if (!_available) return false;
    _miso();
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
      // FatFS background status check may have set card to STA_NOINIT while
      // GPIO35 was in DC-output mode. Recover and retry once before giving up.
      _recover();
      f = SD.open(path, FILE_WRITE);
      if (!f) { _available = false; _dc(); return false; }
    }
    f.print(content);
    f.close();
    _dc();
    return true;
  }

  bool deleteFile(const char* path) override {
    if (!_available) return false;
    _miso(); auto r = SD.remove(path); _dc(); return r;
  }

  bool makeDir(const char* path) override {
    if (!_available) return false;
    _miso();
    String p = path;
    for (int i = 1; i < (int)p.length(); i++) {
      if (p[i] == '/') {
        String sub = p.substring(0, i);
        if (!SD.exists(sub.c_str())) SD.mkdir(sub.c_str());
      }
    }
    bool r = SD.exists(path) ? true : SD.mkdir(path);
    _dc();
    return r;
  }

  bool renameFile(const char* from, const char* to) override {
    if (!_available) return false;
    _miso(); auto r = SD.rename(from, to); _dc(); return r;
  }

  bool removeDir(const char* path) override {
    if (!_available) return false;
    _miso(); auto r = SD.rmdir(path); _dc(); return r;
  }

  uint8_t listDir(const char* path, DirEntry* out, uint8_t max) override {
    if (!_available) return 0;
    _miso();
    File dir = SD.open(path);
    if (!dir) { _dc(); return 0; }
    uint8_t count = 0;
    while (count < max) {
      File f = dir.openNextFile();
      if (!f) break;
      out[count].name  = f.name();
      out[count].isDir = f.isDirectory();
      f.close();
      count++;
    }
    dir.close();
    _dc();
    return count;
  }

  fs::FS& getFS() override { return SD; }

private:
  bool       _available = false;
  uint8_t    _csPin     = 0;
  uint32_t   _freq      = 4000000;
  int8_t     _dcPin     = -1;
  SPIClass*  _spi       = nullptr;

  // Re-initialise the SD bus after a FatFS background status check set the
  // card to STA_NOINIT (happens when GPIO35 DC-output mode disabled the MISO
  // input buffer during an internal sdSelectCard() poll).
  void _recover() {
    SD.end();
    delayMicroseconds(500);
    _miso();
    if (_spi) _available = SD.begin(_csPin, *_spi, _freq);
    else      _available = SD.begin(_csPin);
  }

  // Re-route _dcPin to SPI MISO in the GPIO matrix (cleared by M5GFX/LGFX's
  // gpio_pad_select_gpio call during DC pin setup), then enable input buffer.
  void _miso() {
    if (_dcPin < 0) return;
    if (_spi && _spi->bus()) spiAttachMISO(_spi->bus(), _dcPin);
    gpio_set_direction((gpio_num_t)_dcPin, GPIO_MODE_INPUT);
  }

  // Switch _dcPin back to GPIO output for TFT DC signal.
  // Does NOT call gpio_pad_select_gpio — leaves GPIO matrix MISO routing intact.
  void _dc() {
    if (_dcPin < 0) return;
    gpio_set_direction((gpio_num_t)_dcPin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)_dcPin, 1);
  }
};