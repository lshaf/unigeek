#pragma once

#include <Arduino.h>
#include "core/IStorage.h"

namespace MfcKeyStore {

static constexpr const char* kDictionaryDir = "/unigeek/nfc/dictionaries";
static constexpr const char* kDiscoveredDictionary = "/unigeek/nfc/dictionaries/discovered.txt";

inline bool containsKeyLine(const String& content, const String& key) {
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    if (line.equalsIgnoreCase(key)) return true;
    start = nl + 1;
  }
  return false;
}

// Add every key present in the per-UID persisted-key buffer to the global
// discovered dictionary. The input format is: "Sxx A|B 12HEXCHARS".
// Existing entries are preserved and duplicates are ignored case-insensitively.
inline void updateDiscoveredDictionary(IStorage* storage, const String& persistedKeys) {
  if (!storage || !storage->isAvailable() || persistedKeys.length() == 0) return;

  storage->makeDir(kDictionaryDir);
  String discovered = storage->readFile(kDiscoveredDictionary);
  bool changed = false;

  int start = 0;
  while (start < (int)persistedKeys.length()) {
    int nl = persistedKeys.indexOf('\n', start);
    if (nl < 0) nl = persistedKeys.length();
    String line = persistedKeys.substring(start, nl);
    line.trim();

    int sector = -1;
    char keyType = 0;
    char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sector, &keyType, hex) == 3) {
      String key(hex);
      key.toUpperCase();
      bool validHex = key.length() == 12;
      for (int i = 0; validHex && i < 12; ++i) {
        const char c = key[i];
        validHex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
      }
      if (validHex && !containsKeyLine(discovered, key)) {
        if (discovered.length() && !discovered.endsWith("\n")) discovered += '\n';
        discovered += key;
        discovered += '\n';
        changed = true;
      }
    }
    start = nl + 1;
  }

  if (changed) storage->writeFile(kDiscoveredDictionary, discovered.c_str());
}

} // namespace MfcKeyStore
