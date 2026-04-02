#pragma once
#include "ui/templates/ListScreen.h"

class DownloadScreen : public ListScreen {
public:
  const char* title() override { return _titleBuf; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_MENU,
    STATE_IR_CATEGORIES,
  } _state = STATE_MENU;

  char _titleBuf[32] = "Download";

  // Main menu
  ListItem _menuItems[3];
  String   _wfmVersionSub;
  void _showMenu();
  void _downloadWebPage();
  void _downloadSampleData();
  bool _downloadFile(const char* url, const char* path);

  // IR categories
  static constexpr uint8_t kMaxCategories = 46;
  ListItem _catItems[kMaxCategories];
  String _catFolders[kMaxCategories];
  String _catLabels[kMaxCategories];
  uint8_t _catCount = 0;
  void _showIRCategories();
  void _downloadIRCategory(uint8_t index);

  static constexpr const char* REPO_BASE =
    "https://raw.githubusercontent.com/lshaf/unigeek/main/sdcard";
  static constexpr const char* IR_REPO_BASE =
    "https://raw.githubusercontent.com/lshaf/Flipper-IRDB/main";
};
