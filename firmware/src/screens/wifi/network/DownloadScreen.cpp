#include "DownloadScreen.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "utils/network/WebFileManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
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

void DownloadScreen::onUpdate() {
  // After a blocking HTTP fetch, keyboard FIFOs (TCA8418) or held keys can
  // produce ghost navigation events that skip through screens instantly.
  // Drain those events for 200 ms after any new list is loaded.
  if (_navReadyAt && millis() < _navReadyAt) {
    if (Uni.Nav->wasPressed()) Uni.Nav->readDirection();
    return;
  }
  _navReadyAt = 0;
  ListScreen::onUpdate();
}

void DownloadScreen::onBack() {
  if (_state == STATE_IR_CATEGORIES || _state == STATE_BADUSB_OS) {
    _showMenu();
    return;
  }
  if (_state == STATE_BADUSB_CATEGORIES) {
    _showBadUSBOSFromCache();
    return;
  }
  Screen.setScreen(new NetworkMenuScreen());
}

void DownloadScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_IR_CATEGORIES) {
    _downloadIRCategory(index);
    return;
  }
  if (_state == STATE_BADUSB_OS) {
    _showBadUSBCategoriesForOS(index);
    return;
  }
  if (_state == STATE_BADUSB_CATEGORIES) {
    _downloadBadUSBCategory(index);
    return;
  }

  switch (index) {
    case 0: _downloadWebPage();      break;
    case 1: _downloadSampleData();   break;
    case 2: _showIRCategories();     break;
    case 3: _showBadUSBOS();         break;
  }
}

void DownloadScreen::_showMenu() {
  _state = STATE_MENU;
  strcpy(_titleBuf, "Download");

  _wfmVersionSub = "Not installed";
  if (Uni.Storage) {
    String sha = Uni.Storage->readFile(
      (String(WebFileManager::WEB_PATH) + "/version.txt").c_str());
    if (sha.length() == 40) _wfmVersionSub = "v" + sha.substring(0, 7);
    else if (sha.length() > 0) _wfmVersionSub = sha;
  }
  _menuItems[0] = {"Web File Manager", _wfmVersionSub.c_str()};
  _menuItems[1] = {"Firmware Sample Files"};
  _menuItems[2] = {"Infrared Files"};
  _menuItems[3] = {"BadUSB Scripts"};
  setItems(_menuItems, 4);
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
  ProgressView::init();
  ProgressView::progress("Fetching version...", 0);
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
      ShowStatusAction::show(("Already up to date v" + sha.substring(0, 7)).c_str(), 1500);
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
    ProgressView::progress("Downloading...", 10 + i * 28);

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
  ProgressView::progress("Saving version...", 95);
  String ver = sha.length() > 0 ? sha : "installed";
  Uni.Storage->writeFile((base + "/version.txt").c_str(), ver.c_str());

  ProgressView::progress("Done!", 100);
  String msg = sha.length() >= 7
    ? ("Done! v" + sha.substring(0, 7))
    : "Done!";
  ShowStatusAction::show(msg.c_str(), 1500);
  _showMenu();
}

// ── Download Sample Data ──────────────────────────────────

bool DownloadScreen::_downloadFile(WiFiClientSecure& client, const char* url, const char* path) {
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
  ProgressView::init();
  ProgressView::progress("Fetching file list...", 0);
  String manifestUrl = String(REPO_BASE) + "/manifest/sdcard.txt";
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
    char label[32];
    snprintf(label, sizeof(label), "[%02d/%02d] Downloading...", idx, fileCount);
    ProgressView::progress(label, pct);

    String url  = String(REPO_BASE) + "/" + line;
    String path = "/" + line;

    if (_downloadFile(client, url.c_str(), path.c_str())) {
      downloaded++;
    } else {
      failed++;
    }
  }

  if (downloaded > 0) {
    int nd = Achievement.inc("wifi_download_first");
    if (nd == 1)  Achievement.unlock("wifi_download_first");
    if (nd == 10) Achievement.unlock("wifi_download_10");
  }

  String msg = String(downloaded) + " files downloaded";
  if (failed > 0) msg += "\n" + String(failed) + " failed";
  ShowStatusAction::show(msg.c_str(), 2000);
  _showMenu();
}

// ── Infrared Files ────────────────────────────────────────

void DownloadScreen::_showIRCategories() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  ProgressView::init();
  ProgressView::progress("Fetching categories...", 0);
  String url = String(REPO_BASE) + "/manifest/ir/categories.txt";
  http.begin(client, url);
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    ShowStatusAction::show(("Failed (" + String(code) + ")").c_str());
    render();
    return;
  }

  String content = http.getString();
  http.end();

  // Parse "folder|Display Name" lines
  _catCount = 0;
  int pos = 0;
  while (pos < (int)content.length() && _catCount < kMaxCategories) {
    int nl = content.indexOf('\n', pos);
    if (nl == -1) nl = content.length();
    String line = content.substring(pos, nl);
    line.trim();
    pos = nl + 1;

    if (line.length() == 0) continue;

    int sep = line.indexOf('|');
    if (sep < 0) continue;

    _catFolders[_catCount] = line.substring(0, sep);
    _catLabels[_catCount] = line.substring(sep + 1);
    _catItems[_catCount] = {_catLabels[_catCount].c_str()};
    _catCount++;
  }

  if (_catCount == 0) {
    ShowStatusAction::show("No categories found");
    render();
    return;
  }

  _state = STATE_IR_CATEGORIES;
  strcpy(_titleBuf, "Infrared Files");
  setItems(_catItems, _catCount);
}

void DownloadScreen::_downloadIRCategory(uint8_t index) {
  if (index >= _catCount) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Fetch category manifest
  ProgressView::init();
  ProgressView::progress("Fetching file list...", 0);
  String manifestUrl = String(REPO_BASE) + "/manifest/ir/cat_" + _catFolders[index] + ".txt";
  http.begin(client, manifestUrl);
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    ShowStatusAction::show(("Failed (" + String(code) + ")").c_str());
    render();
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
    ShowStatusAction::show("No files in category");
    render();
    return;
  }

  // Download each IR file from Flipper-IRDB repo
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
    char label[32];
    snprintf(label, sizeof(label), "[%d/%d] Downloading...", idx, fileCount);
    ProgressView::progress(label, pct);

    // Source: Flipper-IRDB repo, path as-is (e.g. "TVs/Samsung/Samsung_TV.ir")
    String fileUrl = String(IR_REPO_BASE) + "/" + line;

    // Extract just the filename from the path
    int lastSlash = line.lastIndexOf('/');
    String fileName = (lastSlash >= 0) ? line.substring(lastSlash + 1) : line;
    String destPath = String(IR_DL_BASE) + "/" + _catFolders[index] + "/" + fileName;

    // Create parent dirs
    for (int i = 1; i < (int)destPath.length(); i++) {
      if (destPath[i] == '/') {
        Uni.Storage->makeDir(destPath.substring(0, i).c_str());
      }
    }

    if (_downloadFile(client, fileUrl.c_str(), destPath.c_str())) {
      downloaded++;
    } else {
      failed++;
    }
  }

  if (downloaded > 0) {
    int nir = Achievement.inc("wifi_download_ir");
    if (nir == 1) Achievement.unlock("wifi_download_ir");
  }

  String msg = String(downloaded) + " files downloaded";
  if (failed > 0) msg += "\n" + String(failed) + " failed";
  ShowStatusAction::show(msg.c_str(), 2000);
  render();
}

// ── BadUSB Scripts ────────────────────────────────────────

// Convert "windows/reverse_shell" → "Windows: Reverse Shell"
static String badusbDisplayName(const String& folder) {
  String result;
  int start = 0;
  while (start <= (int)folder.length()) {
    int slash = folder.indexOf('/', start);
    if (slash < 0) slash = folder.length();
    String part = folder.substring(start, slash);
    part.replace("_", " ");
    if (part.length() > 0) {
      part[0] = toupper((unsigned char)part[0]);
      if (result.length() > 0) result += ": ";
      result += part;
    }
    start = slash + 1;
  }
  return result;
}

// Strip leading "<os>/" then format the remainder.
// "windows/recon/sysinfo" → "Recon: Sysinfo"
static String badusbCategoryName(const String& folder) {
  int slash = folder.indexOf('/');
  if (slash < 0) return badusbDisplayName(folder);
  return badusbDisplayName(folder.substring(slash + 1));
}

void DownloadScreen::_showBadUSBOS() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Fetch root manifest.txt from badusb-collection repo — lists category folders
  ProgressView::init();
  ProgressView::progress("Fetching categories...", 0);
  http.begin(client, String(BADUSB_REPO_BASE) + "/manifest.txt");
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    ShowStatusAction::show(("Failed (" + String(code) + ")").c_str());
    render();
    return;
  }

  String content = http.getString();
  http.end();

  // Cache the full folder list — used both for the OS view and the
  // per-OS category filter, so we never need to re-fetch on Back.
  _badusbAllCount = 0;
  int pos = 0;
  while (pos < (int)content.length() && _badusbAllCount < kMaxBadUSBAll) {
    int nl = content.indexOf('\n', pos);
    if (nl == -1) nl = content.length();
    String line = content.substring(pos, nl);
    line.trim();
    // Strip UTF-8 BOM if present
    if (line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
    pos = nl + 1;

    if (line.length() == 0) continue;

    _badusbAllFolders[_badusbAllCount++] = line;
  }

  _showBadUSBOSFromCache();
}

void DownloadScreen::_showBadUSBOSFromCache() {
  // Build a deduped OS list from the cached folder set.
  _badusbOSCount = 0;
  for (uint8_t i = 0; i < _badusbAllCount; i++) {
    int slash = _badusbAllFolders[i].indexOf('/');
    String os = (slash < 0)
      ? _badusbAllFolders[i]
      : _badusbAllFolders[i].substring(0, slash);

    bool exists = false;
    for (uint8_t j = 0; j < _badusbOSCount; j++) {
      if (_badusbOSFolders[j] == os) { exists = true; break; }
    }
    if (exists) continue;
    if (_badusbOSCount >= kMaxBadUSBOS) break;

    _badusbOSFolders[_badusbOSCount] = os;
    _badusbOSLabels[_badusbOSCount]  = badusbDisplayName(os);
    _badusbOSItems[_badusbOSCount]   = {_badusbOSLabels[_badusbOSCount].c_str()};
    _badusbOSCount++;
  }

  if (_badusbOSCount == 0) {
    ShowStatusAction::show("No categories found");
    render();
    return;
  }

  _state = STATE_BADUSB_OS;
  strcpy(_titleBuf, "BadUSB Scripts");
  _navReadyAt = millis() + 200;   // drain ghost-presses from HTTP wait
  setItems(_badusbOSItems, _badusbOSCount);
}

void DownloadScreen::_showBadUSBCategoriesForOS(uint8_t osIndex) {
  if (osIndex >= _badusbOSCount) return;
  _badusbSelectedOSIndex = osIndex;

  const String& os = _badusbOSFolders[osIndex];
  String osPrefix = os + "/";

  _badusbCount = 0;
  for (uint8_t i = 0; i < _badusbAllCount; i++) {
    const String& folder = _badusbAllFolders[i];
    if (folder != os && !folder.startsWith(osPrefix)) continue;
    if (_badusbCount >= kMaxBadUSBCategories) break;

    _badusbFolders[_badusbCount] = folder;
    _badusbLabels[_badusbCount]  = badusbCategoryName(folder);
    _badusbItems[_badusbCount]   = {_badusbLabels[_badusbCount].c_str()};
    _badusbCount++;
  }

  if (_badusbCount == 0) {
    ShowStatusAction::show("No categories");
    render();
    return;
  }

  _state = STATE_BADUSB_CATEGORIES;
  String t = String("BadUSB: ") + _badusbOSLabels[osIndex];
  strncpy(_titleBuf, t.c_str(), sizeof(_titleBuf) - 1);
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';
  _navReadyAt = millis() + 200;   // drain any remaining ghost-presses
  setItems(_badusbItems, _badusbCount);
}

void DownloadScreen::_downloadBadUSBCategory(uint8_t index) {
  if (index >= _badusbCount) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Each category folder in badusb-collection has its own manifest.txt
  // Lines are full relative paths: "windows/recon/windows_sysinfo.txt"
  ProgressView::init();
  ProgressView::progress("Fetching file list...", 0);
  String folder = _badusbFolders[index];
  http.begin(client, String(BADUSB_REPO_BASE) + "/" + folder + "/manifest.txt");
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    ShowStatusAction::show(("Failed (" + String(code) + ")").c_str());
    render();
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
    if (line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
    if (line.length() > 0) fileCount++;
    pos = nl + 1;
  }

  if (fileCount == 0) {
    ShowStatusAction::show("No files in category");
    render();
    return;
  }

  int downloaded = 0;
  int failed = 0;
  pos = 0;
  int idx = 0;
  while (pos < (int)manifest.length()) {
    int nl = manifest.indexOf('\n', pos);
    if (nl == -1) nl = manifest.length();
    String line = manifest.substring(pos, nl);
    line.trim();
    if (line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
    pos = nl + 1;

    if (line.length() == 0) continue;

    idx++;
    uint8_t pct = (uint8_t)((idx * 100) / fileCount);
    char label[32];
    snprintf(label, sizeof(label), "[%d/%d] Downloading...", idx, fileCount);
    ProgressView::progress(label, pct);

    // line = "windows/recon/windows_sysinfo.txt" — full path from repo root
    String fileUrl  = String(BADUSB_REPO_BASE) + "/" + line;
    String destPath = String(DUCKY_BASE) + "/" + line;

    for (int i = 1; i < (int)destPath.length(); i++) {
      if (destPath[i] == '/') Uni.Storage->makeDir(destPath.substring(0, i).c_str());
    }

    if (_downloadFile(client, fileUrl.c_str(), destPath.c_str())) {
      downloaded++;
    } else {
      failed++;
    }
  }

  if (downloaded > 0) {
    int n = Achievement.inc("wifi_download_badusb");
    if (n == 1) Achievement.unlock("wifi_download_badusb");
  }

  String msg = String(downloaded) + " files downloaded";
  if (failed > 0) msg += "\n" + String(failed) + " failed";
  ShowStatusAction::show(msg.c_str(), 2000);
  render();
}
