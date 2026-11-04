#pragma once
#include <Arduino.h>

// Exposure checks for one open port, driven by a small embedded rule table.
// Scope: misconfiguration exposure only (clear-text services, admin services
// reachable on the network, services answering with no authentication). It is
// not a CVE scanner and makes no login attempts. The active checks ask a
// service, in its own protocol, whether it answers without authenticating.
class ExposureAudit {
public:
  enum Severity : uint8_t { INFO, LOW, MEDIUM, HIGH };

  struct Finding {
    Severity    severity;
    String      text;
  };

  // Up to MAX_FINDINGS results for host:port; returns how many. Each PROBE_*
  // rule opens one short TCP connection; cancel() stops between rules.
  static constexpr uint8_t MAX_FINDINGS = 6;
  static uint8_t run(const char* host, uint16_t port, Finding* out,
                     bool (*cancel)() = nullptr);

  static const char* severityLabel(Severity s) {
    switch (s) {
      case HIGH:   return "HIGH";
      case MEDIUM: return "MED";
      case LOW:    return "LOW";
      default:     return "INFO";
    }
  }
};
