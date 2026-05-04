#include "utils/network/SharedWebServer.h"
#include "utils/network/FastWpaCrack.h"
#include "utils/network/WasmCrack.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include <ESPmDNS.h>

// ── Lifecycle ──────────────────────────────────────────────────────────────

bool SharedWebServer::_ensureRunning() {
  if (_running) return true;
  _registerRoutes();
  MDNS.begin("unigeek");
  _server.begin();
  _startFmServer();
  _running = true;
  return true;
}

void SharedWebServer::end() {
  _stopFmServer();
  if (!_running) return;
  if (_fsUpload) _fsUpload.close();
  _server.reset();
  _routesRegistered = false;
  _dns.stop();
  MDNS.end();
  _running         = false;
  _fmActive        = false;
  _captiveActive   = false;
  _dnsSpoofActive  = false;
  _dnsSpoofCb      = nullptr;
  _dnsSpoofCtx     = nullptr;
}

// ── DNS Spoof mode ─────────────────────────────────────────────────────────

void SharedWebServer::setDnsSpoofHandler(DnsSpoofCb cb, void* ctx) {
  _dnsSpoofCb  = cb;
  _dnsSpoofCtx = ctx;
}

void SharedWebServer::enableDnsSpoof() {
  _dnsSpoofActive = true;
  _ensureRunning();
}

void SharedWebServer::disableDnsSpoof() {
  _dnsSpoofActive = false;
  _dnsSpoofCb     = nullptr;
  _dnsSpoofCtx    = nullptr;
}

// ── File Manager ───────────────────────────────────────────────────────────

bool SharedWebServer::enableFileManager() {
  wifi_mode_t mode = WiFi.getMode();
  if (WiFi.status() != WL_CONNECTED &&
      mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    _lastError = "WiFi not connected";
    return false;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    _lastError = "No storage available";
    return false;
  }
  String idx = String(FM_PATH) + "/index.htm";
  if (!Uni.Storage->exists(idx.c_str())) {
    _lastError = "Web files not installed\nWiFi > Network > Download\n> Web File Manager";
    return false;
  }
  _fmActive = true;
  _ensureRunning();
  return true;
}

void SharedWebServer::disableFileManager() {
  _fmActive = false;
}

String SharedWebServer::fileManagerUrl() const {
  wifi_mode_t mode = WiFi.getMode();
  String ip = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
              ? WiFi.softAPIP().toString()
              : WiFi.localIP().toString();
  return "http://" + ip + ":8000/";
}

// ── Captive Portal ─────────────────────────────────────────────────────────

void SharedWebServer::setCaptiveContent(const String& html,
                                        const String& successHtml,
                                        const String& basePath) {
  _portalHtml     = html;
  _successHtml    = successHtml;
  _portalBasePath = basePath;
}

void SharedWebServer::setCaptiveCallbacks(VisitCb onVisit, PostCb onPost,
                                          void* ctx) {
  _onVisit = onVisit;
  _onPost  = onPost;
  _ctx     = ctx;
}

bool SharedWebServer::enableCaptive(IPAddress apIP) {
  _visitCount   = 0;
  _postCount    = 0;
  _captiveActive = true;
  _ensureRunning();
  _dns.stop();
  _dns.start(53, "*", apIP);
  return true;
}

void SharedWebServer::disableCaptive() {
  _captiveActive = false;
  _dns.stop();
}

void SharedWebServer::resetCaptive() {
  disableCaptive();
  _portalHtml     = "";
  _successHtml    = "";
  _portalBasePath = "";
  _onVisit        = nullptr;
  _onPost         = nullptr;
  _ctx            = nullptr;
}

void SharedWebServer::processDns() {
  _dns.processNextRequest();
}

// ── FM server (port 8000) ──────────────────────────────────────────────────

void SharedWebServer::_startFmServer() {
  if (_fmServerRunning) return;

  _fmServer.on("/", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (req->hasParam("command", true)) _handleFmCommand(req);
    else req->send(400, "text/plain", "Bad Request");
  });

  _fmServer.on("/download", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
    if (!req->hasArg("file")) { req->send(400, "text/plain", "No file specified."); return; }
    const String path = req->arg("file");
    if (!Uni.Storage->exists(path.c_str())) { req->send(404, "text/plain", "File not found."); return; }
    {
      fs::File f = Uni.Storage->open(path.c_str(), FILE_READ);
      size_t sz = f ? f.size() : 0;
      if (f) f.close();
      if (sz == 0) { req->send(200, _mime(path), ""); return; }
    }
    req->send(Uni.Storage->getFS(), path, _mime(path), true);
  });

  _fmServer.on("/upload", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
      if (!_uploadTempPath.isEmpty()) {
        String folder = req->arg("folder");
        if (!folder.isEmpty()) {
          if (!folder.startsWith("/")) folder = "/" + folder;
          if (!folder.endsWith("/"))   folder += "/";
          const int sl = _uploadTempPath.lastIndexOf('/');
          const String target = folder + _uploadTempPath.substring(sl + 1);
          if (target != _uploadTempPath) {
            Uni.Storage->makeDir(folder.substring(0, folder.length() - 1).c_str());
            Uni.Storage->renameFile(_uploadTempPath.c_str(), target.c_str());
          }
        }
        _uploadTempPath = "";
      }
      req->send(200, "text/plain", "ok.");
    },
    [this](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
      if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
      if (!index) {
        String folder = req->arg("folder");
        String path = "/";
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

  _fmServer.on("/crack.wasm", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "application/wasm", WASM_CRACK, WASM_CRACK_LEN);
  });

  _fmServer.on("/theme.css", HTTP_GET, [this](AsyncWebServerRequest* req) {
    const String css = ":root{--color:" + _colorHex(Config.getThemeColor()) + ";}";
    req->send(200, "text/css", css);
  });

  _fmServer.on("*.html", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String url = req->url();
    url = url.substring(0, url.length() - 5) + ".htm";
    String filePath = String(FM_PATH) + url;
    if (Uni.Storage && Uni.Storage->exists(filePath.c_str()))
      req->send(Uni.Storage->getFS(), filePath, "text/html");
    else
      req->send(404, "text/plain", "Not Found");
  });

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    _fmServer.serveStatic("/", Uni.Storage->getFS(),
                          (String(FM_PATH) + "/").c_str());
  }

  MDNS.addService("http", "tcp", 8000);
  _fmServer.begin();
  _fmServerRunning = true;
}

void SharedWebServer::_stopFmServer() {
  if (!_fmServerRunning) return;
  _fmServer.reset();
  _fmServerRunning = false;
}

// ── Route Registration ─────────────────────────────────────────────────────

void SharedWebServer::_registerRoutes() {
  if (_routesRegistered) return;
  _routesRegistered = true;

  // ── GET / ─────────────────────────────────────────────────────────────────
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String host = _extractHost(req);
    if (host.equalsIgnoreCase("unigeek.local")) {
      if (_fmActive && Uni.Storage && Uni.Storage->isAvailable()) {
        String idx = String(FM_PATH) + "/index.htm";
        req->send(Uni.Storage->getFS(), idx, "text/html");
      } else { _sendUnavailable(req); }
    } else if (_dnsSpoofActive && _dnsSpoofCb) {
      _dnsSpoofCb(req, _dnsSpoofCtx);
    } else if (_isCaptiveDomain(host.c_str())) {
      if (_captiveActive) { _visitCount++; if (_onVisit) _onVisit(_ctx); _serveCaptive(req); }
      else _sendUnavailable(req);
    } else {
      _sendUnavailable(req);
    }
  });

  // ── POST / ────────────────────────────────────────────────────────────────
  _server.on("/", HTTP_POST, [this](AsyncWebServerRequest* req) {
    String host = _extractHost(req);
    if (req->hasParam("command", true) && _fmActive &&
        host.equalsIgnoreCase("unigeek.local")) { _handleFmCommand(req); return; }
    if (_dnsSpoofActive && _dnsSpoofCb)                  { _dnsSpoofCb(req, _dnsSpoofCtx); return; }
    if (_isCaptiveDomain(host.c_str()) && _captiveActive) { _capturePost(req); return; }
    _sendUnavailable(req);
  });

  // ── Captive-detection path redirects ──────────────────────────────────────
  // OS probes well-known paths to detect captive portals. Redirect to / when
  // captive is active so the host-based routing above handles serving.
  auto captiveProbe = [this](AsyncWebServerRequest* req) {
    if (_captiveActive) req->redirect("/");
    else req->send(204);
  };
  _server.on("/generate_204",         HTTP_GET, captiveProbe);
  _server.on("/fwlink",               HTTP_GET, captiveProbe);
  _server.on("/hotspot-detect.html",  HTTP_GET, captiveProbe);
  _server.on("/connecttest.txt",      HTTP_GET, captiveProbe);

  // ── File download ──────────────────────────────────────────────────────────
  _server.on("/download", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_isFmHost(req) || !_fmActive) { req->send(404, "text/plain", "Not Found"); return; }
    if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
    if (!req->hasArg("file")) { req->send(400, "text/plain", "No file specified."); return; }
    const String path = req->arg("file");
    if (!Uni.Storage->exists(path.c_str())) {
      req->send(404, "text/plain", "File not found.");
      return;
    }
    {
      fs::File f = Uni.Storage->open(path.c_str(), FILE_READ);
      size_t sz = f ? f.size() : 0;
      if (f) f.close();
      if (sz == 0) { req->send(200, _mime(path), ""); return; }
    }
    req->send(Uni.Storage->getFS(), path, _mime(path), true);
  });

  // ── File upload ────────────────────────────────────────────────────────────
  _server.on("/upload", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_isFmHost(req) || !_fmActive) { req->send(404, "text/plain", "Not Found"); return; }
      if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
      if (!_uploadTempPath.isEmpty()) {
        String folder = req->arg("folder");
        if (!folder.isEmpty()) {
          if (!folder.startsWith("/")) folder = "/" + folder;
          if (!folder.endsWith("/"))   folder += "/";
          const int sl = _uploadTempPath.lastIndexOf('/');
          const String target = folder + _uploadTempPath.substring(sl + 1);
          if (target != _uploadTempPath) {
            Uni.Storage->makeDir(folder.substring(0, folder.length() - 1).c_str());
            Uni.Storage->renameFile(_uploadTempPath.c_str(), target.c_str());
          }
        }
        _uploadTempPath = "";
      }
      req->send(200, "text/plain", "ok.");
    },
    [this](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
      if (!_fmAuth(req)) { req->send(401, "text/plain", "not authenticated."); return; }
      if (!index) {
        String folder = req->arg("folder");
        String path = "/";
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

  // ── FM: WebAssembly + theme CSS ────────────────────────────────────────────
  _server.on("/crack.wasm", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_isFmHost(req) || !_fmActive) { req->send(404); return; }
    req->send(200, "application/wasm", WASM_CRACK, WASM_CRACK_LEN);
  });

  _server.on("/theme.css", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_isFmHost(req) || !_fmActive) { req->send(404); return; }
    const String css = ":root{--color:" + _colorHex(Config.getThemeColor()) + ";}";
    req->send(200, "text/css", css);
  });

  // ── FM: .html → .htm redirect ─────────────────────────────────────────────
  // Registered before serveStatic so it takes precedence.
  _server.on("*.html", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_isFmHost(req) || !_fmActive) { req->send(404); return; }
    String url = req->url();
    url = url.substring(0, url.length() - 5) + ".htm";
    String filePath = String(FM_PATH) + url;
    if (Uni.Storage->exists(filePath.c_str()))
      req->send(Uni.Storage->getFS(), filePath, "text/html");
    else
      req->send(404, "text/plain", "Not Found");
  });

  // ── Catch-all ─────────────────────────────────────────────────────────────
  _server.onNotFound([this](AsyncWebServerRequest* req) {
    const String& path = req->url();
    String host = _extractHost(req);

    if (host.equalsIgnoreCase("unigeek.local")) {
      // Serve FM asset if it exists, otherwise 404
      if (_fmActive && Uni.Storage && Uni.Storage->isAvailable()) {
        String fmFile = String(FM_PATH) + path;
        if (Uni.Storage->exists(fmFile.c_str())) {
          req->send(Uni.Storage->getFS(), fmFile, _mime(path));
          return;
        }
      }
      req->send(404, "text/plain", "Not Found");
    } else if (_dnsSpoofActive && _dnsSpoofCb) {
      _dnsSpoofCb(req, _dnsSpoofCtx);
    } else if (_isCaptiveDomain(host.c_str())) {
      if (!_captiveActive) { _sendUnavailable(req); return; }
      // Serve portal asset if it exists, otherwise redirect to portal root
      if (!_portalBasePath.isEmpty() && Uni.Storage && Uni.Storage->isAvailable()) {
        String asset = _portalBasePath + path;
        if (Uni.Storage->exists(asset.c_str())) {
          req->send(Uni.Storage->getFS(), asset, _mime(path));
          return;
        }
      }
      req->redirect("/");
    } else {
      _sendUnavailable(req);
    }
  });
}

// ── Error page ─────────────────────────────────────────────────────────────

void SharedWebServer::_sendUnavailable(AsyncWebServerRequest* req) {
  req->send(503, "text/html",
    "<html><head><meta name='viewport' content='width=device-width'>"
    "<style>body{font-family:sans-serif;text-align:center;margin:60px auto;"
    "max-width:360px;color:#ccc;background:#111}"
    "h2{color:#fff}code{background:#222;padding:2px 6px;border-radius:4px}"
    "</style></head><body>"
    "<h2>No service active</h2>"
    "<p>Enable <code>Web File Manager</code> or <code>Captive Portal</code> "
    "from the device menu first.</p>"
    "</body></html>");
}

// ── FM: command dispatcher ─────────────────────────────────────────────────

void SharedWebServer::_handleFmCommand(AsyncWebServerRequest* req) {
  const String cmd = req->getParam("command", true)->value();

  if (cmd != "sudo" && !_fmAuth(req)) {
    req->send(401, "text/plain", "not authenticated.");
    return;
  }

  if (cmd == "ls") {
    String path = req->hasParam("path", true)
                  ? req->getParam("path", true)->value() : "/";
    if (path.isEmpty()) path = "/";
    fs::File dir = Uni.Storage->open(path.c_str(), FILE_READ);
    if (!dir || !dir.isDirectory()) {
      req->send(403, "text/plain", "Not a directory.");
      return;
    }
    String out;
    while (true) {
      fs::File f = dir.openNextFile();
      if (!f) break;
      out += (f.isDirectory() ? "DIR:" : "FILE:") +
             String(f.name()) + ":" + String(f.size()) + "\n";
      f.close();
    }
    dir.close();
    req->send(200, "text/plain", out);

  } else if (cmd == "sysinfo") {
    uint64_t total = Uni.Storage ? Uni.Storage->totalBytes() : 0;
    uint64_t used  = Uni.Storage ? Uni.Storage->usedBytes()  : 0;
    String out = "UniGeek File Manager\n";
    out += "FS:" + String(total - used) + "\n";
    out += "US:" + String(used) + "\n";
    out += "TS:" + String(total) + "\n";
    req->send(200, "text/plain", out);

  } else if (cmd == "sudo") {
    const String pw     = req->hasParam("param", true) ? req->getParam("param", true)->value() : "";
    const String stored = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);
    if (pw == stored) {
      const String token = String(esp_random(), HEX);
      _sessionIdx = (_sessionIdx + 1) % MAX_SESSIONS;
      _sessions[_sessionIdx] = token;
      AsyncWebServerResponse* resp =
        req->beginResponse(200, "text/plain", "Login successful");
      resp->addHeader("Set-Cookie", "session=" + token + "; HttpOnly; Max-Age=86400");
      req->send(resp);
    } else {
      req->send(403, "text/plain", "forbidden");
    }

  } else if (cmd == "exit") {
    _fmAuth(req, true);
    AsyncWebServerResponse* resp = req->beginResponse(200, "text/plain", "Logged out");
    resp->addHeader("Set-Cookie",
      "session=; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
    req->send(resp);

  } else if (cmd == "rm") {
    const String path = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";
    if (path.isEmpty()) { req->send(400, "text/plain", "No file specified."); return; }
    if (!Uni.Storage->exists(path.c_str())) { req->send(404, "text/plain", "File not found."); return; }
    fs::File f = Uni.Storage->open(path.c_str(), FILE_READ);
    bool isDir = f.isDirectory();
    f.close();
    bool ok = isDir ? _rmDir(path) : Uni.Storage->deleteFile(path.c_str());
    req->send(ok ? 200 : 500, "text/plain",
      ok ? (isDir ? "Directory deleted." : "File deleted.") : "Failed to delete.");

  } else if (cmd == "mv") {
    const String src = req->hasParam("src", true) ? req->getParam("src", true)->value() : "";
    const String dst = req->hasParam("dst", true) ? req->getParam("dst", true)->value() : "";
    if (src.isEmpty() || dst.isEmpty()) {
      req->send(400, "text/plain", "Source or destination not specified.");
      return;
    }
    if (!Uni.Storage->exists(src.c_str())) { req->send(404, "text/plain", "Source not found."); return; }
    bool ok = Uni.Storage->renameFile(src.c_str(), dst.c_str());
    req->send(ok ? 200 : 500, "text/plain", ok ? "Moved." : "Failed to move.");

  } else if (cmd == "mkdir") {
    const String path = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";
    if (path.isEmpty()) { req->send(400, "text/plain", "No directory specified."); return; }
    bool ok = Uni.Storage->makeDir(path.c_str());
    req->send(ok ? 200 : 500, "text/plain",
      ok ? "Directory created." : "Failed to create directory.");

  } else if (cmd == "touch") {
    const String path = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";
    if (path.isEmpty()) { req->send(400, "text/plain", "No file specified."); return; }
    fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE);
    if (f) { f.close(); req->send(200, "text/plain", "File created."); }
    else   { req->send(500, "text/plain", "Failed to create file."); }

  } else if (cmd == "cat") {
    const String path = req->hasParam("path", true) ? req->getParam("path", true)->value() : "";
    if (path.isEmpty()) { req->send(400, "text/plain", "No file specified."); return; }
    if (!Uni.Storage->exists(path.c_str())) { req->send(404, "text/plain", "File not found."); return; }
    {
      fs::File f = Uni.Storage->open(path.c_str(), FILE_READ);
      size_t sz = f ? f.size() : 0;
      if (f) f.close();
      if (sz == 0) { req->send(200, "text/plain", ""); return; }
    }
    req->send(Uni.Storage->getFS(), path, "text/plain");

  } else if (cmd == "echo") {
    const String path    = req->hasParam("path", true)    ? req->getParam("path", true)->value()    : "";
    const String content = req->hasParam("content", true) ? req->getParam("content", true)->value() : "";
    if (path.isEmpty()) { req->send(400, "text/plain", "No file specified."); return; }
    fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE);
    if (!f) { req->send(500, "text/plain", "Failed to open file."); return; }
    f.print(content);
    f.close();
    req->send(200, "text/plain", "Content written.");

  } else if (cmd == "pw") {
    static constexpr const char* PW_DIR = "/unigeek/utility/passwords";
    const String param = req->hasParam("param", true) ? req->getParam("param", true)->value() : "";

    if (param == "list") {
      fs::File dir = Uni.Storage->open(PW_DIR, FILE_READ);
      if (!dir || !dir.isDirectory()) { req->send(200, "text/plain", ""); return; }
      String out;
      while (true) {
        fs::File f = dir.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) out += String(f.name()) + ":" + String(f.size()) + "\n";
        f.close();
      }
      dir.close();
      req->send(200, "text/plain", out);

    } else if (param == "get") {
      const String name = req->hasParam("name", true) ? req->getParam("name", true)->value() : "";
      if (name.isEmpty()) { req->send(400, "text/plain", "No name specified."); return; }
      String filePath = String(PW_DIR) + "/" + name;
      if (!Uni.Storage->exists(filePath.c_str())) { req->send(404, "text/plain", "Not found."); return; }
      req->send(Uni.Storage->getFS(), filePath, "text/plain");

    } else {
      req->send(400, "text/plain", "param must be list or get");
    }

  } else if (cmd == "saveCrack") {
    const String pcapPath = req->hasParam("pcap", true) ? req->getParam("pcap", true)->value() : "";
    const String pw       = req->hasParam("pw", true)   ? req->getParam("pw", true)->value()   : "";
    if (pcapPath.isEmpty() || pw.isEmpty()) {
      req->send(400, "text/plain", "pcap and pw required.");
      return;
    }
    CrackHandshake hs;
    if (!fast_pcap_parse(pcapPath.c_str(), hs)) { req->send(400, "text/plain", "invalid pcap"); return; }
    bool valid = fast_wpa2_try_password(pw.c_str(), (uint8_t)pw.length(),
                                        hs.ssid, hs.ssid_len,
                                        hs.prf_data, hs.eapol, hs.eapol_len, hs.mic);
    if (!valid) { req->send(403, "text/plain", "password does not match"); return; }
    char bssid[13];
    snprintf(bssid, sizeof(bssid), "%02X%02X%02X%02X%02X%02X",
             hs.ap[0], hs.ap[1], hs.ap[2], hs.ap[3], hs.ap[4], hs.ap[5]);
    static constexpr const char* CRACK_DIR = "/unigeek/wifi/passwords";
    char savePath[96];
    snprintf(savePath, sizeof(savePath), "%s/%.12s_%.32s.pass", CRACK_DIR, bssid, hs.ssid);
    Uni.Storage->makeDir(CRACK_DIR);
    fs::File f = Uni.Storage->open(savePath, FILE_WRITE);
    if (!f) { req->send(500, "text/plain", "write failed"); return; }
    f.print(pw);
    f.close();
    if (Achievement.inc("wifi_wfm_cracked") == 1) Achievement.unlock("wifi_wfm_cracked");
    req->send(200, "text/plain", "saved");

  } else {
    req->send(404, "text/plain", "command not found");
  }
}

// ── Captive helpers ────────────────────────────────────────────────────────

String SharedWebServer::_extractHost(AsyncWebServerRequest* req) {
  String host = req->host();
  int c = host.indexOf(':');
  return c > 0 ? host.substring(0, c) : host;
}

bool SharedWebServer::_isFmHost(AsyncWebServerRequest* req) {
  return _extractHost(req).equalsIgnoreCase("unigeek.local");
}

bool SharedWebServer::_isCaptiveDomain(const char* domain) {
  return strcasestr(domain, "captive.apple") ||
         strcasecmp(domain, "connectivitycheck.gstatic.com") == 0 ||
         strcasecmp(domain, "clients3.google.com") == 0 ||
         strcasecmp(domain, "www.msftconnecttest.com") == 0 ||
         strcasecmp(domain, "ipv6.msftncsi.com") == 0 ||
         strcasecmp(domain, "www.msftncsi.com") == 0 ||
         strcasecmp(domain, "nmcheck.gnome.org") == 0 ||
         strcasecmp(domain, "detectportal.firefox.com") == 0;
}

void SharedWebServer::_serveCaptive(AsyncWebServerRequest* req) {
  if (_portalHtml.length() > 0)
    req->send(200, "text/html", _portalHtml);
  else
    req->send(200, "text/html", "<html><body><h2>Connected</h2></body></html>");
}

void SharedWebServer::_capturePost(AsyncWebServerRequest* req) {
  String data;
  for (int i = 0; i < (int)req->params(); i++) {
    const AsyncWebParameter* p = req->getParam(i);
    if (!p->isPost()) continue;
    if (data.length() > 0) data += "\n";
    data += p->name() + "=" + p->value();
  }
  _postCount++;
  if (_onPost) _onPost(data, _ctx);
  req->send(200, "text/html",
    _successHtml.length() > 0
      ? _successHtml
      : "<html><body><h2>Connected!</h2></body></html>");
}

// ── FM helpers ─────────────────────────────────────────────────────────────

bool SharedWebServer::_fmAuth(AsyncWebServerRequest* req, bool logout) {
  if (!req->hasHeader("Cookie")) return false;
  String cookies = req->header("cookie");
  int start = cookies.indexOf("session=");
  if (start == -1) return false;
  start += 8;
  int end = cookies.indexOf(';', start);
  String token = (end == -1) ? cookies.substring(start) : cookies.substring(start, end);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (_sessions[i] == token) {
      if (logout) _sessions[i] = "";
      return true;
    }
  }
  return false;
}

bool SharedWebServer::_rmDir(const String& path) {
  fs::File dir = Uni.Storage->open(path.c_str(), FILE_READ);
  if (!dir || !dir.isDirectory()) return false;
  fs::File f = dir.openNextFile();
  while (f) {
    String fp = String(f.path());
    if (f.isDirectory()) _rmDir(fp);
    else Uni.Storage->deleteFile(fp.c_str());
    f = dir.openNextFile();
  }
  dir.close();
  return Uni.Storage->removeDir(path.c_str());
}

String SharedWebServer::_mime(const String& path) {
  if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".gif"))  return "image/gif";
  if (path.endsWith(".jpg"))  return "image/jpeg";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".xml"))  return "text/xml";
  if (path.endsWith(".pdf"))  return "application/x-pdf";
  if (path.endsWith(".zip"))  return "application/x-zip";
  if (path.endsWith(".gz"))   return "application/x-gzip";
  if (path.endsWith(".wasm")) return "application/wasm";
  return "text/plain";
}

String SharedWebServer::_colorHex(uint16_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X",
           (uint8_t)((c >> 11) & 0x1F) * 255 / 31,
           (uint8_t)((c >>  5) & 0x3F) * 255 / 63,
           (uint8_t)( c        & 0x1F) * 255 / 31);
  return String(buf);
}
