#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <esp_wifi.h>

#include "ui/templates/BaseScreen.h"
#include "utils/network/WifiAttackUtil.h"
#include "utils/MoodFace.h"   // MoodFace::Mood enum + MoodMsg phrases
#include "utils/MoodArt.h"    // rasterized SVG face bitmaps

// ── Unigotchi ───────────────────────────────────────────────────────────────
// Pwnagotchi-style handshake hunter. Standard unigeek left StatusBar, plus a
// plain-text pwnagotchi layout: name + uptime on top, a big SVG-derived mood
// face centred, a plain-text status/mood line, and plain-text counters at the
// bottom (no boxes). M5PORKCHOP-grade active capture underneath.
//
// Handshakes are written as PCAP into the EAPOL Capture dir/format.
class WifiUnigotchiScreen : public BaseScreen {
public:
  const char* title() override { return nullptr; }      // no header; left StatusBar only
  bool isFullScreen() override { return false; }        // show the standard left StatusBar
  bool inhibitPowerOff() override { return true; }

  ~WifiUnigotchiScreen();

  void onInit()    override;
  void onUpdate()  override;
  void onRender()  override;
  void onRestore() override;

  enum Mode : uint8_t { MODE_PASSIVE = 0, MODE_ACTIVE = 1, MODE_PWNGRID = 2, MODE_PWNSPAM = 3 };
  void applyMode(uint8_t m);

  // Display style: the pwnagotchi face, or a plain scrolling text log (same
  // engine underneath — only the centre body render differs).
  enum DisplayStyle : uint8_t { STYLE_GOTCHI = 0, STYLE_TEXT = 1 };

  // ── Shared capture types (static promiscuous cb needs access) ──────────────
  using MacAddr = std::array<uint8_t, 6>;
  struct MacHash {
    size_t operator()(const MacAddr& m) const noexcept {
      uint64_t v = 0; memcpy(&v, m.data(), 6); return std::hash<uint64_t>{}(v);
    }
  };
  struct MacEqual {
    bool operator()(const MacAddr& a, const MacAddr& b) const noexcept {
      return memcmp(a.data(), b.data(), 6) == 0;
    }
  };
  struct EapolEntry {
    std::string ssid;
    bool beaconWritten = false, validated = false, pmkidSeen = false;
    std::string filepath;
    uint8_t anonce[32] = {}, staMacM1[6] = {}; bool hasAnonce = false;
    uint8_t m2Snonce[32] = {}, staMacM2[6] = {}; bool hasM2Data = false;
  };

  static constexpr int RING_SIZE = 16;
  static constexpr int MAX_FRAME = 400;
  struct RawCapture {
    MacAddr  bssid;
    uint8_t  data[MAX_FRAME];
    uint16_t len;
    uint8_t  channel;
    int8_t   rssi;
    bool     isBeacon;
  };
  static RawCapture   _ring[RING_SIZE];
  static volatile int _ringHead, _ringTail;

  struct ClientSight { uint8_t bssid[6]; uint8_t sta[6]; uint8_t channel; int8_t rssi; };
  static constexpr int CRING = 24;
  static ClientSight  _cring[CRING];
  static volatile int _cHead, _cTail;

  static std::unordered_map<MacAddr, EapolEntry,  MacHash, MacEqual> _eapolMap;
  static std::unordered_map<MacAddr, std::string, MacHash, MacEqual> _ssidMap;
  static std::unordered_map<MacAddr, std::vector<std::vector<uint8_t>>, MacHash, MacEqual> _pending;
  static std::unordered_map<MacAddr, std::vector<uint8_t>, MacHash, MacEqual> _beaconStore;

  static constexpr int MAX_TARGETS = 48;
  static constexpr int MAX_CLIENTS = 5;
  struct ApTarget {
    uint8_t  bssid[6];
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  clientCount;
    uint8_t  clients[MAX_CLIENTS][6];
    uint32_t lastClientMs;
    bool     attempted;
    bool     pmkidProbed;   // active PMKID assoc-request already sent
  };
  static ApTarget _apTargets[MAX_TARGETS];
  static int      _apCount;

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int MAX_PENDING = 8;
  static const char*   SAVE_DIR;
  static const char*   PWN_FILE;   // SD .txt — first line is our advertised name

  static constexpr uint8_t HOP_COUNT = 13;
  static const uint8_t     HOP_ORDER[HOP_COUNT];

  Mode _mode = MODE_PASSIVE;
  DisplayStyle _style = STYLE_GOTCHI;

  // Rolling event log for the text display style — fed by every _say() phrase.
  static constexpr int LOG_LINES = 16;
  String  _log[LOG_LINES];
  uint8_t _logCount = 0;
  void    _pushLog(const String& line);

  // ── Auto-attack state machine ──────────────────────────────────────────────
  enum AutoState : uint8_t { ST_RECON, ST_LOCK, ST_ATTACK, ST_WAIT };
  AutoState     _auto         = ST_RECON;
  unsigned long _stateUntil   = 0;
  unsigned long _lastDeauthMs = 0;
  int           _targetIdx    = -1;
  uint32_t      _hsAtAttack   = 0;
  bool          _reDeauthDone = false;

  uint8_t       _hopIdx       = 0;
  int           _channel      = 1;
  unsigned long _chanHopUntil = 0;
  unsigned long _lastProbeMs  = 0;          // rate-limit active PMKID probes
  uint8_t       _spoofMac[6]  = {0x02, 0, 0, 0, 0, 0};  // locally-administered STA MAC for assoc reqs

  static constexpr uint8_t DEAUTH_BURST = 5;   // frames per client per attack (matches oink)

  WifiAttackUtil* _attacker = nullptr;
  bool          _storageOk     = false;
  unsigned long _lastFreeCheck = 0;
  unsigned long _sessionStart  = 0;

  uint32_t _handshakes = 0, _pmkids = 0, _deauths = 0, _disassocs = 0, _pwngridTx = 0;

  // ── Pwngrid advertise name (from SD .txt) ──────────────────────────────────
  std::vector<String> _pwnNames;
  uint16_t            _pwnIdx    = 0;
  bool                _pwnLoaded = false;

  // ── Pwngrid peers — the real mesh protocol: detect nearby pwnagotchis by
  // parsing their advertisement beacons (source MAC de:ad:be:ef:de:ad) and
  // track them as friends, aging them out when they go silent. ───────────────
  static constexpr int           MAX_PEERS    = 32;
  static constexpr unsigned long PEER_AWAY_MS = 120000;   // drop a friend after 2 min
  struct PwnPeer {
    char          identity[65];   // pubkey fingerprint — dedupe key
    char          name[24];
    char          face[12];
    uint32_t      pwndTot;
    int8_t        rssi;
    unsigned long lastPing;
    bool          used;
  };
  PwnPeer       _peers[MAX_PEERS] = {};
  int           _peerCount    = 0;
  uint8_t       _friendsTot   = 0;
  char          _lastFriend[24] = {};
  unsigned long _lastPeerScan = 0;

  void _drainPwngrid();
  void _parsePwnBeacon(const uint8_t* data, uint16_t len, int8_t rssi);
  void _addPeer(const char* identity, const char* name, const char* face,
                uint32_t pwndTot, int8_t rssi);
  void _expirePeers();
  void _resetPeers();
  static bool _jsonStr(const char* json, const char* key, char* out, size_t outSz);
  static long _jsonInt(const char* json, const char* key);

  // ── Pwnagotchi-style UI (SVG face bitmap + plain text) ─────────────────────
  enum : uint8_t { D_TOP = 1, D_BODY = 2, D_STATS = 4, D_ALL = 7 };
  uint8_t        _dirty        = D_ALL;
  bool           _firstRender  = true;
  MoodFace::Mood _faceMood     = MoodFace::LOOKING;
  uint8_t        _faceVariant  = 0;
  unsigned long  _lastMoodMs   = 0;
  unsigned long  _lastFaceMs   = 0;   // periodic face-variant cycle
  char           _curMsg[96]   = {};

  // ── Capture engine ─────────────────────────────────────────────────────────
  void _flush();
  bool _checkFreeSpace();
  int  _updateValidation(EapolEntry& entry, const uint8_t* data, uint16_t len);
  bool _extractPmkid(const uint8_t* data, uint16_t len);
  int  _registerApTarget(const MacAddr& bssid, uint8_t ch, int8_t rssi);
  void _addClient(const uint8_t* bssid, const uint8_t* sta, uint8_t ch, int8_t rssi);
  void _writePcapHeader(const std::string& path);
  void _appendPcapFrame(const std::string& path, const uint8_t* data, uint16_t len);
  std::string _makePath(const MacAddr& bssid, const std::string& ssid);
  std::string _sanitize(const std::string& s);
  void _onHandshake(const std::string& ssid);

  // ── State machine ──────────────────────────────────────────────────────────
  void _runHunt();
  void _hopNext();
  int  _pickTarget();
  void _attackTarget(bool announce);
  String _ssidForIdx(int idx);
  void _txMgmt(uint8_t subtype, const uint8_t* dst, const uint8_t* src,
               const uint8_t* bssid, uint8_t reason);
  void _sendAssocRequest(const uint8_t* bssid, const char* ssid, uint8_t ssidLen);
  void _probePmkid();
  void _loadPwnNames();
  void _pwngridAdvertise(uint8_t ch, bool spam);

  // ── Radio ──────────────────────────────────────────────────────────────────
  void _startRadio();
  void _stopRadio();

  // ── UI ─────────────────────────────────────────────────────────────────────
  void _openOptionsMenu();
  void _openModeMenu();
  void _openStyleMenu();
  void _say(MoodFace::Mood mood, uint16_t faceColor, const String& phrase);
  String _statusSentence();
  void _drawTop(int bx, int by, int bw);
  void _drawBody(int bx, int by, int bw, int h);
  void _drawBodyText(int bx, int by, int bw, int h);
  void _drawStats(int bx, int by, int bw);
};
