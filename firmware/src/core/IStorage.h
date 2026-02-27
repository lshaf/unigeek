//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include <Arduino.h>

class IStorage
{
public:
  virtual bool   isAvailable()                                    = 0;
  virtual bool   exists(const char* path)                         = 0;
  virtual String readFile(const char* path)                       = 0;
  virtual bool   writeFile(const char* path, const char* content) = 0;
  virtual bool   deleteFile(const char* path)                     = 0;
  virtual bool   makeDir(const char* path)                        = 0;
  virtual ~IStorage() {}
};