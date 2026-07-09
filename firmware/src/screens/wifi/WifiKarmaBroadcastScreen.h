#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"
#include "utils/network/WifiAttackUtil.h"

// Active Karma engine. Beyond passively waiting for probes it can respond to a
// client's directed probe (karma) as a fake AP, actively beacon a pool of SSIDs
// (harvested live + an optional file list), and push clients off nearby APs with
// integrated deauth. Includes security mimicry (WPA2 RSN), client fingerprinting
// across MAC randomization and response-based prioritization (tiers).
//
// Operating modes:
//   Passive   — listen + answer directed probes (karma responses only)
//   Broadcast — actively beacon the SSID pool
//   Full      — both of the above
class WifiKarmaBroadcastScreen : public ListScreen
{
public:
  const char* title() override { return "Active Karma"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  ~WifiKarmaBroadcastScreen() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  // No pre-run config list; the screen auto-starts and all options live in the
  // in-run menu (PRESS). Required override — intentionally does nothing.
  void onItemSelected(uint8_t) override {}

private:
  enum State { STATE_MENU, STATE_RUNNING };
  State _state = STATE_MENU;

  enum Mode { MODE_PASSIVE, MODE_BROADCAST, MODE_FULL };

  // ── Settings (all exposed as cycling menu items) ────────────────────────────
  Mode _mode       = MODE_FULL;  // Passive / Broadcast / Full
  bool _wpa2       = true;   // RSN mimicry: secured (WPA2) vs open
  bool _autoKarma  = true;   // answer directed probes with a fake AP
  bool _beaconing  = true;   // actively beacon the SSID pool
  bool _deauth     = false;  // periodically deauth real APs on this channel
  bool _autoHop    = true;   // rotate channel automatically
  bool _prioritize = true;   // weight beacons toward SSIDs that got responses
  int  _hopIntMs   = 500;    // 500 / 1000 / 2000 / 3000
  int  _beaconMs   = 300;    // beacon pass cadence: 200 / 300 / 500

  // ── Attack engine ────────────────────────────────────────────────────────────
  WifiAttackUtil* _attacker = nullptr;
  LogView         _log;

  static constexpr int MAX_SSID = 64;
  char     _ssids[MAX_SSID][33] = {};
  uint16_t _hits[MAX_SSID]      = {};   // probe responses per SSID (tier score)
  int      _ssidCount           = 0;
  int      _fileCount           = 0;    // SSIDs preloaded from file
  int      _beaconIdx           = 0;    // round-robin cursor
  uint32_t _beaconPass          = 0;    // beacon-pass counter (for prioritize)

  static constexpr int MAX_FP = 64;
  uint32_t _fingerprints[MAX_FP] = {};  // unique client fingerprints (dedup)
  int      _fpCount              = 0;

  // Real APs learned from sniffed beacons — deauth targets.
  static constexpr int MAX_AP = 24;
  struct RealAp { uint8_t bssid[6]; uint8_t channel; char ssid[33]; };
  RealAp _aps[MAX_AP] = {};
  int    _apCount     = 0;

  // Pending directed-probe responses queued from the WiFi callback and drained
  // (transmitted) from onUpdate so no blocking TX happens in callback context.
  static constexpr int PRQ = 16;
  struct ProbeReq { char ssid[33]; uint8_t mac[6]; uint8_t ch; };
  ProbeReq _prq[PRQ];
  int      _prqHead = 0;
  int      _prqTail = 0;

  // Fake-AP BSSID reused across beacons/responses so advertised networks keep a
  // stable identity; rotated periodically (or on demand) to dodge tracking.
  uint8_t       _bssid[6]      = {};
  unsigned long _lastMacRot    = 0;

  // Real (connectable) soft-AP — a genuine open AP a victim can actually join.
  // Beacon spam alone can't complete association; this brings up a real AP.
  bool          _evilAp        = false;
  String        _evilApSsid;

  // Stats
  uint32_t      _beaconsSent = 0;
  uint32_t      _karmaSent   = 0;
  uint32_t      _probeCount  = 0;
  uint8_t       _channel     = 1;
  bool          _paused      = false;
  unsigned long _lastHop     = 0;
  unsigned long _lastBeacon  = 0;
  unsigned long _lastDeauth  = 0;
  unsigned long _lastDraw    = 0;

  // Shared with the promiscuous callback (WiFi task) — guard with the spinlock.
  portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;

  static WifiKarmaBroadcastScreen* _instance;
  static void     _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  static uint32_t _fingerprint(const uint8_t* frame, uint16_t len);

  void _startAttack();
  void _stopAttack();
  void _loadFile();
  int  _addSsid(const char* ssid);  // returns index, -1 if full; bumps hit
  void _doBeaconPass();
  void _drainProbeQueue();
  void _drawLog();

  const char* _modeName() const;

  // ── In-run options menu (opened with PRESS while running) ────────────────────
  void _openOptions();
  void _optSetMode();
  void _optChannelControl();
  void _optAttackSettings();
  void _optAttackStrategy();
  void _optBroadcastControl();
  void _optEvilAp();
  void _rotateMac();
  void _startEvilAp(const char* ssid);
  void _stopEvilAp();
  void _showStats();
  void _saveProbes();
};
