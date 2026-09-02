#include "utils/network/SsdpScanUtil.h"
#include "utils/network/ScanCancelUtil.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <string.h>

namespace {

const IPAddress SSDP_MCAST(239, 255, 255, 250);
constexpr uint16_t SSDP_PORT = 1900;

const char* ciStrStr(const char* haystack, const char* needle)
{
  if (!haystack || !needle || !*needle) return haystack;
  size_t nl = strlen(needle);
  for (const char* p = haystack; *p; ++p) {
    if (strncasecmp(p, needle, nl) == 0) return p;
  }
  return nullptr;
}

bool extractHeader(const char* packet, const char* header, char* out, size_t outLen)
{
  if (!packet || !header || !out || outLen == 0) return false;
  out[0] = '\0';

  const char* p = ciStrStr(packet, header);
  if (!p) return false;
  p += strlen(header);
  while (*p == ' ' || *p == '\t') ++p;

  size_t n = 0;
  while (*p && *p != '\r' && *p != '\n' && n + 1 < outLen) {
    out[n++] = *p++;
  }
  out[n] = '\0';
  return n > 0;
}

void trimAscii(char* value)
{
  if (!value || !*value) return;

  char* start = value;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
  if (start != value) memmove(value, start, strlen(start) + 1);

  size_t len = strlen(value);
  while (len > 0) {
    char c = value[len - 1];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    value[--len] = '\0';
  }
}

bool locationMatchesDevice(const char* location, const char* deviceIp)
{
  if (!location || !*location || !deviceIp || !*deviceIp) return false;

  const char* authority = nullptr;
  if (strncasecmp(location, "http://", 7) == 0) {
    authority = location + 7;
  } else if (strncasecmp(location, "https://", 8) == 0) {
    authority = location + 8;
  } else {
    return false;
  }

  const char* authorityEnd = strchr(authority, '/');
  if (!authorityEnd) authorityEnd = location + strlen(location);
  if (authority == authorityEnd) return false;

  // Reject user-info and IPv6 literals here. SSDP responders in this scanner
  // are IPv4 and LOCATION should point back to the responding device.
  for (const char* p = authority; p < authorityEnd; ++p) {
    if (*p == '@' || *p == '[' || *p == ']') return false;
  }

  const char* hostEnd = authorityEnd;
  for (const char* p = authority; p < authorityEnd; ++p) {
    if (*p == ':') {
      hostEnd = p;
      break;
    }
  }

  size_t hostLen = (size_t)(hostEnd - authority);
  size_t ipLen = strlen(deviceIp);
  return hostLen == ipLen && strncasecmp(authority, deviceIp, ipLen) == 0;
}

struct TagCapture {
  const char* openTag;
  size_t matched;
  char* value;
  size_t valueLen;
  size_t written;
  bool capturing;
  bool done;
};

void feedTag(TagCapture& tag, char c)
{
  if (tag.done) return;

  if (tag.capturing) {
    if (c == '<') {
      tag.value[tag.written] = '\0';
      trimAscii(tag.value);
      tag.done = true;
      tag.capturing = false;
      return;
    }

    if (tag.written + 1 < tag.valueLen) {
      tag.value[tag.written++] = c;
    }
    return;
  }

  if (c == tag.openTag[tag.matched]) {
    tag.matched++;
    if (tag.openTag[tag.matched] == '\0') {
      tag.matched = 0;
      tag.written = 0;
      tag.value[0] = '\0';
      tag.capturing = true;
    }
    return;
  }

  tag.matched = (c == tag.openTag[0]) ? 1 : 0;
}

void fetchFriendlyName(SsdpScanUtil::Device& dev)
{
  if (!dev.location[0] || !locationMatchesDevice(dev.location, dev.ip)) return;

  HTTPClient http;
  http.useHTTP10(true); // Avoid chunked transfer while parsing the stream.
  http.setTimeout(900);
  if (!http.begin(dev.location)) return;

  int code = http.GET();
  if (code >= 200 && code < 300 && !ScanCancelUtil::wasCancelled()) {
    WiFiClient* stream = http.getStreamPtr();
    if (stream) {
      char friendly[sizeof(dev.name)] = {};
      char deviceName[sizeof(dev.name)] = {};
      char modelName[sizeof(dev.name)] = {};

      TagCapture tags[] = {
        {"<friendlyName>", 0, friendly, sizeof(friendly), 0, false, false},
        {"<deviceName>",   0, deviceName, sizeof(deviceName), 0, false, false},
        {"<modelName>",    0, modelName, sizeof(modelName), 0, false, false},
      };

      constexpr size_t MAX_DESCRIPTION_BYTES = 8192;
      constexpr uint32_t MAX_READ_MS = 1200;
      size_t bytesRead = 0;
      uint32_t readStart = millis();

      while (bytesRead < MAX_DESCRIPTION_BYTES &&
             (uint32_t)(millis() - readStart) < MAX_READ_MS &&
             (http.connected() || stream->available() > 0)) {
        if (ScanCancelUtil::poll()) break;

        int available = stream->available();
        if (available <= 0) {
          delay(5);
          continue;
        }

        while (available-- > 0 && bytesRead < MAX_DESCRIPTION_BYTES) {
          int ch = stream->read();
          if (ch < 0) break;
          bytesRead++;

          for (auto& tag : tags) feedTag(tag, (char)ch);
          if (tags[0].done && friendly[0]) break;
        }

        // friendlyName has highest priority, so no need to read further once
        // it has been captured successfully.
        if (tags[0].done && friendly[0]) break;
      }

      const char* name = friendly[0] ? friendly
                         : deviceName[0] ? deviceName
                         : modelName[0] ? modelName
                         : nullptr;
      if (name) {
        strncpy(dev.name, name, sizeof(dev.name) - 1);
        dev.name[sizeof(dev.name) - 1] = '\0';
      }
    }
  }
  http.end();
}

} // namespace

uint8_t SsdpScanUtil::discover(const char* searchTarget,
                               Device* out,
                               uint8_t maxDevices,
                               void (*progressCb)(uint8_t))
{
  if (!searchTarget || !*searchTarget || !out || maxDevices == 0) return 0;
  if (WiFi.status() != WL_CONNECTED) return 0;

  WiFiUDP udp;
  if (!udp.begin(0)) return 0;

  char request[256];
  snprintf(request, sizeof(request),
           "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: %s\r\n"
           "\r\n",
           searchTarget);

  auto sendSearch = [&]() {
    if (!udp.beginPacket(SSDP_MCAST, SSDP_PORT)) return;
    udp.write((const uint8_t*)request, strlen(request));
    udp.endPacket();
  };

  // Match the proven CastBomb/PrinterPrank discovery strategy: multicast
  // responses are lossy, so send the query three times during the window.
  sendSearch();
  uint8_t searchesSent = 1;
  const uint8_t maxSearches = 3;
  const uint32_t resendEveryMs = 900;

  uint8_t count = 0;
  uint32_t startMs = millis();
  const uint32_t totalMs = 4500;
  uint32_t nextResend = startMs + resendEveryMs;

  while ((uint32_t)(millis() - startMs) < totalMs &&
         count < maxDevices &&
         !ScanCancelUtil::wasCancelled()) {
    if (searchesSent < maxSearches &&
        (int32_t)(millis() - nextResend) >= 0) {
      sendSearch();
      searchesSent++;
      nextResend = millis() + resendEveryMs;
    }

    if (progressCb) {
      uint32_t elapsed = millis() - startMs;
      uint8_t pct = elapsed >= totalMs
                      ? 75
                      : (uint8_t)(elapsed * 75 / totalMs);
      progressCb(pct);
    }

    int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(40);
      continue;
    }

    char packet[1024];
    int len = udp.read(packet, sizeof(packet) - 1);
    if (len <= 0) continue;
    packet[len] = '\0';

    IPAddress src = udp.remoteIP();
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
             src[0], src[1], src[2], src[3]);

    // Generic SSDP discovery is device-oriented here: a host may answer
    // ssdp:all for many service/device types, but appears once in the UI.
    bool duplicate = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (strcmp(out[i].ip, ip) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    Device& dev = out[count];
    memset(&dev, 0, sizeof(dev));

    strncpy(dev.ip, ip, sizeof(dev.ip) - 1);
    strncpy(dev.name, "SSDP Device", sizeof(dev.name) - 1);

    extractHeader(packet, "ST:",       dev.st,       sizeof(dev.st));
    extractHeader(packet, "USN:",      dev.usn,      sizeof(dev.usn));
    extractHeader(packet, "SERVER:",   dev.server,   sizeof(dev.server));
    extractHeader(packet, "LOCATION:", dev.location, sizeof(dev.location));

    count++;
  }

  udp.stop();

  // Resolve device descriptions only after multicast discovery is complete.
  // This keeps the discovery window deterministic and makes HTTP enrichment
  // bounded and cancellable without accumulating response bodies in heap.
  if (!ScanCancelUtil::wasCancelled()) {
    for (uint8_t i = 0; i < count; ++i) {
      if (ScanCancelUtil::poll()) break;
      fetchFriendlyName(out[i]);
      if (progressCb) {
        uint8_t pct = 75 + (uint8_t)(((uint16_t)(i + 1) * 25U) / count);
        progressCb(pct);
      }
    }
  }

  if (progressCb && !ScanCancelUtil::wasCancelled()) progressCb(100);
  return count;
}
