#include "utils/network/CastBombUtil.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <string.h>

static const char* MSEARCH_REQ =
  "M-SEARCH * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "MAN: \"ssdp:discover\"\r\n"
  "MX: 2\r\n"
  "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
  "\r\n";

static const char* ci_strstr(const char* h, const char* n)
{
  size_t nl = strlen(n);
  for (; *h; h++) {
    if (strncasecmp(h, n, nl) == 0) return h;
  }
  return nullptr;
}

static String extractTag(const String& body, const char* tag)
{
  String openTag  = String("<")  + tag + ">";
  String closeTag = String("</") + tag + ">";
  int s = body.indexOf(openTag);
  if (s < 0) return "";
  s += openTag.length();
  int e = body.indexOf(closeTag, s);
  if (e < 0) return "";
  String v = body.substring(s, e);
  v.trim();
  return v;
}

uint8_t CastBombUtil::discover(Device* out, uint8_t maxDevices,
                                void (*progressCb)(uint8_t))
{
  if (!out || maxDevices == 0) return 0;
  if (WiFi.status() != WL_CONNECTED) return 0;

  WiFiUDP udp;
  if (!udp.begin(0)) return 0;

  IPAddress mcast(239, 255, 255, 250);
  if (!udp.beginPacket(mcast, 1900)) { udp.stop(); return 0; }
  udp.write((const uint8_t*)MSEARCH_REQ, strlen(MSEARCH_REQ));
  udp.endPacket();

  uint8_t  count    = 0;
  uint32_t startMs  = millis();
  const uint32_t totalMs = 3500;
  uint32_t deadline = startMs + totalMs;

  while (millis() < deadline && count < maxDevices) {
    if (progressCb) {
      uint32_t elapsed = millis() - startMs;
      uint8_t pct = (elapsed >= totalMs) ? 100 : (uint8_t)(elapsed * 100 / totalMs);
      progressCb(pct);
    }

    int sz = udp.parsePacket();
    if (sz <= 0) { delay(50); continue; }

    char buf[768];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = '\0';

    IPAddress src = udp.remoteIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", src[0], src[1], src[2], src[3]);

    bool dup = false;
    for (uint8_t j = 0; j < count; j++) {
      if (strcmp(out[j].ip, ipStr) == 0) { dup = true; break; }
    }
    if (dup) continue;

    const char* loc = ci_strstr(buf, "LOCATION:");
    if (!loc) continue;
    loc += 9;
    while (*loc == ' ' || *loc == '\t') loc++;

    char locUrl[200];
    int li = 0;
    while (li < (int)sizeof(locUrl) - 1 && *loc && *loc != '\r' && *loc != '\n') {
      locUrl[li++] = *loc++;
    }
    locUrl[li] = '\0';
    if (li == 0) continue;

    HTTPClient http;
    http.setTimeout(2000);
    if (!http.begin(locUrl)) continue;

    const char* hdrs[] = { "Application-URL" };
    http.collectHeaders(hdrs, 1);
    int code = http.GET();
    if (code != 200) { http.end(); continue; }

    String appUrl = http.header("Application-URL");
    String body   = http.getString();
    http.end();
    if (appUrl.isEmpty()) continue;

    String name = extractTag(body, "friendlyName");
    if (name.isEmpty()) name = extractTag(body, "deviceName");
    if (name.isEmpty()) name = "Cast Device";

    strncpy(out[count].ip,     ipStr,         sizeof(out[count].ip)     - 1);
    out[count].ip[sizeof(out[count].ip) - 1] = '\0';
    strncpy(out[count].name,   name.c_str(),  sizeof(out[count].name)   - 1);
    out[count].name[sizeof(out[count].name) - 1] = '\0';
    strncpy(out[count].appUrl, appUrl.c_str(),sizeof(out[count].appUrl) - 1);
    out[count].appUrl[sizeof(out[count].appUrl) - 1] = '\0';
    count++;
  }

  udp.stop();
  if (progressCb) progressCb(100);
  return count;
}

bool CastBombUtil::launchYouTube(const Device& dev, const char* videoId)
{
  if (!dev.appUrl[0] || !videoId || !*videoId) return false;

  String url = dev.appUrl;
  if (!url.endsWith("/")) url += "/";
  url += "YouTube";

  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "v=";
  body += videoId;

  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 300;
}
