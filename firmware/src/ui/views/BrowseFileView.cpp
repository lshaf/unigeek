#include "BrowseFileView.h"
#include "core/Device.h"
#include "ui/actions/ShowStatusAction.h"

void BrowseFileView::showLoading()
{
  ShowStatusAction::show("Loading...", 0);
}

uint8_t BrowseFileView::load(BaseScreen* host, const String& dir,
                              const char* ext, const char* fileSublabel)
{
  _count = 0;
  showLoading();

  if (!Uni.Storage || !Uni.Storage->isAvailable()) return 0;

  IStorage::DirEntry raw[kCap];
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
    if (ext && !raw[i].isDir && !raw[i].name.endsWith(ext)) continue;
    _entries[_count].name  = raw[i].name;
    _entries[_count].path  = base + "/" + raw[i].name;
    _entries[_count].isDir = raw[i].isDir;
    _listItems[_count]     = { _entries[_count].name.c_str(),
                                raw[i].isDir ? "DIR" : fileSublabel };
    _count++;
  }
  return _count;
}
