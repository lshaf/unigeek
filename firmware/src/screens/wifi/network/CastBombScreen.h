#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/CastBombUtil.h"

class CastBombScreen : public ListScreen
{
public:
  CastBombScreen();

  const char* title()    override { return "Cast Bomb"; }
  bool inhibitPowerOff() override {
    return _state == STATE_DISCOVERING || _state == STATE_CASTING;
  }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,
    STATE_DISCOVERING,
    STATE_DEVICES,
    STATE_CASTING,
  };

  struct VideoPreset {
    const char* label;
    const char* videoId;
  };

  static const VideoPreset PRESETS[];
  static const uint8_t     PRESET_COUNT;

  State    _state      = STATE_CONFIG;
  uint8_t  _presetIdx  = 0;
  String   _customId;            // overrides preset when non-empty
  String   _videoSub;            // current video name (for sublabel)

  CastBombUtil::Device _devices[CastBombUtil::MAX_DEVICES];
  uint8_t              _devCount = 0;
  ListItem             _devItems[CastBombUtil::MAX_DEVICES + 1];
  String               _devSubs[CastBombUtil::MAX_DEVICES];

  ListItem _configItems[3];

  void _showConfig();
  void _showDevices();
  void _discover();
  void _cast(uint8_t index);     // index == _devCount means "Cast All"
  void _pickPreset();
  void _setCustom();

  const char* _currentVideoId()    const;
  const char* _currentVideoLabel() const;
};
