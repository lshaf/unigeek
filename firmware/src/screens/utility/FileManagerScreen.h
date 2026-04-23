#pragma once

#include "ui/templates/ListScreen.h"

class FileManagerScreen : public ListScreen
{
public:
  FileManagerScreen() = default;
  FileManagerScreen(const String& startPath) : _curPath(startPath) {}

  const char* title() override { return _titleBuf; }

  void onInit() override;
  void onUpdate() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_FILE, STATE_MENU } _state = STATE_FILE;

  enum MenuAction {
    ACT_VIEW, ACT_VIEW_HEX, ACT_NEW_FOLDER, ACT_RENAME, ACT_DELETE,
    ACT_COPY, ACT_CUT, ACT_PASTE, ACT_CANCEL_CLIP, ACT_CLOSE_MENU, ACT_EXIT,
  };

  static constexpr uint8_t kMaxFiles     = 40;
  static constexpr uint8_t kMaxMenu      = 11;
  static constexpr uint8_t kMaxPathDepth = 8;

  // File browser
  String   _curPath = "/";
  String   _fileName[kMaxFiles];
  String   _filePath[kMaxFiles];
  bool     _fileIsDir[kMaxFiles];
  ListItem _fileItems[kMaxFiles];
  uint8_t  _fileCount  = 0;
  uint8_t  _menuSelIdx = 0;
  bool     _holdFired  = false;

  // Navigation history (folder path + selected index, pushed on dir enter)
  struct PathState { String path; uint8_t index; };
  PathState _pathHistory[kMaxPathDepth];
  uint8_t   _pathDepth = 0;

  // Clipboard
  String _clipPath;
  String _clipOp;

  // Context menu
  MenuAction _menuActions[kMaxMenu];
  ListItem   _menuItems[kMaxMenu];
  uint8_t    _menuCount = 0;

  // Dynamic title (folder name)
  char _titleBuf[64] = "File Manager";

  void _loadDir(const String& path, uint8_t restoreIdx = 0);
  void _openMenu(uint8_t fileIdx);
  void _handleMenuAction(uint8_t index);
  bool _removeDir(const String& path);
  void _updateTitle();
};