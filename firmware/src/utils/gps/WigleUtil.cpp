//
// WigleUtil.cpp — Shared Wigle API utilities.
//

#include "utils/gps/WigleUtil.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "core/Device.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

// ── Token helpers ────────────────────────────────────────

String WigleUtil::readToken(IStorage* storage) {
  if (!storage) return "";
  String token = storage->readFile(TOKEN_PATH);
  token.trim();
  return token;
}

bool WigleUtil::saveToken(IStorage* storage, const String& token) {
  if (!storage) return false;
  storage->makeDir("/unigeek");
  return storage->writeFile(TOKEN_PATH, token.c_str());
}

String WigleUtil::tokenSublabel(IStorage* storage) {
  String token = readToken(storage);
  if (token.length() > 4)
    return "..." + token.substring(token.length() - 4);
  if (token.length() > 0)
    return token;
  return "No token";
}

// ── JSON helper ──────────────────────────────────────────

String WigleUtil::jsonVal(const String& json, const char* key) {
  char search[64];
  snprintf(search, sizeof(search), "\"%s\":", key);
  int pos = json.indexOf(search);
  if (pos < 0) return "";
  pos += strlen(search);
  while (pos < (int)json.length() && json[pos] == ' ') pos++;
  if (json[pos] == '"') {
    int end = json.indexOf('"', pos + 1);
    return (end > pos) ? json.substring(pos + 1, end) : "";
  }
  int end = pos;
  while (end < (int)json.length() && json[end] != ',' && json[end] != '}') end++;
  return json.substring(pos, end);
}

// ── HTTP GET helper ──────────────────────────────────────

String WigleUtil::_httpGet(const String& token, const String& path) {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.wigle.net", 443, 10000)) return "";

  client.print(
    "GET " + path + " HTTP/1.1\r\n"
    "Host: api.wigle.net\r\n"
    "Authorization: Basic " + token + "\r\n"
    "Connection: close\r\n\r\n"
  );

  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) delay(50);

  String response;
  response.reserve(2048);
  while (client.available()) {
    char tmp[256];
    int n = client.read((uint8_t*)tmp, sizeof(tmp) - 1);
    if (n > 0) { tmp[n] = '\0'; response += tmp; }
  }
  client.stop();

  int bodyStart = response.indexOf("\r\n\r\n");
  return (bodyStart > 0) ? response.substring(bodyStart + 4) : response;
}

// ── Fetch stats ──────────────────────────────────────────

uint8_t WigleUtil::fetchStats(ScrollListView::Row* rows, uint8_t maxRows) {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to internet first");
    return 0;
  }
  String token = readToken(Uni.Storage);
  if (token.length() == 0) {
    ShowStatusAction::show("Set Wigle token first");
    return 0;
  }

  ProgressView::init();
  ProgressView::progress("Fetching profile...", 30);
  String profileBody = _httpGet(token, "/api/v2/profile/user");
  String username = jsonVal(profileBody, "userid");
  if (username.length() == 0) username = jsonVal(profileBody, "userName");
  if (username.length() == 0) {
    ShowStatusAction::show("Failed to get username");
    return 0;
  }

  ProgressView::progress("Fetching stats...", 60);
  String statsBody = _httpGet(token, "/api/v2/stats/user?user=" + username);

  if (jsonVal(statsBody, "success") != "true") {
    ShowStatusAction::show("Failed to get stats");
    return 0;
  }

  uint8_t r = 0;
  if (r < maxRows) rows[r++] = {"User",           jsonVal(statsBody, "userName")};
  if (r < maxRows) rows[r++] = {"Rank",           jsonVal(statsBody, "rank")};
  if (r < maxRows) rows[r++] = {"Month Rank",     jsonVal(statsBody, "monthRank")};
  if (r < maxRows) rows[r++] = {"WiFi (GPS)",     jsonVal(statsBody, "discoveredWiFiGPS")};
  if (r < maxRows) rows[r++] = {"WiFi Total",     jsonVal(statsBody, "discoveredWiFi")};
  if (r < maxRows) rows[r++] = {"Cell (GPS)",     jsonVal(statsBody, "discoveredCellGPS")};
  if (r < maxRows) rows[r++] = {"Cell Total",     jsonVal(statsBody, "discoveredCell")};
  if (r < maxRows) rows[r++] = {"BT (GPS)",       jsonVal(statsBody, "discoveredBtGPS")};
  if (r < maxRows) rows[r++] = {"BT Total",       jsonVal(statsBody, "discoveredBt")};
  if (r < maxRows) rows[r++] = {"WiFi Locations", jsonVal(statsBody, "totalWiFiLocations")};
  if (r < maxRows) rows[r++] = {"First Upload",   jsonVal(statsBody, "first")};
  if (r < maxRows) rows[r++] = {"Last Upload",    jsonVal(statsBody, "last")};

  return r;
}

// ── List wardrive files ──────────────────────────────────

uint8_t WigleUtil::listFiles(IStorage* storage, String* names, String* labels,
                              bool* uploaded, uint8_t max) {
  uint8_t count = 0;
  if (!storage || !storage->isAvailable()) return 0;

  IStorage::DirEntry entries[MAX_FILES];
  uint8_t dirCount = storage->listDir(WARDRIVE_PATH, entries, max);

  for (uint8_t i = 0; i < dirCount && count < max; i++) {
    if (!entries[i].isDir && entries[i].name.endsWith(".csv")) {
      names[count] = entries[i].name;
      count++;
    }
  }

  // Sort descending
  for (uint8_t i = 1; i < count; i++) {
    for (uint8_t j = i; j > 0 && names[j] > names[j - 1]; j--) {
      String tmp = names[j];
      names[j] = names[j - 1];
      names[j - 1] = tmp;
    }
  }

  // Build labels
  for (uint8_t i = 0; i < count; i++) {
    uploaded[i] = names[i].endsWith("_uploaded.csv");
    if (uploaded[i])
      labels[i] = names[i].substring(0, names[i].length() - 13);
    else
      labels[i] = names[i].substring(0, names[i].length() - 4);
  }

  return count;
}

// ── Upload file ──────────────────────────────────────────

bool WigleUtil::uploadFile(IStorage* storage, const String& fileName) {
  String token = readToken(storage);

  String filePath = String(WARDRIVE_PATH) + "/" + fileName;
  if (!storage->exists(filePath.c_str())) {
    ShowStatusAction::show("File not found");
    return false;
  }

  ProgressView::init();
  ProgressView::progress("Reading file...", 10);
  fs::File f = storage->open(filePath.c_str(), FILE_READ);
  if (!f) {
    ShowStatusAction::show("Failed to open file");
    return false;
  }
  size_t fileSize = f.size();

  ProgressView::progress("Connecting to Wigle...", 20);

  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.wigle.net", 443, 10000)) {
    f.close();
    ShowStatusAction::show("Connection failed!");
    return false;
  }

  String boundary = "----UniGeek" + String(millis());
  String head = "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\n"
    "Content-Type: text/csv\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  size_t totalLen = head.length() + fileSize + tail.length();

  ProgressView::progress("Uploading to Wigle...", 30);

  client.print(
    "POST /api/v2/file/upload HTTP/1.1\r\n"
    "Host: api.wigle.net\r\n"
    "Authorization: Basic " + token + "\r\n"
    "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
    "Content-Length: " + String(totalLen) + "\r\n"
    "Connection: close\r\n\r\n"
  );
  client.print(head);

  uint8_t buf[512];
  size_t sent = 0;
  while (f.available()) {
    size_t bytesRead = f.read(buf, sizeof(buf));
    client.write(buf, bytesRead);
    sent += bytesRead;
    int pct = 30 + (int)(sent * 60 / fileSize);
    ProgressView::progress("Uploading to Wigle...", pct);
  }
  f.close();
  client.print(tail);

  ProgressView::progress("Waiting for response...", 95);

  unsigned long timeout = millis() + 15000;
  while (!client.available() && millis() < timeout) delay(50);

  String response;
  response.reserve(1024);
  while (client.available()) {
    char tmp[256];
    int n = client.read((uint8_t*)tmp, sizeof(tmp) - 1);
    if (n > 0) { tmp[n] = '\0'; response += tmp; }
  }
  client.stop();

  if (response.indexOf("\"success\":true") >= 0 || response.indexOf("200") >= 0) {
    // Mark as uploaded by renaming
    if (!fileName.endsWith("_uploaded.csv")) {
      String newName = fileName.substring(0, fileName.length() - 4) + "_uploaded.csv";
      String oldPath = String(WARDRIVE_PATH) + "/" + fileName;
      String newPath = String(WARDRIVE_PATH) + "/" + newName;
      storage->renameFile(oldPath.c_str(), newPath.c_str());
    }
    ShowStatusAction::show("Upload successful!");
    return true;
  } else if (response.indexOf("401") >= 0 || response.indexOf("\"success\":false") >= 0) {
    ShowStatusAction::show("Upload failed! Check token");
  } else {
    ShowStatusAction::show("Upload error. Check connection");
  }
  return false;
}

