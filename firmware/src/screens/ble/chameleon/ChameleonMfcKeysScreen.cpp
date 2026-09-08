#include "ChameleonMfcKeysScreen.h"
#include "ChameleonMfcScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

const char* ChameleonMfcKeysScreen::title() {
  if (_state == STATE_DATABASES) return "Dictionaries";
  if (_state == STATE_VIEW && _viewTitle.length()) return _viewTitle.c_str();
  return "Keys";
}

void ChameleonMfcKeysScreen::onInit() {
  _goMenu();
}

// ── states ─────────────────────────────────────────────

void ChameleonMfcKeysScreen::_goMenu() {
  _state   = STATE_MENU;
  _menu[0] = {"Check Known Keys"};
  _menu[1] = {"Dictionaries"};
  setItems(_menu);
  render();
}

void ChameleonMfcKeysScreen::_loadDatabases() {
  _state = STATE_DATABASES;
  if (!_pickDir.length()) _pickDir = kDictDir;

  _browser.root = kDictDir;
  uint8_t n = _browser.load(this, _pickDir, ".txt", nullptr, BrowseFileView::STEM_CAPITALIZED,
                            _pickDir == kDictDir ? "discovered.txt" : nullptr);
  setItems(_browser.items(), n);
  render();

  if (!n && _pickDir == kDictDir) ShowStatusAction::show("No dictionaries");
}

void ChameleonMfcKeysScreen::_openDatabase(const String& path, const String& name) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    return;
  }

  String content = Uni.Storage->readFile(path.c_str());
  _rowCount = 0;

  // One key per line; blank lines and '#' comments are skipped. Rows are capped
  // at kMaxRows so a large dictionary can't run past the row/label/value arrays.
  int start = 0;
  while (start < (int)content.length() && _rowCount < kMaxRows) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();

    String line = content.substring(start, nl);
    line.trim();

    if (line.length() && !line.startsWith("#")) {
      _labels[_rowCount] = String(_rowCount + 1);
      _values[_rowCount] = line;
      _rows[_rowCount]   = {_labels[_rowCount].c_str(), _values[_rowCount]};
      ++_rowCount;
    }

    start = nl + 1;
  }

  if (!_rowCount) {
    ShowStatusAction::show("No keys in file");
    return;
  }

  _viewTitle = name;
  _state     = STATE_VIEW;
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

// ── screen hooks ───────────────────────────────────────

void ChameleonMfcKeysScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) {
      Screen.push(new ChameleonMfcScreen(ChameleonMfcScreen::ACTION_SHOW_KEYS));
    } else if (index == 1) {
      _pickDir = kDictDir;
      _loadDatabases();
    }
    return;
  }

  if (_state == STATE_DATABASES) {
    if (index >= _browser.count()) return;

    const auto& e = _browser.entry(index);
    if (e.isDir) {
      _pickDir = e.path;
      _loadDatabases();
    } else {
      _openDatabase(e.path, e.label);
    }
  }
}

void ChameleonMfcKeysScreen::onUpdate() {
  if (_state == STATE_VIEW) {
    if (Uni.Nav->wasPressed()) {
      auto d = Uni.Nav->readDirection();
      if (d == INavigation::DIR_BACK) _loadDatabases();
      else                            _scrollView.onNav(d);
    }
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonMfcKeysScreen::onRender() {
  if (_state == STATE_VIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcKeysScreen::onBack() {
  if (_state == STATE_VIEW) {
    _loadDatabases();
    return;
  }

  if (_state == STATE_DATABASES) {
    // Walk back up the dictionary tree; leaving the root returns to the menu.
    if (_pickDir == kDictDir || !_pickDir.length()) {
      _pickDir = "";
      _goMenu();
    } else {
      int slash = _pickDir.lastIndexOf('/');
      _pickDir  = (slash > 0) ? _pickDir.substring(0, slash) : String(kDictDir);
      if (!_pickDir.startsWith(kDictDir)) _pickDir = kDictDir;
      _loadDatabases();
    }
    return;
  }

  Screen.goBack();
}
