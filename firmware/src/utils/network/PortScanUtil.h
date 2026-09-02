#pragma once

#include <WiFiClient.h>
#include <ctype.h>
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

// TCP port scanner for a target IP (local or internet).
class PortScanUtil {
public:
  struct Result {
    char label[24];   // "ip:port"
    char service[64]; // service name or detected banner/product
  };

  struct PortEntry { uint16_t port; const char* service; };

  static constexpr uint8_t MAX_RESULTS = 80;
  static constexpr uint8_t MAX_CUSTOM_PORTS = 64;

  static const PortEntry* commonPorts(uint8_t& count) {
    static constexpr PortEntry kPorts[] = {
      {20,    "FTP Data"},       {21,    "FTP"},            {22,    "SSH"},
      {23,    "Telnet"},         {25,    "SMTP"},           {53,    "DNS"},
      {67,    "DHCP"},           {68,    "DHCP"},           {69,    "TFTP"},
      {80,    "HTTP"},           {110,   "POP3"},           {123,   "NTP"},
      {135,   "MS-RPC"},        {137,   "NetBIOS"},        {139,   "NetBIOS"},
      {143,   "IMAP"},          {161,   "SNMP"},           {162,   "SNMP Trap"},
      {389,   "LDAP"},          {443,   "HTTPS"},          {445,   "SMB"},
      {465,   "SMTPS"},         {514,   "Syslog"},         {554,   "RTSP"},
      {587,   "SMTP Submit"},   {631,   "IPP"},            {636,   "LDAPS"},
      {873,   "rsync"},         {993,   "IMAPS"},          {995,   "POP3S"},
      {1194,  "OpenVPN"},       {1433,  "MSSQL"},          {1521,  "Oracle"},
      {1723,  "PPTP"},          {2049,  "NFS"},            {2181,  "Zookeeper"},
      {2375,  "Docker"},        {2376,  "DockerTLS"},      {3306,  "MySQL"},
      {3389,  "RDP"},           {3690,  "SVN"},            {5000,  "UPnP"},
      {5432,  "PostgreSQL"},    {5555,  "ADB"},            {5900,  "VNC"},
      {5985,  "WinRM HTTP"},    {5986,  "WinRM HTTPS"},    {6379,  "Redis"},
      {8000,  "HTTP Alt"},      {8080,  "HTTP Proxy"},     {8443,  "HTTPS Alt"},
      {8888,  "HTTP Alt"},      {9000,  "SonarQube"},      {9100,  "JetDirect"},
      {9200,  "Elasticsearch"}, {10000, "Webmin"},         {11211, "Memcached"},
      {27017, "MongoDB"},
    };
    count = sizeof(kPorts) / sizeof(kPorts[0]);
    return kPorts;
  }

  static const char* serviceForPort(uint16_t port) {
    uint8_t count = 0;
    const PortEntry* ports = commonPorts(count);
    for (uint8_t i = 0; i < count; i++) {
      if (ports[i].port == port) return ports[i].service;
    }
    return "Unknown";
  }

  // Backward-compatible common-port scan.
  static uint8_t scan(const char* targetIp, Result results[], uint8_t maxResults,
                      const char* msg = "Port scanning...", bool serviceScan = false, bool patient = false) {
    uint8_t count = 0;
    const PortEntry* ports = commonPorts(count);
    uint16_t list[MAX_CUSTOM_PORTS];
    uint8_t n = count > MAX_CUSTOM_PORTS ? MAX_CUSTOM_PORTS : count;
    for (uint8_t i = 0; i < n; i++) list[i] = ports[i].port;
    return scanPorts(targetIp, list, n, results, maxResults, msg, serviceScan, patient);
  }

  static uint8_t scanRange(const char* targetIp, uint16_t startPort, uint16_t endPort,
                           Result results[], uint8_t maxResults,
                           const char* msg = "Port scanning...", bool serviceScan = false, bool patient = false) {
    if (startPort == 0 || endPort == 0 || startPort > endPort) return 0;

    uint8_t found = 0;
    uint32_t total = (uint32_t)endPort - startPort + 1;
    uint8_t lastPct = 255;

    for (uint32_t port = startPort; port <= endPort && found < maxResults; port++) {
      if (ScanCancelUtil::poll()) break;
      uint8_t pct = (uint8_t)(((port - startPort) * 100UL) / total);
      if (pct != lastPct) {
        ProgressView::progress(msg, pct);
        lastPct = pct;
      }
      yield();
      if (_scanOne(targetIp, (uint16_t)port, results[found], serviceScan, patient)) found++;
    }

    if (!ScanCancelUtil::wasCancelled()) ProgressView::progress(msg, 100);
    return found;
  }

  static uint8_t scanPorts(const char* targetIp, const uint16_t ports[], uint16_t portCount,
                           Result results[], uint8_t maxResults,
                           const char* msg = "Port scanning...", bool serviceScan = false, bool patient = false) {
    if (!targetIp || !ports || !results || portCount == 0 || maxResults == 0) return 0;

    uint8_t found = 0;
    uint8_t lastPct = 255;
    for (uint16_t i = 0; i < portCount && found < maxResults; i++) {
      if (ScanCancelUtil::poll()) break;
      uint8_t pct = (uint8_t)((uint32_t)i * 100UL / portCount);
      if (pct != lastPct) {
        ProgressView::progress(msg, pct);
        lastPct = pct;
      }
      yield();
      if (_scanOne(targetIp, ports[i], results[found], serviceScan, patient)) found++;
    }

    if (!ScanCancelUtil::wasCancelled()) ProgressView::progress(msg, 100);
    return found;
  }

private:
  static bool _scanOne(const char* targetIp, uint16_t port, Result& out,
                       bool serviceScan, bool patient) {
    WiFiClient client;
    const uint32_t connectTimeout = patient ? 450 : 300;
    if (!client.connect(targetIp, port, connectTimeout)) return false;

    snprintf(out.label, sizeof(out.label), "%s:%u", targetIp, port);
    const char* fallback = serviceForPort(port);
    strncpy(out.service, fallback, sizeof(out.service) - 1);
    out.service[sizeof(out.service) - 1] = '\0';

    if (serviceScan) _detectService(client, targetIp, port, fallback, out.service, sizeof(out.service));
    client.stop();
    return true;
  }

  static void _detectService(WiFiClient& client, const char* targetIp, uint16_t port,
                             const char* fallback, char* out, size_t outLen) {
    if (!out || outLen == 0) return;

    // Protocols such as SSH/FTP/SMTP/POP3/IMAP often send a banner immediately.
    String banner = _readLine(client, 220);

    // HTTP-family services usually need a request before replying.
    if (banner.length() == 0 && (_isHttpPort(port) || strcmp(fallback, "Unknown") == 0)) {
      client.print("HEAD / HTTP/1.0\r\nHost: ");
      client.print(targetIp);
      client.print("\r\nConnection: close\r\n\r\n");
      banner = _readHttpIdentity(client, 450);
    } else if (banner.length() == 0 && port == 554) {
      client.print("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n");
      banner = _readLine(client, 350);
    }

    _sanitize(banner);
    if (banner.length() == 0) {
      strncpy(out, fallback, outLen - 1);
      out[outLen - 1] = '\0';
      return;
    }

    // Keep the known service name as context when the banner itself does not include it.
    String identity;
    if (strcmp(fallback, "Unknown") != 0 && banner.indexOf(fallback) < 0)
      identity = String(fallback) + " | " + banner;
    else
      identity = banner;

    identity.toCharArray(out, outLen);
  }

  static bool _isHttpPort(uint16_t port) {
    return port == 80 || port == 631 || port == 2375 || port == 5985 ||
           port == 8000 || port == 8080 || port == 8888 || port == 9000 ||
           port == 9200 || port == 10000;
  }

  static String _readLine(WiFiClient& client, uint32_t timeoutMs) {
    String s;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && s.length() < 80) {
      while (client.available() && s.length() < 80) {
        char c = (char)client.read();
        if (c == '\n') return s;
        if (c != '\r') s += c;
      }
      delay(5);
    }
    return s;
  }

  static String _readHttpIdentity(WiFiClient& client, uint32_t timeoutMs) {
    String firstLine;
    String server;
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
      if (!client.available()) { delay(5); continue; }
      String line = client.readStringUntil('\n');
      line.trim();
      if (firstLine.length() == 0) firstLine = line;
      if (line.startsWith("Server:") || line.startsWith("server:")) {
        server = line.substring(7);
        server.trim();
      }
      if (line.length() == 0) break;
    }
    if (server.length() > 0) return String("HTTP ") + server;
    return firstLine;
  }

  static void _sanitize(String& s) {
    s.trim();
    String clean;
    for (size_t i = 0; i < s.length() && clean.length() < 60; i++) {
      char c = s[i];
      if (c >= 32 && c <= 126) clean += c;
    }
    clean.trim();
    s = clean;
  }
};
