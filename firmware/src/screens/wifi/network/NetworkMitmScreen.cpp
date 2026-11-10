#include "NetworkMitmScreen.h"

#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "screens/wifi/network/MitmReverseDnsScreen.h"

static constexpr uint64_t kMinFree = 64 * 1024;

static uint32_t s_captureMask = 0;  // relay calls the filter by fn ptr
static bool mitmCaptureFilter(const uint8_t* eth, uint16_t len);  // defined below
namespace {
uint32_t    metaBit(const MitmRelay::PacketMeta& m);
const char* protoName(uint32_t bit);
uint16_t    protoColor(uint32_t bit);
void        ipStr(uint32_t ipN, char* out, size_t n);
void        ipShort(uint32_t ipN, char* out, size_t n);
void        sizeStr(uint16_t len, char* out, size_t n);
}  // namespace

// Live Preview: the relay sink (WiFi task) writes PacketMeta into this lock-free
// ring; onUpdate() on the main loop drains it into the display list. Preview
// filter applied on drain (UI side), so it never slows the capture path.
static uint32_t s_previewMask = 0;              // 0 = All (subset of capture)
static constexpr uint8_t PREV_RING = 64;
static volatile MitmRelay::PacketMeta s_ring[PREV_RING];
static volatile uint8_t s_ringHead = 0;         // written by WiFi task
static volatile uint8_t s_ringTail = 0;         // read by main loop

// Display ring: drained, filtered entries. Pause freezes this, not the capture.
static constexpr uint8_t DISP_RING = 48;
static MitmRelay::PacketMeta s_disp[DISP_RING];
static uint8_t s_dispHead = 0;
static uint8_t s_dispCount = 0;
static uint32_t s_dispVersion = 0;
// Frozen copy taken on pause: navigation/Details read this, not the live ring.
static MitmRelay::PacketMeta s_frozen[DISP_RING];
static uint8_t s_frozenCount = 0;
static uint32_t s_counts[NetworkMitmScreen::FILTER_COUNT] = {};  // per-protocol tally (Counter view)
static uint32_t s_targetIps[4] = {};  // Targeted mode: shown as T1..T4 in the preview

static void dispPush(const MitmRelay::PacketMeta& m) {
  s_disp[s_dispHead] = m;
  s_dispHead = (uint8_t)((s_dispHead + 1) % DISP_RING);
  if (s_dispCount < DISP_RING) s_dispCount++;
  s_dispVersion++;
}
static bool dispGet(uint8_t offset, MitmRelay::PacketMeta& out) {
  if (offset >= s_dispCount) return false;
  const uint8_t idx = (uint8_t)((s_dispHead + DISP_RING - 1 - offset) % DISP_RING);
  out = s_disp[idx];
  return true;
}
// Copy the live ring newest-first into the frozen buffer (called on pause).
static void dispFreeze() {
  s_frozenCount = s_dispCount;
  for (uint8_t i = 0; i < s_dispCount; ++i) dispGet(i, s_frozen[i]);
}
// Paused reads come from the frozen snapshot; live reads from the ring.
static bool frozenGet(uint8_t offset, MitmRelay::PacketMeta& out) {
  if (offset >= s_frozenCount) return false;
  out = s_frozen[offset];
  return true;
}

NetworkMitmScreen::~NetworkMitmScreen() {
  if (_state == STATE_RUNNING) _stop("Screen closed");
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void NetworkMitmScreen::onInit() {
  _showMenu();
}

void NetworkMitmScreen::onBack() {
  if (_state == STATE_DETAILS) { _state = STATE_RUNNING; render(); return; }
  if (_state == STATE_RUNNING) {
    _stop("Stopped");
    _showMenu();
    return;
  }
  if (_state == STATE_FILTERS) { _showMenu(_menuIndex); return; }
  Screen.goBack();
}

// ── Menu ────────────────────────────────────────────────────────────────────

// Row kinds, so a row can move as the mode changes without a fixed index.
enum : uint8_t { R_MODE, R_TARGET1, R_TARGET2, R_TARGET3, R_TARGET4,
                 R_FILTERS, R_PREVIEW, R_PREVFILT, R_SAVE, R_FILENAME, R_START };

void NetworkMitmScreen::_showMenu(uint8_t selected) {
  _state = STATE_MENU;
  static const char* kModeNames[] = {"ARP Poison - All", "ARP Poison - Targets", "Passive"};
  const bool decrypted = _mode != MODE_PASSIVE;

  uint8_t n = 0;
  auto add = [&](const char* label, const String& val, uint8_t kind) {
    if (n >= MAX_ROWS) return;
    snprintf(_rowLabels[n], sizeof(_rowLabels[n]), "%s", label);  // persistent
    _subs[n] = val;
    _menuItems[n] = {_rowLabels[n], _subs[n].length() ? _subs[n].c_str() : nullptr};
    _rowKind[n] = kind; ++n;
  };
  add("Mode", kModeNames[_mode], R_MODE);
  if (_mode == MODE_TARGETS) {
    for (uint8_t i = 0; i < 4; ++i) {
      char lbl[12]; snprintf(lbl, sizeof(lbl), "Target %u", i + 1);
      add(lbl, _targets[i] ? String(_targetText[i]) : String("-"), R_TARGET1 + i);
    }
  }
  // Capture filters only mean something on decrypted traffic.
  if (decrypted) add("Capture Filters", _captureMask ? "Custom" : "All", R_FILTERS);
  // Preview (Counter/Traffic) and its filters only apply to decrypted traffic.
  if (decrypted) {
    static const char* kPreviewNames[] = {"Counters", "Packets"};
    add("Preview", kPreviewNames[_livePreview], R_PREVIEW);
    add("Preview Filters", _previewMask ? "Custom" : "All", R_PREVFILT);
  }
  add("Save to File", _saveToFile ? "On" : "Off", R_SAVE);
  if (_saveToFile) add("File Name", _fileName.length() ? _fileName : String("Auto"), R_FILENAME);
  add("Start", String(), R_START);

  _rowCount = n;
  setItems(_menuItems, n, selected < n ? selected : n - 1);
}

void NetworkMitmScreen::_editTarget(uint8_t i) {
  const String prefix = WiFi.localIP().toString();
  const int dot = prefix.lastIndexOf('.');
  const String initial = _targets[i] ? String(_targetText[i])
                                     : (dot > 0 ? prefix.substring(0, dot + 1) : String());
#ifdef DEVICE_HAS_KEYBOARD
  const auto mode = InputTextAction::INPUT_TEXT;
#else
  const auto mode = InputTextAction::INPUT_IP_ADDRESS;
#endif
  String v = InputTextAction::popup("Target IP", initial, mode);
  render();  // the popup wipes its area to black and does not restore the menu
  if (InputTextAction::wasCancelled()) return;
  v.trim();
  if (v.length() == 0) { _targets[i] = 0; _targetText[i][0] = '\0'; return; }
  IPAddress ip;
  if (!ip.fromString(v)) { ShowStatusAction::show("Invalid IP", 1200); return; }
  _targets[i] = (uint32_t)ip;  // network order on ESP32
  snprintf(_targetText[i], sizeof(_targetText[i]), "%s", v.c_str());
}

void NetworkMitmScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_FILTERS) {
    uint32_t& mask = _editingPreview ? _previewMask : _captureMask;
    if (index == 0) {
      mask = 0;  // All
    } else if (index < _filterRowCount) {
      mask ^= (1u << _filterBitOfRow[index]);
    }
    // Preview can only narrow what capture keeps.
    if (_captureMask) _previewMask &= _captureMask;
    // SSH/DHCP pattern: pin selection, then rebuild.
    _selectedIndex = index;
    _showFilters();
    return;
  }
  if (_state != STATE_MENU || index >= _rowCount) return;
  _menuIndex = index;
  _selectedIndex = index;

  switch (_rowKind[index]) {
    case R_MODE:
      _mode = (Mode)((_mode + 1) % 3);
      _selectedIndex = 0;  // Mode is row 0; rows below change with the mode
      _showMenu(_selectedIndex);
      break;
    case R_TARGET1: case R_TARGET2: case R_TARGET3: case R_TARGET4:
      _editTarget(_rowKind[index] - R_TARGET1);
      _showMenu(_selectedIndex);
      break;
    case R_FILTERS:
      _editingPreview = false;
      _selectedIndex = 0;  // fresh list → top
      _showFilters();
      break;
    case R_PREVIEW:
      _livePreview = (PreviewMode)((_livePreview + 1) % 2);
      _showMenu(_selectedIndex);
      break;
    case R_PREVFILT:
      _editingPreview = true;
      _selectedIndex = 0;
      _showFilters();
      break;
    case R_SAVE:
      if (!_saveToFile && (!Uni.StorageSD || !Uni.StorageSD->isAvailable())) {
        ShowStatusAction::show("SD card not available", 1500); render(); return;
      }
      _saveToFile = !_saveToFile;
      _showMenu(_selectedIndex);
      break;
    case R_FILENAME: {
      String v = InputTextAction::popup("File Name", _fileName, InputTextAction::INPUT_TEXT);
      render();
      if (!InputTextAction::wasCancelled()) { v.trim(); _fileName = v; }
      _showMenu(_selectedIndex);
      break;
    }
    case R_START:
      _start();
      break;
  }
}

void NetworkMitmScreen::_showFilters() {
  static const char* kNames[FILTER_COUNT] = {
    "ARP", "ICMP", "DNS", "DHCP", "mDNS", "LLMNR", "NetBIOS", "SSDP",
    "HTTP", "TLS/HTTPS", "QUIC", "SSH", "Telnet", "RDP", "VNC",
    "FTP", "SMB", "SMTP", "IMAP", "POP3", "NTP", "MQTT", "SIP", "Other"
  };
  _state = STATE_FILTERS;
  const uint32_t mask = _editingPreview ? _previewMask : _captureMask;
  // Preview lists only protocols Capture keeps (All capture = every protocol).
  const uint32_t allowed = (_editingPreview && _captureMask) ? _captureMask : 0xFFFFFFFFu;

  uint8_t n = 0;
  snprintf(_filterLabels[n], sizeof(_filterLabels[n]), "[%c] All", mask == 0 ? 'x' : ' ');
  _filterItems[n] = {_filterLabels[n], nullptr, 0, false, false};
  _filterBitOfRow[n] = 0xFF;
  ++n;

  for (uint8_t i = 0; i < FILTER_COUNT; ++i) {
    if (!(allowed & (1u << i))) continue;
    snprintf(_filterLabels[n], sizeof(_filterLabels[n]), "[%c] %s",
             (mask & (1u << i)) ? 'x' : ' ', kNames[i]);
    _filterItems[n] = {_filterLabels[n], nullptr, 0, false, false};
    _filterBitOfRow[n] = i;
    ++n;
  }
  _filterRowCount = n;

  // Same as SshClientScreen::_rebuildItems / DhcpAttackScreen::_showMenu.
  setItems(_filterItems, n, min<uint8_t>(_selectedIndex, n > 0 ? (uint8_t)(n - 1) : 0));
}

// ── Start ───────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_start() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return;
  }
  // Mode -> engine flags. All/Targets poison (relay, decrypted); Passive does
  // not (monitor, encrypted). Targets needs at least one IP.
  _arpEnabled     = _mode != MODE_PASSIVE;
  _snifferEnabled = _saveToFile;
  uint32_t peerIps[4];
  uint8_t  peerN = 0;
  if (_mode == MODE_TARGETS) {
    for (uint8_t i = 0; i < 4; ++i) if (_targets[i]) peerIps[peerN++] = _targets[i];
    if (peerN == 0) { ShowStatusAction::show("No target set", 1500); return; }
  }

  _relayUp = _arpUp = _pcapUp = false;
  _phase       = PHASE_SWEEP;

  char buf[60];
  char mac[20];
  snprintf(buf, sizeof(buf), "IP: %s, gateway: %s",
           WiFi.localIP().toString().c_str(),
           WiFi.gatewayIP().toString().c_str());

  // Our MAC is what a poisoned victim should show for the gateway. Print it
  // here so the two values can be compared without leaving this screen.
  uint8_t self[6];
  WiFi.macAddress(self);
  _fmtMac(self, mac, sizeof(mac));
  snprintf(buf, sizeof(buf), "MAC: %s", mac);

  // Baseline heap. Serving portal files is the heaviest allocator in the whole
  // attack, so knowing what we started with tells a leak apart from simply not
  // having had the room to begin with.
  const uint32_t heap0 = ESP.getFreeHeap() / 1024;
  snprintf(buf, sizeof(buf), "Heap: %lu KB free", (unsigned long)heap0);

  // 1. Capture file. Link type depends on where the frames come from: the relay
  //    yields decrypted Ethernet, the monitor fallback raw 802.11.
  const bool relayMode = _arpEnabled;
  // A chosen file name overrides the auto SSID-based one.
  const char* nameBase = _fileName.length() ? _fileName.c_str() : WiFi.SSID().c_str();
  if (_snifferEnabled) {
    if (!Uni.StorageSD || !Uni.StorageSD->isAvailable()) {
      _snifferEnabled = false;
    } else if (Uni.StorageSD->freeBytes() < kMinFree) {
      _snifferEnabled = false;
    } else {
      const uint32_t linktype = relayMode ? PcapWriter::LINKTYPE_ETHERNET
                                          : PcapWriter::LINKTYPE_IEEE802_11;
      if (_pcap.begin(Uni.StorageSD, SAVE_DIR, nameBase, linktype, MitmRelay::SNAP_LEN)) {
        _pcapUp = true;
        snprintf(buf, sizeof(buf), "PCAP: %s", _pcap.filename().c_str());
      } else {
        snprintf(buf, sizeof(buf), "%s", _pcap.error() ? _pcap.error() : "Failed to create PCAP");
        _snifferEnabled = false;
      }
    }
  }

  // 2. ARP spoofer — must be up before the relay so the hook can feed it.
  if (_arpEnabled) {
    if (_arp.begin()) {
      // setTargets after begin() so self/gateway are known and filtered out.
      // With two or more targets the spoofer also poisons each against the
      // others, so traffic between them is intercepted even when neither is
      // the gateway.
      _arp.setTargets(_mode == MODE_TARGETS ? peerIps : nullptr,
                      _mode == MODE_TARGETS ? peerN : 0);
      _arpUp = true;
      snprintf(buf, sizeof(buf), "Sweeping %lu hosts...",
               (unsigned long)_arp.sweepTotal());
    } else {
      _arpEnabled = false;
    }
  }

  // 3. The engine. Nothing above matters if this does not come up.
  if (_arpEnabled || _snifferEnabled) {
    _relay.setPcap(_pcapUp ? &_pcap : nullptr);
    _relay.setArp(_arpUp ? &_arp : nullptr);
    _relay.setForward(_arpEnabled);
    // Capture Filters only apply to decrypted (relay) traffic.
    s_captureMask = relayMode ? _captureMask : 0;
    _relay.setCaptureFilter(relayMode ? &mitmCaptureFilter : nullptr);
    // Live Preview only on decrypted (relay) traffic.
    const bool preview = relayMode;   // always previewing (Counter/Traffic)
    s_previewMask = preview ? _previewMask : 0;
    s_ringHead = s_ringTail = 0;
    s_dispHead = s_dispCount = 0;
    memset((void*)s_counts, 0, sizeof(s_counts));
    s_dispVersion = 0;
    _previewPaused = false;
    _previewSel = _previewView = 0;
    _relay.setPreviewSink(preview ? &_previewSink : nullptr);

    if (_relay.begin(relayMode ? MitmRelay::MODE_RELAY : MitmRelay::MODE_MONITOR)) {
      _relayUp = true;
    } else {
      snprintf(buf, sizeof(buf), "Relay: %s", _relay.error());
      _stop(_relay.error());
      _showMenu();
      return;
    }
  }

  if (!_relayUp) {
    // Everything the user picked failed to come up. The per-component lines
    // above already say why; repeat the first cause in the toast so it is
    // visible without scrolling the log.
    _stop(_pcap.error() ? _pcap.error() : "Nothing started");
    _showMenu();
    return;
  }

  // Registered as id 52 in AchievementManager but never fired by the previous
  // implementation of this screen.
  if (Achievement.inc("wifi_mitm_started") == 1) Achievement.unlock("wifi_mitm_started");

  _state       = STATE_RUNNING;
  _lastSweep   = 0;
  _lastPoison  = 0;
  _lastDraw    = 0;
  _lastFreeChk = millis();
  _sessionStartMs = millis();
  _lastRateMs = _sessionStartMs;
  _lastRateSeen = 0;
  _packetsPerSec = 0;
  setItems(nullptr, 0);
  render();
}

// ── Stop ────────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_stop(const char* reason) {
  // Tear the relay down before touching the ARP table: its RX hook calls
  // observe(), which writes the same entries restore() is about to walk.
  if (_relayUp) { _relay.end(); _relayUp = false; }

  // Then un-poison. A victim left with our MAC cached loses its network until
  // the entry ages out, which can take minutes. Transmitting does not depend on
  // the relay, so ordering it after the teardown costs nothing.
  if (_arpUp) {
    _arp.restore();
    _arp.end();
    _arpUp = false;
  }
  if (_pcapUp)  { _pcap.end(); _pcapUp = false; }

  _arpEnabled = _snifferEnabled = false;
  _state      = STATE_MENU;

  char buf[64];
  snprintf(buf, sizeof(buf), "%s - %lu frames",
           reason ? reason : "Stopped", (unsigned long)_pcap.frames());
  ShowStatusAction::show(buf, 2000);
}

// ── Update ──────────────────────────────────────────────────────────────────

void NetworkMitmScreen::onUpdate() {
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  const unsigned long now = millis();

  if (_relayUp) _relay.update();

  if (_arpUp) {
    // Answers to reachability probes the driver refused. Runs every iteration
    // and paces itself — it has to get in ahead of everything else here, since
    // a probe left unanswered costs the victim.
    _arp.flushRetries();

    // Sweeping and poisoning now run side by side: a rescan must never pause
    // the attack on hosts already known.
    if (!_arp.sweepDone() && now - _lastSweep > 80) {
      _arp.sweepStep();
      _lastSweep = now;

      if (_arp.sweepDone() && _phase == PHASE_SWEEP) {
        _phase      = PHASE_POISON;
        _lastRescan = now;
      }
    }

    if (_phase == PHASE_POISON) {
      // Frequent but tiny. poisonStep() now emits two frames (one target, both
      // directions) plus a broadcast every fourth call, so this is ~7 frames a
      // second — the same rate as the old one-second batch of seven, just
      // spread out. The batch was the problem: the WiFi TX buffers come from
      // the heap, and with a portal page being served the pool is empty by the
      // second frame of any burst.
      if (now - _lastPoison > 300) {
        _arp.poisonStep();
        _lastPoison = now;
      }

      // One pass only finds hosts that were awake and quick. Re-arm the sweep
      // periodically so sleeping devices and new joiners get picked up.
      if (now - _lastRescan > 120000) {
        _arp.restartSweep();
        _lastRescan = now;
      }
    }
  }

  if (_relayUp && _relay.storageFailed()) {
    _stop("Failed to write to SD");
    _showMenu();
    return;
  }

  if (_pcapUp && now - _lastFreeChk > 5000) {
    _lastFreeChk = now;
    if (!_checkFreeSpace()) {
      _stop("SD full");
      _showMenu();
      return;
    }
  }

  if (now - _lastRateMs >= 1000) {
    const uint32_t seen = _relay.seen();
    const uint32_t elapsed = now - _lastRateMs;
    _packetsPerSec = elapsed ? (uint32_t)((uint64_t)(seen - _lastRateSeen) * 1000ULL / elapsed) : 0;
    _lastRateSeen = seen;
    _lastRateMs = now;
  }

  // Redraw the list only when new packets arrived (throttled), and refresh the
  // Time/P/s line once a second. A full clear every tick is what flickered.
  static uint32_t s_drawnVersion = 0;
  const bool listDirty = s_dispVersion != s_drawnVersion;
  const bool statsDue  = now - _lastDraw >= 1000;
  if (!_previewPaused && (listDirty || statsDue) && now - _lastDraw >= 200) {
    s_drawnVersion = s_dispVersion;
    render();
    _lastDraw = now;
  }

  // Drain the preview ring into the display list (unless paused). Capture never
  // pauses — only the on-screen list freezes for inspection.
  if (_arpEnabled && !_previewPaused && _state != STATE_DETAILS) {
    while (s_ringTail != s_ringHead) {
      volatile MitmRelay::PacketMeta& v = s_ring[s_ringTail];
      MitmRelay::PacketMeta m;
      m.ethertype = v.ethertype; m.ipProto = v.ipProto;
      m.srcIp = v.srcIp; m.dstIp = v.dstIp;
      m.srcPort = v.srcPort; m.dstPort = v.dstPort; m.length = v.length;
      s_ringTail = (uint8_t)((s_ringTail + 1) % PREV_RING);
      const uint32_t bit = metaBit(m);
      // Counter view tallies every captured packet (before the preview filter).
      for (uint8_t b = 0; b < NetworkMitmScreen::FILTER_COUNT; ++b)
        if (bit == (1u << b)) { s_counts[b]++; break; }
      if (s_previewMask && !(s_previewMask & bit)) continue;
      dispPush(m);
    }
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (_state == STATE_DETAILS) {
      if (dir == INavigation::DIR_BACK) { _state = STATE_RUNNING; render(); }
      return;
    }
    if (_livePreview == PREVIEW_COUNTER && _arpEnabled) {
      if (dir == INavigation::DIR_BACK) { _stop("Stopped"); _showMenu(); }
      return;  // running total: no pause/inspect
    }
    if (_livePreview == PREVIEW_TRAFFIC && _arpEnabled) {
      if (!_previewPaused) {
        if (dir == INavigation::DIR_PRESS) {
          dispFreeze();                 // stable snapshot for inspection
          _previewPaused = true;
          _previewSel = _previewView = 0;
          render();
          return;
        }
        if (dir == INavigation::DIR_BACK) { _stop("Stopped"); _showMenu(); return; }
      } else {
        if (dir == INavigation::DIR_BACK) { _previewPaused = false; render(); return; }
        if (dir == INavigation::DIR_PRESS) {
          if (s_frozenCount) _showPreviewOptions();
          return;
        }
        // Newest is at the top: Down/Right moves to older, Up/Left to newer.
        const bool down = dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT;
        const bool up   = dir == INavigation::DIR_UP   || dir == INavigation::DIR_LEFT;
        if (down && _previewSel + 1 < s_frozenCount) { _previewSel++; render(); return; }
        if (up   && _previewSel > 0)               { _previewSel--; render(); return; }
      }
      return;
    }
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop("Stopped");
      _showMenu();
    }
  }
}

void NetworkMitmScreen::_fmtMac(const uint8_t* mac, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool NetworkMitmScreen::_checkFreeSpace() {
  return Uni.StorageSD && Uni.StorageSD->freeBytes() >= kMinFree;
}

void NetworkMitmScreen::onRender() {
  if (_state == STATE_DETAILS) { _drawDetails(); return; }
  if (_state == STATE_RUNNING) {
    if (_arpEnabled && _livePreview == PREVIEW_COUNTER) { _drawCounters(); return; }
    if (_arpEnabled && _livePreview == PREVIEW_TRAFFIC) { _drawPreview(); return; }
    _drawCapturing();
    return;
  }
  ListScreen::onRender();
}

void NetworkMitmScreen::_drawStats(int x, int y, int w) {
  auto& lcd = Uni.Lcd;
  const uint32_t seconds = (millis() - _sessionStartMs) / 1000;
  char elapsed[12], left[20], mid[20], right[16];
  if (seconds >= 3600)
    snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu:%02lu",
             (unsigned long)(seconds / 3600), (unsigned long)((seconds / 60) % 60),
             (unsigned long)(seconds % 60));
  else
    snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu",
             (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  snprintf(left,  sizeof(left),  "Time %s", elapsed);
  snprintf(mid,   sizeof(mid),   "Total %lu", (unsigned long)_relay.seen());
  snprintf(right, sizeof(right), "P/s %lu", (unsigned long)_packetsPerSec);

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.setTextDatum(TL_DATUM);
  lcd.drawString(left, x + 2, y);
  lcd.setTextDatum(TC_DATUM);
  lcd.drawString(mid, x + w / 2, y);
  lcd.setTextDatum(TR_DATUM);
  lcd.drawString(right, x + w - 2, y);
  lcd.setTextDatum(TL_DATUM);
}

void NetworkMitmScreen::_drawCapturing() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  const int fh = lcd.fontHeight();
  const int footerH = fh + 6;

  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Sniffing...", bodyX() + bodyW() / 2, bodyY() + (bodyH() - footerH) / 2);

  const int sepY = bodyY() + bodyH() - footerH;
  lcd.drawFastHLine(bodyX(), sepY, bodyW(), TFT_DARKGREY);
  _drawStats(bodyX(), sepY + 2, bodyW());
}

void NetworkMitmScreen::_drawPreview() {
  // Per-line sprites, same as LogView: each row is pushed atomically, so there
  // is no full-body black flash.
  auto& panel = Uni.Lcd;
  panel.setTextSize(1);
  const int fh = panel.fontHeight();
  const int lineH = fh + 3;
  const int footerH = fh * 2 + 8;  // stats + one hint line, like Packet Sniffer
  const int bh = bodyH();
  const int slots = (bh - footerH) / lineH;
  const int listH = slots > 0 ? slots * lineH : 0;

  // Nothing captured yet: show a single centred hint over the list area.
  const uint8_t shown = _previewPaused ? s_frozenCount : s_dispCount;
  if (shown == 0) {
    panel.fillRect(bodyX(), bodyY(), bodyW(), listH, TFT_BLACK);
    panel.setTextColor(TFT_DARKGREY, TFT_BLACK);
    panel.setTextDatum(MC_DATUM);
    panel.drawString("Sniffing...", bodyX() + bodyW() / 2, bodyY() + listH / 2);
    panel.setTextDatum(TL_DATUM);
    _drawStats(bodyX(), bodyY() + listH + 1, bodyW());
    return;
  }

  if (_previewPaused && slots > 0) {
    if (_previewSel < _previewView) _previewView = _previewSel;
    if (_previewSel >= _previewView + slots)
      _previewView = _previewSel - slots + 1;
  }

  int y = bodyY();
  Sprite row(&panel);
  row.createSprite(bodyW(), lineH);
  row.setTextSize(1);
  row.setTextDatum(TL_DATUM);
  for (int i = 0; i < slots; i++) {
    const uint8_t off = (_previewPaused ? _previewView : 0) + i;
    MitmRelay::PacketMeta m;
    const bool have = _previewPaused ? frozenGet(off, m) : dispGet(off, m);
    const uint32_t bit = have ? metaBit(m) : 0;
    const bool selected = have && _previewPaused && off == _previewSel;
    const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
    row.fillSprite(bg);

    if (have) {
      char src[8] = "", dst[8] = "", sz[8] = "", rest[36];
      if (m.ethertype == 0x0800) { ipShort(m.srcIp, src, sizeof(src)); ipShort(m.dstIp, dst, sizeof(dst)); }
      sizeStr(m.length, sz, sizeof(sz));
      if (m.srcPort || m.dstPort)
        snprintf(rest, sizeof(rest), "%s:%u>%s:%u", src, m.srcPort, dst, m.dstPort);
      else if (m.ethertype == 0x0800)
        snprintf(rest, sizeof(rest), "%s>%s", src, dst);
      else
        rest[0] = '\0';

      const char* name = protoName(bit);
      row.setTextDatum(TL_DATUM);
      row.setTextColor(protoColor(bit), bg);
      row.drawString(name, 2, 1);
      row.setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY, bg);
      row.setTextDatum(TR_DATUM);
      row.drawString(sz, bodyW() - 2, 1);

      // Address centred in the gap between the protocol and the size.
      const int left = 2 + row.textWidth(name) + 4;
      const int right = bodyW() - 2 - row.textWidth(sz) - 4;
      if (rest[0] && right > left) {
        while (rest[0] && row.textWidth(rest) > right - left)
          rest[strlen(rest) - 1] = '\0';
        row.setTextDatum(TC_DATUM);
        row.drawString(rest, (left + right) / 2, 1);
      }
      row.setTextDatum(TL_DATUM);
    }
    row.pushSprite(bodyX(), y);
    y += lineH;
  }
  row.deleteSprite();

  const int statusY = bodyY() + listH;
  panel.drawFastHLine(bodyX(), statusY, bodyW(), TFT_DARKGREY);
  panel.fillRect(bodyX(), statusY + 1, bodyW(), fh + 2, TFT_BLACK);
  _drawStats(bodyX(), statusY + 2, bodyW());

  const int hintSep = statusY + fh + 4;
  panel.drawFastHLine(bodyX(), hintSep, bodyW(), TFT_DARKGREY);
  panel.fillRect(bodyX(), hintSep + 1, bodyW(), bodyY() + bodyH() - hintSep - 1, TFT_BLACK);
  // One centred hint line, as in the Packet Sniffer.
  panel.setTextDatum(BC_DATUM);
  panel.setTextColor(TFT_DARKGREY, TFT_BLACK);
  panel.drawString(_previewPaused ? "[Press] Options  [Back] Resume" : "[Press] Pause",
                   bodyX() + bodyW() / 2, bodyY() + bodyH() - 1);
  panel.setTextDatum(TL_DATUM);
}

void NetworkMitmScreen::_drawCounters() {
  // Per-protocol packet counts, names in their protocol colour. Two columns;
  // only protocols kept by the Preview Filters are shown (All = every one with
  // a non-zero count, plus the always-present common ones).
  auto& lcd = Uni.Lcd;
  lcd.setTextSize(1);
  const int fh = lcd.fontHeight();
  const int rowH = fh + 4;
  const int footerH = fh * 2 + 6;
  const int bh = bodyH();
  const int rows = (bh - footerH) / rowH;
  const int colW = bodyW() / 2;

  lcd.fillRect(bodyX(), bodyY(), bodyW(), bh - footerH, TFT_BLACK);

  // Build the visible protocol list: filtered subset, or all with activity.
  uint8_t order[FILTER_COUNT]; uint8_t n = 0;
  for (uint8_t b = 0; b < FILTER_COUNT; ++b) {
    const uint32_t bit = 1u << b;
    if (_previewMask && !(_previewMask & bit)) continue;   // respect Preview Filters
    if (!_previewMask && s_counts[b] == 0) continue;       // All: hide idle protocols
    order[n++] = b;
  }

  int x = bodyX() + 2, y = bodyY() + 2, col = 0;
  char cnt[12];
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t b = order[i];
    const uint32_t bit = 1u << b;
    if (col == 0 && (y + rowH > bodyY() + bh - footerH)) { col = 1; x = bodyX() + colW + 2; y = bodyY() + 2; }
    if (col == 1 && (y + rowH > bodyY() + bh - footerH)) break;  // no room
    lcd.setTextColor(protoColor(bit), TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    lcd.drawString(protoName(bit), x, y);
    snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)s_counts[b]);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString(cnt, x + colW - 6, y);
    y += rowH;
  }
  lcd.setTextDatum(TL_DATUM);
  if (n == 0) {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.drawString("Sniffing...", bodyX() + bodyW() / 2, bodyY() + (bh - footerH) / 2);
    lcd.setTextDatum(TL_DATUM);
  }

  // Footer: the stats bar, no pause (Counter is a running total; Back stops).
  const int statusY = bodyY() + bh - footerH;
  lcd.drawFastHLine(bodyX(), statusY, bodyW(), TFT_DARKGREY);
  lcd.fillRect(bodyX(), statusY + 1, bodyW(), footerH - 1, TFT_BLACK);
  _drawStats(bodyX(), statusY + 2, bodyW());
}

// Paused preview, a packet selected: Options popup (Details, DNS Lookup). DNS
// Lookup does a reverse (PTR) lookup of the selected packet's source IP.
void NetworkMitmScreen::_showPreviewOptions() {
  MitmRelay::PacketMeta m;
  if (!frozenGet(_previewSel, m)) return;
  const bool haveIp = m.ethertype == 0x0800 && m.srcIp;
  InputSelectAction::Option opts[2];
  uint8_t count = 0;
  opts[count++] = {"Details", "details", nullptr, 0, false};
  if (haveIp) opts[count++] = {"DNS Lookup", "dns", nullptr, 0, false};
  const char* choice = InputSelectAction::popup("Options", opts, count);
  // Clear the whole body: the popup drew over the list and the stats footer,
  // and _drawPreview only repaints the list rows, so a cleared body avoids a
  // leftover black rectangle where the popup was.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  if (!choice) { render(); return; }
  if (strcmp(choice, "details") == 0) {
    _state = STATE_DETAILS;
    render();
  } else if (strcmp(choice, "dns") == 0 && haveIp) {
    char ip[16]; ipStr(m.srcIp, ip, sizeof(ip));
    auto* dns = new MitmReverseDnsScreen();   // runs the PTR straight away
    dns->configure(ip);
    Screen.push(dns);                          // Back returns to the paused preview
  }
}

void NetworkMitmScreen::_drawDetails() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  MitmRelay::PacketMeta m;
  if (!frozenGet(_previewSel, m)) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("No entry", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
    return;
  }
  const uint32_t bit = metaBit(m);
  char src[16] = "-", dst[16] = "-";
  if (m.ethertype == 0x0800) { ipStr(m.srcIp, src, sizeof(src)); ipStr(m.dstIp, dst, sizeof(dst)); }

  // Same layout as Packet Sniffer details: grey labels, white values, type in
  // its colour, 2 px gap, no footer hint.
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  const int fh = lcd.fontHeight();
  const int gap = 2;
  int y = bodyY() + 2;
  const int bottom = bodyY() + bodyH();
  const int x = bodyX() + 2;
  const int valueRight = bodyX() + bodyW() - 2;
  auto row = [&](const char* label, const char* text, uint16_t color = TFT_WHITE) {
    if (y + fh > bottom) return;
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(label, x, y);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString(text, valueRight, y);
    lcd.setTextDatum(TL_DATUM);
    y += fh + gap;
  };
  char value[24];
  row("Protocol", protoName(bit), protoColor(bit));
  row("Source", src);
  row("Dest", dst);
  snprintf(value, sizeof(value), "%u", m.srcPort);
  row("Src Port", value);
  snprintf(value, sizeof(value), "%u", m.dstPort);
  row("Dst Port", value);
  snprintf(value, sizeof(value), "%u bytes", (unsigned)m.length);
  row("Length", value);
  snprintf(value, sizeof(value), "0x%04X", m.ethertype);
  row("EtherType", value);
}

// ── Status bar ──────────────────────────────────────────────────────────────

// Etapa 1 capture filter: true when the decrypted Ethernet frame matches the
// Capture Filters (All passes everything). Reads only headers (EtherType, IP
// protocol, TCP/UDP ports) to decide which packets to keep — never content.
static bool mitmCaptureFilter(const uint8_t* eth, uint16_t len) {
  using M = NetworkMitmScreen;
  const uint32_t m = s_captureMask;
  if (m == 0) return true;            // All
  if (len < 14) return m & M::F_OTHER;
  const uint16_t ethertype = (eth[12] << 8) | eth[13];
  if (ethertype == 0x0806) return m & M::F_ARP;
  if (ethertype != 0x0800) return m & M::F_OTHER;   // non-IPv4
  if (len < 14 + 20) return m & M::F_OTHER;
  const uint8_t* ip = eth + 14;
  const uint8_t ihl = (ip[0] & 0x0F) * 4;
  const uint8_t proto = ip[9];
  if (proto == 1) return m & M::F_ICMP;
  if (proto == 6 || proto == 17) {   // TCP / UDP
    const uint8_t* l4 = ip + ihl;
    if (len < (uint16_t)(14 + ihl + 4)) return m & M::F_OTHER;
    const uint16_t sp = (l4[0] << 8) | l4[1], dp = (l4[2] << 8) | l4[3];
    auto hit = [&](uint16_t port, uint32_t bit) {
      return (sp == port || dp == port) && (m & bit);
    };
    if (hit(53, M::F_DNS)) return true;
    if (hit(67, M::F_DHCP) || hit(68, M::F_DHCP)) return true;
    if (hit(5353, M::F_MDNS)) return true;
    if (hit(5355, M::F_LLMNR)) return true;
    if (hit(137, M::F_NETBIOS) || hit(138, M::F_NETBIOS) || hit(139, M::F_NETBIOS)) return true;
    if (hit(1900, M::F_SSDP)) return true;
    if (hit(80, M::F_HTTP)) return true;
    if (proto == 17 && hit(443, M::F_QUIC)) return true;
    if (proto == 6  && hit(443, M::F_TLS))  return true;
    if (hit(22, M::F_SSH)) return true;
    if (hit(23, M::F_TELNET)) return true;
    if (hit(3389, M::F_RDP)) return true;
    if (hit(5900, M::F_VNC)) return true;
    if (hit(21, M::F_FTP)) return true;
    if (hit(445, M::F_SMB)) return true;
    if (hit(25, M::F_SMTP) || hit(587, M::F_SMTP)) return true;
    if (hit(143, M::F_IMAP) || hit(993, M::F_IMAP)) return true;
    if (hit(110, M::F_POP3) || hit(995, M::F_POP3)) return true;
    if (hit(123, M::F_NTP)) return true;
    if (hit(1883, M::F_MQTT) || hit(8883, M::F_MQTT)) return true;
    if (hit(5060, M::F_SIP)) return true;
    return m & M::F_OTHER;
  }
  return m & M::F_OTHER;
}


// WiFi-task callback: stash header-only metadata, drop if the ring is full
// (preview is best-effort; the PCAP is the record). Cheap and non-blocking.
void NetworkMitmScreen::_previewSink(const MitmRelay::PacketMeta& m) {
  const uint8_t next = (uint8_t)((s_ringHead + 1) % PREV_RING);
  if (next == s_ringTail) return;   // full — skip
  volatile MitmRelay::PacketMeta& slot = s_ring[s_ringHead];
  slot.ethertype = m.ethertype; slot.ipProto = m.ipProto;
  slot.srcIp = m.srcIp; slot.dstIp = m.dstIp;
  slot.srcPort = m.srcPort; slot.dstPort = m.dstPort; slot.length = m.length;
  s_ringHead = next;
}

namespace {
uint32_t metaBit(const MitmRelay::PacketMeta& m) {
  using M = NetworkMitmScreen;
  if (m.ethertype == 0x0806) return M::F_ARP;
  if (m.ethertype != 0x0800) return M::F_OTHER;
  if (m.ipProto == 1) return M::F_ICMP;
  if (m.ipProto == 6 || m.ipProto == 17) {
    const uint16_t sp = m.srcPort, dp = m.dstPort;
    auto is = [&](uint16_t p){ return sp == p || dp == p; };
    if (is(53))   return M::F_DNS;
    if (is(67) || is(68)) return M::F_DHCP;
    if (is(5353)) return M::F_MDNS;
    if (is(5355)) return M::F_LLMNR;
    if (is(137) || is(138) || is(139)) return M::F_NETBIOS;
    if (is(1900)) return M::F_SSDP;
    if (is(80))   return M::F_HTTP;
    if (m.ipProto == 17 && is(443)) return M::F_QUIC;
    if (m.ipProto == 6  && is(443)) return M::F_TLS;
    if (is(22))   return M::F_SSH;
    if (is(23))   return M::F_TELNET;
    if (is(3389)) return M::F_RDP;
    if (is(5900)) return M::F_VNC;
    if (is(21))   return M::F_FTP;
    if (is(445))  return M::F_SMB;
    if (is(25) || is(587)) return M::F_SMTP;
    if (is(143) || is(993)) return M::F_IMAP;
    if (is(110) || is(995)) return M::F_POP3;
    if (is(123))  return M::F_NTP;
    if (is(1883) || is(8883)) return M::F_MQTT;
    if (is(5060)) return M::F_SIP;
  }
  return M::F_OTHER;
}

const char* protoName(uint32_t bit) {
  switch (bit) {
    case NetworkMitmScreen::F_ARP:     return "ARP";
    case NetworkMitmScreen::F_ICMP:    return "ICMP";
    case NetworkMitmScreen::F_DNS:     return "DNS";
    case NetworkMitmScreen::F_DHCP:    return "DHCP";
    case NetworkMitmScreen::F_MDNS:    return "mDNS";
    case NetworkMitmScreen::F_LLMNR:   return "LLMNR";
    case NetworkMitmScreen::F_NETBIOS: return "NB";
    case NetworkMitmScreen::F_SSDP:    return "SSDP";
    case NetworkMitmScreen::F_HTTP:    return "HTTP";
    case NetworkMitmScreen::F_TLS:     return "TLS";
    case NetworkMitmScreen::F_QUIC:    return "QUIC";
    case NetworkMitmScreen::F_SSH:     return "SSH";
    case NetworkMitmScreen::F_TELNET:  return "TEL";
    case NetworkMitmScreen::F_RDP:     return "RDP";
    case NetworkMitmScreen::F_VNC:     return "VNC";
    case NetworkMitmScreen::F_FTP:     return "FTP";
    case NetworkMitmScreen::F_SMB:     return "SMB";
    case NetworkMitmScreen::F_SMTP:    return "SMTP";
    case NetworkMitmScreen::F_IMAP:    return "IMAP";
    case NetworkMitmScreen::F_POP3:    return "POP3";
    case NetworkMitmScreen::F_NTP:     return "NTP";
    case NetworkMitmScreen::F_MQTT:    return "MQTT";
    case NetworkMitmScreen::F_SIP:     return "SIP";
    default: return "TCP";
  }
}

uint16_t protoColor(uint32_t bit) {
  switch (bit) {
    case NetworkMitmScreen::F_ARP:     return TFT_YELLOW;
    case NetworkMitmScreen::F_ICMP:    return TFT_PINK;
    case NetworkMitmScreen::F_DNS:     return TFT_CYAN;
    case NetworkMitmScreen::F_DHCP:    return TFT_GREEN;
    case NetworkMitmScreen::F_MDNS:
    case NetworkMitmScreen::F_LLMNR:
    case NetworkMitmScreen::F_NETBIOS:
    case NetworkMitmScreen::F_SSDP:    return TFT_DARKGREEN;
    case NetworkMitmScreen::F_HTTP:    return TFT_GREENYELLOW;
    case NetworkMitmScreen::F_TLS:     return TFT_BLUE;
    case NetworkMitmScreen::F_QUIC:    return TFT_SKYBLUE;
    case NetworkMitmScreen::F_SSH:
    case NetworkMitmScreen::F_TELNET:  return TFT_ORANGE;
    case NetworkMitmScreen::F_RDP:
    case NetworkMitmScreen::F_VNC:     return TFT_VIOLET;
    case NetworkMitmScreen::F_FTP:
    case NetworkMitmScreen::F_SMB:     return TFT_GOLD;
    case NetworkMitmScreen::F_SMTP:
    case NetworkMitmScreen::F_IMAP:
    case NetworkMitmScreen::F_POP3:    return TFT_MAGENTA;
    case NetworkMitmScreen::F_NTP:     return TFT_DARKCYAN;
    case NetworkMitmScreen::F_MQTT:    return TFT_CYAN;
    case NetworkMitmScreen::F_SIP:     return TFT_PINK;
    default: return TFT_LIGHTGREY;
  }
}

void ipStr(uint32_t ipN, char* out, size_t n) {
  const uint8_t* b = (const uint8_t*)&ipN;
  snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

// In Targeted mode an explicit target shows as T1..T4; otherwise the last octet
// on our /24, or the last two. Keeps a one-line entry readable.
void ipShort(uint32_t ipN, char* out, size_t n) {
  for (uint8_t i = 0; i < 4; ++i)
    if (s_targetIps[i] && s_targetIps[i] == ipN) { snprintf(out, n, "T%u", i + 1); return; }
  const uint8_t* b = (const uint8_t*)&ipN;
  const uint32_t self = (uint32_t)WiFi.localIP();
  const uint8_t* s = (const uint8_t*)&self;
  if (b[0] == s[0] && b[1] == s[1] && b[2] == s[2])
    snprintf(out, n, ".%u", b[3]);
  else
    snprintf(out, n, "%u.%u", b[2], b[3]);
}

void sizeStr(uint16_t len, char* out, size_t n) {
  if (len >= 1000) snprintf(out, n, "%u.%uk", len / 1000, (len % 1000) / 100);
  else snprintf(out, n, "%uB", len);
}
}  // namespace
