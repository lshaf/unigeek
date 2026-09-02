#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class ChameleonSlotEditScreen : public ListScreen {
public:
  explicit ChameleonSlotEditScreen(uint8_t slot) : _slot(slot) {}

  const char* title() override { return _title; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  static constexpr int kCount = 15;

  uint8_t  _slot;
  char     _title[14];

  ListItem _items[kCount];
  char     _labels[kCount][28];
  char     _subs[kCount][22];

  BrowseFileView _browser;  // shared file picker (HF dump browse)

  uint16_t _hfType    = 0;
  uint16_t _lfType    = 0;
  bool     _hfEnabled = false;
  bool     _lfEnabled = false;
  char     _hfNick[18] = {};
  char     _lfNick[18] = {};
  bool     _isActive  = false;

  void _load();
  void _rebuildLabels();
  void _setActive();
  void _editType(bool lf);
  void _toggleEnable(bool lf);
  void _editNick(bool lf);
  void _loadDefault();
  void _writeContent();
  void _downloadDump();
  void _viewContent();
  void _viewData();
  void _writeTag();
  bool _writeHfFromBin(const char* path);
  bool _writeLfFromHex(const char* hex);
  void _deleteSlot(bool lf);
  void _saveNicks();
};
