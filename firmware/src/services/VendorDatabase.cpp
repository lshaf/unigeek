#include "services/VendorDatabase.h"
#include <FS.h>
#include <cstring>
#include <cstdio>

namespace {
constexpr const char* ROOT = "/unigeek/wifi/manuf/";
uint32_t u32(const uint8_t* b) {
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
uint64_t u64(const uint8_t* b) { return uint64_t(u32(b)) | (uint64_t(u32(b + 4)) << 32); }
bool expired(uint32_t deadline) { return int32_t(millis() - deadline) >= 0; }
}

VendorDatabase::Match VendorDatabase::lookup(IStorage* sd, const uint8_t mac[6], uint32_t deadline) {
  Match result;
  // Avoid repeated SD probes on every lookup. Locally administered MACs
  // cannot reliably identify a manufacturer and need no SD access.
  if (mac[0] & 0x02) {
    result.status = Status::LocallyAdministered;
    return result;
  }
  if (!sd || !sd->isAvailable()) {
    result.status = Status::DatabaseUnavailable;
    return result;
  }
  bool openedAny = false;
  uint64_t addr = 0;
  for (int i = 0; i < 6; ++i) addr = (addr << 8) | mac[i];
  const int widths[] = {36, 28, 24};
  for (int bits : widths) {
    const uint64_t key = addr >> (48 - bits);
    for (int priority = 1; priority <= 3; ++priority) {
      if (expired(deadline)) { result.status = Status::Timeout; return result; }
      char path[80];
      snprintf(path, sizeof(path), "%sprefix_%d_p%d.bin", ROOT, bits, priority);
      fs::File file = sd->open(path, "r");
      if (!file) {
        // Missing priority partitions are skipped; a missing database is reported below.
        continue;
      }
      openedAny = true;
      uint32_t low = 0, high = file.size() / 12;
      uint32_t vendorId = 0;
      bool found = false;
      while (low < high && !expired(deadline)) {
        const uint32_t mid = low + (high - low) / 2;
        uint8_t rec[12];
        if (!file.seek(mid * 12) || file.read(rec, 12) != 12) break;
        const uint64_t candidate = u64(rec);
        if (candidate < key) low = mid + 1;
        else if (candidate > key) high = mid;
        else { vendorId = u32(rec + 8); found = true; break; }
      }
      file.close();
      if (expired(deadline)) { result.status = Status::Timeout; return result; }
      if (!found) continue;
      fs::File index = sd->open("/unigeek/wifi/manuf/vendors.idx", "r");
      if (!index || !index.seek(vendorId * 8)) {
        Serial.printf("[VendorLookup][DB] vendors.idx open/seek failed, vendorId=%lu\n", static_cast<unsigned long>(vendorId));
        if (index) index.close(); result.status = Status::DatabaseUnavailable; return result;
      }
      uint8_t entry[8];
      if (index.read(entry, 8) != 8) { index.close(); result.status = Status::DatabaseUnavailable; return result; }
      index.close();
      uint32_t offset = u32(entry), length = u32(entry + 4);
      if (expired(deadline)) { result.status = Status::Timeout; return result; }
      if (!length || length > 1024) { result.status = Status::DatabaseUnavailable; return result; }
      fs::File names = sd->open("/unigeek/wifi/manuf/vendors.txt", "r");
      if (!names || !names.seek(offset)) {
        Serial.printf("[VendorLookup][DB] vendors.txt open/seek failed, offset=%lu\n", static_cast<unsigned long>(offset));
        if (names) names.close(); result.status = Status::DatabaseUnavailable; return result;
      }
      char name[64] = {};
      size_t n = 0;
      while (n < sizeof(name) - 1 && n < length && !expired(deadline)) {
        int c = names.read();
        if (c < 0 || c == '\t' || c == '\n') break;
        name[n++] = char(c);
      }
      names.close();
      if (expired(deadline)) { result.status = Status::Timeout; return result; }
      if (!n) { result.status = Status::DatabaseUnavailable; return result; }
      memcpy(result.name, name, n + 1);
      result.found = true;
      result.status = Status::Found;
      return result;
    }
  }
  if (!openedAny) {
    Serial.println("[VendorLookup][DB] ERROR: none of the nine prefix files opened");
    result.status = Status::DatabaseUnavailable;
  }
  return result;
}
