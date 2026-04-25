//
// Created by L Shaf on 2026-03-02.
//

#pragma once

#include "ui/templates/ListScreen.h"

class SettingScreen : public ListScreen
{
public:
  const char* title() override { return "Settings"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  void _refresh();

  enum : uint8_t {
    SETT_NAME = 0,
    SETT_DISP_OFF_EN,
    SETT_DISP_OFF,
#ifdef APP_MENU_POWER_OFF
    SETT_POWER_OFF_EN,
    SETT_POWER_OFF,
#endif
    SETT_BRIGHTNESS,
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
    SETT_VOLUME,
#endif
#ifdef DEVICE_HAS_SOUND
    SETT_NAV_SOUND,
    SETT_SPEAKER_TEST,
#endif
    SETT_COLOR,
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    SETT_NAV_MODE,
#endif
#ifdef DEVICE_HAS_SCREEN_ORIENT
    SETT_SCREEN_ORIENT,
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
    SETT_TOUCH_GUIDE,
    SETT_OVERLAY,
#endif
#ifdef DEVICE_CYD
    SETT_TOUCH_CAL,
#endif
    SETT_WEB_PASSWORD,
    SETT_PIN_SETTING,
    SETT_DEVICE_STATUS,
    SETT_ABOUT,
    SETT_COUNT
  };

  String _nameSub;
  String _dispOffEnSub;
  String _dispOffSub;
#ifdef APP_MENU_POWER_OFF
  String _powerOffEnSub;
  String _powerOffSub;
#endif
  String _brightSub;
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
  String _volSub;
#endif
#ifdef DEVICE_HAS_SOUND
  String _navSndSub;
#endif
  String _colorSub;
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
  String _navModeSub;
#endif
#ifdef DEVICE_HAS_SCREEN_ORIENT
  String _screenOrientSub;
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
  String _overlaySub;
#endif
  String _webPwdSub;

  ListItem _items[SETT_COUNT] = {
    {"Name",             ""},
    {"Auto Display Off", ""},
    {"Display Off",      ""},
#ifdef APP_MENU_POWER_OFF
    {"Auto Power Off",   ""},
    {"Power Off",        ""},
#endif
    {"Brightness",       ""},
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
    {"Volume",           ""},
#endif
#ifdef DEVICE_HAS_SOUND
    {"Navigation Sound", ""},
    {"Speaker Test"},
#endif
    {"Primary Color",    ""},
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    {"Navigation Mode",  ""},
#endif
#ifdef DEVICE_HAS_SCREEN_ORIENT
    {"Screen Orientation", ""},
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
    {"Touch Guide"},
    {"Navigation Overlay", ""},
#endif
#ifdef DEVICE_CYD
    {"Touch Calibration"},
#endif
    {"Web Password",     ""},
    {"Pin Setting"},
    {"Device Status"},
    {"About"},
  };
};