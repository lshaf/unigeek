#include "WebScanUtil.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

bool WebScanUtil::probe(
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
  out.port = port;
  out.https = https;

  if (https) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(patient ? 1500 : 800);

    if (!client.connect(ip, port)) return false;
    bool ok = _readResponse(client, ip, port, https, out, patient);
    client.stop();
    return ok;
  }

  WiFiClient client;
  client.setTimeout(patient ? 1500 : 800);

  if (!client.connect(ip, port, patient ? 1000 : 500)) return false;
  bool ok = _readResponse(client, ip, port, https, out, patient);
  client.stop();
  return ok;
}

bool WebScanUtil::_readResponse(
  Client& client,
  const char* ip,
  uint16_t port,
  bool https,
  Result& out,
  bool patient
)
{
  client.printf(
    "GET / HTTP/1.0\r\n"
    "Host: %s\r\n"
    "User-Agent: UniGeek\r\n"
    "Connection: close\r\n\r\n",
    ip
  );

  unsigned long deadline = millis() + (patient ? 1800 : 900);
  while (!client.available() && client.connected() && millis() < deadline) {
    delay(5);
  }

  if (!client.available()) return false;

  String firstLine = client.readStringUntil('\n');
  firstLine.trim();

  if (!firstLine.startsWith("HTTP/")) {
    return false;
  }

  int firstSpace = firstLine.indexOf(' ');
  if (firstSpace >= 0) {
    out.status = firstLine.substring(firstSpace + 1).toInt();
  }

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) break;

    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("server:")) {
      String value = line.substring(7);
      value.trim();
      strlcpy(out.server, value.c_str(), sizeof(out.server));
    }
  }

  String body;
  body.reserve(2048);

  deadline = millis() + (patient ? 1350 : 700);
  while (body.length() < 2048 && millis() < deadline) {
    while (client.available() && body.length() < 2048) {
      body += (char)client.read();
    }

    if (!client.connected() && !client.available()) break;
    delay(2);
  }

  String lowerBody = body;
  lowerBody.toLowerCase();

  int titleStart = lowerBody.indexOf("<title");
  if (titleStart >= 0) {
    titleStart = lowerBody.indexOf('>', titleStart);
    int titleEnd = lowerBody.indexOf("</title>", titleStart + 1);

    if (titleStart >= 0 && titleEnd > titleStart) {
      String title = body.substring(titleStart + 1, titleEnd);
      title.replace("\r", " ");
      title.replace("\n", " ");
      title.trim();

      while (title.indexOf("  ") >= 0) {
        title.replace("  ", " ");
      }

      String normalizedTitle = title;
      normalizedTitle.toLowerCase();
      if (normalizedTitle != "opening..." &&
          normalizedTitle != "redirecting..." &&
          normalizedTitle != "loading...") {
        strlcpy(out.title, title.c_str(), sizeof(out.title));
      }
    }
  }

  return true;
}
