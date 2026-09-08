#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfcKeysScreen : public ListScreen {
public:
  const char* title()                override;
  void        onInit()               override;
  void        onUpdate()             override;
  void        onRender()             override;
  void onItemSelected(uint8_t index) override;
  void        onBack()               override;

private:
  enum State { STATE_MENU, STATE_DATABASES, STATE_VIEW };

  static constexpr const char* kDictDir = "/unigeek/nfc/dictionaries";
  static constexpr uint16_t    kMaxRows = 256;

  State               _state = STATE_MENU;
  ListItem            _menu[2];
  BrowseFileView      _browser;
  String              _pickDir;
  ScrollListView      _scrollView;
  ScrollListView::Row _rows[kMaxRows];
  String              _labels[kMaxRows];
  String              _values[kMaxRows];
  uint16_t            _rowCount = 0;
  String              _viewTitle;

  void _goMenu();
  void _loadDatabases();
  void _openDatabase(const String& path, const String& name);
};
