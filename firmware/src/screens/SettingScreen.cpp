//
// Created by L Shaf on 2026-03-02.
//

#include "SettingScreen.h"
#include "screens/MainMenuScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"

void SettingScreen::onInit() {
  setItems(_items);
  _refresh();
}

void SettingScreen::_refresh() {
  _nameSub     = Config.get(APP_CONFIG_DEVICE_NAME,          APP_CONFIG_DEVICE_NAME_DEFAULT);
  _powerSavSub = Config.get(APP_CONFIG_ENABLE_POWER_SAVING,  APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT).toInt() ? "On" : "Off";
  _dispOffSub  = Config.get(APP_CONFIG_INTERVAL_DISPLAY_OFF, APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT) + "s";
  _powerOffSub = Config.get(APP_CONFIG_INTERVAL_POWER_OFF,   APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT)   + "s";
  _brightSub   = Config.get(APP_CONFIG_BRIGHTNESS,           APP_CONFIG_BRIGHTNESS_DEFAULT)            + "%";
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
  _volSub      = Config.get(APP_CONFIG_VOLUME,               APP_CONFIG_VOLUME_DEFAULT)                + "%";
#endif
#ifdef DEVICE_HAS_SOUND
  _navSndSub   = Config.get(APP_CONFIG_NAV_SOUND,            APP_CONFIG_NAV_SOUND_DEFAULT).toInt() ? "On" : "Off";
#endif
  _colorSub    = Config.get(APP_CONFIG_PRIMARY_COLOR,        APP_CONFIG_PRIMARY_COLOR_DEFAULT);

  _items[SETT_NAME].sublabel      = _nameSub.c_str();
  _items[SETT_POWER_SAV].sublabel = _powerSavSub.c_str();
  _items[SETT_DISP_OFF].sublabel  = _dispOffSub.c_str();
  _items[SETT_POWER_OFF].sublabel = _powerOffSub.c_str();
  _items[SETT_BRIGHTNESS].sublabel = _brightSub.c_str();
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
  _items[SETT_VOLUME].sublabel    = _volSub.c_str();
#endif
#ifdef DEVICE_HAS_SOUND
  _items[SETT_NAV_SOUND].sublabel = _navSndSub.c_str();
#endif
  _items[SETT_COLOR].sublabel     = _colorSub.c_str();

  render();
}

void SettingScreen::onItemSelected(uint8_t index) {
  switch (index) {

    case SETT_NAME: {
      String cur    = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
      String result = InputTextAction::popup("Device Name", cur.c_str());
      if (result.length() > 0 && result.length() <= 15) {
        Config.set(APP_CONFIG_DEVICE_NAME, result);
        Config.save(Uni.Storage);
      } else if (result.length() > 15) {
        ShowStatusAction::show("Name must be 1-15 characters.", 1500);
      }
      _refresh();
      break;
    }

    case SETT_POWER_SAV: {
      bool cur = Config.get(APP_CONFIG_ENABLE_POWER_SAVING, APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT).toInt();
      Config.set(APP_CONFIG_ENABLE_POWER_SAVING, cur ? "0" : "1");
      Config.save(Uni.Storage);
      _refresh();
      break;
    }

    case SETT_DISP_OFF: {
      int cur    = Config.get(APP_CONFIG_INTERVAL_DISPLAY_OFF, APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT).toInt();
      int result = InputNumberAction::popup("Display Off (secs)", 5, 3600, cur);
      if (result != 0) {
        Config.set(APP_CONFIG_INTERVAL_DISPLAY_OFF, String(result));
        Config.save(Uni.Storage);
      }
      _refresh();
      break;
    }

    case SETT_POWER_OFF: {
      int cur    = Config.get(APP_CONFIG_INTERVAL_POWER_OFF, APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT).toInt();
      int result = InputNumberAction::popup("Power Off (secs)", 5, 3600, cur);
      if (result != 0) {
        Config.set(APP_CONFIG_INTERVAL_POWER_OFF, String(result));
        Config.save(Uni.Storage);
      }
      _refresh();
      break;
    }

    case SETT_BRIGHTNESS: {
      int cur    = Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt();
      int result = InputNumberAction::popup("Brightness %", 5, 100, cur);
      if (result != 0) {
        Config.set(APP_CONFIG_BRIGHTNESS, String(result));
        Config.save(Uni.Storage);
        Uni.Lcd.setBrightness((uint8_t)result);
      }
      _refresh();
      break;
    }

#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
    case SETT_VOLUME: {
      int cur    = Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt();
      int result = InputNumberAction::popup("Volume %", 0, 100, cur);
      if (result != 0 || cur != 0) {
        Config.set(APP_CONFIG_VOLUME, String(result));
        Config.save(Uni.Storage);
        if (Uni.Speaker) Uni.Speaker->setVolume((uint8_t)result);
      }
      _refresh();
      break;
    }
#endif

#ifdef DEVICE_HAS_SOUND
    case SETT_NAV_SOUND: {
      bool cur = Config.get(APP_CONFIG_NAV_SOUND, APP_CONFIG_NAV_SOUND_DEFAULT).toInt();
      Config.set(APP_CONFIG_NAV_SOUND, cur ? "0" : "1");
      Config.save(Uni.Storage);
      _refresh();
      break;
    }
#endif

    case SETT_COLOR: {
      static constexpr InputSelectAction::Option opts[] = {
        {"Blue",   "Blue"},
        {"Red",    "Red"},
        {"Green",  "Green"},
        {"Cyan",   "Cyan"},
        {"Purple", "Purple"},
        {"Brown",  "Brown"},
        {"Orange", "Orange"},
        {"Violet", "Violet"},
      };
      String      cur    = Config.get(APP_CONFIG_PRIMARY_COLOR, APP_CONFIG_PRIMARY_COLOR_DEFAULT);
      const char* result = InputSelectAction::popup("Primary Color", opts, 8, cur.c_str());
      if (result != nullptr) {
        Config.set(APP_CONFIG_PRIMARY_COLOR, result);
        Config.save(Uni.Storage);
      }
      _refresh();
      break;
    }

    case SETT_ABOUT: {
      ShowStatusAction::show("UniGeek Firmware\nPlatformIO + TFT_eSPI\nCreated by L Shaf");
      _refresh();
      break;
    }
  }
}

void SettingScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}