//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include "INavigation.h"
#include "IDisplay.h"
#include "IPower.h"
#include "IKeyboard.h"
#include "IStorage.h"
#include "ISpeaker.h"
#include "ExtSpiClass.h"
#include <Wire.h>

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

  void begin()
  {
    Lcd.begin();
    Lcd.setRotation(TFT_DEFAULT_ORIENTATION);

    Power.begin();
    Nav->begin();

    if (Keyboard) Keyboard->begin();
    if (Speaker)  Speaker->begin();
  }

  // Init LFS + SD (if SD_CS defined) and update Storage pointer.
  // Shared implementation in src/core/Device.cpp — call once from setup() after begin().
  void initStorage();

  void update()
  {
    boardHook();
    if (Keyboard) Keyboard->update();
    Nav->update();

    // Track activity for power saving — works inside blocking actions too
    bool active = Nav->isPressed();
#ifdef DEVICE_HAS_KEYBOARD
    if (Keyboard && Keyboard->available()) active = true;
#endif
    if (active) lastActiveMs = millis();
  }

  void boardHook();   // board-specific per-frame hook, defined in each Device.cpp

  void switchNavigation(INavigation* newNav)
  {
    Nav = newNav;
    Nav->begin();
  }

  virtual void applyNavMode();        // board override: switch nav based on APP_CONFIG_NAV_MODE
  virtual void onPinConfigApply() {}  // board override: react to PinConfig changes at runtime
  void applyOrientation();            // apply hand orientation: rotate screen + flip UP/DOWN

  IDisplay& Lcd;
  IPower& Power;
  INavigation* Nav;
  IStorage*   Storage    = nullptr;  // primary — set by initStorage()
  IStorage*   StorageSD  = nullptr;  // direct SD access — set by initStorage()
  IStorage*   StorageLFS = nullptr;  // direct LFS access — set by initStorage()
  IKeyboard*  Keyboard   = nullptr;
  ISpeaker*   Speaker    = nullptr;
  ExtSpiClass* Spi        = nullptr;  // shared SPI bus (nullable, board-specific)
  int8_t      StorageDcPin = -1;     // shared DC/MISO GPIO — set by board before initStorage() if needed
  TwoWire*    ExI2C      = nullptr;  // external I2C — free state, caller must begin(sda,scl)/end()
  TwoWire*    InI2C      = nullptr;  // internal I2C — board-initialized, do not end()
  unsigned long lastActiveMs = 0;    // last user input timestamp — updated by update()
  bool lcdOff = false;               // true while display is off — screens should skip rendering

  // Prevent copying
  Device(const Device&)            = delete;
  Device& operator=(const Device&) = delete;
private:
  // Private constructor — takes concrete implementations.
  // Storage objects are managed by initStorage() in src/core/Device.cpp.
  Device(IDisplay& lcd, IPower& power, INavigation* nav,
        IKeyboard* keyboard = nullptr,
        ExtSpiClass* spi = nullptr,
        ISpeaker* sound = nullptr)
     : Lcd(lcd), Power(power), Nav(nav),
       Keyboard(keyboard),
       Spi(spi),
       Speaker(sound) {}
  // Returns a heap-allocated instance — defined in Device.cpp
  static Device* createInstance();
};


// Global access macro for convenience
#define Uni Device::getInstance()