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

  BrowseFileView::showLoading();
  _loadDir(kRootDir);
}

void RandomLinePickerScreen::onBack() {
  if (_depth == 0) {
    _selectedCount = 0;
    Screen.goBack();
    return;
  }
  _depth--;
  String parentPath  = _pathStack[_depth];
  uint8_t restoreIdx = _savedIdx[_depth];
  BrowseFileView::showLoading();
  _loadDir(parentPath, restoreIdx);
}

void RandomLinePickerScreen::onItemSelected(uint8_t index) {
  // Last item is always Start
  if (index == _itemCount - 1) {
    _startViewer();
    return;
  }
  if (index >= _browser.count()) return;

  const auto& e = _browser.entry(index);
  if (e.isDir) {
    if (_depth < kMaxDepth) {
      _pathStack[_depth] = _curPath;
      _savedIdx[_depth]  = index;
      _depth++;
    }
    _loadDir(e.path);
    return;
  }

  // Toggle file selection, keep cursor at this item
  _toggleSelect(e.path);
  _buildItems(index);
}

// ── Private ──────────────────────────────────────────────────────────────────

void RandomLinePickerScreen::_loadDir(const String& path, uint8_t restoreIdx) {
  _curPath = path;
  _browser.load(this, path);
  _buildItems(restoreIdx);
}

void RandomLinePickerScreen::_buildItems(uint8_t restoreIdx) {
  _itemCount = 0;
  uint8_t n = _browser.count();

  for (uint8_t i = 0; i < n; i++) {
    const auto& e = _browser.entry(i);
    if (e.isDir) {
      snprintf(_labelBuf[i], sizeof(_labelBuf[i]), "%s/", e.name.c_str());
      _listItems[_itemCount++] = { _labelBuf[i] };
    } else {
      bool sel = _isSelected(e.path);
      _listItems[_itemCount++] = { e.name.c_str(), sel ? "*" : nullptr };
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
  ShowStatusAction::show("Loading...", 0);
  Screen.push(new RandomLineViewerScreen(_selected, _selectedCount));
}
