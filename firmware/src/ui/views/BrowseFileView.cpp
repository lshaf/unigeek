#include "BrowseFileView.h"
#include "core/Device.h"
#include "ui/actions/ShowStatusAction.h"

void BrowseFileView::showLoading()
{
  ShowStatusAction::show("Loading...", 0);
}

String BrowseFileView::prettifyTitle(const String& filename)
{
  // Strip the last extension (".lua", ".ir", …).
  int dot = filename.lastIndexOf('.');
  String base = (dot > 0) ? filename.substring(0, dot) : filename;

  // Split on '-'/'_'/space; Title-Case each word; join with single spaces.
  String out;
  bool   startWord = true;
  for (uint16_t i = 0; i < base.length(); i++) {
    char c = base[i];
    if (c == '-' || c == '_' || c == ' ') {
      if (out.length() > 0 && !out.endsWith(" ")) out += ' ';
      startWord = true;
      continue;
    }
    if (startWord) { c = toupper((unsigned char)c); startWord = false; }
    out += c;
  }
  out.trim();
  return out.length() > 0 ? out : filename;
}

uint8_t BrowseFileView::load(BaseScreen* host, String dir,
                              Mode mode, const char* fileSublabel,
                              LabelStyle style)
{
  _count = 0;
  showLoading();

  if (!Uni.Storage || !Uni.Storage->isAvailable()) return 0;

  // Prepend ".." → parent dir, but only when we're below `root`. The parent
  // is clamped to `root` so a picker confined to /unigeek/foo can never let
  // its ".." entry resolve into /unigeek or /. Stays in `_entries[0]`
  // regardless of sort, so the user-facing list always has Up first.
  if (dir != root && dir.length() > 0 && _count < kCap) {
    int slash = dir.lastIndexOf('/');
    String parent = (slash > 0) ? dir.substring(0, slash) : root;
    if (root.length() > 1 && !parent.startsWith(root)) parent = root;
    _entries[_count].name  = "..";
    _entries[_count].label = "..";
    _entries[_count].path  = parent;
    _entries[_count].isDir = true;
    _listItems[_count]     = { _entries[_count].label.c_str(), "Up" };
    _count++;
  }

  auto* raw = new IStorage::DirEntry[kCap];
  if (!raw) return _count;
  uint8_t n = Uni.Storage->listDir(dir.c_str(), raw, kCap);

  // Sort: dirs first, then alphabetical (case-insensitive)
  for (uint8_t i = 1; i < n; i++) {
    IStorage::DirEntry tmp = raw[i];
    int j = i - 1;
    while (j >= 0) {
      bool swap = false;
      if (tmp.isDir && !raw[j].isDir) swap = true;
      else if (tmp.isDir == raw[j].isDir &&
               strcasecmp(tmp.name.c_str(), raw[j].name.c_str()) < 0) swap = true;
      if (!swap) break;
      raw[j + 1] = raw[j];
      j--;
    }
    raw[j + 1] = tmp;
  }

  String base = (dir == "/") ? "" : dir;
  for (uint8_t i = 0; i < n && _count < kCap; i++) {
    if (mode.kind == Mode::DIRECTORY && !raw[i].isDir) continue;
    if (mode.ext && !raw[i].isDir && !raw[i].name.endsWith(mode.ext)) continue;
    _entries[_count].name  = raw[i].name;
    _entries[_count].label = (style == TITLE && !raw[i].isDir)
                               ? prettifyTitle(raw[i].name)
                               : raw[i].name;
    _entries[_count].path  = base + "/" + raw[i].name;
    _entries[_count].isDir = raw[i].isDir;
    _listItems[_count]     = { _entries[_count].label.c_str(),
                                raw[i].isDir ? "DIR" : fileSublabel };
    _count++;
  }
  delete[] raw;
  return _count;
}
