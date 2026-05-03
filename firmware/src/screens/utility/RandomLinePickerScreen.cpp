#include "RandomLinePickerScreen.h"
#include "RandomLineViewerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

// ── Lifecycle ────────────────────────────────────────────────────────────────

void RandomLinePickerScreen::onInit() {
  _curPath = kRootDir;
  _depth   = 0;
  _selectedCount = 0;

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    Screen.goBack();
    return;
  }

  _drawLoading();
  _loadDir(kRootDir);
}

void RandomLinePickerScreen::onBack() {
  _selectedCount = 0;
  if (_depth == 0) {
    Screen.goBack();
    return;
  }
  _depth--;
  String parentPath  = _pathStack[_depth];
  uint8_t restoreIdx = _savedIdx[_depth];
  _drawLoading();
  _loadDir(parentPath, restoreIdx);
}

void RandomLinePickerScreen::onItemSelected(uint8_t index) {
  // Last item is always Start
  if (index == _itemCount - 1) {
    _startViewer();
    return;
  }
  if (index >= _entryCount) return;

  if (_entries[index].isDir) {
    if (_depth < kMaxDepth) {
      _pathStack[_depth] = _curPath;
      _savedIdx[_depth]  = index;
      _depth++;
    }
    _drawLoading();
    _loadDir(_entries[index].fullPath);
    return;
  }

  // Toggle file selection, keep cursor at this item
  _toggleSelect(_entries[index].fullPath);
  _buildItems(index);
}

// ── Private ──────────────────────────────────────────────────────────────────

void RandomLinePickerScreen::_drawLoading() {
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  Sprite sp(&lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.drawString("Loading...", bw / 2, bh / 2);
  sp.pushSprite(bx, by);
  sp.deleteSprite();
}

void RandomLinePickerScreen::_loadDir(const String& path, uint8_t restoreIdx) {
  _curPath     = path;
  _entryCount  = 0;

  IStorage::DirEntry raw[kMaxFiles];
  uint8_t count = Uni.Storage->listDir(path.c_str(), raw, kMaxFiles);

  // Dirs first, then alphabetical ascending (case-insensitive)
  for (uint8_t i = 1; i < count; i++) {
    IStorage::DirEntry tmp = raw[i];
    int j = i - 1;
    while (j >= 0) {
      bool swap = false;
      if (tmp.isDir && !raw[j].isDir) swap = true;
      else if (tmp.isDir == raw[j].isDir && strcasecmp(tmp.name.c_str(), raw[j].name.c_str()) < 0) swap = true;
      if (!swap) break;
      raw[j + 1] = raw[j];
      j--;
    }
    raw[j + 1] = tmp;
  }

  String base = path;
  for (uint8_t i = 0; i < count && _entryCount < kMaxFiles; i++) {
    _entries[_entryCount].name     = raw[i].name;
    _entries[_entryCount].fullPath = base + "/" + raw[i].name;
    _entries[_entryCount].isDir    = raw[i].isDir;
    _entryCount++;
  }

  _buildItems(restoreIdx);
}

void RandomLinePickerScreen::_buildItems(uint8_t restoreIdx) {
  _itemCount = 0;

  for (uint8_t i = 0; i < _entryCount; i++) {
    if (_entries[i].isDir) {
      snprintf(_labelBuf[i], sizeof(_labelBuf[i]), "%s/", _entries[i].name.c_str());
      _listItems[_itemCount++] = { _labelBuf[i] };
    } else {
      bool sel = _isSelected(_entries[i].fullPath);
      _listItems[_itemCount++] = { _entries[i].name.c_str(), sel ? "*" : nullptr };
    }
  }

  // Start item — sublabel shows selection count when non-zero
  if (_selectedCount > 0) {
    snprintf(_startBuf, sizeof(_startBuf), "%u selected", _selectedCount);
    _listItems[_itemCount++] = { "Start", _startBuf };
  } else {
    _listItems[_itemCount++] = { "Start" };
  }

  setItems(_listItems, _itemCount, restoreIdx);
}

bool RandomLinePickerScreen::_isSelected(const String& path) {
  for (uint8_t i = 0; i < _selectedCount; i++) {
    if (_selected[i] == path) return true;
  }
  return false;
}

void RandomLinePickerScreen::_toggleSelect(const String& path) {
  for (uint8_t i = 0; i < _selectedCount; i++) {
    if (_selected[i] == path) {
      for (uint8_t j = i; j < _selectedCount - 1; j++) {
        _selected[j] = _selected[j + 1];
      }
      _selectedCount--;
      return;
    }
  }
  if (_selectedCount < kMaxSelected) {
    _selected[_selectedCount++] = path;
  }
}

void RandomLinePickerScreen::_startViewer() {
  if (_selectedCount == 0) {
    ShowStatusAction::show("No files selected", 1500);
    render();
    return;
  }
  Screen.push(new RandomLineViewerScreen(_selected, _selectedCount));
}
