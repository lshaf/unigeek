#pragma once

#include "ui/templates/ListScreen.h"
#include "core/IStorage.h"

// Reusable file-listing helper for directory-navigating screens.
// Encapsulates: loading indicator + listDir() + sort (dirs first, alpha) + ListItem build.
//
// Usage — aggregate into your screen class:
//   BrowseFileView _browser;
//
//   void _loadDir(const String& path) {
//     uint8_t n = _browser.load(this, path);          // all files + dirs
//     uint8_t n = _browser.load(this, path, ".ir");   // .ir files + dirs only
//     setItems(_browser.items(), n);
//   }
//
//   // On selection:
//   if (_browser.entry(index).isDir)  _loadDir(_browser.entry(index).path);
//   else                              _openFile(_browser.entry(index).path);
//
// For screens with fully custom listing logic, call showLoading() explicitly:
//   BrowseFileView::showLoading();   // before your manual listDir

struct BrowseFileView {
  using Item = ListScreen::ListItem;

  static constexpr uint8_t kCap = 150;

  struct Entry {
    String name;
    String path;
    bool   isDir = false;
  };

  // Show "Loading..." status bar overlay.
  static void showLoading();

  // Load a directory: flash loading, sort dirs-first then alpha, build Item rows.
  //   ext         - nullptr = all files; ".ir" = only .ir files (dirs always included)
  //   fileSublabel - sublabel on file rows; nullptr = none, "FILE" = "FILE"
  //                  Directory rows always get "DIR".
  // Returns populated count. Returns 0 if storage unavailable.
  uint8_t load(BaseScreen* host, const String& dir,
               const char* ext          = nullptr,
               const char* fileSublabel = nullptr);

  uint8_t        count()          const { return _count; }
  const Entry&   entry(uint8_t i) const { return _entries[i]; }
  Item*          items()                { return _listItems; }
  const Item*    items()          const { return _listItems; }

private:
  Entry    _entries[kCap];
  Item     _listItems[kCap];
  uint8_t  _count = 0;
};
