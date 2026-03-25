#pragma once

#include <WiFiClient.h>
#include "ui/actions/ShowProgressAction.h"

// TCP port scanner for a target IP (local or internet).
class PortScanUtil {
public:
  struct Result {
    char label[24];   // "ip:port"
    char service[20];
  };

  static constexpr uint8_t MAX_RESULTS = 80;

  // Scans common TCP ports on targetIp.
  // Shows ShowProgressAction with msg during scan.
  // Returns number of open ports found, writes to results[].
  static uint8_t scan(const char* targetIp, Result results[], uint8_t maxResults,
                      const char* msg = "Port scanning...") {
    struct PortEntry { int port; const char* service; };
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
    static constexpr uint8_t kPortCount = sizeof(kPorts) / sizeof(kPorts[0]);

    uint8_t count   = 0;
    uint8_t lastPct = 255;

    for (uint8_t i = 0; i < kPortCount && count < maxResults; i++) {
      uint8_t pct = (uint8_t)(i * 100 / kPortCount);
      if (pct != lastPct) {
        ShowProgressAction::show(msg, pct);
        lastPct = pct;
      }
      yield();
      WiFiClient client;
      if (client.connect(targetIp, kPorts[i].port, 300)) {
        client.stop();
        snprintf(results[count].label,   sizeof(results[0].label),   "%s:%d", targetIp, kPorts[i].port);
        strncpy(results[count].service,  kPorts[i].service,          sizeof(results[0].service) - 1);
        results[count].service[sizeof(results[0].service) - 1] = '\0';
        count++;
      }
    }
    ShowProgressAction::show(msg, 100);
    return count;
  }
};
