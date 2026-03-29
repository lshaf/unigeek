#include "FileManagerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "screens/utility/FileViewerScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

// ── Lifecycle ───────────────────────────────────────────────────────────────

void FileManagerScreen::onInit()
{
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    Screen.setScreen(new UtilityMenuScreen());
    return;
  }
  _loadDir(_curPath);
}

void FileManagerScreen::onUpdate()
{
  // Trigger menu immediately on 1s hold in file state
  if (_state == STATE_FILE && !_holdFired &&
      Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    _holdFired = true;
    _openMenu(_selectedIndex);
    return;
  }

  // Drain the release event so ListScreen doesn't fire onItemSelected
  if (_holdFired) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      _holdFired = false;
    }
    return;
  }

  ListScreen::onUpdate();
}

void FileManagerScreen::onBack()
{
  if (_state == STATE_MENU) {
    _loadDir(_curPath);
    return;
  }
  if (_curPath == "/") {
    Screen.setScreen(new UtilityMenuScreen());
    return;
  }
  int slash = _curPath.lastIndexOf('/');
  _loadDir(slash > 0 ? _curPath.substring(0, slash) : "/");
}

void FileManagerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_FILE) {
    if (index < _fileCount) {
      if (_fileIsDir[index]) {
        _loadDir(_filePath[index]);
      } else {
        Screen.setScreen(new FileViewerScreen(_filePath[index]));
      }
    }
  } else if (_state == STATE_MENU) {
    _handleMenuAction(index);
  }
}

// ── Private ─────────────────────────────────────────────────────────────────

void FileManagerScreen::_loadDir(const String& path)
{
  _state     = STATE_FILE;
  _curPath   = path;
  _fileCount = 0;

  IStorage::DirEntry entries[kMaxFiles];
  uint8_t count = Uni.Storage->listDir(path.c_str(), entries, kMaxFiles);

  // Sort: directories first, then alphabetical ascending
  for (uint8_t i = 1; i < count; i++) {
    IStorage::DirEntry tmp = entries[i];
    int j = i - 1;
    while (j >= 0) {
      bool swap = false;
      if (tmp.isDir && !entries[j].isDir) swap = true;
      else if (tmp.isDir == entries[j].isDir && strcasecmp(tmp.name.c_str(), entries[j].name.c_str()) < 0) swap = true;
      if (!swap) break;
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = tmp;
  }

  String base = (path == "/") ? "" : path;
  for (uint8_t i = 0; i < count; i++) {
    _fileName[i]  = entries[i].name;
    _filePath[i]  = base + "/" + entries[i].name;
    _fileIsDir[i] = entries[i].isDir;
    _fileItems[i] = {_fileName[i].c_str(), entries[i].isDir ? "DIR" : nullptr};
  }
  _fileCount = count;

  _updateTitle();
  setItems(_fileItems, _fileCount);
}

void FileManagerScreen::_openMenu(uint8_t fileIdx)
{
  if (fileIdx >= _fileCount) return;

  _state      = STATE_MENU;
  _menuSelIdx = fileIdx;
  _menuCount  = 0;

  bool isFile = !_fileIsDir[fileIdx];

  if (isFile) {
    _menuActions[_menuCount] = ACT_VIEW;
    _menuItems[_menuCount++] = {"View"};
  }

  _menuActions[_menuCount] = ACT_NEW_FOLDER;
  _menuItems[_menuCount++] = {"New Folder"};

  _menuActions[_menuCount] = ACT_RENAME;
  _menuItems[_menuCount++] = {"Rename"};

  _menuActions[_menuCount] = ACT_DELETE;
  _menuItems[_menuCount++] = {"Delete"};

  if (isFile) {
    _menuActions[_menuCount] = ACT_COPY;
    _menuItems[_menuCount++] = {"Copy"};

    _menuActions[_menuCount] = ACT_CUT;
    _menuItems[_menuCount++] = {"Cut"};
  }

  if (!_clipPath.isEmpty()) {
    String base = (_curPath == "/") ? "" : _curPath;
    int slash = _clipPath.lastIndexOf('/');
    String clipName = (slash >= 0) ? _clipPath.substring(slash + 1) : _clipPath;
    bool exists = Uni.Storage->exists((base + "/" + clipName).c_str());

    _menuActions[_menuCount] = ACT_PASTE;
    _menuItems[_menuCount++] = {exists ? "Replace" : "Paste", _clipOp.c_str()};

    _menuActions[_menuCount] = ACT_CANCEL_CLIP;
    _menuItems[_menuCount++] = {"Clear Clipboard"};
  }

  _menuActions[_menuCount] = ACT_CLOSE_MENU;
  _menuItems[_menuCount++] = {"Close"};

  _menuActions[_menuCount] = ACT_EXIT;
  _menuItems[_menuCount++] = {"Exit"};

  setItems(_menuItems, _menuCount);
}

void FileManagerScreen::_handleMenuAction(uint8_t index)
{
  if (index >= _menuCount) return;

  String base       = (_curPath == "/") ? "" : _curPath;
  String targetPath = _filePath[_menuSelIdx];
  String targetName = _fileName[_menuSelIdx];

  switch (_menuActions[index]) {

    case ACT_VIEW:
      Screen.setScreen(new FileViewerScreen(targetPath));
      return;

    case ACT_NEW_FOLDER: {
      String name = InputTextAction::popup("New Folder");
      render();
      if (!name.isEmpty()) {
        String newPath = base + "/" + name;
        if (Uni.Storage->exists(newPath.c_str())) {
          ShowStatusAction::show("Already exists", 1500);
        } else {
          Uni.Storage->makeDir(newPath.c_str());
        }
      }
      break;
    }

    case ACT_RENAME: {
      String newName = InputTextAction::popup("Rename", targetName);
      render();
      if (!newName.isEmpty() && newName != targetName) {
        String newPath = base + "/" + newName;
        if (Uni.Storage->exists(newPath.c_str())) {
          ShowStatusAction::show("Already exists", 1500);
        } else if (!Uni.Storage->renameFile(targetPath.c_str(), newPath.c_str())) {
          ShowStatusAction::show("Rename failed", 1500);
        }
      }
      break;
    }

    case ACT_DELETE: {
      ShowStatusAction::show("Deleting...", 0);
      bool ok = _fileIsDir[_menuSelIdx]
                  ? _removeDir(targetPath)
                  : Uni.Storage->deleteFile(targetPath.c_str());
      if (!ok) ShowStatusAction::show("Delete failed", 1500);
      break;
    }

    case ACT_COPY:
      _clipPath = targetPath;
      _clipOp   = "Copy";
      break;

    case ACT_CUT:
      _clipPath = targetPath;
      _clipOp   = "Cut";
      break;

    case ACT_PASTE: {
      int slash = _clipPath.lastIndexOf('/');
      String clipName = (slash >= 0) ? _clipPath.substring(slash + 1) : _clipPath;
      String destPath = base + "/" + clipName;
      String msg = _clipOp + " File...";
      ShowStatusAction::show(msg.c_str(), 0);
      bool ok = false;
      if (_clipOp == "Copy") {
        String content = Uni.Storage->readFile(_clipPath.c_str());
        ok = Uni.Storage->writeFile(destPath.c_str(), content.c_str());
      } else {
        ok = Uni.Storage->renameFile(_clipPath.c_str(), destPath.c_str());
      }
      if (!ok) {
        ShowStatusAction::show("Operation failed", 1500);
      } else {
        _clipPath = "";
        _clipOp   = "";
      }
      break;
    }

    case ACT_CANCEL_CLIP:
      _clipPath = "";
      _clipOp   = "";
      break;

    case ACT_CLOSE_MENU:
      _loadDir(_curPath);
      return;

    case ACT_EXIT:
      Screen.setScreen(new UtilityMenuScreen());
      return;
  }

  _loadDir(_curPath);
}

bool FileManagerScreen::_removeDir(const String& path)
{
  IStorage::DirEntry entries[kMaxFiles];
  uint8_t count = Uni.Storage->listDir(path.c_str(), entries, kMaxFiles);

  String base = (path == "/") ? "" : path;
  for (uint8_t i = 0; i < count; i++) {
    String entryPath = base + "/" + entries[i].name;
    if (entries[i].isDir) {
      _removeDir(entryPath);
    } else {
      Uni.Storage->deleteFile(entryPath.c_str());
    }
  }
  return Uni.Storage->removeDir(path.c_str());
}

void FileManagerScreen::_updateTitle()
{
  if (_curPath == "/") {
    strncpy(_titleBuf, "File Manager", sizeof(_titleBuf) - 1);
  } else {
    int slash = _curPath.lastIndexOf('/');
    String name = (slash >= 0) ? _curPath.substring(slash + 1) : _curPath;
    strncpy(_titleBuf, name.c_str(), sizeof(_titleBuf) - 1);
  }
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';
}