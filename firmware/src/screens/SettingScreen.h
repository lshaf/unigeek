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
    SETT_POWER_SAV,
    SETT_DISP_OFF,
    SETT_POWER_OFF,
    SETT_BRIGHTNESS,
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
    SETT_VOLUME,
#endif
#ifdef DEVICE_HAS_SOUND
    SETT_NAV_SOUND,
#endif
    SETT_COLOR,
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    SETT_NAV_MODE,
#endif
    SETT_ABOUT,
    SETT_COUNT
  };

  String _nameSub;
  String _powerSavSub;
  String _dispOffSub;
  String _powerOffSub;
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

  ListItem _items[SETT_COUNT] = {
    {"Name",             ""},
    {"Power Saving",     ""},
    {"Display Off",      ""},
    {"Auto Power Off",   ""},
    {"Brightness",       ""},
#if defined(DEVICE_HAS_SOUND) && defined(DEVICE_HAS_VOLUME_CONTROL)
    {"Volume",           ""},
#endif
#ifdef DEVICE_HAS_SOUND
    {"Navigation Sound", ""},
#endif
    {"Primary Color",    ""},
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    {"Navigation Mode",  ""},
#endif
    {"About"},
  };
};