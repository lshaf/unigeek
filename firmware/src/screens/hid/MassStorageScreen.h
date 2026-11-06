#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_USB_HID is defined

#ifdef DEVICE_HAS_USB_HID

#include "ui/templates/BaseScreen.h"

// MassStorageScreen — exposes the device's storage to a USB host as a removable
// drive. Standalone: while mounted the host owns the volume, so the firmware
// does not touch the filesystem. BACK ejects and returns to the menu.
//
// The exposed volume is whatever Storage the board loaded at boot — the SD card
// on boards that have one. On boards running from internal LittleFS there is no
// FAT block device to expose, so the screen shows an "unsupported" message.
class MassStorageScreen : public BaseScreen {
public:
  const char* title()    override { return "Mass Storage"; }
  bool inhibitPowerOff() override { return true; }  // don't auto power-off while a drive is mounted

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State : uint8_t {
    ST_MOUNTED,       // exposed as a USB drive
    ST_UNSUPPORTED,   // active storage isn't a FAT block device (e.g. LittleFS / no SD)
    ST_USB_BUSY,      // another USB profile claimed USB this boot
  };

  State    _state        = ST_MOUNTED;
  bool     _chromeDrawn  = false;
  uint32_t _lastReads    = (uint32_t)-1;
  uint32_t _lastWrites   = (uint32_t)-1;
  bool     _lastEjected  = false;

  void _exit();  // eject + goBack
};

#endif  // DEVICE_HAS_USB_HID
