#include "CctvScanUtil.h"
#include <WiFi.h>

constexpr CctvScanUtil::PortEntry CctvScanUtil::kPorts[];
constexpr const char* CctvScanUtil::kStreamPaths[];

// ── Brand keyword tables ──────────────────────────────────────────────────────

struct BrandEntry { const char* brand; const char* keywords[6]; };

static const BrandEntry kBrands[] = {
  {"Hikvision",  {"hikvision", "hikv", "hik", "ds-", "ivms", nullptr}},
  {"Dahua",      {"dahua", "dh-", "amcrest", "lorex", nullptr, nullptr}},
  {"Axis",       {"axis", "vapix", nullptr, nullptr, nullptr, nullptr}},
  {"Bosch",      {"bosch", "dinion", "flexidome", nullptr, nullptr, nullptr}},
  {"Samsung",    {"samsung", "hanwha", "wisenet", nullptr, nullptr, nullptr}},
  {"Panasonic",  {"panasonic", "i-pro", "wv-", nullptr, nullptr, nullptr}},
  {"Vivotek",    {"vivotek", "vast", nullptr, nullptr, nullptr, nullptr}},
  {"Foscam",     {"foscam", nullptr, nullptr, nullptr, nullptr, nullptr}},
  {"Reolink",    {"reolink", nullptr, nullptr, nullptr, nullptr, nullptr}},
  {"TP-Link",    {"tp-link", "tapo", nullptr, nullptr, nullptr, nullptr}},
};
static constexpr int kBrandCount = sizeof(kBrands) / sizeof(kBrands[0]);

static bool containsAny(const String& text, const char* const keywords[]) {
  for (int i = 0; keywords[i] != nullptr; i++) {
    if (text.indexOf(keywords[i]) >= 0) return true;
  }
  return false;
}

static const char* matchBrand(const String& text) {
  String low = text;
  low.toLowerCase();
  for (int i = 0; i < kBrandCount; i++) {
    if (containsAny(low, kBrands[i].keywords)) return kBrands[i].brand;
  }
  // Generic camera indicators
  if (low.indexOf("camera") >= 0 || low.indexOf("webcam") >= 0 ||
      low.indexOf("ipcam") >= 0 || low.indexOf("surveillance") >= 0) {
    return "Camera";
  }
  return nullptr;
}

// ── Port scanning ─────────────────────────────────────────────────────────────

uint8_t CctvScanUtil::scanPorts(const char* ip, Camera results[], uint8_t maxResults) {
  uint8_t count = 0;

  for (uint8_t i = 0; i < kPortCount && count < maxResults; i++) {
    yield();
    WiFiClient client;
    if (client.connect(ip, kPorts[i].port, 300)) {
      client.stop();

      strncpy(results[count].ip, ip, sizeof(results[0].ip) - 1);
      results[count].ip[sizeof(results[0].ip) - 1] = '\0';
      results[count].port = kPorts[i].port;
      strncpy(results[count].service, kPorts[i].service, sizeof(results[0].service) - 1);
      results[count].service[sizeof(results[0].service) - 1] = '\0';
      results[count].brand[0] = '\0';
      count++;
    }
  }
  return count;
}

// ── HTTP brand detection ──────────────────────────────────────────────────────

bool CctvScanUtil::detectBrand(const char* ip, uint16_t port, char* brandOut, size_t brandLen) {
  HTTPClient http;
  WiFiClient client;

  String url = "http://" + String(ip) + ":" + String(port) + "/";
  http.begin(client, url);
  http.setTimeout(3000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  static const char* hdrKeys[] = {"Server"};
  http.collectHeaders(hdrKeys, 1);

  int code = http.GET();
  if (code <= 0) { http.end(); return false; }

  // Check Server header
  String server = http.header("Server");
  if (server.length()) {
    const char* brand = matchBrand(server);
    if (brand) {
      strncpy(brandOut, brand, brandLen - 1);
      brandOut[brandLen - 1] = '\0';
      http.end();
      return true;
    }
  }

  // Check body (first 2KB)
  String body = http.getString().substring(0, 2048);
  const char* brand = matchBrand(body);
  if (brand) {
    strncpy(brandOut, brand, brandLen - 1);
    brandOut[brandLen - 1] = '\0';
    http.end();
    return true;
  }

  http.end();

  // 401 with WWW-Authenticate often indicates a camera
  if (code == 401) {
    strncpy(brandOut, "Camera", brandLen - 1);
    brandOut[brandLen - 1] = '\0';
    return true;
  }

  return false;
}

// ── RTSP probe ────────────────────────────────────────────────────────────────

bool CctvScanUtil::probeRtsp(const char* ip, uint16_t port, char* brandOut, size_t brandLen) {
  WiFiClient client;
  if (!client.connect(ip, port, 1000)) return false;

  client.printf(
    "OPTIONS * RTSP/1.0\r\n"
    "CSeq: 1\r\n"
    "User-Agent: ESP32\r\n"
    "\r\n"
  );

  unsigned long start = millis();
  String response;
  while (client.connected() && millis() - start < 1500) {
    while (client.available()) {
      response += (char)client.read();
    }
    if (response.indexOf("\r\n\r\n") >= 0) break;
    delay(10);
  }
  client.stop();

  if (response.indexOf("RTSP/1.0 200") < 0) return false;

  // Check Server header in response
  int serverIdx = response.indexOf("Server:");
  if (serverIdx >= 0) {
    int eol = response.indexOf("\r\n", serverIdx);
    String server = response.substring(serverIdx + 7, eol);
    server.trim();
    const char* brand = matchBrand(server);
    if (brand) {
      strncpy(brandOut, brand, brandLen - 1);
      brandOut[brandLen - 1] = '\0';
      return true;
    }
  }

  strncpy(brandOut, "RTSP", brandLen - 1);
  brandOut[brandLen - 1] = '\0';
  return true;
}

// ── Credential testing ────────────────────────────────────────────────────────

bool CctvScanUtil::testCredential(const char* ip, uint16_t port,
                                  const char* username, const char* password) {
  HTTPClient http;
  WiFiClient client;

  String url = "http://" + String(ip) + ":" + String(port) + "/";
  http.begin(client, url);
  http.setTimeout(3000);
  http.setAuthorization(username, password);

  int code = http.GET();
  http.end();

  return (code == 200);
}

// ── Stream finding ────────────────────────────────────────────────────────────

bool CctvScanUtil::findStream(const char* ip, uint16_t port,
                              const char* username, const char* password,
                              char* urlOut, size_t urlLen) {
  for (uint8_t i = 0; i < kStreamPathCount; i++) {
    yield();
    HTTPClient http;
    WiFiClient client;

    String url = "http://" + String(ip) + ":" + String(port) + String(kStreamPaths[i]);
    http.begin(client, url);
    http.setTimeout(3000);
    if (username && username[0]) {
      http.setAuthorization(username, password);
    }

    static const char* hdrKeys[] = {"Content-Type"};
    http.collectHeaders(hdrKeys, 1);

    int code = http.sendRequest("GET");
    if (code == 200) {
      String ct = http.header("Content-Type");
      ct.toLowerCase();
      if (ct.indexOf("multipart") >= 0 || ct.indexOf("mjpeg") >= 0 ||
          ct.indexOf("jpeg") >= 0 || ct.indexOf("image") >= 0 ||
          ct.indexOf("video") >= 0) {
        http.end();
        snprintf(urlOut, urlLen, "http://%s:%u%s", ip, port, kStreamPaths[i]);
        return true;
      }
    }
    http.end();
  }
  return false;
}
