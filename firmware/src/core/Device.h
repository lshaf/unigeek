//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include "INavigation.h"
#include "IDisplay.h"
#include "IPower.h"
#include "IKeyboard.h"
#include "IStorage.h"
#include <SPI.h>

#ifndef TFT_DEFAULT_ORIENTATION
#define TFT_DEFAULT_ORIENTATION 1
#endif

class Device
{
public:
  static Device& getInstance() {
    static Device* instance = createInstance();  // ← pointer, no copy/default ctor needed
    return *instance;
  }

  void setupIo();

  void begin()
  {
    setupIo();

    Lcd.begin();
    Lcd.setBrightness(80);
    Lcd.setRotation(TFT_DEFAULT_ORIENTATION);
    Lcd.invertDisplay(true);

    Power.begin();
    Nav->begin();

    if (Keyboard) Keyboard->begin();
  }

  void update()
  {
    Nav->update();
    if (Keyboard) Keyboard->update();
  }

  void switchNavigation(INavigation* newNav)
  {
    Nav = newNav;
    Nav->begin();
  }

  IDisplay& Lcd;
  IPower& Power;
  INavigation* Nav;
  IStorage*   Storage    = nullptr;  // primary — set by board
  IStorage*   StorageSD  = nullptr;  // direct SD access (nullable)
  IStorage*   StorageLFS = nullptr;  // direct LFS access (nullable)
  IKeyboard*  Keyboard   = nullptr;
  SPIClass*   Spi        = nullptr;  // shared SPI bus (nullable, board-specific)

  // Prevent copying
  Device(const Device&)            = delete;
  Device& operator=(const Device&) = delete;
private:
  // Private constructor — takes concrete implementations
  Device(IDisplay& lcd, IPower& power, INavigation* nav,
        IKeyboard* keyboard = nullptr,
        IStorage* storageSD = nullptr,
        IStorage* storageLFS = nullptr,
        SPIClass* spi = nullptr)
     : Lcd(lcd), Power(power), Nav(nav),
       Keyboard(keyboard),
       StorageSD(storageSD), StorageLFS(storageLFS),
       Storage(storageSD && storageSD->isAvailable()
               ? storageSD
               : storageLFS),
       Spi(spi) {}
  // Returns a heap-allocated instance — defined in Device.cpp
  static Device* createInstance();
};


// Global access macro for convenience
#define Uni Device::getInstance()