#include "KeeloqKeystore.h"
#include "core/Device.h"

KeeloqKeystore& KeeloqKeystore::instance() {
  static KeeloqKeystore inst;
  return inst;
}

void KeeloqKeystore::ensureLoaded() {
  if (_attempted) return;
  _attempted = true;
  _doLoad();
}

void KeeloqKeystore::reload() {
  _attempted = false;
  _count     = 0;
  for (size_t i = 0; i < MAX_KEYS; i++) _keys[i] = {};
  ensureLoaded();
}

size_t KeeloqKeystore::count()   { ensureLoaded(); return _count; }
bool   KeeloqKeystore::isLoaded() { return count() > 0; }

void KeeloqKeystore::_doLoad() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  if (!Uni.Storage->exists(PATH)) return;

  String content = Uni.Storage->readFile(PATH);
  if (content.length() == 0) return;

  int start = 0;
  while (start < (int)content.length() && _count < MAX_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    start = nl + 1;

    if (line.length() == 0 || line.startsWith("#")) continue;

    int sep1 = line.indexOf(';');
    int sep2 = line.indexOf(';', sep1 + 1);
    if (sep1 < 0 || sep2 < 0) continue;

    String name = line.substring(0, sep1);          name.trim();
    String hex  = line.substring(sep1 + 1, sep2);   hex.trim();
    String typeStr = line.substring(sep2 + 1);      typeStr.trim();
    if (name.length() == 0 || hex.length() == 0) continue;

    if (hex.startsWith("0x") || hex.startsWith("0X")) hex.remove(0, 2);

    _keys[_count].mf_name = name;
    _keys[_count].key     = strtoull(hex.c_str(), nullptr, 16);
    // Preserve type as-is (Bruce's keeloq_identify switch silently skips
    // anything that isn't 1=simple or 2=normal; we match that semantics in
    // KeeloqUtil::identify). type=0 entries in shared keystores ride along
    // as inactive — same behavior as Bruce.
    _keys[_count].type    = (uint8_t)typeStr.toInt();
    _count++;
  }
}
