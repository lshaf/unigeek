#include "ModuleMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/module/MFRC522Screen.h"
#include "screens/module/PN532UartScreen.h"
#include "screens/module/PN532I2cScreen.h"
#include "screens/module/GPSScreen.h"
#include "screens/module/IRScreen.h"
#include "screens/module/SubGHzScreen.h"
#include "screens/module/M5RF433Screen.h"
#include "screens/module/NRF24Screen.h"
#include "screens/setting/PinSettingScreen.h"

void ModuleMenuScreen::onInit() {
  _visibleCount = 0;
  for (uint8_t id = 0; id < ModuleRegistry::MOD_COUNT; id++) {
    if (ModuleRegistry::isHidden(id)) continue;
    _items[_visibleCount].label    = ModuleRegistry::LABELS[id];
    _items[_visibleCount].sublabel = nullptr;
    _ids[_visibleCount]            = id;
    _visibleCount++;
  }
  setItems(_items, _visibleCount);
}

void ModuleMenuScreen::onBack() {
  Screen.goBack();
}

void ModuleMenuScreen::onItemSelected(uint8_t index) {
  if (index >= _visibleCount) return;
  switch (_ids[index]) {
    case ModuleRegistry::MOD_MFRC522_I2C: Screen.push(new MFRC522Screen());    break;
    case ModuleRegistry::MOD_PN532_UART:  Screen.push(new PN532UartScreen());  break;
    case ModuleRegistry::MOD_PN532_I2C:   Screen.push(new PN532I2cScreen());   break;
    case ModuleRegistry::MOD_GPS:         Screen.push(new GPSScreen());        break;
    case ModuleRegistry::MOD_IR:          Screen.push(new IRScreen());         break;
    case ModuleRegistry::MOD_SUBGHZ:      Screen.push(new SubGHzScreen());     break;
    case ModuleRegistry::MOD_M5_RF433:    Screen.push(new M5RF433Screen());    break;
    case ModuleRegistry::MOD_NRF24:       Screen.push(new NRF24Screen());      break;
    case ModuleRegistry::MOD_PIN_SETTING: Screen.push(new PinSettingScreen()); break;
  }
}