#pragma once
#include <Arduino.h>
#include <cstdint>

class SsdpScanUtil
{
public:
  struct Device {
    char name[48];
    char ip[16];
    char st[96];
    char usn[128];
    char server[96];
    char location[200];
  };

  static constexpr uint8_t MAX_DEVICES = 24;

  // Sends SSDP M-SEARCH for `searchTarget` (for example "ssdp:all") and
  // returns unique responders by IP. When LOCATION is available, fetches the
  // device description and extracts friendlyName/modelName.
  static uint8_t discover(const char* searchTarget,
                          Device* out,
                          uint8_t maxDevices,
                          void (*progressCb)(uint8_t) = nullptr);
};
