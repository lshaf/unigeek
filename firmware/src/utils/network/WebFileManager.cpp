#include "utils/network/WebFileManager.h"
#include "utils/network/FastWpaCrack.h"
#include "utils/network/WasmCrack.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"

bool WebFileManager::begin() {
  wifi_mode_t mode = WiFi.getMode();
  if (WiFi.status() != WL_CONNECTED && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    _lastError = "WiFi not connected";
    return false;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    _lastError = "No storage available";
    return false;
  }
  const String indexPath = String(WEB_PATH) + "/index.htm";
  if (!Uni.Storage->exists(indexPath.c_str())) {
    _lastError = "Web page not installed\nWiFi > Network > Download\n> Web File Manager";
    return false;
  }

  MDNS.begin("unigeek");
  _prepareServer();
  if (!_started) {
    _server.begin();
    _started = true;
  }
  return true;
}

void WebFileManager::end() {
  if (_fsUpload) _fsUpload.close();
  _server.reset();
  MDNS.end();
}

bool WebFileManager::_isAuthenticated(AsyncWebServerRequest* request, bool logout) {
  if (!request->hasHeader("Cookie")) return false;
  String cookies = request->header("cookie");
  int start = cookies.indexOf("session=");
  if (start == -1) return false;
  start += 8;
  int end = cookies.indexOf(';', start);
  String token = (end == -1) ? cookies.substring(start) : cookies.substring(start, end);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (_activeSessions[i] == token) {
      if (logout) _activeSessions[i] = "";
      return true;
    }
  }
  return false;
}

bool WebFileManager::_removeDirectory(const String& path) {
  fs::File dir = Uni.Storage->open(path.c_str(), FILE_READ);
  if (!dir || !dir.isDirectory()) return false;
  fs::File f = dir.openNextFile();
  while (f) {
    String fp = String(f.path());
    if (f.isDirectory()) _removeDirectory(fp);
    else Uni.Storage->deleteFile(fp.c_str());
    f = dir.openNextFile();
  }
  dir.close();
  return Uni.Storage->removeDir(path.c_str());
}

String WebFileManager::_getContentType(const String& filename) {
  if (filename.endsWith(".htm"))  return "text/html";
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css"))  return "text/css";
  if (filename.endsWith(".js"))   return "application/javascript";
  if (filename.endsWith(".png"))  return "image/png";
  if (filename.endsWith(".gif"))  return "image/gif";
  if (filename.endsWith(".jpg"))  return "image/jpeg";
  if (filename.endsWith(".ico"))  return "image/x-icon";
  if (filename.endsWith(".xml"))  return "text/xml";
  if (filename.endsWith(".pdf"))  return "application/x-pdf";
  if (filename.endsWith(".zip"))  return "application/x-zip";
  if (filename.endsWith(".gz"))   return "application/x-gzip";
  return "text/plain";
}

String WebFileManager::_color565ToWebHex(uint16_t color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X",
           (uint8_t)((color >> 11) & 0x1F) * 255 / 31,
           (uint8_t)((color >> 5)  & 0x3F) * 255 / 63,
           (uint8_t)(color & 0x1F)          * 255 / 31);
  return String(buf);
}

void WebFileManager::_prepareServer() {
  _server.on("/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!_isAuthenticated(request)) {
      request->send(401, "text/plain", "not authenticated.");
      return;
    }
    if (!request->hasArg("file")) {
      request->send(400, "text/plain", "No file specified.");
      return;
    }
    const String path = request->arg("file");
    if (!Uni.Storage->exists(path.c_str())) {
      request->send(404, "text/plain", "File not found.");
      return;
    }
    request->send(Uni.Storage->getFS(), path, _getContentType(path), true);
  });

  _server.on("/upload", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      if (!_isAuthenticated(request)) {
        request->send(401, "text/plain", "not authenticated.");
        return;
      }
      // By the time the completion handler runs, all multipart fields are
      // parsed — including "folder" even if it arrived after the file data.
      // Move the file from its temp path to the requested folder if needed.
      if (!_uploadTempPath.isEmpty()) {
        String path = request->arg("folder");
        if (!path.isEmpty()) {
          if (!path.startsWith("/")) path = "/" + path;
          if (!path.endsWith("/"))   path += "/";
          const int sl = _uploadTempPath.lastIndexOf('/');
          const String target = path + _uploadTempPath.substring(sl + 1);
          if (target != _uploadTempPath) {
            Uni.Storage->makeDir(path.substring(0, path.length() - 1).c_str());
            Uni.Storage->renameFile(_uploadTempPath.c_str(), target.c_str());
          }
        }
        _uploadTempPath = "";
      }
      request->send(200, "text/plain", "ok.");
    },
    [this](AsyncWebServerRequest* request, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
      if (!_isAuthenticated(request)) {
        request->send(401, "text/plain", "not authenticated.");
        return;
      }
      if (!index) {
        // "folder" may not be in _params yet if it arrives after the file in
        // the multipart stream. Write to root first; completion handler moves
        // the file to the correct folder once all params are parsed.
        String path = "/";
        const String folder = request->arg("folder");
        if (!folder.isEmpty()) {
          path = folder;
          if (!path.startsWith("/")) path = "/" + path;
          if (!path.endsWith("/"))   path += "/";
          Uni.Storage->makeDir(path.substring(0, path.length() - 1).c_str());
        }
        _uploadTempPath = path + filename;
        _fsUpload = Uni.Storage->open(_uploadTempPath.c_str(), FILE_WRITE);
      }
      if (len && _fsUpload) _fsUpload.write(data, len);
      if (final && _fsUpload) _fsUpload.close();
    }
  );

  const String password = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);
  _server.on("/", HTTP_POST, [this, password](AsyncWebServerRequest* request) {
    if (!request->hasParam("command", true)) {
      request->send(404, "text/plain", "404");
      return;
    }
    const String command = request->getParam("command", true)->value();
    if (command != "sudo" && !_isAuthenticated(request)) {
      request->send(401, "text/plain", "not authenticated.");
      return;
    }

    if (command == "ls") {
      String currentPath = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "/";
      if (currentPath == "") currentPath = "/";
      fs::File dir = Uni.Storage->open(currentPath.c_str(), FILE_READ);
      if (!dir || !dir.isDirectory()) {
        request->send(403, "text/plain", "Not a directory.");
        return;
      }
      String resp = "";
      while (true) {
        fs::File f = dir.openNextFile();
        if (!f) break;
        resp += (f.isDirectory() ? "DIR:" : "FILE:") +
                String(f.name()) + ":" + String(f.size()) + "\n";
        f.close();
      }
      dir.close();
      request->send(200, "text/plain", resp);

    } else if (command == "sysinfo") {
      uint64_t total = Uni.Storage ? Uni.Storage->totalBytes() : 0;
      uint64_t used  = Uni.Storage ? Uni.Storage->usedBytes()  : 0;
      String resp = "UniGeek File Manager\n";
      resp += "FS:" + String(total - used) + "\n";
      resp += "US:" + String(used) + "\n";
      resp += "TS:" + String(total) + "\n";
      request->send(200, "text/plain", resp);

    } else if (command == "sudo") {
      const String pw = request->hasParam("param", true)
        ? request->getParam("param", true)->value() : "";
      if (pw == password) {
        const String token = String(esp_random(), HEX);
        _sessionCounter = (_sessionCounter + 1) % MAX_SESSIONS;
        _activeSessions[_sessionCounter] = token;
        AsyncWebServerResponse* resp =
          request->beginResponse(200, "text/plain", "Login successful");
        resp->addHeader("Set-Cookie", "session=" + token + "; HttpOnly; Max-Age=86400");
        request->send(resp);
      } else {
        request->send(403, "text/plain", "forbidden");
      }

    } else if (command == "exit") {
      _isAuthenticated(request, true);
      AsyncWebServerResponse* resp =
        request->beginResponse(200, "text/plain", "Logged out");
      resp->addHeader("Set-Cookie",
        "session=; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
      request->send(resp);

    } else if (command == "rm") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      if (!Uni.Storage->exists(path.c_str())) { request->send(404, "text/plain", "File not found."); return; }
      fs::File f = Uni.Storage->open(path.c_str(), FILE_READ);
      bool isDir = f.isDirectory();
      f.close();
      bool ok = isDir ? _removeDirectory(path) : Uni.Storage->deleteFile(path.c_str());
      request->send(ok ? 200 : 500, "text/plain",
        ok ? (isDir ? "Directory deleted." : "File deleted.") : "Failed to delete.");

    } else if (command == "mv") {
      const String src = request->hasParam("src", true)
        ? request->getParam("src", true)->value() : "";
      const String dst = request->hasParam("dst", true)
        ? request->getParam("dst", true)->value() : "";
      if (src == "" || dst == "") {
        request->send(400, "text/plain", "Source or destination not specified.");
        return;
      }
      if (!Uni.Storage->exists(src.c_str())) { request->send(404, "text/plain", "Source not found."); return; }
      bool ok = Uni.Storage->renameFile(src.c_str(), dst.c_str());
      request->send(ok ? 200 : 500, "text/plain", ok ? "Moved." : "Failed to move.");

    } else if (command == "mkdir") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No directory specified."); return; }
      bool ok = Uni.Storage->makeDir(path.c_str());
      request->send(ok ? 200 : 500, "text/plain",
        ok ? "Directory created." : "Failed to create directory.");

    } else if (command == "touch") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE);
      if (f) { f.close(); request->send(200, "text/plain", "File created."); }
      else   { request->send(500, "text/plain", "Failed to create file."); }

    } else if (command == "cat") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      if (!Uni.Storage->exists(path.c_str())) { request->send(404, "text/plain", "File not found."); return; }
      request->send(Uni.Storage->getFS(), path, "text/plain");

    } else if (command == "echo") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      const String content = request->hasParam("content", true)
        ? request->getParam("content", true)->value() : "";
      if (path == "") {
        request->send(400, "text/plain", "No file specified.");
        return;
      }
      fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE);
      if (!f) { request->send(500, "text/plain", "Failed to open file."); return; }
      f.print(content);
      f.close();
      request->send(200, "text/plain", "Content written.");

    } else if (command == "pw") {
      const String param = request->hasParam("param", true)
        ? request->getParam("param", true)->value() : "";
      static const char* pwDir = "/unigeek/utility/passwords";

      if (param == "list") {
        fs::File dir = Uni.Storage->open(pwDir, FILE_READ);
        if (!dir || !dir.isDirectory()) {
          request->send(200, "text/plain", "");
          return;
        }
        String resp = "";
        while (true) {
          fs::File f = dir.openNextFile();
          if (!f) break;
          if (!f.isDirectory()) {
            resp += String(f.name()) + ":" + String(f.size()) + "\n";
          }
          f.close();
        }
        dir.close();
        request->send(200, "text/plain", resp);

      } else if (param == "get") {
        const String name = request->hasParam("name", true)
          ? request->getParam("name", true)->value() : "";
        if (name.isEmpty()) { request->send(400, "text/plain", "No name specified."); return; }
        String filePath = String(pwDir) + "/" + name;
        if (!Uni.Storage->exists(filePath.c_str())) {
          request->send(404, "text/plain", "Not found.");
          return;
        }
        request->send(Uni.Storage->getFS(), filePath, "text/plain");

      } else {
        request->send(400, "text/plain", "param must be list or get");
      }

    } else if (command == "saveCrack") {
      const String pcapPath = request->hasParam("pcap", true)
        ? request->getParam("pcap", true)->value() : "";
      const String pw = request->hasParam("pw", true)
        ? request->getParam("pw", true)->value() : "";
      if (pcapPath.isEmpty() || pw.isEmpty()) {
        request->send(400, "text/plain", "pcap and pw required.");
        return;
      }
      CrackHandshake hs;
      if (!fast_pcap_parse(pcapPath.c_str(), hs)) {
        request->send(400, "text/plain", "invalid pcap");
        return;
      }
      bool valid = fast_wpa2_try_password(pw.c_str(), (uint8_t)pw.length(),
                                          hs.ssid, hs.ssid_len,
                                          hs.prf_data, hs.eapol,
                                          hs.eapol_len, hs.mic);
      if (!valid) {
        request->send(403, "text/plain", "password does not match");
        return;
      }
      char bssid[13];
      snprintf(bssid, sizeof(bssid), "%02X%02X%02X%02X%02X%02X",
               hs.ap[0], hs.ap[1], hs.ap[2], hs.ap[3], hs.ap[4], hs.ap[5]);
      static const char* crackedDir = "/unigeek/wifi/passwords";
      char savePath[96];
      snprintf(savePath, sizeof(savePath), "%s/%.12s_%.32s.pass", crackedDir, bssid, hs.ssid);
      Uni.Storage->makeDir(crackedDir);
      fs::File f = Uni.Storage->open(savePath, FILE_WRITE);
      if (!f) { request->send(500, "text/plain", "write failed"); return; }
      f.print(pw);
      f.close();
      if (Achievement.inc("wifi_wfm_cracked") == 1) Achievement.unlock("wifi_wfm_cracked");
      request->send(200, "text/plain", "saved");

    } else {
      request->send(404, "text/plain", "command not found");
    }
  });

  _server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404");
  });

  _server.on("/crack.wasm", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "application/wasm", WASM_CRACK, WASM_CRACK_LEN);
  });

  _server.on("/theme.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
    const String css = ":root{--color:" + _color565ToWebHex(Config.getThemeColor()) + ";}";
    AsyncWebServerResponse* resp = request->beginResponse(200, "text/css", css);
    request->send(resp);
  });

  // Map .html requests to .htm files
  _server.on("*.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String url = request->url();
    url = url.substring(0, url.length() - 5) + ".htm";
    String filePath = String(WEB_PATH) + url;
    if (Uni.Storage->exists(filePath.c_str())) {
      request->send(Uni.Storage->getFS(), filePath, "text/html");
    } else {
      request->send(404, "text/plain", "Not Found");
    }
  });

  _server.serveStatic("/", Uni.Storage->getFS(), (String(WEB_PATH) + "/").c_str());
}
