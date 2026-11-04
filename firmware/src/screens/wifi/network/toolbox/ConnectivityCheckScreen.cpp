#include "ConnectivityCheckScreen.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include "ui/actions/ShowStatusAction.h"

namespace {
constexpr const char* kProbeHost = "connectivitycheck.gstatic.com";
constexpr const char* kProbeUrl  = "http://connectivitycheck.gstatic.com/generate_204";
}

bool ConnectivityCheckScreen::_begin() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return false;
  }
  _stepNo = STEP_GATEWAY;
  _waiting = false;
  _gatewayOk = _publicOk = _dnsOk = false;
  _web = Web::Unknown;
  _probeOpen = _probe.open();

  _print("Wi-Fi: " + WiFi.SSID() + "  " + String(WiFi.RSSI()) + " dBm");
  _print("IP " + WiFi.localIP().toString() + "  GW " + WiFi.gatewayIP().toString());
  _print("DNS " + WiFi.dnsIP().toString());
  return true;
}

// Sends one echo, then reports it once known; true while still in progress.
bool ConnectivityCheckScreen::_pingStep(const IPAddress& target, const char* label, bool& result) {
  if (!_probeOpen) {
    _print(String(label) + ": skipped (ICMP unavailable)");
    return false;
  }
  if (!_waiting) {
    _waiting = _probe.send(target, 64, (uint16_t)(_stepNo + 1), 32, 2000);
    if (!_waiting) {
      _print(String(label) + ": send failed");
      return false;
    }
    return true;
  }
  IPAddress from;
  uint32_t rtt = 0;
  const auto r = _probe.poll(from, rtt);
  if (r == IcmpProbe::Result::Pending) return true;
  _waiting = false;
  result = r == IcmpProbe::Result::Reply;
  _print(String(label) + (result ? String(": OK  ") + String(rtt) + " ms" : String(": no reply")));
  return false;
}

bool ConnectivityCheckScreen::_step() {
  switch (_stepNo) {
    case STEP_GATEWAY:
      if (_pingStep(WiFi.gatewayIP(), "Gateway", _gatewayOk)) return true;
      _stepNo = STEP_PUBLIC_IP;
      return true;

    case STEP_PUBLIC_IP:
      if (_pingStep(IPAddress(1, 1, 1, 1), "Internet (1.1.1.1)", _publicOk)) return true;
      _stepNo = STEP_DNS;
      return true;

    case STEP_DNS: {
      IPAddress ip;
      const unsigned long t0 = millis();
      _dnsOk = WiFi.hostByName(kProbeHost, ip) == 1 && ip != IPAddress(0, 0, 0, 0);
      _print(_dnsOk ? String("DNS: OK  ") + String(millis() - t0) + " ms" : String("DNS: failed"));
      _stepNo = STEP_HTTP;
      return true;
    }

    case STEP_HTTP: {
      // A 204 means open internet; anything else from this URL is a portal
      // (or a proxy) answering in its place.
      WiFiClient client;
      HTTPClient http;
      http.setTimeout(6000);
      http.setConnectTimeout(6000);
      http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      static const char* kHeaders[] = {"Location"};
      if (http.begin(client, kProbeUrl)) {
        http.collectHeaders(kHeaders, 1);
        const int code = http.GET();
        if (code == 204) {
          _web = Web::Ok;
          _print("Web: OK (204)");
        } else if (code > 0) {
          _web = Web::Portal;
          _print("Web: HTTP " + String(code) + " instead of 204");
          const String loc = http.header("Location");
          if (loc.length()) _print("Redirect: " + loc);
        } else {
          _web = Web::Fail;
          _print("Web: " + HTTPClient::errorToString(code));
        }
        http.end();
      } else {
        _web = Web::Fail;
        _print("Web: request failed");
      }
      _stepNo = STEP_SUMMARY;
      return true;
    }

    case STEP_SUMMARY:
    default: {
      const char* verdict;
      if (_web == Web::Ok)                  verdict = "Internet is working.";
      else if (_web == Web::Portal)         verdict = "Captive portal: sign in through a browser.";
      else if (_publicOk && !_dnsOk)        verdict = "Internet reachable, DNS failing.";
      else if (!_gatewayOk && !_publicOk)   verdict = "No route: gateway and internet silent.";
      else if (!_publicOk)                  verdict = "Local network only: no internet.";
      else                                  verdict = "Internet reachable, web blocked.";
      _print(String("Result: ") + verdict);
      if (!_gatewayOk && (_publicOk || _web == Web::Ok))
        _print("(the gateway just ignores ping)");
      return false;
    }
  }
}

void ConnectivityCheckScreen::_end() {
  if (_probeOpen) _probe.close();
  _probeOpen = false;
  _waiting = false;
}
