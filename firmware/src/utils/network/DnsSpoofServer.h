#pragma once

#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include "utils/network/SharedWebServer.h"

class DnsSpoofServer {
public:
  static constexpr int MAX_RECORDS = 20;
  static constexpr const char* CONFIG_PATH = "/unigeek/web/portals/dns_config";

  struct DnsRecord {
    char domain[64];
    char path[128];
  };

  using VisitCallback = void(*)(const char* clientIP, const char* domain);
  using PostCallback  = void(*)(const char* clientIP, const char* domain, const char* data);

  bool begin(IPAddress apIP);
  void end();
  void update();  // call each frame to process DNS packets

  void setVisitCallback(VisitCallback cb) { _visitCb = cb; }
  void setPostCallback(PostCallback cb)  { _postCb = cb; }
  void setCaptivePortalPath(const char* path) { strncpy(_captivePath, path, sizeof(_captivePath) - 1); }
  void setUpstreamDns(IPAddress dns) { _upstreamDns = dns; }
  void setCaptiveIntercept(bool en) { _captiveIntercept = en; }

  const DnsRecord* records() const { return _records; }
  uint8_t recordCount() const { return _recordCount; }

private:
  IPAddress _apIP;
  bool _running = false;
  VisitCallback _visitCb = nullptr;
  PostCallback  _postCb  = nullptr;
  char _captivePath[128] = {};
  bool _captiveIntercept = false;
  IPAddress _upstreamDns;

  // Config
  DnsRecord _records[MAX_RECORDS];
  uint8_t   _recordCount = 0;
  bool _loadConfig();

  // DNS server (port 53)
  WiFiUDP _dnsUdp;
  void _processDns();
  void _forwardDns(uint8_t* buf, int len);
  bool _matchDomain(const char* query, const char* config);
  const char* _findPath(const char* domain);
  bool _isCaptiveDomain(const char* domain);

  // HTTP handling — delegates to SharedWebServer
  void _startWeb();
  void _stopWeb();
  void _handleRequest(AsyncWebServerRequest* req);
  void _serveFromPath(const char* portalPath, AsyncWebServerRequest* req);
  static const char* _mimeType(const String& path);
};
