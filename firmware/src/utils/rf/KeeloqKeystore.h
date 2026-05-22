//
// KeeloqKeystore — loads manufacturer KeeLoq keys from /unigeek/mfcodes.
//
// Format (Bruce-compatible drop-in):
//   mf_name;hex_key;learning_type
//
// where learning_type is 1 (simple) or 2 (normal). Blank lines and #-prefixed
// comments are skipped.
//
// Singleton with lazy load — first count()/isLoaded()/identify() call reads
// the file once and caches in RAM. reload() forces a re-read.
//

#pragma once
#include <Arduino.h>

struct KeeloqKey {
  String   mf_name;
  uint64_t key  = 0;
  uint8_t  type = 0;
};

class KeeloqKeystore {
public:
  static constexpr size_t MAX_KEYS = 64;
  static constexpr const char* PATH = "/unigeek/mfcodes";

  static KeeloqKeystore& instance();

  // Loads from PATH on first call (idempotent).
  void ensureLoaded();

  // Force a re-read from storage.
  void reload();

  size_t count();                       // calls ensureLoaded()
  bool   isLoaded();                    // count() > 0
  const KeeloqKey& at(size_t i) const { return _keys[i]; }

private:
  KeeloqKeystore() = default;

  KeeloqKey _keys[MAX_KEYS];
  size_t    _count    = 0;
  bool      _attempted = false;

  void _doLoad();
};
