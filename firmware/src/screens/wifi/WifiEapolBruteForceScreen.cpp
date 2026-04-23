#include "WifiEapolBruteForceScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/FastWpaCrack.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

// ── Built-in test wordlist (common passwords, all >= 8 chars) ─────────────

static const char* const kTestPasswords[] = {
  // Numeric sequences
  "12345678",  "123456789", "1234567890", "11111111",  "00000000",
  "87654321",  "11223344",  "12344321",   "99999999",  "88888888",
  "55555555",  "12121212",  "13131313",   "10101010",  "98765432",
  "12341234",  "11112222",  "22222222",   "33333333",  "44444444",
  "66666666",  "77777777",  "01234567",   "20202020",  "19191919",
  // Password/admin patterns
  "password",  "password1", "passw0rd",   "pass1234",  "password12",
  "password123","admin123", "admin1234",  "admin2020",  "root1234",
  "master12",  "login123",  "access14",   "letmein1",  "trustno1",
  "welcome1",  "changeme",  "default1",   "guest1234", "user1234",
  "test1234",  "temp1234",  "pass12345",  "p@ssw0rd",  "p@ss1234",
  // Keyboard patterns
  "qwerty123", "qwertyui",  "qwerty12",   "qwer1234",  "qwerasdf",
  "asdfghjk",  "asdf1234",  "zxcvbnm1",   "1234asdf",  "1234qwer",
  "1q2w3e4r",  "zaq12wsx",  "1qaz2wsx",   "qazwsx123", "!q2w3e4r",
  // WiFi/router defaults
  "wifi1234",  "wifi12345", "wlan1234",   "router12",  "netgear1",
  "linksys1",  "dlink1234", "tplink12",   "huawei12",  "modem123",
  "internet",  "wireless",  "network1",   "connect1",  "homewifi",
  "mywifi123", "wifiwifi",  "setup1234",  "broadband", "fiber123",
  // IT/tech defaults
  "abc12345",  "abcd1234",  "1234abcd",   "aa123456",  "a1234567",
  "a1b2c3d4",  "aaa11111",  "xyz12345",   "system12",  "server12",
  "cisco123",  "ubnt1234",  "mikrotik",   "radius12",  "monitor1",
  // PIN-style numeric
  "14141414",  "12345679",  "11111112",   "01020304",  "02468024",
  "13572468",  "10203040",  "11235813",   "31415926",  "27182818",
};
static constexpr int kTestPasswordCount = sizeof(kTestPasswords) / sizeof(kTestPasswords[0]);
static char _builtInSublabel[24] = {};

// ── Statics ───────────────────────────────────────────────────────────────

const char* WifiEapolBruteForceScreen::PCAP_DIR = "/unigeek/wifi/eapol";
const char* WifiEapolBruteForceScreen::PASS_DIR = "/unigeek/utility/passwords";

WifiEapolBruteForceScreen::CrackCtx     WifiEapolBruteForceScreen::_ctx       = {};
TaskHandle_t                             WifiEapolBruteForceScreen::_taskHandle = nullptr;

// ── File-scope PCAP constants ─────────────────────────────────────────────

static const uint8_t kSnapSig[8]  = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
static constexpr uint16_t kKiAck     = 0x0080;
static constexpr uint16_t kKiMic     = 0x0100;
static constexpr uint16_t kKiInstall = 0x0040;

// ── File-scope helpers ────────────────────────────────────────────────────

static bool pcapRead32(File& f, uint32_t& v) {
  uint8_t b[4];
  if (f.read(b, 4) != 4) return false;
  v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}

static int findSnap(const uint8_t* frm, uint16_t len) {
  for (uint16_t i = 0; i + 8 <= len; i++) {
    bool ok = true;
    for (int k = 0; k < 8; k++) if (frm[i + k] != kSnapSig[k]) { ok = false; break; }
    if (ok) return (int)i;
  }
  return -1;
}

// ── Destructor ────────────────────────────────────────────────────────────

WifiEapolBruteForceScreen::~WifiEapolBruteForceScreen() {
  _stopCrack();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

static String _truncSub(const char* name) {
  int len = strlen(name);
  if (len <= 14) return String(name);
  return String("..") + String(name + len - 12);
}

void WifiEapolBruteForceScreen::_showMenu() {
  _state = STATE_MENU;

  const char* pcapBase = strrchr(_selectedPcap, '/');
  const char* wlBase   = strrchr(_selectedWordlist, '/');
  const char* pcapName = pcapBase ? pcapBase + 1 : _selectedPcap;
  const char* wlName   = wlBase   ? wlBase   + 1 : _selectedWordlist;

  _pcapSub     = _selectedPcap[0]     ? _truncSub(pcapName) : "(not selected)";
  _wordlistSub = _selectedWordlist[0] ? _truncSub(wlName)   : "(not selected)";

  _menuItems[0] = {"PCAP File", _pcapSub.c_str()};
  _menuItems[1] = {"Wordlist",  _wordlistSub.c_str()};
  _menuItems[2] = {"Start",     nullptr};
  setItems(_menuItems, 3);
}

void WifiEapolBruteForceScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available.");
    Screen.goBack();
    return;
  }

  if (_builtInSublabel[0] == '\0')
    snprintf(_builtInSublabel, sizeof(_builtInSublabel), "%d entries", kTestPasswordCount);

  Uni.Storage->makeDir(PASS_DIR);
  _selectedPcap[0]      = '\0';
  _selectedWordlist[0]  = '\0';
  _showMenu();
}

void WifiEapolBruteForceScreen::onUpdate() {
  if (_state == STATE_CRACKING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stopCrack();
        return;
      }
    }
    if (_ctx.done) {
      _state = STATE_DONE;
      _taskHandle = nullptr;
      if (_ctx.queue)   { vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr; }
      if (_ctx.doneSem) { vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr; }
      _ctx.workerHandle = nullptr;
      if (_ctx.found) {
        _saveCrackedPassword();
        if (Uni.Speaker) Uni.Speaker->playWin();
      } else {
        if (Uni.Speaker) Uni.Speaker->playLose();
      }
      render();
    } else {
      // Throttle UI to 1/s during cracking — free core 1 for producer task
      static uint32_t lastRender = 0;
      uint32_t now = millis();
      if (now - lastRender >= 1000) {
        lastRender = now;
        render();
      }
    }
    return;
  }

  if (_state == STATE_DONE) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      _showMenu();
    }
    return;
  }

  ListScreen::onUpdate();
}

void WifiEapolBruteForceScreen::onRender() {
  if (_state == STATE_CRACKING) { _renderCracking(); return; }
  if (_state == STATE_DONE)     { _renderDone();     return; }
  ListScreen::onRender();
}

void WifiEapolBruteForceScreen::onBack() {
  if (_state == STATE_SELECT_PCAP || _state == STATE_SELECT_WORDLIST) {
    if (_currentDir == _browseRoot) {
      // At root → cancel selection, back to menu
      _showMenu();
    } else {
      // Go up one level
      int slash = _currentDir.lastIndexOf('/');
      String parent = (slash > 0) ? _currentDir.substring(0, slash) : _browseRoot;
      const char* ext = (_state == STATE_SELECT_PCAP) ? ".pcap" : nullptr;
      _currentDir = parent;
      _listFiles(ext);
      setItems(_fileItems, (uint8_t)_fileCount);
    }
    return;
  }
  Screen.goBack();
}

void WifiEapolBruteForceScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) {
      // Select PCAP
      _browseRoot = PCAP_DIR;
      _currentDir = PCAP_DIR;
      if (!_listFiles(".pcap")) {
        ShowStatusAction::show("No PCAP files found. Capture EAPOL first.");
        render();
        return;
      }
      _state = STATE_SELECT_PCAP;
      setItems(_fileItems, (uint8_t)_fileCount);
    } else if (index == 1) {
      // Select wordlist
      _browseRoot = PASS_DIR;
      _currentDir = PASS_DIR;
      _listFiles(nullptr);  // always has Test Wordlist even if empty
      _state = STATE_SELECT_WORDLIST;
      setItems(_fileItems, (uint8_t)_fileCount);
    } else if (index == 2) {
      // Start — require both selected
      if (_selectedPcap[0] == '\0') {
        ShowStatusAction::show("Select a PCAP file first.");
        render();
        return;
      }
      if (_selectedWordlist[0] == '\0') {
        ShowStatusAction::show("Select a wordlist first.");
        render();
        return;
      }
      ShowStatusAction::show("Parsing PCAP...", 0);
      if (!_parsePcap(_selectedPcap)) {
        ShowStatusAction::show("Handshake incomplete. M2 missing. Recapture needed.");
        render();
        return;
      }
      _startCrack();
    }
    return;
  }

  if (index >= (uint8_t)_fileCount) return;

  if (_state == STATE_SELECT_PCAP) {
    if (_fileIsDir[index]) {
      _currentDir = _filePaths[index];
      _listFiles(".pcap");
      setItems(_fileItems, (uint8_t)_fileCount);
    } else {
      strncpy(_selectedPcap, _filePaths[index].c_str(), sizeof(_selectedPcap) - 1);
      _selectedPcap[sizeof(_selectedPcap) - 1] = '\0';
      _showMenu();
    }
    return;
  }

  if (_state == STATE_SELECT_WORDLIST) {
    if (_fileIsDir[index]) {
      _currentDir = _filePaths[index];
      _listFiles(nullptr);
      setItems(_fileItems, (uint8_t)_fileCount);
    } else {
      strncpy(_selectedWordlist, _filePaths[index].c_str(), sizeof(_selectedWordlist) - 1);
      _showMenu();
    }
    return;
  }

}

// ── File browser ──────────────────────────────────────────────────────────

bool WifiEapolBruteForceScreen::_listFiles(const char* ext) {
  _fileCount = 0;
  const char* dir = _currentDir.c_str();
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;

  static constexpr int kMaxEntries = 64;
  IStorage::DirEntry entries[kMaxEntries];
  uint8_t count = Uni.Storage->listDir(dir, entries, kMaxEntries);

  // Collect base names for both groups
  struct RawEntry { String base; bool isDir; };
  RawEntry raw[kMaxEntries];
  int rawCount = 0;
  for (uint8_t i = 0; i < count && rawCount < kMaxEntries; i++) {
    String name = entries[i].name;
    int slash = name.lastIndexOf('/');
    raw[rawCount++] = { (slash >= 0) ? name.substring(slash + 1) : name, entries[i].isDir };
  }

  // Insertion sort: dirs before files, each group alphabetically
  for (int i = 1; i < rawCount; i++) {
    RawEntry key = raw[i];
    int j = i - 1;
    while (j >= 0) {
      bool moveUp = false;
      if (key.isDir && !raw[j].isDir) moveUp = true;
      else if (key.isDir == raw[j].isDir && key.base < raw[j].base) moveUp = true;
      if (!moveUp) break;
      raw[j + 1] = raw[j];
      j--;
    }
    raw[j + 1] = key;
  }

  // Build file items from sorted list
  const bool isPcap = (ext && strcmp(ext, ".pcap") == 0);
  for (int i = 0; i < rawCount && _fileCount < kMaxEntries; i++) {
    const RawEntry& e = raw[i];
    // A directory whose name ends with the target extension is a mislabeled file
    // (FAT32 can set the directory attribute on a regular file)
    const bool mislabeledFile = e.isDir && ext && e.base.endsWith(ext);
    if (e.isDir && !mislabeledFile) {
      _fileIsDir[_fileCount]  = true;
      _fileLabels[_fileCount] = e.base;
      _filePaths[_fileCount]  = String(dir) + "/" + e.base;
      _fileItems[_fileCount]  = {_fileLabels[_fileCount].c_str(), "DIR"};
      _fileCount++;
    } else {
      if (!mislabeledFile && ext && !e.base.endsWith(ext)) continue;
      _fileIsDir[_fileCount]  = false;
      _fileLabels[_fileCount] = e.base;
      _filePaths[_fileCount]  = String(dir) + "/" + e.base;
      _fileItems[_fileCount]  = {_fileLabels[_fileCount].c_str(), isPcap ? "PCAP" : nullptr};
      _fileCount++;
    }
  }

  // Built-in test wordlist only at the password root
  if (ext == nullptr && strcmp(dir, PASS_DIR) == 0 && _fileCount < kMaxEntries) {
    _fileIsDir[_fileCount]  = false;
    _fileLabels[_fileCount] = "Built In";
    _filePaths[_fileCount]  = "builtin";
    _fileItems[_fileCount]  = {_fileLabels[_fileCount].c_str(), _builtInSublabel};
    _fileCount++;
  }

  return _fileCount > 0;
}

// ── PCAP parser ───────────────────────────────────────────────────────────

// Helper: parse one 802.11 EAPOL-Key frame; returns pointer to key descriptor or nullptr.
// Writes eapol ptr, total len, ki into out params.
static const uint8_t* _parseEapolKey(const uint8_t* frm, uint16_t flen,
                                      const uint8_t** eapolOut, uint16_t* totalOut) {
  if (flen < 24) return nullptr;
  const uint16_t fc = (uint16_t)frm[0] | ((uint16_t)frm[1] << 8);
  if (((fc & 0x000C) >> 2) != 2) return nullptr;  // not data frame
  int snap = findSnap(frm, flen);
  if (snap < 0 || (uint16_t)(snap + 9) >= flen) return nullptr;
  const uint8_t* eapol = frm + snap + 8;
  if (eapol[1] != 0x03) return nullptr;           // not EAPOL-Key
  uint16_t eap_len = ((uint16_t)eapol[2] << 8) | eapol[3];
  uint16_t total   = 4 + eap_len;
  // Need at least 97 bytes from EAPOL start to cover MIC (offset 81..96)
  uint16_t avail = flen - (uint16_t)(snap + 8);
  if (total < 97 || avail < 97) return nullptr;
  // Cap total to available bytes (frame may be truncated in PCAP)
  if (total > avail) total = avail;
  *eapolOut = eapol;
  *totalOut = total;
  return eapol + 4;  // key descriptor
}

bool WifiEapolBruteForceScreen::_parsePcap(const char* path) {
  Handshake& hs = _ctx.hs;
  memset(&hs, 0, sizeof(hs));

  File f = Uni.Storage->open(path, FILE_READ);
  if (!f) return false;

  // Global header (24 bytes)
  uint8_t gh[24];
  if (f.read(gh, 24) != 24) { f.close(); return false; }
  if (!(gh[0] == 0xD4 && gh[1] == 0xC3 && gh[2] == 0xB2 && gh[3] == 0xA1)) {
    f.close(); return false;
  }
  uint32_t linktype = (uint32_t)gh[20] | ((uint32_t)gh[21] << 8) |
                      ((uint32_t)gh[22] << 16) | ((uint32_t)gh[23] << 24);

  uint8_t rec[512];

  // Helper macro: read one pcap record into rec[], strip radiotap.
  #define READ_FRAME(frm, flen) \
    uint32_t _ts, _tu, _incl, _orig; \
    if (!pcapRead32(f,_ts)||!pcapRead32(f,_tu)||!pcapRead32(f,_incl)||!pcapRead32(f,_orig)) break; \
    if (!_incl || _incl > sizeof(rec)) { f.seek(f.position()+_incl); continue; } \
    if (f.read(rec, _incl) != (int)_incl) break; \
    uint16_t _off = 0; \
    if (linktype == 127) { \
      if (_incl < 4) continue; \
      _off = (uint16_t)rec[2] | ((uint16_t)rec[3] << 8); \
      if (_off >= _incl) continue; \
    } \
    const uint8_t* frm  = rec + _off; \
    uint16_t       flen = (uint16_t)(_incl - _off);

  // ── Single pass: pair M1/M3 with M2 in either order ─────────────────────
  bool gotAnonce = false;
  bool gotM2     = false;
  uint8_t lastAnonce[32] = {};
  uint8_t lastAp[6]  = {};
  uint8_t lastSta[6] = {};

  // Buffer for unpaired M2 (when M2 arrives before M1/M3)
  bool    pendM2 = false;
  uint8_t pendM2Sta[6]  = {};
  uint8_t pendM2Ap[6]   = {};
  uint8_t pendM2Snonce[32] = {};
  uint8_t pendM2Mic[16] = {};
  uint8_t pendM2Eapol[400] = {};
  uint16_t pendM2EapolLen = 0;

  while (f.available() > 16) {
    READ_FRAME(frm, flen)

    const uint16_t fc    = (uint16_t)frm[0] | ((uint16_t)frm[1] << 8);
    const uint8_t  fcTyp = (fc & 0x000C) >> 2;
    const uint8_t  fcSub = (fc & 0x00F0) >> 4;

    // Beacon — grab SSID
    if (fcTyp == 0 && fcSub == 8 && flen >= 36 && hs.ssid[0] == '\0') {
      uint16_t pos = 36;
      while (pos + 2 <= flen) {
        uint8_t id = frm[pos], elen = frm[pos + 1];
        if (pos + 2 + elen > flen) break;
        if (id == 0 && elen > 0 && elen <= 32) {
          memcpy(hs.ssid, frm + pos + 2, elen);
          hs.ssid[elen] = '\0';
          hs.ssid_len   = (uint8_t)elen;
          break;
        }
        pos += 2 + elen;
      }
      continue;
    }

    // EAPOL frame
    const uint8_t* eapol; uint16_t total;
    const uint8_t* key = _parseEapolKey(frm, flen, &eapol, &total);
    if (!key) continue;
    uint16_t ki   = ((uint16_t)key[1] << 8) | key[2];
    bool     ack  = ki & kKiAck;
    bool     mic  = ki & kKiMic;
    bool     inst = ki & kKiInstall;

    if (ack && (!mic || inst)) {
      // ── M1 or M3 ──────────────────────────────────────────────────────
      memcpy(lastAp,     frm + 10, 6);   // addr2 = AP
      memcpy(lastSta,    frm + 4,  6);   // addr1 = STA
      memcpy(lastAnonce, key + 13, 32);
      gotAnonce = true;

      // Check if a buffered M2 from this same AP/STA pair can be paired now
      if (pendM2 &&
          memcmp(pendM2Sta, lastSta, 6) == 0 &&
          memcmp(pendM2Ap,  lastAp,  6) == 0) {
        memcpy(hs.ap,     lastAp,  6);
        memcpy(hs.sta,    lastSta, 6);
        memcpy(hs.anonce, lastAnonce, 32);
        memcpy(hs.snonce, pendM2Snonce, 32);
        memcpy(hs.mic,    pendM2Mic, 16);
        if (pendM2EapolLen <= sizeof(hs.eapol)) {
          memcpy(hs.eapol, pendM2Eapol, pendM2EapolLen);
          memset(hs.eapol + 81, 0, 16);
          hs.eapol_len = pendM2EapolLen;
        }
        gotM2 = true;
      }
    } else if (!ack && mic && !inst) {
      // ── M2 candidate ───────────────────────────────────────────────────
      bool nonceZero = true;
      for (int z = 0; z < 32 && nonceZero; z++) nonceZero = (key[13 + z] == 0);
      if (nonceZero) continue;

      if (gotAnonce) {
        // Forward pairing: M1/M3 already seen
        bool staMatch = memcmp(frm + 10, lastSta, 6) == 0;
        bool apMatch  = memcmp(frm + 4,  lastAp,  6) == 0;
        if (staMatch && apMatch) {
          memcpy(hs.ap,     lastAp,  6);
          memcpy(hs.sta,    lastSta, 6);
          memcpy(hs.anonce, lastAnonce, 32);
          memcpy(hs.snonce, key + 13, 32);
          memcpy(hs.mic,    eapol + 81, 16);
          if (total <= sizeof(hs.eapol)) {
            memcpy(hs.eapol, eapol, total);
            memset(hs.eapol + 81, 0, 16);
            hs.eapol_len = total;
          }
          gotM2 = true;
          continue;
        }
      }

      // No M1/M3 yet — buffer this M2 for reverse pairing
      pendM2 = true;
      memcpy(pendM2Sta, frm + 10, 6);
      memcpy(pendM2Ap,  frm + 4,  6);
      memcpy(pendM2Snonce, key + 13, 32);
      memcpy(pendM2Mic,    eapol + 81, 16);
      if (total <= sizeof(pendM2Eapol)) {
        memcpy(pendM2Eapol, eapol, total);
        memset(pendM2Eapol + 81, 0, 16);
        pendM2EapolLen = total;
      }
    }
  }

  #undef READ_FRAME
  f.close();

  if (!gotM2) return false;

  // SSID fallback: extract from filename (BSSID_SSID.pcap)
  if (hs.ssid[0] == '\0') {
    String p   = String(path);
    int    sl  = p.lastIndexOf('/');
    int    und = p.indexOf('_', sl >= 0 ? sl + 1 : 0);
    int    dot = p.lastIndexOf('.');
    if (und >= 0 && dot > und + 1) {
      String part = p.substring(und + 1, dot);
      snprintf(hs.ssid, sizeof(hs.ssid), "%s", part.c_str());
      hs.ssid_len = (uint8_t)strlen(hs.ssid);
    }
    if (hs.ssid[0] == '\0') return false;
  }

  // Build prf_data[76]: min(AP,STA)||max(AP,STA)||min(ANonce,SNonce)||max(ANonce,SNonce)
  uint8_t* p = hs.prf_data;
  if (memcmp(hs.ap, hs.sta, 6) < 0) {
    memcpy(p, hs.ap,  6); p += 6; memcpy(p, hs.sta, 6); p += 6;
  } else {
    memcpy(p, hs.sta, 6); p += 6; memcpy(p, hs.ap,  6); p += 6;
  }
  if (memcmp(hs.anonce, hs.snonce, 32) < 0) {
    memcpy(p, hs.anonce, 32); p += 32; memcpy(p, hs.snonce, 32);
  } else {
    memcpy(p, hs.snonce, 32); p += 32; memcpy(p, hs.anonce, 32);
  }

  hs.valid = true;
  return true;
}

// ── Worker (core 0) — consumes passwords from queue and cracks ────────────

void WifiEapolBruteForceScreen::_workerTask(void* param) {
  CrackCtx* ctx = static_cast<CrackCtx*>(param);
  PwEntry entry;

  while (true) {
    if (xQueueReceive(ctx->queue, &entry, portMAX_DELAY) != pdTRUE) break;
    if (entry.len == 0) break; // poison pill
    if (ctx->found || ctx->stop) {
      __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
      continue;
    }
    if (fast_wpa2_try_password(entry.pw, entry.len,
                                ctx->hs.ssid, ctx->hs.ssid_len,
                                ctx->hs.prf_data,
                                ctx->hs.eapol, ctx->hs.eapol_len,
                                ctx->hs.mic)) {
      ctx->found = true;
      memcpy(ctx->foundPass, entry.pw, entry.len + 1);
    }
    __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
  }
  xSemaphoreGive(ctx->doneSem);
  vTaskDelete(NULL);
}

// ── Producer + cracker (core 1) — reads wordlist, feeds queue, also cracks ─

void WifiEapolBruteForceScreen::_crackTask(void* param) {
  CrackCtx* ctx = static_cast<CrackCtx*>(param);

  uint32_t t0 = millis();

  // Crack one password on this core (producer side)
  auto tryHere = [&](const char* pw, size_t len) {
    memcpy(ctx->curPass, pw, len + 1);
    if (fast_wpa2_try_password(pw, (uint8_t)len,
                                ctx->hs.ssid, ctx->hs.ssid_len,
                                ctx->hs.prf_data,
                                ctx->hs.eapol, ctx->hs.eapol_len,
                                ctx->hs.mic)) {
      memcpy(ctx->foundPass, pw, len + 1);
      ctx->found = true;
    }
    __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
    uint32_t el = millis() - t0;
    if (el >= 2000) ctx->speed = ctx->tested * 1000.0f / el;
  };

  // Send one password to worker queue (non-blocking)
  auto sendToWorker = [&](const char* pw, size_t len) -> bool {
    PwEntry entry;
    memcpy(entry.pw, pw, len + 1);
    entry.len = (uint8_t)len;
    return xQueueSend(ctx->queue, &entry, 0) == pdTRUE;
  };

  if (strcmp(ctx->wordlistPath, "builtin") == 0) {
    int i = 0;
    while (i < kTestPasswordCount && !ctx->stop && !ctx->found) {
      // Send one to worker (core 0)
      size_t n = strlen(kTestPasswords[i]);
      if (!sendToWorker(kTestPasswords[i], n)) {
        // Queue full — crack it here instead
        tryHere(kTestPasswords[i], n);
        i++;
      } else {
        i++;
        // Crack the next one ourselves (core 1)
        if (i < kTestPasswordCount && !ctx->stop && !ctx->found) {
          tryHere(kTestPasswords[i], strlen(kTestPasswords[i]));
          i++;
        }
      }
      ctx->bytesDone = (uint32_t)i;
    }
  } else {
    char line[64];
    char line2[64];
    fs::File f = Uni.Storage->open(ctx->wordlistPath, FILE_READ);
    if (f) {
      while (f.available() && !ctx->stop && !ctx->found) {
        // Read line for worker
        size_t n = f.readBytesUntil('\n', line, 63);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = '\0';
        if (n < 8 || n > 63) { ctx->bytesDone = f.position(); continue; }

        if (!sendToWorker(line, n)) {
          // Queue full — crack it here
          tryHere(line, n);
        } else {
          // Worker got one — read next and crack it here
          if (f.available() && !ctx->stop && !ctx->found) {
            size_t n2 = f.readBytesUntil('\n', line2, 63);
            line2[n2] = '\0';
            while (n2 > 0 && (line2[n2 - 1] == '\r' || line2[n2 - 1] == '\n')) line2[--n2] = '\0';
            if (n2 >= 8 && n2 <= 63) {
              tryHere(line2, n2);
            }
          }
        }
        ctx->bytesDone = f.position();
      }
      f.close();
    }
  }

  // Poison pill to stop worker
  PwEntry poison;
  memset(&poison, 0, sizeof(poison));
  xQueueSend(ctx->queue, &poison, pdMS_TO_TICKS(2000));

  // Wait for worker to finish
  xSemaphoreTake(ctx->doneSem, pdMS_TO_TICKS(5000));

  ctx->done = true;
  vTaskDelete(nullptr);
}

// ── Start / Stop ──────────────────────────────────────────────────────────

void WifiEapolBruteForceScreen::_startCrack() {
  memset(_ctx.foundPass, 0, sizeof(_ctx.foundPass));
  memset(_ctx.curPass,   0, sizeof(_ctx.curPass));
  _ctx.stop      = false;
  _ctx.done      = false;
  _ctx.found     = false;
  _ctx.tested    = 0;
  _ctx.bytesDone = 0;
  _ctx.speed     = 0.0f;
  strncpy(_ctx.wordlistPath, _selectedWordlist, sizeof(_ctx.wordlistPath) - 1);

  // Get file size for progress bar
  if (strcmp(_selectedWordlist, "builtin") == 0) {
    _ctx.fileSize = kTestPasswordCount;
  } else {
    fs::File f = Uni.Storage->open(_selectedWordlist, FILE_READ);
    _ctx.fileSize = f ? f.size() : 0;
    if (f) f.close();
  }

  // Create queue + semaphore for dual-core
  _ctx.queue   = xQueueCreate(QUEUE_DEPTH, sizeof(PwEntry));
  _ctx.doneSem = xSemaphoreCreateBinary();

  // Worker on core 0 — pure cracking from queue
  xTaskCreatePinnedToCore(_workerTask, "wpa2_w", 8192, &_ctx, 1, &_ctx.workerHandle, 0);

  // Producer + cracker on core 1 — reads wordlist, feeds queue, also cracks
  xTaskCreatePinnedToCore(_crackTask, "wpa2_p", 8192, &_ctx, 1, &_taskHandle, 1);

  _state       = STATE_CRACKING;
  _chromeDrawn = false;
  render();
}

void WifiEapolBruteForceScreen::_stopCrack() {
  if (!_taskHandle) return;
  _ctx.stop = true;
  // Wait up to 2s for tasks to self-exit cleanly
  for (int i = 0; i < 200 && !_ctx.done; i++) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  if (!_ctx.done) {
    vTaskDelete(_taskHandle);
    if (_ctx.workerHandle) vTaskDelete(_ctx.workerHandle);
  }
  _taskHandle = nullptr;
  _ctx.workerHandle = nullptr;
  if (_ctx.queue)   { vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr; }
  if (_ctx.doneSem) { vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr; }
  if (_state == STATE_CRACKING) {
    _state = STATE_DONE;
    render();
  }
}

// ── Save cracked password (same format as NetworkMenuScreen) ───────────────

void WifiEapolBruteForceScreen::_saveCrackedPassword() {
  if (!Uni.Storage || !_ctx.found) return;
  // Format BSSID from AP MAC bytes: "AABBCCDDEEFF"
  char bssid[13];
  snprintf(bssid, sizeof(bssid), "%02X%02X%02X%02X%02X%02X",
           _ctx.hs.ap[0], _ctx.hs.ap[1], _ctx.hs.ap[2],
           _ctx.hs.ap[3], _ctx.hs.ap[4], _ctx.hs.ap[5]);
  // Save to /unigeek/wifi/passwords/<BSSID>_<SSID>.pass
  static const char* dir = "/unigeek/wifi/passwords";
  Uni.Storage->makeDir(dir);
  String path = String(dir) + "/" + bssid + "_" + _ctx.hs.ssid + ".pass";
  Uni.Storage->writeFile(path.c_str(), _ctx.foundPass);
}

// ── Render ────────────────────────────────────────────────────────────────

void WifiEapolBruteForceScreen::_renderCracking() {
  auto& lcd = Uni.Lcd;

  const uint16_t accent = Config.getThemeColor();
  const int      lh     = 14;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("SSID:", bodyX() + 4, bodyY() + 4);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(_ctx.hs.ssid, bodyX() + 40, bodyY() + 4);

    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
#ifdef DEVICE_HAS_KEYBOARD
    lcd.drawString("BACK / ENTER: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
#else
    lcd.drawString("Any btn: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
#endif

    _chromeDrawn = true;
  }

  const int barPad = 3;
  const int barH   = 8 + barPad * 2;
  const int dynH   = lh + 4 + barH + 6 + lh + 2;
  const int dynY   = bodyY() + 4 + lh;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), dynH);
  sp.fillSprite(TFT_BLACK);

  int y = 0;

  char disp[24];
  snprintf(disp, sizeof(disp), "%.23s", _ctx.curPass);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Try:", 4, y);
  sp.setTextColor(accent, TFT_BLACK);
  sp.drawString(disp, 36, y);
  y += lh + 4;

  const int barX = barPad;
  const int barW = bodyW() - barPad * 2;
  int pct = (_ctx.fileSize > 0)
    ? (int)((uint64_t)_ctx.bytesDone * 100 / _ctx.fileSize)
    : 0;
  if (pct > 100) pct = 100;

  sp.drawRect(barX, y, barW, barH, TFT_DARKGREY);
  if (pct > 0) sp.fillRect(barX + 1, y + 1, (barW - 2) * pct / 100, barH - 2, accent);

  char pctBuf[6];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, barX + barW / 2, y + barH / 2);
  y += barH + 6;

  char statBuf[48];
  snprintf(statBuf, sizeof(statBuf), "%.1f/s  |  %lu tested",
           (float)_ctx.speed, (unsigned long)_ctx.tested);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(statBuf, bodyW() / 2, y);

  sp.pushSprite(bodyX(), dynY);
  sp.deleteSprite();
}

void WifiEapolBruteForceScreen::_renderDone() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  const int cx = bodyX() + bodyW() / 2;
  const int cy = bodyY() + bodyH() / 2;

  if (_ctx.found) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString("PASSWORD FOUND!", cx, cy - 22);

    // measure password text width and draw a pill behind it
    int pwW = lcd.textWidth(_ctx.foundPass);
    int padX = 6, padY = 3;
    int rx = cx - pwW / 2 - padX;
    int ry = cy - 6 - 6 - padY;
    lcd.fillRoundRect(rx, ry, pwW + padX * 2, 12 + padY * 2, 4, Config.getThemeColor());
    lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
    lcd.drawString(_ctx.foundPass, cx, cy - 6);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu tries", (unsigned long)_ctx.tested);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(buf, cx, cy + 10);
  } else {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("Not in wordlist", cx, cy - 10);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu tested", (unsigned long)_ctx.tested);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(buf, cx, cy + 6);
  }

  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Any key to continue", cx, bodyY() + bodyH());
}
