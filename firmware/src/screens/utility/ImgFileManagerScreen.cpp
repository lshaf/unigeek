#include "ImgFileManagerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include <string.h>

namespace {
constexpr const char* IMG_DIR = "/unigeek/hid/imgs";
}

void ImgFileManagerScreen::_closeImage() {
  _volume.close();
  _selectingSd = false;
  _showingMenu = false;
  _destinationMenu = false;
  _browsing = false;
  _depth = 0;
}

bool ImgFileManagerScreen::_openImage(const String& path, String& error) {
  _closeImage();
  if (!_volume.open(*Uni.Storage, path, error)) return false;
  _paths[0] = "/";
  _browsing = true;
  return true;
}

bool ImgFileManagerScreen::_listDirectory(const String& path) {
  if (!_volume.list(path, _names, _dirs, _sizes, MAX_ENTRIES, _count)) return false;
  for (uint8_t i = 0; i < _count; ++i) {
    _items[i].label = _names[i].c_str();
    _items[i].sublabel = _dirs[i] ? "DIR" : nullptr;
  }
  setItems(_items, _count);
  return true;
}

void ImgFileManagerScreen::_listImages() {
  _count = 0;
  fs::File dir = Uni.Storage->open(IMG_DIR, "r");
  if (dir && dir.isDirectory()) {
    fs::File f = dir.openNextFile();
    while (f && _count < MAX_IMAGES) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      String lower = name;
      lower.toLowerCase();
      if (!f.isDirectory() && lower.endsWith(".img")) {
        _names[_count] = name;
        _items[_count].label = _names[_count].c_str();
        _items[_count].sublabel = nullptr;
        ++_count;
      }
      f.close();
      f = dir.openNextFile();
    }
    if (f) f.close();
    dir.close();
  }
  setItems(_items, _count);
}

void ImgFileManagerScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    Screen.goBack(); return;
  }
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/hid");
  Uni.Storage->makeDir(IMG_DIR);
  _listImages();
  if (!_count) { ShowStatusAction::show("No IMG files available"); Screen.goBack(); }
}

void ImgFileManagerScreen::onUpdate() {
  if (((_browsing || _selectingSd) && !_showingMenu) && !_holdFired && Uni.Nav->isPressed() &&
      Uni.Nav->heldDuration() >= 1000) {
    _holdFired = true;
    if (_selectingSd) _showDestinationActions();
    else _showEntryActions(_selectedIndex);
    return;
  }
  if (_holdFired) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      _holdFired = false;
    }
    return;
  }
  ListScreen::onUpdate();
}

void ImgFileManagerScreen::onBack() {
  if (_showingMenu) {
    const bool destinationMenu = _destinationMenu;
    _showingMenu = false;
    _destinationMenu = false;
    if (destinationMenu) _listSdDirectories();
    else _listDirectory(_paths[_depth]);
    return;
  }
  if (_selectingSd) {
    if (_sdPath != "/") {
      const int slash = _sdPath.lastIndexOf('/');
      _sdPath = slash <= 0 ? "/" : _sdPath.substring(0, slash);
      _listSdDirectories();
    } else {
      _selectingSd = false;
      _listDirectory(_paths[_depth]);
    }
    return;
  }
  if (_browsing) {
    if (_depth) {
      --_depth;
      if (!_listDirectory(_paths[_depth])) ShowStatusAction::show("Cannot read directory");
    } else {
      _closeImage();
      _listImages();
    }
    return;
  }
  _closeImage();
  Screen.goBack();
}

void ImgFileManagerScreen::onItemSelected(uint8_t index) {
  if (_showingMenu) {
    if (_destinationMenu) {
      if (index == 0) {
        _showingMenu = false;
        _destinationMenu = false;
        _confirmSdDestination();
      } else {
        _showingMenu = false;
        _destinationMenu = false;
        _listSdDirectories();
      }
    } else if (index < _menuCount) _executeEntryAction(index);
    return;
  }
  if (index >= _count) return;
  if (_selectingSd) {
    _sdPath = (_sdPath == "/" ? String("") : _sdPath) + "/" + _names[index];
    _listSdDirectories();
    return;
  }
  if (!_browsing) {
    String error;
    if (!_openImage(String(IMG_DIR) + "/" + _names[index], error)) {
      ShowStatusAction::show(error.c_str()); render(); return;
    }
    if (!_listDirectory(_paths[0])) {
      _closeImage(); ShowStatusAction::show("Cannot read FILES"); _listImages();
    }
    return;
  }
  if (_dirs[index]) {
    if (_depth >= 31) { ShowStatusAction::show("Directory too deep"); return; }
    String parent = _paths[_depth];
    if (!parent.endsWith("/")) parent += "/";
    const String path = parent + _names[index];
    _paths[++_depth] = path;
    if (!_listDirectory(path)) {
      --_depth;
      ShowStatusAction::show("Cannot read directory");
      _listDirectory(_paths[_depth]);
    }
  }
}

void ImgFileManagerScreen::_showEntryActions(uint8_t index) {
  if (!_browsing || _showingMenu || index >= _count) return;
  _destinationMenu = false;
  _menuTarget = index;
  _menuName = _names[index];
  _menuCount = _dirs[index] ? 2 : 3;
  _menuItems[0] = {"Rename", nullptr};
  _menuItems[1] = {"Delete", nullptr};
  _menuItems[2] = {"Copy to SD Card", nullptr};
  _showingMenu = true;
  setItems(_menuItems, _menuCount);
}

void ImgFileManagerScreen::_executeEntryAction(uint8_t action) {
  if (!_showingMenu || action >= _menuCount) return;
  _showingMenu = false;
  const String name = _menuName;
  String parent = _paths[_depth];
  if (!parent.endsWith("/")) parent += "/";
  const String path = parent + name;
  if (action == 2) {
    _copySource = path;
    _copyName = name;
    _sdPath = "/";
    _selectingSd = true;
    _listSdDirectories();
    return;
  }
  if (action == 0) {
    String newName = InputTextAction::popup("Rename", name);
    render();
    if (InputTextAction::wasCancelled() || newName.isEmpty() || newName == name) {
      _listDirectory(_paths[_depth]); return;
    }
    if (newName == "." || newName == ".." || newName.indexOf('/') >= 0 ||
        newName.indexOf('\\') >= 0 || newName.indexOf(':') >= 0) {
      ShowStatusAction::show("Invalid name");
    } else {
      String error;
      if (!_volume.renameEntry(path, parent + newName, error)) ShowStatusAction::show(error.c_str());
    }
  } else if (action == 1) {
    ShowStatusAction::show("Deleting...", 0);
    String error;
    if (!_volume.deleteEntry(path, error)) ShowStatusAction::show("Failed", 1500);
  }
  if (!_listDirectory(_paths[_depth])) ShowStatusAction::show("Cannot refresh directory");
  render();
}

// Destination browser: Press opens a folder; Hold opens the Paste menu
// for the currently displayed location, as in the regular File Manager.
void ImgFileManagerScreen::_listSdDirectories() {
  _count = 0;
  // Enumerate the SD directly: listDir(capacity) may consume its limit on
  // regular files before reaching any folders.
  fs::File dir = Uni.Storage->open(_sdPath.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    ShowStatusAction::show("Cannot read SD folder", 1500);
    setItems(_items, _count);
    return;
  }
  bool truncated = false;
  fs::File entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (!name.isEmpty() && name != "." && name != "..") {
        if (_count >= MAX_ENTRIES) {
          truncated = true;
          entry.close();
          break;
        }
        _names[_count] = name;
        _items[_count] = { _names[_count].c_str(), "DIR" };
        ++_count;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  if (truncated) ShowStatusAction::show("Folder list limited", 1500);
  setItems(_items, _count);
}

void ImgFileManagerScreen::_showDestinationActions() {
  _destinationMenu = true;
  _showingMenu = true;
  _menuItems[0] = {"Paste", nullptr};
  _menuItems[1] = {"Close", nullptr};
  _menuCount = 2;
  setItems(_menuItems, _menuCount);
}

void ImgFileManagerScreen::_confirmSdDestination() {
  const String destination = (_sdPath == "/" ? String("") : _sdPath) + "/" + _copyName;
  _selectingSd = false;
  if (Uni.Storage->exists(destination.c_str())) {
    ShowStatusAction::show("Already exists", 1500);
  } else {
    ShowStatusAction::show("Copying...", 0);
    String error;
    if (!_volume.copyFileToStorage(_copySource, *Uni.Storage, destination, error))
      ShowStatusAction::show(error.c_str(), 1500);
    else
      ShowStatusAction::show("Copied to SD", 1200);
  }
  _listDirectory(_paths[_depth]);
  render();
}
