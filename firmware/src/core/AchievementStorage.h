#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include "IStorage.h"

// Compact binary storage for unlocked/achieved state.
//
// File layout at /unigeek/achievements.bin  (little-endian):
//
//   offset  size  field
//   0       4     magic  = 'U','G','A','1'
//   4       4     total_exp                 (u32)
//   8       4     total_unlocked            (u32)
//   12      ..    entry stream — repeats to EOF
//
//   Each entry starts with a u16 tagged index:
//     bit 15 clear → "done" entry, index = tag
//     bit 15 set   → "counter" entry, index = tag & 0x7FFF, followed by u16 value
//
// Indices are `AchDef::idx` values from AchievementManager's catalog.
// Counters are capped at 65535 — anything higher would overflow the u16 and is
// clamped by the setter.
//
// A fresh unlock appends 2 bytes; a counter update is 4 bytes. Typical full
// catalog footprint ≈ 12 + 2·199 + 4·(active counters) ≈ 1 KB.
//
// Legacy text format at /unigeek/achievements (key=value lines) is auto-migrated
// and then deleted on first load.
//
// TODO(2-releases): drop the legacy importer. Remove:
//   - kLegacyPath + _importLegacy() + _legacyEntries plumbing below
//   - the legacy branch in load()
//   - AchievementManager::recalibrate()'s "Step 1: migrate legacy entries"
// Once removed, upgrading from any pre-binary firmware will silently wipe EXP.

class AchievementStorage
{
public:
  static constexpr uint32_t kMagic = 0x32414755;   // "UGA2" LE — counter = u16
  static constexpr const char* kBinPath    = "/unigeek/achievements.bin";
  static constexpr const char* kLegacyPath = "/unigeek/achievements";

  static AchievementStorage& getInstance() {
    static AchievementStorage instance;
    return instance;
  }

  // ── Totals ──────────────────────────────────────────────────────────────
  uint32_t totalExp() const        { return _totalExp; }
  uint32_t totalUnlocked() const   { return _totalUnlocked; }
  void     setTotalExp(uint32_t v)       { if (v != _totalExp) { _totalExp = v; _dirty = true; } }
  void     setTotalUnlocked(uint32_t v)  { if (v != _totalUnlocked) { _totalUnlocked = v; _dirty = true; } }

  // ── Done flags ──────────────────────────────────────────────────────────
  bool isDone(uint16_t idx) const {
    return std::binary_search(_done.begin(), _done.end(), idx);
  }

  // Returns true if newly added.
  bool markDone(uint16_t idx) {
    auto it = std::lower_bound(_done.begin(), _done.end(), idx);
    if (it != _done.end() && *it == idx) return false;
    _done.insert(it, idx);
    _dirty = true;
    return true;
  }

  // ── Counters ────────────────────────────────────────────────────────────
  // Stored as u16 on disk/RAM. Values > 65535 are clamped by setCounter.
  uint16_t getCounter(uint16_t idx) const {
    auto it = _counters.find(idx);
    return it != _counters.end() ? it->second : 0;
  }

  void setCounter(uint16_t idx, uint32_t v) {
    uint16_t clamped = v > 0xFFFF ? 0xFFFF : (uint16_t)v;
    auto it = _counters.find(idx);
    if (it != _counters.end() && it->second == clamped) return;
    _counters[idx] = clamped;
    _dirty = true;
  }

  // Counter no longer needed (e.g. achievement got unlocked) — free its slot.
  void dropCounter(uint16_t idx) {
    if (_counters.erase(idx)) _dirty = true;
  }

  // ── Persistence ─────────────────────────────────────────────────────────
  void load(IStorage* storage) {
    _totalExp = 0;
    _totalUnlocked = 0;
    _done.clear();
    _counters.clear();
    _dirty = false;
    if (!storage || !storage->isAvailable()) return;

    if (storage->exists(kBinPath)) {
      fs::File f = storage->open(kBinPath, "r");
      if (f) _readBinary(f);
      return;
    }

    if (storage->exists(kLegacyPath)) {
      // Only stash the parsed key/value pairs here. Converting them to the
      // new binary layout and deleting the legacy file is AchievementManager::
      // recalibrate()'s job — it happens once the real data has been written,
      // so a power cut between the two can't wipe achievements.
      _importLegacy(storage);
    }
  }

  // Called by AchievementManager::recalibrate() *after* the migrated data has
  // been flushed to the binary file — only then is it safe to drop the legacy
  // text file.
  void removeLegacyFile(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    if (storage->exists(kLegacyPath)) storage->deleteFile(kLegacyPath);
  }

  void save(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    if (!_dirty) return;
    storage->makeDir("/unigeek");

    // Estimate file size: 12B header + 2B × done + 4B × counters.
    size_t total = 12 + _done.size() * 2 + _counters.size() * 4;
    std::vector<uint8_t> buf;
    buf.reserve(total);

    _appendU32(buf, kMagic);
    _appendU32(buf, _totalExp);
    _appendU32(buf, _totalUnlocked);

    for (uint16_t idx : _done) _appendU16(buf, idx);

    for (auto& kv : _counters) {
      _appendU16(buf, kv.first | 0x8000);
      _appendU16(buf, kv.second);
    }

    fs::File f = storage->open(kBinPath, "w");
    if (!f) return;
    f.write(buf.data(), buf.size());
    f.close();
    _dirty = false;
  }

  AchievementStorage(const AchievementStorage&)            = delete;
  AchievementStorage& operator=(const AchievementStorage&) = delete;

private:
  AchievementStorage() = default;

  uint32_t                       _totalExp      = 0;
  uint32_t                       _totalUnlocked = 0;
  std::vector<uint16_t>          _done;       // sorted ascending
  std::map<uint16_t, uint16_t>   _counters;
  bool                           _dirty = false;

  // ── Binary helpers ──────────────────────────────────────────────────────
  static void _appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
  }
  static void _appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
  }
  static uint16_t _readU16(fs::File& f, bool& ok) {
    uint8_t b[2];
    if (f.read(b, 2) != 2) { ok = false; return 0; }
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
  }
  static uint32_t _readU32(fs::File& f, bool& ok) {
    uint8_t b[4];
    if (f.read(b, 4) != 4) { ok = false; return 0; }
    return (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
  }

  void _readBinary(fs::File& f) {
    bool ok = true;
    uint32_t magic   = _readU32(f, ok);
    uint32_t exp     = _readU32(f, ok);
    uint32_t unlk    = _readU32(f, ok);
    if (!ok || magic != kMagic) { f.close(); return; }
    _totalExp      = exp;
    _totalUnlocked = unlk;

    while (f.available() >= 2) {
      uint16_t tag = _readU16(f, ok);
      if (!ok) break;
      if (tag & 0x8000) {
        uint16_t idx = tag & 0x7FFF;
        uint16_t v   = _readU16(f, ok);
        if (!ok) break;
        _counters[idx] = v;
      } else {
        // keep sorted uniqueness
        if (_done.empty() || _done.back() < tag) _done.push_back(tag);
        else {
          auto it = std::lower_bound(_done.begin(), _done.end(), tag);
          if (it == _done.end() || *it != tag) _done.insert(it, tag);
        }
      }
    }
    f.close();
  }

  // ── One-shot migration from legacy key=value text file ─────────────────
  // We only have access to the full catalog via AchievementManager, and this
  // header is included *before* that definition, so we can't call into it.
  // Instead: import into an interim String→String map, then let
  // AchievementManager::recalibrate() rebuild totals and indices on the
  // first run. For the migration itself we only carry done flags and
  // numeric counters — each can be mapped via a catalog lookup done by
  // the caller right after load(). See AchievementManager::recalibrate().
  void _importLegacy(IStorage* storage) {
    _legacyEntries.clear();
    String content = storage->readFile(kLegacyPath);
    if (content.length() == 0) return;
    int start = 0;
    while (start < (int)content.length()) {
      int nl = content.indexOf('\n', start);
      if (nl < 0) nl = content.length();
      String line = content.substring(start, nl);
      line.trim();
      int sep = line.indexOf('=');
      if (sep > 0)
        _legacyEntries[line.substring(0, sep)] = line.substring(sep + 1);
      start = nl + 1;
    }
  }

public:
  // Iteration helpers used by AchievementManager::recalibrate for:
  //   - migrating legacy `ach_done_<id>` / `ach_cnt_<id>` entries once
  //   - recomputing totals from the current set of done indices
  const std::map<String, String>& legacyEntries() const { return _legacyEntries; }
  void clearLegacyEntries()                             { _legacyEntries.clear(); }

  size_t doneCount() const { return _done.size(); }
  uint16_t doneAt(size_t i) const { return _done[i]; }

private:
  std::map<String, String> _legacyEntries;   // only non-empty right after load migrated
};

#define AchStore AchievementStorage::getInstance()
