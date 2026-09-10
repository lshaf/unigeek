#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfuNdefScreen : public ListScreen {
public:
  const char* title()                override;
  bool        inhibitPowerOff()      override { return _running; }
  void        onInit()               override;
  void        onUpdate()             override;
  void        onRender()             override;
  void onItemSelected(uint8_t index) override;
  void        onBack()               override;

private:
  enum State { MENU, WRITE_MENU, RESULT, FILE_SELECT };

  // Detail rows shown after a read; add() clamps to this.
  static constexpr uint8_t kMaxRows = 24;

  State _state   = MENU;
  bool  _running = false;

  ListItem _menu[3] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
  };

  ListItem _write[6] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
    {"Load from File"},
  };

  ScrollListView      _view;
  ScrollListView::Row _rows[kMaxRows];
  String              _l[kMaxRows];
  String              _v[kMaxRows];
  uint8_t             _n = 0;

  BrowseFileView _browser;
  String         _pickDir;

  void goMenu();
  void goWrite();
  bool readImage(uint8_t*& img, size_t& len, uint8_t uid[7]);
  bool writeRecord(const uint8_t* ndef, size_t len, const char* opTitle);
  void read();
  void erase();
  void show(const uint8_t* ndef, size_t len, const uint8_t uid[7]);
  void add(const String& label, const String& value);
  void writeBuilt(uint8_t kind);
  void files();
  void fileSelected(uint8_t index);
};
