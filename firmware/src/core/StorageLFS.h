//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IStorage.h"
#include <LittleFS.h>

class StorageLFS : public IStorage
{
public:
  bool begin() {
    _available = LittleFS.begin(true);  // true = format on fail
    return _available;
  }

  bool isAvailable() override { return _available; }

  bool exists(const char* path) override {
    if (!_available) return false;
    return LittleFS.exists(path);
  }

  String readFile(const char* path) override {
    if (!_available) return "";
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
  }

  bool writeFile(const char* path, const char* content) override {
    if (!_available) return false;
    _makeDir(path);
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
  }

  bool deleteFile(const char* path) override {
    if (!_available) return false;
    return LittleFS.remove(path);
  }

  bool makeDir(const char* path) override {
    if (!_available) return false;
    // LittleFS creates dirs implicitly but mkdir still works
    return LittleFS.mkdir(path);
  }

private:
  bool _available = false;

  // create parent dirs before writing
  void _makeDir(const char* filePath) {
    String p = filePath;
    int last = p.lastIndexOf('/');
    if (last <= 0) return;
    String dir = p.substring(0, last);
    if (!LittleFS.exists(dir.c_str())) {
      LittleFS.mkdir(dir.c_str());
    }
  }
};