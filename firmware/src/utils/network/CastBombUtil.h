#pragma once
#include <Arduino.h>
#include <cstdint>

// Discovers DIAL-capable cast targets on the local network and pushes a
// YouTube video ID at them. Works against modern Chromecasts, Roku, and
// most smart TVs that still expose the DIAL receiver — the YouTube DIAL
// app endpoint is unauthenticated.
class CastBombUtil
{
public:
  struct Device {
    char name[32];      // friendlyName / deviceName from device description
    char ip[16];
    char appUrl[120];   // DIAL Application-URL (base, ends in /apps/ usually)
  };

  static constexpr uint8_t MAX_DEVICES = 16;

  // SSDP M-SEARCH (~3.5 s) on the current subnet. Resolves each unique
  // responder's description URL to extract the Application-URL header and
  // friendly name. Returns count written to `out`.
  static uint8_t discover(Device* out, uint8_t maxDevices,
                          void (*progressCb)(uint8_t) = nullptr);

  // POST `<appUrl>/YouTube` with body `v=<videoId>`. Returns true on 2xx.
  static bool launchYouTube(const Device& dev, const char* videoId);
};
