#pragma once
#include <Arduino.h>
#include "core/IStorage.h"

// SD-only lookup, independent of any Wi-Fi screen.
class VendorDatabase {
public:
  enum class Status : uint8_t { Found, NotFound, Timeout, DatabaseUnavailable, LocallyAdministered };
  struct Match { bool found = false; Status status = Status::NotFound; char name[64] = {}; };
  static Match lookup(IStorage* sd, const uint8_t mac[6], uint32_t deadline);
};
