//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IStorage.h"
#include <SD.h>
#include <SPI.h>

class StorageSD : public IStorage
{
public:
  bool begin(uint8_t csPin, SPIClass& spi, uint32_t freq = 4000000) {
    _available = SD.begin(csPin, spi, freq);
    return _available;
  }

  bool     isAvailable() override { return _available; }
  uint64_t freeBytes()   override {
    if (!_available) return 0;
    uint64_t total = SD.totalBytes();
    uint64_t used  = SD.usedBytes();
    return (used < total) ? (total - used) : 0;
  }
  fs::File open(const char* path, const char* mode) override {
    if (!_available) return fs::File();
    return SD.open(path, mode);
  }
  bool exists(const char* path) override {
    if (!_available) return false;
    return SD.exists(path);
  }

  String readFile(const char* path) override {
    if (!_available) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";  // file not found is not an SD failure
    String content = f.readString();
    f.close();
    return content;
  }

  bool writeFile(const char* path, const char* content) override {
    if (!_available) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) { _available = false; return false; }
    f.print(content);
    f.close();
    return true;
  }

  bool deleteFile(const char* path) override {
    if (!_available) return false;
    return SD.remove(path);
  }

  bool makeDir(const char* path) override {
    if (!_available) return false;
    // create recursively
    String p = path;
    for (int i = 1; i < (int)p.length(); i++) {
      if (p[i] == '/') {
        String sub = p.substring(0, i);
        if (!SD.exists(sub.c_str())) SD.mkdir(sub.c_str());
      }
    }
    if (!SD.exists(path)) return SD.mkdir(path);
    return true;
  }

  bool renameFile(const char* from, const char* to) override {
    if (!_available) return false;
    return SD.rename(from, to);
  }

  bool removeDir(const char* path) override {
    if (!_available) return false;
    return SD.rmdir(path);
  }

  uint8_t listDir(const char* path, DirEntry* out, uint8_t max) override {
    if (!_available) return 0;
    File dir = SD.open(path);
    if (!dir) return 0;
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
    return count;
  }

private:
  bool _available = false;
};