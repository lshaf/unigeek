#pragma once

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <WiFi.h>

// Singleton HTTP server on port 80.
// Hosts the web file manager (static at /fm/, API at /) and/or a captive
// portal (at /). Enable each feature independently; both can run together.
// When neither is active, all requests return a service-unavailable page.
//
// Usage from screens (via Device singleton):
//   Uni.Server.enableFileManager();
//   Uni.Server.enableCaptive(apIP);
//   Uni.Server.disableFileManager();
class SharedWebServer {
public:
  using VisitCb = void (*)(void* ctx);
  using PostCb  = void (*)(const String& data, void* ctx);

  static SharedWebServer& instance() {
    static SharedWebServer s;
    return s;
  }

  // ─── File Manager ─────────────────────────────────────────────────────────
  // Activate file-manager routes. Returns false if WiFi/storage/files are
  // not ready (call lastError() for details). Idempotent.
  bool   enableFileManager();
  void   disableFileManager();
  bool   isFileManagerActive() const { return _fmActive; }
  // Primary URL to show the user (uses STA IP, AP IP, or mDNS as available).
  String fileManagerUrl()      const;
  String fileManagerMdnsUrl()  const { return "http://unigeek.local/"; }

  // ─── Captive Portal ────────────────────────────────────────────────────────
  // Set portal content before calling enableCaptive().
  void  setCaptiveContent(const String& html, const String& successHtml,
                          const String& basePath);
  void  setCaptiveCallbacks(VisitCb onVisit, PostCb onPost, void* ctx);
  // Start DNS wildcard + activate captive routes. Returns false on failure.
  bool  enableCaptive(IPAddress apIP);
  void  disableCaptive();
  // disableCaptive + clear stored HTML and callbacks.
  void  resetCaptive();
  bool  isCaptiveActive()  const { return _captiveActive; }
  // Call every frame while captive is active.
  void  processDns();
  int   visitCount()       const { return _visitCount; }
  int   postCount()        const { return _postCount; }

  // ─── Raw server access ────────────────────────────────────────────────────
  // Returns nullptr if server is not running.
  // Callers may add extra routes after enabling a feature — later registrations
  // for the same path take precedence over the shared handlers.
  AsyncWebServer* server()     { return _running ? &_server : nullptr; }
  bool            isRunning()  const { return _running; }
  const String&   lastError()  const { return _lastError; }

  // ─── DNS Spoof mode ───────────────────────────────────────────────────────
  // When active, GET / and onNotFound are delegated to the registered handler
  // instead of captive/FM dispatch. FM routes (/fm/*) are always protected.
  // DnsSpoofServer calls these internally — screens don't call them directly.
  using DnsSpoofCb = void (*)(AsyncWebServerRequest* req, void* ctx);
  void enableDnsSpoof();
  void disableDnsSpoof();
  void setDnsSpoofHandler(DnsSpoofCb cb, void* ctx);
  bool isDnsSpoofActive() const { return _dnsSpoofActive; }

  // Force-stop the server (use sparingly — prefer disable* calls).
  void end();

  static constexpr const char* FM_PATH     = "/unigeek/web/file_manager";

private:
  SharedWebServer() : _server(80) {}
  SharedWebServer(const SharedWebServer&)            = delete;
  SharedWebServer& operator=(const SharedWebServer&) = delete;
  static constexpr int         MAX_SESSIONS = 10;

  AsyncWebServer _server{80};
  AsyncWebServer _fmServer{8000};
  DNSServer      _dns;

  bool   _running          = false;
  bool   _routesRegistered = false;
  bool   _fmActive         = false;
  bool   _fmServerRunning  = false;
  bool   _captiveActive    = false;
  bool   _dnsSpoofActive   = false;
  String _lastError;

  DnsSpoofCb _dnsSpoofCb  = nullptr;
  void*      _dnsSpoofCtx = nullptr;

  // FM state
  int      _sessionIdx     = 0;
  String   _sessions[MAX_SESSIONS];
  fs::File _fsUpload;
  String   _uploadTempPath;

  // Captive state
  String   _portalHtml;
  String   _successHtml;
  String   _portalBasePath;
  VisitCb  _onVisit    = nullptr;
  PostCb   _onPost     = nullptr;
  void*    _ctx        = nullptr;
  int      _visitCount = 0;
  int      _postCount  = 0;

  bool _ensureRunning();
  void _registerRoutes();
  void _startFmServer();
  void _stopFmServer();
  void _sendUnavailable(AsyncWebServerRequest* req);

  // FM helpers
  bool   _fmAuth(AsyncWebServerRequest* req, bool logout = false);
  bool   _rmDir(const String& path);
  void   _handleFmCommand(AsyncWebServerRequest* req);
  String _mime(const String& path);
  String _colorHex(uint16_t c565);

  // Routing helpers
  static String _extractHost(AsyncWebServerRequest* req);
  static bool   _isFmHost(AsyncWebServerRequest* req);
  static bool   _isCaptiveDomain(const char* domain);
  void _serveCaptive(AsyncWebServerRequest* req);
  void _capturePost(AsyncWebServerRequest* req);
};
