#pragma once
#include "ui/templates/ListScreen.h"
#include "ImgFatVolume.h"

// FAT32 directory browser for the FILES partition inside an IMG.
class ImgFileManagerScreen : public ListScreen {
public:
  const char* title() override { return "IMG File Manager"; }
  void onInit() override;
  void onBack() override;
  void onUpdate() override;
  void onItemSelected(uint8_t index) override;
  ~ImgFileManagerScreen() override { _closeImage(); }
private:
  static constexpr uint8_t MAX_IMAGES = 32;
  static constexpr uint8_t MAX_ENTRIES = 64;
  String _names[MAX_ENTRIES];
  ListItem _items[MAX_ENTRIES] = {};
  uint32_t _sizes[MAX_ENTRIES] = {};
  bool _dirs[MAX_ENTRIES] = {};
  uint8_t _count = 0;
  bool _browsing = false;
  bool _selectingSd = false;
  bool _showingMenu = false;
  bool _destinationMenu = false;
  uint8_t _menuTarget = 0;
  uint8_t _menuCount = 0;
  String _menuName;
  ListItem _menuItems[3] = {};
  String _sdPath = "/";
  String _copySource;
  String _copyName;
  ImgFatVolume _volume;
  String _paths[32];
  uint8_t _depth = 0;
  bool _holdFired = false;
  void _showEntryActions(uint8_t index);
  void _executeEntryAction(uint8_t action);
  void _listSdDirectories();
  void _confirmSdDestination();
  void _showDestinationActions();
  void _closeImage();
  bool _openImage(const String& path, String& error);
  bool _listDirectory(const String& path);
  void _listImages();
};
