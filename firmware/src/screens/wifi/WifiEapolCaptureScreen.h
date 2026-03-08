#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <esp_wifi.h>

#include "ui/templates/ListScreen.h"
#include "utils/WifiAttackUtil.h"

class WifiEapolCaptureScreen : public ListScreen {
public:
  const char* title() override { return "EAPOL Capture"; }
  bool inhibitPowerOff() override { return true; }

  ~WifiEapolCaptureScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override {}

  // ── Shared types (callback needs access) ─────────────────────────────────

  using MacAddr = std::array<uint8_t, 6>;

  struct MacHash {
    size_t operator()(const MacAddr& m) const noexcept {
      uint64_t v = 0;
      memcpy(&v, m.data(), 6);
      return std::hash<uint64_t>{}(v);
    }
  };

  struct MacEqual {
    bool operator()(const MacAddr& a, const MacAddr& b) const noexcept {
      return memcmp(a.data(), b.data(), 6) == 0;
    }
  };

  struct EapolEntry {
    std::string ssid;
    uint16_t    count         = 0;
    bool        hasM1         = false;  // got ANonce from AP (no MIC)
    bool        hasM2         = false;  // got SNonce + MIC from STA
    bool        beaconWritten = false;
    std::string filepath;           // empty until PCAP file created
  };

  // Ring buffer for ISR → main loop communication (lock-free SPSC)
  static constexpr int RING_SIZE = 8;
  static constexpr int MAX_FRAME = 400;

  struct RawCapture {
    MacAddr  bssid;
    uint8_t  data[MAX_FRAME];
    uint16_t len;
    uint8_t  channel;   // rx channel (beacons only — used for deauth targeting)
    bool     isBeacon;
  };

  static RawCapture   _ring[RING_SIZE];
  static volatile int _ringHead;   // producer (ISR)
  static volatile int _ringTail;   // consumer (main loop)

  static std::unordered_map<MacAddr, EapolEntry,  MacHash, MacEqual> _eapolMap;
  static std::unordered_map<MacAddr, std::string, MacHash, MacEqual> _ssidMap;

  // EAPOL frames buffered before the AP's SSID is known
  static std::unordered_map<MacAddr,
    std::vector<std::vector<uint8_t>>,
    MacHash, MacEqual> _pending;

  // Known APs for deauth injection — fixed array, no heap
  struct ApTarget {
    uint8_t bssid[6];
    uint8_t channel;
  };
  static constexpr int MAX_TARGETS = 20;
  static ApTarget _apTargets[MAX_TARGETS];
  static int      _apCount;

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int      MAX_PENDING        = 8;       // max buffered EAPOL frames per AP before SSID known
  static constexpr uint32_t MIN_FREE_BYTES      = 50 * 1024;
  static constexpr unsigned long DISCOVERY_DWELL_MS = 500;   // ms per channel during discovery scan
  static constexpr unsigned long ATTACK_DWELL_MS    = 4000;  // ms to stay on channel after deauth

  // ── Scan phase ────────────────────────────────────────────────────────────
  enum Phase { PHASE_DISCOVERY, PHASE_ATTACK };
  Phase         _phase            = PHASE_DISCOVERY;
  int           _discoveryCount   = 0;    // channels scanned in current discovery pass
  uint8_t       _attackChans[13]  = {};   // unique channels with APs needing EAPOL
  int           _attackChanCount  = 0;
  int           _attackChanIdx    = 0;
  bool          _deauthFired      = false;  // initial deauth sent for current attack slot
  bool          _midDeauthSent    = false;  // mid-dwell deauth sent
  unsigned long _chanDwellUntil   = 0;      // absolute time to hop away
  unsigned long _midDeauthAt      = 0;      // absolute time to send mid-dwell deauth

  // ── Action log ────────────────────────────────────────────────────────────
  static constexpr int LOG_SIZE = 32;
  static constexpr int ROW_H   = 10;  // px per log row (font1 = 8px + 2px gap)

  struct LogEntry {
    char     msg[44];
    uint16_t color;
  };
  LogEntry      _log[LOG_SIZE] = {};
  int           _logHead       = 0;   // next write position
  int           _logCount      = 0;   // total entries written (capped at LOG_SIZE)
  uint32_t      _totalEapol    = 0;
  uint32_t      _handshakes    = 0;   // confirmed M1+M2 pairs
  char          _lastEapolName[20] = {};

  static const char* SAVE_DIR;

  int           _channel       = 1;
  bool          _needRefresh   = false;
  bool          _storageOk     = false;
  unsigned long _lastFreeCheck = 0;

  WifiAttackUtil* _attacker = nullptr;

  void _pushLog(const char* msg, uint16_t color);
  bool _checkFreeSpace();
  void _flush();
  void _buildAttackChans();
  void _hopToAttackChan();
  void _writePcapHeader(const std::string& path);
  void _appendPcapFrame(const std::string& path, const uint8_t* data, uint16_t len);
  void _registerApTarget(const MacAddr& bssid, uint8_t ch);
  void _sendDeauth(int ch);
  std::string _makePath(const MacAddr& bssid, const std::string& ssid);
  std::string _sanitize(const std::string& s);
};