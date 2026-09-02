#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ctype.h>

class TargetResolveUtil {
public:
  static bool isIpv4(const String& value) {
    IPAddress ip;
    return ip.fromString(value);
  }

  static bool isValidTarget(const String& value) {
    if (!value.length() || value.length() > 253) return false;
    if (isIpv4(value)) return true;
    if (value[0] == '.' || value[value.length() - 1] == '.') return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const char c = value[i];
      if (!(isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_')) return false;
    }
    return true;
  }

  static bool resolve(const String& target, IPAddress& out) {
    if (!isValidTarget(target)) return false;
    if (out.fromString(target)) return true;

    String host = target;
    host.trim();

    if (host.endsWith(".local")) {
      host.remove(host.length() - 6);
      if (!host.length()) return false;

      // Use a short-lived mDNS responder only for .local target resolution.
      // The scanner's own mDNS discovery continues to use MdnsScanUtil.
      if (MDNS.begin("unigeek-scanner")) {
        out = MDNS.queryHost(host, 2000);
        MDNS.end();
        if (out != IPAddress(0, 0, 0, 0)) return true;
      }
      return false;
    }

    return WiFi.hostByName(host.c_str(), out) == 1 &&
           out != IPAddress(0, 0, 0, 0);
  }

  static bool resolve(const String& target, String& out) {
    IPAddress ip;
    if (!resolve(target, ip)) return false;
    out = ip.toString();
    return true;
  }
};
