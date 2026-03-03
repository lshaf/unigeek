//
// Created by L Shaf on 2026-02-16.
//

#include "core/Device.h"
#include "core/StorageLFS.h"
#include "core/ConfigManager.h"
#include "Navigation.h"
#include "EncoderNavigation.h"
#include "Display.h"
#include "Power.h"
#include "Speaker.h"
#include <AXP192.h>

AXP192 axp;

static DisplayImpl      display(&axp);
static NavigationImpl   navigation(&axp);
static EncoderNavigation encoderNavigation;
static PowerImpl        power(&axp);
static StorageLFS       storageLFS;
static SpeakerBuzzer    speaker;

void Device::setupIo()
{
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_A, INPUT_PULLUP);
}

Device* Device::createInstance() {
  storageLFS.begin();

  return new Device(display, power, &navigation, nullptr,
                    nullptr, &storageLFS, nullptr, &speaker);
}

void Device::applyNavMode() {
  String mode = Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT);
  if (mode == "encoder") {
    switchNavigation(&encoderNavigation);
  } else {
    switchNavigation(&navigation);
  }
}

void Device::boardHook() {
  static unsigned long _btnAHeld = 0;
  if (digitalRead(BTN_A) == LOW) {
    if (_btnAHeld == 0) _btnAHeld = millis();
    else if (millis() - _btnAHeld >= 3000) {
      Config.set(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT);
      Config.save(Storage);
      applyNavMode();
      _btnAHeld = 0;
    }
  } else {
    _btnAHeld = 0;
  }
}