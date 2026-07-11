#pragma once

#include "ui/templates/ListScreen.h"
#include "core/IStorage.h"

// Reusable file-listing helper for directory-navigating screens.
// Encapsulates: loading indicator + listDir() + sort (dirs first, alpha) + ListItem build.
//
// Usage — aggregate into your screen class:
//   BrowseFileView _browser;
//
//   void onInit() { _browser.root = "/unigeek/lua"; }  // confine to this subtree
//
//   void _loadDir(const String& path) {
//     uint8_t n = _browser.load(this, path);                    // all files + dirs
//     uint8_t n = _browser.load(this, path, ".ir");             // .ir files + dirs only
//     uint8_t n = _browser.load(this, path, Mode::DIRECTORY);   // dirs only
//     setItems(_browser.items(), n);
//   }
//
//   // On selection:
//   if (_browser.entry(index).isDir)  _loadDir(_browser.entry(index).path);
//   else                              _openFile(_browser.entry(index).path);
//
// Parent navigation: a clickable ".." row is inserted at index 0 whenever the
// loaded dir is not equal to `root`. Its `entry.path` resolves to the lexical
// parent (clamped to `root` so the picker can never escape the configured
// subtree). When `root` stays at the default "/" the picker can climb to the
// filesystem root only.
//
// For screens with fully custom listing logic, call showLoading() explicitly:
//   BrowseFileView::showLoading();   // before your manual listDir

struct BrowseFileView {
  using Item = ListScreen::ListItem;

  static constexpr uint8_t kCap = 150;

  // Filter mode passed to load().
  //   ALL       — show all files and directories
  //   DIRECTORY — show directories only
  //   const char* (implicit) — show files matching extension + directories
  struct Mode {
    enum Kind { ALL, DIRECTORY };
    Kind        kind;
    const char* ext;

    Mode()              : kind(ALL), ext(nullptr) {}
    Mode(Kind k)        : kind(k),   ext(nullptr) {}
    Mode(const char* e) : kind(ALL), ext(e)        {}
  };

  struct Entry {
    String name;   // raw filename — used to build path / selection
    String label;  // display label — defaults to name; TITLE style prettifies files
    String path;
    bool   isDir = false;
  };

  // Display label style for file rows (directories always show their name).
  //   NAME  — show the raw filename (default; unchanged behavior)
  //   TITLE — show prettifyTitle(name): strip extension, dashes/underscores to
  //           spaces, Title Case. e.g. "hacker-news.lua" -> "Hacker News".
  enum LabelStyle { NAME, TITLE };

  // Filename -> friendly title. Static so other screens (e.g. the remote Lua
  // browser) can reuse the exact same mapping. Keep in sync with the website
  // catalog generator's prettify().
  static String prettifyTitle(const String& filename);

  // Subtree the picker is confined to. ".." inserts only below this path and
  // never resolves above it. Default "/" = no confinement (filesystem root).
  String root = "/";

  // Show "Loading..." status bar overlay.
  static void showLoading();

  // Load a directory: flash loading, sort dirs-first then alpha, build Item
  // rows. Inserts ".." at index 0 when `dir != root`.
  //   mode           - ALL, DIRECTORY, or a file extension string like ".ir"
  //   fileSublabel   - sublabel on file rows; nullptr = none
  //                    Directory rows always get "DIR".
  // Returns populated count. Returns 0 if storage unavailable.
  // `dir` is taken by value on purpose: callers commonly pass entry(i).path,
  // which aliases _entries[]. load() overwrites _entries[] as it builds the
  // list, so a reference would be mutated mid-call (the ".." row would clobber
  // the selected dir's path). The copy keeps the input stable.
  uint8_t load(BaseScreen* host, String dir,
               Mode        mode         = {},
               const char* fileSublabel = nullptr,
               LabelStyle  style        = NAME);

  uint8_t        count()          const { return _count; }
  const Entry&   entry(uint8_t i) const { return _entries[i]; }
  Item*          items()                { return _listItems; }
  const Item*    items()          const { return _listItems; }

private:
  Entry    _entries[kCap];
  Item     _listItems[kCap];
  uint8_t  _count = 0;
};
