//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IStorage.h"
#include <FS.h>
#include <LittleFS.h>

class StorageLFS : public IStorage
{
public:
  bool begin() {
    _available = LittleFS.begin(true);  // true = format on fail
    return _available;
  }

  bool     isAvailable() override { return _available; }
  uint64_t freeBytes()   override {
    if (!_available) return 0;
    uint64_t total = LittleFS.totalBytes();
    uint64_t used  = LittleFS.usedBytes();
    return (used < total) ? (total - used) : 0;
  }
  fs::File open(const char* path, const char* mode) override {
    if (!_available) return fs::File();
    return LittleFS.open(path, mode);
  }

  bool exists(const char* path) override {
    if (!_available) return false;
    return LittleFS.exists(path);
  }

  String readFile(const char* path) override {
    if (!_available) return "";
    fs::File f = LittleFS.open(path, FILE_READ);
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
  }

  bool writeFile(const char* path, const char* content) override {
    if (!_available) return false;
    _makeDir(path);
    fs::File f = LittleFS.open(path, FILE_WRITE);
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
    String p = path;
    for (int i = 1; i < (int)p.length(); i++) {
      if (p[i] == '/') {
        String sub = p.substring(0, i);
        if (!LittleFS.exists(sub.c_str())) LittleFS.mkdir(sub.c_str());
      }
    }
    if (!LittleFS.exists(path)) return LittleFS.mkdir(path);
    return true;
  }

  bool renameFile(const char* from, const char* to) override {
    if (!_available) return false;
    return LittleFS.rename(from, to);
  }

  bool removeDir(const char* path) override {
    if (!_available) return false;
    return LittleFS.rmdir(path);
  }

  uint8_t listDir(const char* path, DirEntry* out, uint8_t max) override {
    if (!_available) return 0;
    fs::File dir = LittleFS.open(path);
    if (!dir) return 0;
    uint8_t count = 0;
    while (count < max) {
      fs::File f = dir.openNextFile();
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

  // create parent dirs before writing
  void _makeDir(const char* filePath) {
    String p = filePath;
    int last = p.lastIndexOf('/');
    if (last <= 0) return;
    makeDir(p.substring(0, last).c_str());
  }
};