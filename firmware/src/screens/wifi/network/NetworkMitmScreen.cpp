#include "NetworkMitmScreen.h"

#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

static constexpr uint64_t kMinFree = 64 * 1024;

NetworkMitmScreen::~NetworkMitmScreen() {
  if (_state == STATE_RUNNING) _stop("Screen closed");
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void NetworkMitmScreen::onInit() {
  _showMenu();
}

void NetworkMitmScreen::onBack() {
  if (_state == STATE_RUNNING) {
    _stop("Stopped by user");
    _showMenu();
    return;
  }
  Screen.goBack();
}

// ── Menu ────────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_showMenu() {
  _state      = STATE_MENU;
  _arpSub     = _arpEnabled     ? "On" : "Off";
  _snifferSub = _snifferEnabled ? "On" : "Off";

  _menuItems[0] = {"ARP Spoof",       _arpSub.c_str()};
  _menuItems[1] = {"Network Sniffer", _snifferSub.c_str()};
  _menuItems[2] = {"Start"};
  setItems(_menuItems, 3);
}

void NetworkMitmScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_MENU) return;

  switch (index) {
    case 0:
      _arpEnabled = !_arpEnabled;
      _showMenu();
      break;

    case 1:
      if (!_snifferEnabled &&
          (!Uni.StorageSD || !Uni.StorageSD->isAvailable())) {
        ShowStatusAction::show("SD card required!", 1500);
        render();
        return;
      }
      _snifferEnabled = !_snifferEnabled;
      _showMenu();
      break;

    case 2:
      _start();
      break;
  }
}

// ── Start ───────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_start() {
  if (!_arpEnabled && !_snifferEnabled) {
    ShowStatusAction::show("Enable at least one option!");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to a network!", 1500);
    return;
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
  snprintf(buf, sizeof(buf), "[*] %s  gw %s",
           WiFi.localIP().toString().c_str(),
           WiFi.gatewayIP().toString().c_str());
  _log.addLine(buf);

  // Our MAC is what a poisoned victim should show for the gateway. Print it
  // here so the two values can be compared without leaving this screen.
  uint8_t self[6];
  WiFi.macAddress(self);
  _fmtMac(self, mac, sizeof(mac));
  snprintf(buf, sizeof(buf), "[*] Us  %s", mac);
  _log.addLine(buf, TFT_YELLOW);

  // Baseline heap. Serving portal files is the heaviest allocator in the whole
  // attack, so knowing what we started with tells a leak apart from simply not
  // having had the room to begin with.
  const uint32_t heap0 = ESP.getFreeHeap() / 1024;
  snprintf(buf, sizeof(buf), "[*] Heap %luk free", (unsigned long)heap0);
  _log.addLine(buf, heap0 < 40 ? TFT_RED : TFT_DARKGREY);

  // 1. Capture file. Link type depends on where the frames come from: the relay
  //    yields decrypted Ethernet, the monitor fallback raw 802.11.
  const bool relayMode = _arpEnabled;
  if (_snifferEnabled) {
    if (!Uni.StorageSD || !Uni.StorageSD->isAvailable()) {
      _log.addLine("[!] No SD card", TFT_RED);
      _snifferEnabled = false;
    } else if (Uni.StorageSD->freeBytes() < kMinFree) {
      _log.addLine("[!] SD card full", TFT_RED);
      _snifferEnabled = false;
    } else {
      const uint32_t linktype = relayMode ? PcapWriter::LINKTYPE_ETHERNET
                                          : PcapWriter::LINKTYPE_IEEE802_11;
      if (_pcap.begin(Uni.StorageSD, SAVE_DIR, WiFi.SSID(), linktype, MitmRelay::SNAP_LEN)) {
        _pcapUp = true;
        snprintf(buf, sizeof(buf), "[+] %s", _pcap.filename().c_str());
        _log.addLine(buf, TFT_GREEN);
        _log.addLine(relayMode ? "    Ethernet (decrypted)" : "    802.11 (encrypted)");
      } else {
        snprintf(buf, sizeof(buf), "[!] %s", _pcap.error() ? _pcap.error() : "PCAP failed");
        _log.addLine(buf, TFT_RED);
        _snifferEnabled = false;
      }
    }
  }

  // 2. ARP spoofer — must be up before the relay so the hook can feed it.
  if (_arpEnabled) {
    if (_arp.begin()) {
      _arpUp = true;
      snprintf(buf, sizeof(buf), "[*] Sweeping %lu hosts...",
               (unsigned long)_arp.sweepTotal());
      _log.addLine(buf);
    } else {
      _log.addLine("[!] ARP spoof init failed", TFT_RED);
      _arpEnabled = false;
    }
  }

  // 3. The engine. Nothing above matters if this does not come up.
  if (_arpEnabled || _snifferEnabled) {
    _relay.setPcap(_pcapUp ? &_pcap : nullptr);
    _relay.setArp(_arpUp ? &_arp : nullptr);
    _relay.setForward(_arpEnabled);

    if (_relay.begin(relayMode ? MitmRelay::MODE_RELAY : MitmRelay::MODE_MONITOR)) {
      _relayUp = true;
      _log.addLine(relayMode ? "[+] Relay active" : "[+] Monitor active", TFT_GREEN);
    } else {
      snprintf(buf, sizeof(buf), "[!] Relay: %s", _relay.error());
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
    _log.addLine("[!] Nothing started", TFT_RED);
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
    snprintf(line, sizeof(line), "[*] GW  %s", mac);
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
        snprintf(buf, sizeof(buf), "[+] %d hosts, gw %s",
                 _arp.targetCount(), _arp.gatewayKnown() ? "ok" : "UNKNOWN");
        _log.addLine(buf, _arp.gatewayKnown() ? TFT_GREEN : TFT_RED);
        if (!_arp.gatewayKnown())
          _log.addLine("[!] No gateway MAC - not poisoning", TFT_RED);
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
        snprintf(buf, sizeof(buf), "[!] %lu gw claim lost - TX starved",
                 (unsigned long)_lastProbeLost);
        _log.addLine(buf, TFT_RED);
      }
      if (_arp.probeDropped() != _lastProbeDropped) {
        _lastProbeDropped = _arp.probeDropped();
        char buf[52];
        snprintf(buf, sizeof(buf), "[!] %lu gw claim dropped - queue full",
                 (unsigned long)_lastProbeDropped);
        _log.addLine(buf, TFT_RED);
      }

      if (_arp.targetCount() != _lastTargets) {
        _lastTargets = _arp.targetCount();
        char buf[48];
        snprintf(buf, sizeof(buf), "[+] Poisoning %d hosts", _lastTargets);
        _log.addLine(buf, TFT_GREEN);
      }
    }
  }

  if (_relayUp && _relay.storageFailed()) {
    _log.addLine("[!] SD write failed", TFT_RED);
    _stop("SD error");
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

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop("Stopped by user");
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
