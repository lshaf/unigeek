//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IStorage.h"
#include <SD.h>

class StorageSD : public IStorage
{
public:
  bool begin(uint8_t csPin) {
    _available = SD.begin(csPin);
    return _available;
  }

  bool isAvailable() override { return _available; }

  bool exists(const char* path) override {
    if (!_available) return false;
    return SD.exists(path);
  }

  String readFile(const char* path) override {
    if (!_available) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
  }

  bool writeFile(const char* path, const char* content) override {
    if (!_available) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
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

private:
  bool _available = false;
};