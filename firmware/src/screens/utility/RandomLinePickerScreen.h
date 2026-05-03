#pragma once

#include "ui/templates/ListScreen.h"

class RandomLinePickerScreen : public ListScreen {
public:
  const char* title() override { return "Random Line"; }
  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  static constexpr uint8_t    kMaxFiles    = 50;
  static constexpr uint8_t    kMaxSelected = 30;
  static constexpr uint8_t    kMaxDepth    = 6;
  static constexpr const char* kRootDir    = "/unigeek/utility/random_line";

  struct Entry {
    String name;
    String fullPath;
    bool   isDir = false;
  };

  Entry    _entries[kMaxFiles];
  uint8_t  _entryCount    = 0;

  String   _selected[kMaxSelected];
  uint8_t  _selectedCount = 0;

  String   _curPath;
  String   _pathStack[kMaxDepth];
  uint8_t  _savedIdx[kMaxDepth] = {};
  uint8_t  _depth = 0;

  ListItem _listItems[kMaxFiles + 1]; // entries + Start
  char     _labelBuf[kMaxFiles][48];  // per-entry display label
  char     _startBuf[24] = {};        // sublabel for Start item
  uint8_t  _itemCount = 0;

  void _drawLoading();
  void _loadDir(const String& path, uint8_t restoreIdx = 0);
  void _buildItems(uint8_t restoreIdx = 0);
  bool _isSelected(const String& path);
  void _toggleSelect(const String& path);
  void _startViewer();
};
