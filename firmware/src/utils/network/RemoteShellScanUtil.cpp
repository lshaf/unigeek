#include "RemoteShellScanUtil.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

bool RemoteShellScanUtil::_readBanner(
  Client& client,
  char* out,
  size_t outLen,
  uint32_t timeoutMs
)
{
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  unsigned long deadline = millis() + timeoutMs;
  size_t pos = 0;

  while (millis() < deadline && pos + 1 < outLen) {
    while (client.available() && pos + 1 < outLen) {
      int c = client.read();
      if (c < 0) break;

      if (c == '\r' || c == '\n') {
        if (pos > 0) {
          out[pos] = '\0';
          return true;
        }
        continue;
      }

      if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        out[pos++] = (char)c;
      }
    }

    if (!client.connected() && !client.available()) break;
    delay(5);
  }

  out[pos] = '\0';
  return pos > 0;
}

bool RemoteShellScanUtil::probeSsh(
  const char* ip,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0]) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));
  out.port = 22;
  out.protocol = PROTO_SSH;

  WiFiClient client;
  client.setTimeout(patient ? 1200 : 650);

  if (!client.connect(ip, 22, patient ? 1000 : 450)) return false;

  bool gotBanner = _readBanner(
    client,
    out.banner,
    sizeof(out.banner),
    patient ? 1350 : 700
  );

  client.stop();

  if (!gotBanner) return false;

  return strncmp(out.banner, "SSH-", 4) == 0;
}

bool RemoteShellScanUtil::probeTelnet(
  const char* ip,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0]) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));
  out.port = 23;
  out.protocol = PROTO_TELNET;

  WiFiClient client;
  client.setTimeout(patient ? 1200 : 650);

  if (!client.connect(ip, 23, patient ? 1000 : 450)) return false;

  unsigned long deadline = millis() + (patient ? 1350 : 700);
  size_t pos = 0;
  bool sawTelnetNegotiation = false;

  while (millis() < deadline && pos + 1 < sizeof(out.banner)) {
    while (client.available()) {
      int c = client.read();
      if (c < 0) break;

      if ((uint8_t)c == 255) {
        sawTelnetNegotiation = true;
        if (client.available()) client.read();
        if (client.available()) client.read();
        continue;
      }

      if (c == '\r' || c == '\n') {
        if (pos > 0) break;
        continue;
      }

      if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        out.banner[pos++] = (char)c;
      }
    }

    if (pos > 0 || sawTelnetNegotiation) break;
    if (!client.connected() && !client.available()) break;
    delay(5);
  }

  out.banner[pos] = '\0';
  client.stop();

  return sawTelnetNegotiation || pos > 0;
}

bool RemoteShellScanUtil::_probeWinRmClient(
  Client& client,
  const char* ip,
  uint16_t port,
  bool https,
  Result& out,
  bool patient
)
{
  client.printf(
    "POST /wsman HTTP/1.1\r\n"
    "Host: %s:%u\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n\r\n",
    ip,
    port
  );

  unsigned long deadline = millis() + (patient ? 1800 : 900);
  while (!client.available() && client.connected() && millis() < deadline) {
    delay(5);
  }

  if (!client.available()) return false;

  String firstLine = client.readStringUntil('\n');
  firstLine.trim();

  if (!firstLine.startsWith("HTTP/")) return false;

  int firstSpace = firstLine.indexOf(' ');
  if (firstSpace >= 0) {
    out.status = firstLine.substring(firstSpace + 1).toInt();
  }

  bool winrmHint = false;

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) break;

    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("server:") ||
        lower.indexOf("microsoft-httpapi") >= 0 ||
        lower.indexOf("wsman") >= 0) {
      if (out.banner[0] == '\0') {
        strlcpy(out.banner, line.c_str(), sizeof(out.banner));
      }
    }

    if (lower.indexOf("microsoft-httpapi") >= 0 ||
        lower.indexOf("wsman") >= 0) {
      winrmHint = true;
    }

    if (lower.startsWith("www-authenticate:")) {
      if (lower.indexOf("negotiate") >= 0 ||
          lower.indexOf("kerberos") >= 0 ||
          lower.indexOf("ntlm") >= 0) {
        winrmHint = true;
      }
    }
  }

  if (out.status == 401 || out.status == 405 || out.status == 400) {
    winrmHint = true;
  }

  return winrmHint;
}

bool RemoteShellScanUtil::probeWinRm(
  const char* ip,
  bool https,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0]) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));

  const uint16_t port = https ? 5986 : 5985;
  out.port = port;
  out.protocol = https ? PROTO_WINRM_HTTPS : PROTO_WINRM_HTTP;

  if (https) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(patient ? 1500 : 800);

    if (!client.connect(ip, port)) return false;
    bool ok = _probeWinRmClient(client, ip, port, https, out, patient);
    client.stop();
    return ok;
  }

  WiFiClient client;
  client.setTimeout(patient ? 1500 : 800);

  if (!client.connect(ip, port, patient ? 1000 : 450)) return false;
  bool ok = _probeWinRmClient(client, ip, port, https, out, patient);
  client.stop();
  return ok;
}
