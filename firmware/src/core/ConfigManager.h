//
// Created by L Shaf on 2026-02-22.
//

#pragma once
#include <TFT_eSPI.h>

class ConfigManager
{
public:
  static ConfigManager& getInstance() {
    static ConfigManager instance;
    return instance;
  }

  int getThemeColor()
  {
    return TFT_NAVY;
  }

  ConfigManager(const ConfigManager&)            = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;
private:
  ConfigManager() = default;
};

#define Config ConfigManager::getInstance()