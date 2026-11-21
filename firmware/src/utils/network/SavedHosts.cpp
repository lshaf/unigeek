#include "SavedHosts.h"
#include "core/Device.h"

namespace {

String escape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '\\')      out += "\\\\";
    else if (c == '\t') out += "\\t";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') continue;
    else if (c == '=')  out += "\\e";  // keeps key=value splitting unambiguous
    else                out += c;
  }
  return out;
}

String unescape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c != '\\' || i + 1 >= s.length()) { out += c; continue; }
    const char n = s[++i];
    if (n == 't')      out += '\t';
    else if (n == 'n') out += '\n';
    else if (n == 'e') out += '=';
    else               out += n;
  }
  return out;
}

String pathOf(const char* dir) { return String(dir) + "/hosts.txt"; }

bool storageReady() { return Uni.Storage && Uni.Storage->isAvailable(); }

void makeDirs(const char* dir) {
  // Not every storage backend creates parents.
  String path(dir);
  int from = 1;
  while (true) {
    const int slash = path.indexOf('/', from);
    Uni.Storage->makeDir((slash < 0 ? path : path.substring(0, slash)).c_str());
    if (slash < 0) break;
    from = slash + 1;
  }
}

bool write(const char* dir, const std::vector<SavedHost>& hosts) {
  if (!storageReady()) return false;
  makeDirs(dir);
  String text;
  for (const auto& h : hosts) {
    text += escape(h.name);
    for (const auto& f : h.fields) text += "\t" + escape(f.first) + "=" + escape(f.second);
    text += "\n";
  }
  return Uni.Storage->writeFile(pathOf(dir).c_str(), text.c_str());
}

}  // namespace

String SavedHost::get(const char* key, const String& fallback) const {
  for (const auto& f : fields) if (f.first == key) return f.second;
  return fallback;
}

int SavedHost::getInt(const char* key, int fallback) const {
  const String v = get(key);
  return v.length() ? v.toInt() : fallback;
}

bool SavedHost::has(const char* key) const {
  for (const auto& f : fields) if (f.first == key) return true;
  return false;
}

void SavedHost::set(const char* key, const String& value) {
  for (auto& f : fields) if (f.first == key) { f.second = value; return; }
  fields.emplace_back(String(key), value);
}

namespace SavedHosts {

std::vector<SavedHost> load(const char* dir) {
  std::vector<SavedHost> hosts;
  if (!storageReady()) return hosts;
  const String path = pathOf(dir);
  if (!Uni.Storage->exists(path.c_str())) return hosts;
  const String text = Uni.Storage->readFile(path.c_str());
  int start = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    const String line = text.substring(start, end);
    start = end + 1;
    if (line.length() == 0) continue;

    SavedHost h;
    int pos = 0;
    bool first = true;
    while (pos <= (int)line.length()) {
      int tab = line.indexOf('\t', pos);
      if (tab < 0) tab = line.length();
      const String part = line.substring(pos, tab);
      pos = tab + 1;
      if (first) { h.name = unescape(part); first = false; }
      else {
        const int eq = part.indexOf('=');
        if (eq > 0) h.fields.emplace_back(unescape(part.substring(0, eq)), unescape(part.substring(eq + 1)));
      }
      if (tab >= (int)line.length()) break;
    }
    if (h.name.length()) hosts.push_back(h);
  }
  return hosts;
}

bool find(const char* dir, const String& name, SavedHost& out) {
  for (const auto& h : load(dir)) if (h.name == name) { out = h; return true; }
  return false;
}

bool upsert(const char* dir, const SavedHost& host, const String& previousName) {
  // The entry being edited (previousName) and any other entry already using
  // the new name are replaced by `host`, in the place of the first of them.
  const std::vector<SavedHost> hosts = load(dir);
  std::vector<SavedHost> out;
  bool placed = false;
  for (const auto& h : hosts) {
    const bool replaces = h.name == host.name || (previousName.length() && h.name == previousName);
    if (!replaces) { out.push_back(h); continue; }
    if (!placed) { out.push_back(host); placed = true; }
  }
  if (!placed) out.push_back(host);
  return write(dir, out);
}

bool remove(const char* dir, const String& name) {
  std::vector<SavedHost> hosts = load(dir);
  bool removed = false;
  for (auto it = hosts.begin(); it != hosts.end();) {
    if (it->name == name) { it = hosts.erase(it); removed = true; }
    else ++it;
  }
  return removed && write(dir, hosts);
}

}  // namespace SavedHosts
