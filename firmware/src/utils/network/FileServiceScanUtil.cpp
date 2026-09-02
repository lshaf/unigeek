#include "FileServiceScanUtil.h"
#include "RemoteShellScanUtil.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

namespace {

static void encodeNetbiosName(
  const char* name,
  uint8_t suffix,
  uint8_t* out
)
{
  uint8_t raw[16];
  memset(raw, ' ', sizeof(raw));

  size_t len = strlen(name);
  if (len > 15) len = 15;
  memcpy(raw, name, len);
  raw[15] = suffix;

  out[0] = 0x20;
  for (uint8_t i = 0; i < 16; ++i) {
    out[1 + i * 2] = 'A' + ((raw[i] >> 4) & 0x0F);
    out[2 + i * 2] = 'A' + (raw[i] & 0x0F);
  }
  out[33] = 0x00;
}

static const uint8_t SMB2_NEGOTIATE[] = {
  0x00, 0x00, 0x00, 0x66,
  0xFE, 0x53, 0x4D, 0x42,
  0x40, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x24, 0x00,
  0x01, 0x00,
  0x01, 0x00,
  0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00,
  0x00, 0x00,
  0x02, 0x02
};

}

bool FileServiceScanUtil::_readLine(
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

bool FileServiceScanUtil::probeFtp(
  const char* ip,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0]) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));
  out.port = 21;
  out.service = SERVICE_FTP;

  WiFiClient client;
  client.setTimeout(patient ? 1350 : 700);

  if (!client.connect(ip, 21, patient ? 1000 : 450)) return false;

  char banner[96];
  bool got = _readLine(
    client,
    banner,
    sizeof(banner),
    patient ? 1500 : 750
  );
  client.stop();

  if (!got || strncmp(banner, "220", 3) != 0) return false;

  strlcpy(out.info, banner, sizeof(out.info));
  return true;
}

bool FileServiceScanUtil::probeSftpCandidate(
  const char* ip,
  Result& out,
  bool patient
)
{
  RemoteShellScanUtil::Result ssh;
  if (!RemoteShellScanUtil::probeSsh(ip, ssh, patient)) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));
  out.port = 22;
  out.service = SERVICE_SFTP_CANDIDATE;
  strlcpy(out.info, ssh.banner, sizeof(out.info));
  return true;
}

bool FileServiceScanUtil::_startNetbiosSession(
  Client& client,
  bool patient
)
{
  uint8_t packet[72];
  memset(packet, 0, sizeof(packet));

  packet[0] = 0x81;
  packet[1] = 0x00;
  packet[2] = 0x00;
  packet[3] = 0x44;

  encodeNetbiosName("*SMBSERVER", 0x20, &packet[4]);
  encodeNetbiosName("UNIGEEK", 0x00, &packet[38]);

  if (client.write(packet, sizeof(packet)) != sizeof(packet)) {
    return false;
  }

  unsigned long deadline = millis() + (patient ? 1350 : 700);
  while (!client.available() && client.connected() && millis() < deadline) {
    delay(5);
  }

  if (!client.available()) return false;

  int type = client.read();
  return type == 0x82;
}

bool FileServiceScanUtil::_probeSmbDirect(
  Client& client,
  const char* ip,
  uint16_t port,
  Result& out,
  bool patient
)
{
  if (client.write(
        SMB2_NEGOTIATE,
        sizeof(SMB2_NEGOTIATE)
      ) != sizeof(SMB2_NEGOTIATE)) {
    return false;
  }

  unsigned long deadline = millis() + (patient ? 1650 : 800);
  while (client.available() < 8 &&
         client.connected() &&
         millis() < deadline) {
    delay(5);
  }

  if (client.available() < 8) return false;

  uint8_t header[8];
  if (client.read(header, sizeof(header)) != sizeof(header)) {
    return false;
  }

  if (header[0] != 0x00 ||
      header[4] != 0xFE ||
      header[5] != 'S' ||
      header[6] != 'M' ||
      header[7] != 'B') {
    return false;
  }

  strlcpy(out.ip, ip, sizeof(out.ip));
  out.port = port;
  out.service = SERVICE_SMB;
  strlcpy(
    out.info,
    port == 445 ? "SMB2/3 negotiation" : "NetBIOS SMB negotiation",
    sizeof(out.info)
  );
  return true;
}

bool FileServiceScanUtil::probeSmb(
  const char* ip,
  uint16_t port,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0] || (port != 445 && port != 139)) {
    return false;
  }

  memset(&out, 0, sizeof(out));

  WiFiClient client;
  client.setTimeout(patient ? 1650 : 850);

  if (!client.connect(ip, port, patient ? 1200 : 500)) return false;

  if (port == 139 && !_startNetbiosSession(client, patient)) {
    client.stop();
    return false;
  }

  bool ok = _probeSmbDirect(client, ip, port, out, patient);
  client.stop();
  return ok;
}

bool FileServiceScanUtil::_probeWebDavClient(
  Client& client,
  const char* ip,
  uint16_t port,
  bool https,
  Result& out,
  bool patient
)
{
  client.printf(
    "OPTIONS / HTTP/1.1\r\n"
    "Host: %s:%u\r\n"
    "User-Agent: UniGeek\r\n"
    "Connection: close\r\n\r\n",
    ip,
    port
  );

  char firstLine[96];
  if (!_readLine(
    client,
    firstLine,
    sizeof(firstLine),
    patient ? 1800 : 900
  )) {
    return false;
  }

  if (strncmp(firstLine, "HTTP/", 5) != 0) return false;

  char* firstSpace = strchr(firstLine, ' ');
  if (firstSpace) out.status = atoi(firstSpace + 1);

  bool dav = false;
  char bestInfo[96] = {};

  unsigned long deadline = millis() + (patient ? 1800 : 900);

  while ((client.connected() || client.available()) &&
         millis() < deadline) {
    char line[160];
    if (!_readLine(client, line, sizeof(line), patient ? 375 : 250)) {
      if (!client.connected()) break;
      continue;
    }

    if (line[0] == '\0') break;

    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("dav:")) {
      dav = true;
      strlcpy(bestInfo, line, sizeof(bestInfo));
    }

    if (lower.startsWith("allow:") &&
        (lower.indexOf("propfind") >= 0 ||
         lower.indexOf("mkcol") >= 0 ||
         lower.indexOf("proppatch") >= 0)) {
      dav = true;
      if (bestInfo[0] == '\0') {
        strlcpy(bestInfo, line, sizeof(bestInfo));
      }
    }

    if (lower.startsWith("server:") && bestInfo[0] == '\0') {
      strlcpy(bestInfo, line, sizeof(bestInfo));
    }
  }

  if (!dav) return false;

  out.port = port;
  out.service = https
    ? SERVICE_WEBDAV_HTTPS
    : SERVICE_WEBDAV_HTTP;

  if (bestInfo[0]) {
    strlcpy(out.info, bestInfo, sizeof(out.info));
  } else {
    strlcpy(out.info, "WebDAV", sizeof(out.info));
  }

  return true;
}

bool FileServiceScanUtil::probeWebDav(
  const char* ip,
  uint16_t port,
  bool https,
  Result& out,
  bool patient
)
{
  if (!ip || !ip[0]) return false;

  memset(&out, 0, sizeof(out));
  strlcpy(out.ip, ip, sizeof(out.ip));

  if (https) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(patient ? 1650 : 850);

    if (!client.connect(ip, port)) return false;
    bool ok = _probeWebDavClient(
      client,
      ip,
      port,
      true,
      out,
      patient
    );
    client.stop();
    return ok;
  }

  WiFiClient client;
  client.setTimeout(patient ? 1650 : 850);

  if (!client.connect(ip, port, patient ? 1200 : 500)) return false;
  bool ok = _probeWebDavClient(
    client,
    ip,
    port,
    false,
    out,
    patient
  );
  client.stop();
  return ok;
}
