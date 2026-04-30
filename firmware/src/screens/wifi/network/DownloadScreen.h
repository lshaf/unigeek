#pragma once
#include "ui/templates/ListScreen.h"
#include <WiFiClientSecure.h>

class DownloadScreen : public ListScreen {
public:
  const char* title() override { return _titleBuf; }

  void onInit() override;
  void onUpdate() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_MENU,
    STATE_IR_CATEGORIES,
    STATE_BADUSB_OS,
    STATE_BADUSB_CATEGORIES,
  } _state = STATE_MENU;

  char _titleBuf[32] = "Download";

  // Main menu
  ListItem _menuItems[4];
  String   _wfmVersionSub;
  void _showMenu();
  void _downloadWebPage();
  void _downloadSampleData();
  bool _downloadFile(WiFiClientSecure& client, const char* url, const char* path);

  // IR categories
  static constexpr uint8_t kMaxCategories = 46;
  ListItem _catItems[kMaxCategories];
  String _catFolders[kMaxCategories];
  String _catLabels[kMaxCategories];
  uint8_t _catCount = 0;
  void _showIRCategories();
  void _downloadIRCategory(uint8_t index);

  // BadUSB OS selection
  static constexpr uint8_t kMaxBadUSBOS = 12;
  ListItem _badusbOSItems[kMaxBadUSBOS];
  String   _badusbOSFolders[kMaxBadUSBOS];
  String   _badusbOSLabels[kMaxBadUSBOS];
  uint8_t  _badusbOSCount = 0;
  uint8_t  _badusbSelectedOSIndex = 0;
  uint32_t _navReadyAt = 0;   // millis() deadline — drain ghost-presses after HTTP loads

  // Cached full folder list parsed from manifest
  static constexpr uint8_t kMaxBadUSBAll = 64;
  String   _badusbAllFolders[kMaxBadUSBAll];
  uint8_t  _badusbAllCount = 0;

  // BadUSB categories (filtered by selected OS)
  static constexpr uint8_t kMaxBadUSBCategories = 48;
  ListItem _badusbItems[kMaxBadUSBCategories];
  String _badusbFolders[kMaxBadUSBCategories];
  String _badusbLabels[kMaxBadUSBCategories];
  uint8_t _badusbCount = 0;

  void _showBadUSBOS();
  void _showBadUSBOSFromCache();
  void _showBadUSBCategoriesForOS(uint8_t osIndex);
  void _downloadBadUSBCategory(uint8_t index);

  static constexpr const char* REPO_BASE =
    "https://raw.githubusercontent.com/lshaf/unigeek/main/sdcard";
  static constexpr const char* IR_REPO_BASE =
    "https://raw.githubusercontent.com/lshaf/Flipper-IRDB/main";
  static constexpr const char* BADUSB_REPO_BASE =
    "https://raw.githubusercontent.com/lshaf/badusb-collection/main";
  static constexpr const char* DUCKY_BASE =
    "/unigeek/hid/duckyscript/downloads";
  static constexpr const char* IR_DL_BASE =
    "/unigeek/ir/downloads";
};
