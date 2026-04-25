//
// Created by L Shaf on 2026-03-02.
//

#include "screens/setting/SettingScreen.h"
#include "screens/MainMenuScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/setting/PinSettingScreen.h"
#include "screens/setting/DeviceStatusScreen.h"
#include "screens/setting/AboutScreen.h"
#ifdef DEVICE_HAS_TOUCH_NAV
#include "screens/setting/TouchGuideScreen.h"
#endif
#ifdef DEVICE_CYD
#include "screens/setting/CYDTouchCalScreen.h"
#endif
#ifdef DEVICE_HAS_SOUND
#include "screens/setting/SpeakerTestScreen.h"
#endif

void SettingScreen::onInit() {
  setItems(_items);
  _refresh();
}

void SettingScreen::_refresh() {
  _nameSub       = Config.get(APP_CONFIG_DEVICE_NAME,          APP_CONFIG_DEVICE_NAME_DEFAULT);
  _dispOffEnSub  = Config.get(APP_CONFIG_ENABLE_POWER_SAVING,  APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT).toInt() ? "On" : "Off";
  _dispOffSub    = Config.get(APP_CONFIG_INTERVAL_DISPLAY_OFF, APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT) + "s";
#ifdef APP_MENU_POWER_OFF
  _powerOffEnSub = Config.get(APP_CONFIG_ENABLE_POWER_OFF,     APP_CONFIG_ENABLE_POWER_OFF_DEFAULT).toInt() ? "On" : "Off";
  _powerOffSub   = Config.get(APP_CONFIG_INTERVAL_POWER_OFF,   APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT)   + "s";
#endif
  _brightSub   = Config.get(APP_CONFIG_BRIGHTNESS,           APP_CONFIG_BRIGHTNESS_DEFAULT)            + "%";
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
  _volSub      = Config.get(APP_CONFIG_VOLUME,               APP_CONFIG_VOLUME_DEFAULT)                + "%";
#endif
#ifdef DEVICE_HAS_SOUND
  _navSndSub   = Config.get(APP_CONFIG_NAV_SOUND,            APP_CONFIG_NAV_SOUND_DEFAULT).toInt() ? "On" : "Off";
#endif
  _colorSub    = Config.get(APP_CONFIG_PRIMARY_COLOR,        APP_CONFIG_PRIMARY_COLOR_DEFAULT);
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
  _navModeSub  = Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT) == "encoder" ? "Encoder" : "Default";
#endif
#ifdef DEVICE_HAS_SCREEN_ORIENT
  _screenOrientSub = Config.get(APP_CONFIG_SCREEN_ORIENT, APP_CONFIG_SCREEN_ORIENT_DEFAULT) == "flipped" ? "Flipped" : "Normal";
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
  _overlaySub  = Config.get(APP_CONFIG_SHOW_OVERLAY, APP_CONFIG_SHOW_OVERLAY_DEFAULT).toInt() ? "Show" : "Hide";
#endif
  _webPwdSub   = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);

  _items[SETT_NAME].sublabel         = _nameSub.c_str();
  _items[SETT_DISP_OFF_EN].sublabel  = _dispOffEnSub.c_str();
  _items[SETT_DISP_OFF].sublabel     = _dispOffSub.c_str();
#ifdef APP_MENU_POWER_OFF
  _items[SETT_POWER_OFF_EN].sublabel = _powerOffEnSub.c_str();
  _items[SETT_POWER_OFF].sublabel    = _powerOffSub.c_str();
#endif
  _items[SETT_BRIGHTNESS].sublabel = _brightSub.c_str();
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
  _items[SETT_VOLUME].sublabel    = _volSub.c_str();
#endif
#ifdef DEVICE_HAS_SOUND
  _items[SETT_NAV_SOUND].sublabel = _navSndSub.c_str();
#endif
  _items[SETT_COLOR].sublabel        = _colorSub.c_str();
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
  _items[SETT_NAV_MODE].sublabel     = _navModeSub.c_str();
#endif
#ifdef DEVICE_HAS_SCREEN_ORIENT
  _items[SETT_SCREEN_ORIENT].sublabel  = _screenOrientSub.c_str();
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
  _items[SETT_OVERLAY].sublabel      = _overlaySub.c_str();
#endif
  _items[SETT_WEB_PASSWORD].sublabel = _webPwdSub.c_str();

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
        int n = Achievement.inc("settings_name_changed");
        if (n == 1) Achievement.unlock("settings_name_changed");
      } else if (result.length() > 15) {
        ShowStatusAction::show("Name must be 1-15 characters.", 1500);
      }
      _refresh();
      break;
    }

    case SETT_DISP_OFF_EN: {
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

#ifdef APP_MENU_POWER_OFF
    case SETT_POWER_OFF_EN: {
      bool cur = Config.get(APP_CONFIG_ENABLE_POWER_OFF, APP_CONFIG_ENABLE_POWER_OFF_DEFAULT).toInt();
      Config.set(APP_CONFIG_ENABLE_POWER_OFF, cur ? "0" : "1");
      Config.save(Uni.Storage);
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
#endif

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
      if (!InputNumberAction::wasCancelled()) {
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

    case SETT_SPEAKER_TEST: {
      Screen.push(new SpeakerTestScreen());
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
        {"Navy", "Navy"},
      };
      String      cur    = Config.get(APP_CONFIG_PRIMARY_COLOR, APP_CONFIG_PRIMARY_COLOR_DEFAULT);
      const char* result = InputSelectAction::popup("Primary Color", opts, 9, cur.c_str());
      if (result != nullptr) {
        Config.set(APP_CONFIG_PRIMARY_COLOR, result);
        Config.save(Uni.Storage);
        int n = Achievement.inc("settings_color_changed");
        if (n == 1) Achievement.unlock("settings_color_changed");
      }
      _refresh();
      break;
    }

#ifdef DEVICE_HAS_TOUCH_NAV
    case SETT_TOUCH_GUIDE: {
      Screen.push(new TouchGuideScreen(true));
      break;
    }

    case SETT_OVERLAY: {
      bool cur = Config.get(APP_CONFIG_SHOW_OVERLAY, APP_CONFIG_SHOW_OVERLAY_DEFAULT).toInt();
      Config.set(APP_CONFIG_SHOW_OVERLAY, cur ? "0" : "1");
      Config.save(Uni.Storage);
      _refresh();
      break;
    }
#endif

#ifdef DEVICE_CYD
    case SETT_TOUCH_CAL: {
      Screen.push(new CYDTouchCalScreen(true));
      break;
    }
#endif

    case SETT_WEB_PASSWORD: {
      String cur    = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);
      String result = InputTextAction::popup("Web Password", cur.c_str());
      if (result.length() > 0) {
        Config.set(APP_CONFIG_WEB_PASSWORD, result);
        Config.save(Uni.Storage);
      }
      _refresh();
      break;
    }

    case SETT_PIN_SETTING: {
      Screen.push(new PinSettingScreen());
      break;
    }

    case SETT_DEVICE_STATUS: {
      Screen.push(new DeviceStatusScreen());
      break;
    }

    case SETT_ABOUT: {
      Screen.push(new AboutScreen());
      break;
    }

#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    case SETT_NAV_MODE: {
      static constexpr InputSelectAction::Option opts[] = {
        {"Default", "default"},
        {"Encoder", "encoder"},
      };
      String      cur    = Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT);
      const char* result = InputSelectAction::popup("Navigation Mode", opts, 2, cur.c_str());
      if (result != nullptr) {
        Config.set(APP_CONFIG_NAV_MODE, result);
        Config.save(Uni.Storage);
        Uni.applyNavMode();
      }
      _refresh();
      break;
    }
#endif

#ifdef DEVICE_HAS_SCREEN_ORIENT
    case SETT_SCREEN_ORIENT: {
      bool isFlipped = Config.get(APP_CONFIG_SCREEN_ORIENT, APP_CONFIG_SCREEN_ORIENT_DEFAULT) == "flipped";
      Config.set(APP_CONFIG_SCREEN_ORIENT, isFlipped ? "normal" : "flipped");
      Config.save(Uni.Storage);
      Uni.applyOrientation();
      _refresh();
      break;
    }
#endif
  }
}

void SettingScreen::onBack() {
  Screen.goBack();
}