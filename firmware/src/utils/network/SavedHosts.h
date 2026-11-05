#pragma once
#include <Arduino.h>
#include <vector>

// Saved Remote Access hosts, one file per service folder:
//   <dir>/hosts.txt   one host per line: name<TAB>key=value<TAB>key=value...
// (tabs, newlines and backslashes inside names / values are escaped).
// Not "known_hosts": in SSH that name means the servers' host keys.
struct SavedHost {
  String name;
  std::vector<std::pair<String, String>> fields;

  String get(const char* key, const String& fallback = String()) const;
  int    getInt(const char* key, int fallback) const;
  bool   has(const char* key) const;
  void   set(const char* key, const String& value);
};

namespace SavedHosts {

// All hosts of a service folder, in file order (empty if none or no storage).
std::vector<SavedHost> load(const char* dir);
bool find(const char* dir, const String& name, SavedHost& out);
// Writes `host`, replacing the entry named `previousName` (a rename) or
// `host.name`; otherwise appends it.
bool upsert(const char* dir, const SavedHost& host, const String& previousName = String());
bool remove(const char* dir, const String& name);

}  // namespace SavedHosts
