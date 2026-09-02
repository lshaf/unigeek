#include "IoTScanUtil.h"
#include <WiFiClient.h>
#include <string.h>

bool IoTScanUtil::_contains(
  const String& haystack,
  const char* needle
)
{
  if (!needle || !needle[0]) return false;

  String lower = haystack;
  lower.toLowerCase();

  String n = needle;
  n.toLowerCase();

  return lower.indexOf(n) >= 0;
}

void IoTScanUtil::_appendEvidence(
  char* dst,
  size_t dstLen,
  const char* evidence
)
{
  if (!dst || dstLen == 0 || !evidence || !evidence[0]) return;

  if (strstr(dst, evidence)) return;

  if (dst[0]) {
    strlcat(dst, "; ", dstLen);
  }

  strlcat(dst, evidence, dstLen);
}

void IoTScanUtil::_set(
  Device& out,
  const char* ip,
  const char* name,
  const char* category,
  const char* device,
  Confidence confidence,
  const char* evidence
)
{
  memset(&out, 0, sizeof(out));

  if (ip) strlcpy(out.ip, ip, sizeof(out.ip));
  if (name) strlcpy(out.name, name, sizeof(out.name));

  strlcpy(
    out.category,
    category ? category : "Unknown IoT",
    sizeof(out.category)
  );

  strlcpy(
    out.device,
    device ? device : "Unknown",
    sizeof(out.device)
  );

  out.confidence = confidence;

  if (evidence) {
    strlcpy(out.evidence, evidence, sizeof(out.evidence));
  }
}

bool IoTScanUtil::addMdnsEvidence(
  const MdnsScanUtil::Service& svc,
  Device& out
)
{
  String all =
    String(svc.service) + " " +
    String(svc.name) + " " +
    String(svc.host) + " " +
    String(svc.txt);

  if (_contains(all, "_googlecast._tcp")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Media",
      "Google Cast",
      CONFIDENCE_HIGH,
      "_googlecast._tcp"
    );
    return true;
  }

  if (_contains(all, "_airplay._tcp")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Media",
      "AirPlay device",
      CONFIDENCE_HIGH,
      "_airplay._tcp"
    );
    return true;
  }

  if (_contains(all, "_hap._tcp")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Smart Home",
      "HomeKit device",
      CONFIDENCE_HIGH,
      "_hap._tcp"
    );
    return true;
  }

  if (_contains(all, "_matter._tcp") ||
      _contains(all, "_matterc._udp") ||
      _contains(all, "_matterd._udp")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Smart Home",
      "Matter device",
      CONFIDENCE_HIGH,
      "Matter DNS-SD"
    );
    return true;
  }

  if (_contains(all, "_home-assistant._tcp") ||
      _contains(all, "home assistant")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Smart Home",
      "Home Assistant",
      CONFIDENCE_HIGH,
      "Home Assistant mDNS"
    );
    return true;
  }

  if (_contains(all, "_esphomelib._tcp") ||
      _contains(all, "esphome")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Automation",
      "ESPHome device",
      CONFIDENCE_HIGH,
      "ESPHome mDNS"
    );
    return true;
  }

  if (_contains(all, "_ipp._tcp") ||
      _contains(all, "_printer._tcp")) {
    _set(
      out,
      svc.ip,
      svc.name,
      "Printer",
      "Network printer",
      CONFIDENCE_HIGH,
      "IPP / printer mDNS"
    );
    return true;
  }

  return false;
}

bool IoTScanUtil::addSsdpEvidence(
  const SsdpScanUtil::Device& dev,
  Device& out
)
{
  String all =
    String(dev.name) + " " +
    String(dev.st) + " " +
    String(dev.server) + " " +
    String(dev.usn) + " " +
    String(dev.location);

  if (_contains(all, "roku:ecp") ||
      (_contains(all, "roku") &&
       _contains(all, ":8060"))) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Media",
      "Roku device",
      CONFIDENCE_HIGH,
      "SSDP Roku ECP"
    );
    return true;
  }

  if (_contains(all, "digitalsecuritycamera") ||
      _contains(all, "networkcamera") ||
      _contains(all, "ip camera") ||
      _contains(all, "camera")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Camera",
      "IP camera",
      CONFIDENCE_HIGH,
      "SSDP camera descriptor"
    );
    return true;
  }

  if (_contains(all, "printer")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Printer",
      "Network printer",
      CONFIDENCE_HIGH,
      "SSDP printer descriptor"
    );
    return true;
  }

  if (_contains(all, "mediarenderer")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Media",
      "DLNA/UPnP Renderer",
      CONFIDENCE_HIGH,
      "UPnP MediaRenderer"
    );
    return true;
  }

  if (_contains(all, "mediaserver")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Storage",
      "DLNA/UPnP Media Server",
      CONFIDENCE_HIGH,
      "UPnP MediaServer"
    );
    return true;
  }

  if (_contains(all, "sonos") ||
      _contains(all, "zoneplayer")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Media",
      "Sonos device",
      CONFIDENCE_HIGH,
      "SSDP Sonos/ZonePlayer"
    );
    return true;
  }

  if (_contains(all, "hue") ||
      _contains(all, "philips")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Smart Home",
      "Philips Hue",
      CONFIDENCE_HIGH,
      "SSDP Hue fingerprint"
    );
    return true;
  }

  if (_contains(all, "internetgatewaydevice") ||
      _contains(all, "wanipconnection") ||
      _contains(all, "wlanaccesspoint")) {
    _set(
      out,
      dev.ip,
      dev.name,
      "Network",
      "Network appliance",
      CONFIDENCE_HIGH,
      "UPnP network device"
    );
    return true;
  }

  return false;
}

bool IoTScanUtil::addWebEvidence(
  const WebScanUtil::Result& web,
  Device& out
)
{
  String all =
    String(web.server) + " " +
    String(web.title);

  if (_contains(all, "home assistant")) {
    _set(
      out,
      web.ip,
      web.title,
      "Smart Home",
      "Home Assistant",
      CONFIDENCE_HIGH,
      "HTTP title: Home Assistant"
    );
    return true;
  }

  if (_contains(all, "tasmota")) {
    _set(
      out,
      web.ip,
      web.title,
      "Automation",
      "Tasmota device",
      CONFIDENCE_HIGH,
      "HTTP Tasmota fingerprint"
    );
    return true;
  }

  if (_contains(all, "esphome")) {
    _set(
      out,
      web.ip,
      web.title,
      "Automation",
      "ESPHome device",
      CONFIDENCE_HIGH,
      "HTTP ESPHome fingerprint"
    );
    return true;
  }

  if (_contains(all, "shelly")) {
    _set(
      out,
      web.ip,
      web.title,
      "Smart Home",
      "Shelly device",
      CONFIDENCE_HIGH,
      "HTTP Shelly fingerprint"
    );
    return true;
  }

  if (_contains(all, "philips hue") ||
      _contains(all, "hue bridge")) {
    _set(
      out,
      web.ip,
      web.title,
      "Smart Home",
      "Philips Hue",
      CONFIDENCE_HIGH,
      "HTTP Hue fingerprint"
    );
    return true;
  }

  if (_contains(all, "sonos")) {
    _set(
      out,
      web.ip,
      web.title,
      "Media",
      "Sonos device",
      CONFIDENCE_HIGH,
      "HTTP Sonos fingerprint"
    );
    return true;
  }

  if (_contains(all, "camera") ||
      _contains(all, "webcam") ||
      _contains(all, "nvr") ||
      _contains(all, "dvr")) {
    _set(
      out,
      web.ip,
      web.title,
      "Camera",
      "Camera / recorder",
      CONFIDENCE_MEDIUM,
      "HTTP camera fingerprint"
    );
    return true;
  }

  return false;
}

bool IoTScanUtil::probeTcpPort(
  const char* ip,
  uint16_t port,
  uint32_t timeoutMs
)
{
  if (!ip || !ip[0]) return false;

  WiFiClient client;
  bool open = client.connect(ip, port, timeoutMs);
  client.stop();

  return open;
}

bool IoTScanUtil::addPortEvidence(
  const char* ip,
  uint16_t port,
  Device& out,
  bool patient
)
{
  if (!probeTcpPort(ip, port, patient ? 750 : 350)) return false;

  switch (port) {
    case 1883:
      _set(
        out,
        ip,
        nullptr,
        "Automation",
        "MQTT endpoint",
        CONFIDENCE_LOW,
        "MQTT TCP :1883"
      );
      return true;

    case 8883:
      _set(
        out,
        ip,
        nullptr,
        "Automation",
        "MQTT TLS endpoint",
        CONFIDENCE_LOW,
        "MQTT TLS :8883"
      );
      return true;

    case 554:
      _set(
        out,
        ip,
        nullptr,
        "Camera",
        "RTSP device",
        CONFIDENCE_MEDIUM,
        "RTSP TCP :554"
      );
      return true;

    case 1400:
      _set(
        out,
        ip,
        nullptr,
        "Media",
        "Sonos-like media device",
        CONFIDENCE_MEDIUM,
        "Media service TCP :1400"
      );
      return true;

    case 8008:
    case 8009:
      _set(
        out,
        ip,
        nullptr,
        "Media",
        "Cast-like device",
        CONFIDENCE_MEDIUM,
        port == 8008
          ? "Cast-like TCP :8008"
          : "Cast-like TCP :8009"
      );
      return true;

    case 8123:
      _set(
        out,
        ip,
        nullptr,
        "Smart Home",
        "Home Assistant candidate",
        CONFIDENCE_MEDIUM,
        "TCP :8123"
      );
      return true;
  }

  return false;
}

void IoTScanUtil::merge(Device& dst, const Device& src)
{
  if (!dst.ip[0]) {
    dst = src;
    return;
  }

  if (!dst.name[0] && src.name[0]) {
    strlcpy(dst.name, src.name, sizeof(dst.name));
  }

  if (src.confidence > dst.confidence) {
    strlcpy(dst.category, src.category, sizeof(dst.category));
    strlcpy(dst.device, src.device, sizeof(dst.device));
    dst.confidence = src.confidence;
  } else if (
    src.confidence == dst.confidence &&
    strcmp(dst.device, "Unknown") == 0 &&
    strcmp(src.device, "Unknown") != 0
  ) {
    strlcpy(dst.category, src.category, sizeof(dst.category));
    strlcpy(dst.device, src.device, sizeof(dst.device));
  }

  _appendEvidence(
    dst.evidence,
    sizeof(dst.evidence),
    src.evidence
  );
}

const char* IoTScanUtil::confidenceName(Confidence confidence)
{
  switch (confidence) {
    case CONFIDENCE_HIGH:
      return "High";
    case CONFIDENCE_MEDIUM:
      return "Medium";
    case CONFIDENCE_LOW:
    default:
      return "Low";
  }
}
