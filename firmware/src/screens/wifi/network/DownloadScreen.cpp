#include "DownloadScreen.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "utils/WebFileManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowProgressAction.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void DownloadScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available");
    Screen.setScreen(new NetworkMenuScreen());
    return;
  }
  _showMenu();
}

void DownloadScreen::onBack() {
  Screen.setScreen(new NetworkMenuScreen());
}

void DownloadScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _downloadWebPage();    break;
    case 1: _downloadSampleData(); break;
  }
}

void DownloadScreen::_showMenu() {
  _wfmVersionSub = "Not installed";
  if (Uni.Storage) {
    String sha = Uni.Storage->readFile(
      (String(WebFileManager::WEB_PATH) + "/version.txt").c_str());
    if (sha.length() == 40) _wfmVersionSub = "v" + sha.substring(0, 7);
    else if (sha.length() > 0) _wfmVersionSub = sha;
  }
  _menuItems[0] = {"Web File Manager", _wfmVersionSub.c_str()};
  _menuItems[1] = {"Firmware Sample Files"};
  setItems(_menuItems, 2);
}

// ── Download Web File Manager Page ────────────────────────

void DownloadScreen::_downloadWebPage() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Fetch latest commit SHA
  ShowProgressAction::show("Fetching version...", 0);
  String sha = "";
  http.begin(client,
    "https://api.github.com/repos/lshaf/puteros-file-manager/git/ref/heads/main");
  http.addHeader("User-Agent", "ESP32");
  if (http.GET() == HTTP_CODE_OK) {
    String body = http.getString();
    int idx = body.indexOf("\"sha\":");
    if (idx >= 0) {
      int start = idx + 6;
      while (start < (int)body.length() && (body[start] == ' ' || body[start] == '"')) start++;
      if (start + 40 <= (int)body.length()) sha = body.substring(start, start + 40);
    }
  }
  http.end();

  // Check if already up to date
  if (sha.length() == 40) {
    const String base = WebFileManager::WEB_PATH;
    String local = Uni.Storage->readFile((base + "/version.txt").c_str());
    bool filesExist = Uni.Storage->exists((base + "/index.htm").c_str()) &&
                      Uni.Storage->exists((base + "/index.css").c_str()) &&
                      Uni.Storage->exists((base + "/index.js").c_str());
    if (local == sha && filesExist) {
      ShowStatusAction::show(("Already up to date\nv" + sha.substring(0, 7)).c_str(), 1500);
      _showMenu();
      return;
    }
  }

  // Download files
  struct FileEntry { const char* url; const char* path; };
  static constexpr FileEntry kFiles[] = {
    {"https://raw.githubusercontent.com/lshaf/puteros-file-manager/main/interface/index.html",
     "/index.htm"},
    {"https://raw.githubusercontent.com/lshaf/puteros-file-manager/main/interface/index.css",
     "/index.css"},
    {"https://raw.githubusercontent.com/lshaf/puteros-file-manager/main/interface/index.js",
     "/index.js"},
  };
  static constexpr uint8_t kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);

  const String base = WebFileManager::WEB_PATH;
  Uni.Storage->makeDir(base.c_str());

  for (uint8_t i = 0; i < kFileCount; i++) {
    ShowProgressAction::show("Downloading...", 10 + i * 28);

    http.begin(client, kFiles[i].url);
    http.addHeader("User-Agent", "ESP32");
    http.addHeader("Cache-Control", "no-cache");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      ShowStatusAction::show(("Download failed (" + String(code) + ")").c_str());
      return;
    }

    String content = http.getString();
    http.end();

    String path = base + kFiles[i].path;
    if (!Uni.Storage->writeFile(path.c_str(), content.c_str())) {
      ShowStatusAction::show("Failed to save file");
      return;
    }
  }

  // Write version.txt
  ShowProgressAction::show("Saving version...", 95);
  String ver = sha.length() > 0 ? sha : "installed";
  Uni.Storage->writeFile((base + "/version.txt").c_str(), ver.c_str());

  ShowProgressAction::show("Done!", 100);
  String msg = sha.length() >= 7
    ? ("Done! v" + sha.substring(0, 7))
    : "Done!";
  ShowStatusAction::show(msg.c_str(), 1500);
  _showMenu();
}

// ── Download Sample Data ──────────────────────────────────

bool DownloadScreen::_downloadFile(const char* url, const char* path) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, url);
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String content = http.getString();
  http.end();

  // Create parent directories
  String pathStr = path;
  for (int i = 1; i < (int)pathStr.length(); i++) {
    if (pathStr[i] == '/') {
      Uni.Storage->makeDir(pathStr.substring(0, i).c_str());
    }
  }

  return Uni.Storage->writeFile(path, content.c_str());
}

void DownloadScreen::_downloadSampleData() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Download manifest
  ShowProgressAction::show("Fetching file list...", 0);
  String manifestUrl = String(REPO_BASE) + "/manifest.txt";
  http.begin(client, manifestUrl);
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    ShowStatusAction::show(("Failed (" + String(code) + ")").c_str());
    _showMenu();
    return;
  }

  String manifest = http.getString();
  http.end();

  // Count files
  int fileCount = 0;
  int pos = 0;
  while (pos < (int)manifest.length()) {
    int nl = manifest.indexOf('\n', pos);
    if (nl == -1) nl = manifest.length();
    String line = manifest.substring(pos, nl);
    line.trim();
    if (line.length() > 0) fileCount++;
    pos = nl + 1;
  }

  if (fileCount == 0) {
    ShowStatusAction::show("No files in manifest");
    _showMenu();
    return;
  }

  // Download each file
  int downloaded = 0;
  int failed = 0;
  pos = 0;
  int idx = 0;
  while (pos < (int)manifest.length()) {
    int nl = manifest.indexOf('\n', pos);
    if (nl == -1) nl = manifest.length();
    String line = manifest.substring(pos, nl);
    line.trim();
    pos = nl + 1;

    if (line.length() == 0) continue;

    idx++;
    uint8_t pct = (uint8_t)((idx * 100) / fileCount);
    ShowProgressAction::show(line.c_str(), pct);

    String url  = String(REPO_BASE) + "/" + line;
    String path = "/" + line;

    if (_downloadFile(url.c_str(), path.c_str())) {
      downloaded++;
    } else {
      failed++;
    }
  }

  String msg = String(downloaded) + " files downloaded";
  if (failed > 0) msg += "\n" + String(failed) + " failed";
  ShowStatusAction::show(msg.c_str(), 2000);
  _showMenu();
}
