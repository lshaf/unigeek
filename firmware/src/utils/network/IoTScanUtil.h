#pragma once
#include <Arduino.h>
#include "MdnsScanUtil.h"
#include "SsdpScanUtil.h"
#include "WebScanUtil.h"

class IoTScanUtil {
public:
  enum Confidence {
    CONFIDENCE_LOW,
    CONFIDENCE_MEDIUM,
    CONFIDENCE_HIGH,
  };

  struct Device {
    char ip[16];
    char name[48];
    char category[24];
    char device[48];
    char evidence[144];
    Confidence confidence;
  };

  static constexpr uint8_t MAX_DEVICES = 32;

  static bool addMdnsEvidence(
    const MdnsScanUtil::Service& svc,
    Device& out
  );

  static bool addSsdpEvidence(
    const SsdpScanUtil::Device& dev,
    Device& out
  );

  static bool addWebEvidence(
    const WebScanUtil::Result& web,
    Device& out
  );

  static bool probeTcpPort(
    const char* ip,
    uint16_t port,
    uint32_t timeoutMs = 500
  );

  static bool addPortEvidence(
    const char* ip,
    uint16_t port,
    Device& out,
    bool patient = false
  );

  static void merge(Device& dst, const Device& src);

  static const char* confidenceName(Confidence confidence);

private:
  static void _set(
    Device& out,
    const char* ip,
    const char* name,
    const char* category,
    const char* device,
    Confidence confidence,
    const char* evidence
  );

  static void _appendEvidence(
    char* dst,
    size_t dstLen,
    const char* evidence
  );

  static bool _contains(const String& haystack, const char* needle);
};
