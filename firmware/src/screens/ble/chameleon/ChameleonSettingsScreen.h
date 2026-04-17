#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonSettingsScreen : public ListScreen {
public:
  const char* title() override { return "Chameleon Settings"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  static constexpr int kCount = 8;

  ListItem _items[kCount];
  char     _labels[kCount][24];
  char     _subs[kCount][20];

  uint8_t _anim         = 0;
  uint8_t _btnAShort    = 0;
  uint8_t _btnBShort    = 0;
  uint8_t _btnALong     = 0;
  uint8_t _btnBLong     = 0;
  bool    _blePairing   = false;

  void _load();
  void _rebuildLabels();
  const char* _animName(uint8_t v);
  const char* _btnName(uint8_t v);
  void _editAnim();
  void _editButton(bool isB, bool longPress);
  void _togglePairing();
  void _save();
  void _reset();
  void _clearBonds();
};
