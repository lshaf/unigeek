#pragma once
#include <Arduino.h>
#include <cstdint>

class MdnsScanUtil
{
public:
  struct Service {
    char name[48];
    char host[64];
    char ip[16];
    char service[48];
    uint16_t port;
    char txt[128];
  };

  static constexpr uint8_t MAX_RESULTS = 16;

  // Queries a DNS-SD service type (for example "_ipp._tcp.local") over mDNS.
  // Parses PTR/SRV/TXT/A records from responses and returns discovered services.
  static uint8_t discover(const char* serviceType,
                          Service* out,
                          uint8_t maxResults,
                          void (*progressCb)(uint8_t) = nullptr);
};
