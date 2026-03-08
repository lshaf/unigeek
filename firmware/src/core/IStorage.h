//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include <Arduino.h>
#include <FS.h>

class IStorage
{
public:
  struct DirEntry {
    String name;
    bool   isDir;
  };

  virtual bool     isAvailable()                                          = 0;
  virtual uint64_t freeBytes()                                            = 0;
  virtual fs::File open(const char* path, const char* mode)               = 0;
  virtual bool    exists(const char* path)                               = 0;
  virtual String  readFile(const char* path)                             = 0;
  virtual bool    writeFile(const char* path, const char* content)       = 0;
  virtual bool    deleteFile(const char* path)                           = 0;
  virtual bool    makeDir(const char* path)                              = 0;
  virtual uint8_t listDir(const char* path, DirEntry* out, uint8_t max)  = 0;
  virtual bool    renameFile(const char* from, const char* to)           = 0;
  virtual bool    removeDir(const char* path)                            = 0;
  virtual ~IStorage() {}
};