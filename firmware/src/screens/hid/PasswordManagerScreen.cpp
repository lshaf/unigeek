#include "PasswordManagerScreen.h"
#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_WEBAUTHN is defined
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/IStorage.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/hid/KeyboardMenuScreen.h"
#include "screens/hid/KeyboardScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <string.h>

#ifdef DEVICE_HAS_WEBAUTHN
#include "utils/webauthn/CredentialStore.h"
#endif

// ── Constructor ─────────────────────────────────────────────────────────────

PasswordManagerScreen::PasswordManagerScreen(HIDKeyboardUtil* keyboard, int mode)
  : _keyboard(keyboard), _mode(mode)
{}

// ── Lifecycle ───────────────────────────────────────────────────────────────

const char* PasswordManagerScreen::title()
{
  if (_state == STATE_ADD)  return "New Entry";
  if (_state == STATE_VIEW) return "Password";
  return "Password Mgr";
}

void PasswordManagerScreen::onInit()
{
  _tryUnlock();
}

void PasswordManagerScreen::onUpdate()
{
  if (_state == STATE_VIEW) {
    if (!Uni.Nav->isPressed()) _holdFired = false;
    if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
      _holdFired = true;
      Uni.Nav->suppressCurrentPress();
      static constexpr InputSelectAction::Option opts[] = {
        {"View",   "view"},
        {"Type",   "type"},
        {"Delete", "delete"},
      };
      const char* r = InputSelectAction::popup(_entries[_viewIdx].label, opts, 3, nullptr);
      if (!r) { render(); return; }
      if (strcmp(r, "view")   == 0) { render(); return; }
      if (strcmp(r, "type")   == 0) { _typePassword(); render(); return; }
      if (strcmp(r, "delete") == 0) { _deleteEntry(_viewIdx); return; }
      return;
    }

    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_PRESS) {
      _typePassword();
      render();
    } else if (dir == INavigation::DIR_BACK) {
      _reloadMenu();
      render();
    }
    return;
  }

  if (_state == STATE_MENU && _selectedIndex < _entryCount) {
    if (!Uni.Nav->isPressed()) _holdFired = false;
    if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
      _holdFired = true;
      Uni.Nav->suppressCurrentPress();
      _showEntryOptions(_selectedIndex);
      return;
    }
  }

  ListScreen::onUpdate();
}

void PasswordManagerScreen::onRender()
{
  if (_state == STATE_VIEW) {
    _renderView();
    return;
  }
  ListScreen::onRender();
}

void PasswordManagerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_ADD) {
    // Index layout depends on whether the Source row is present.
    // With webauthn:    0=Label 1=Source 2=Type 3=Case 4=Length 5=Save
    // Without webauthn: 0=Label         1=Type 2=Case 3=Length 4=Save
#ifdef DEVICE_HAS_WEBAUTHN
    constexpr uint8_t IDX_LABEL = 0, IDX_SOURCE = 1, IDX_TYPE = 2,
                      IDX_CASE = 3, IDX_LEN = 4, IDX_SAVE = 5;
#else
    constexpr uint8_t IDX_LABEL = 0, IDX_TYPE = 1, IDX_CASE = 2,
                      IDX_LEN = 3, IDX_SAVE = 4;
    constexpr uint8_t IDX_SOURCE = 0xFF;  // never matches
#endif

    if (index == IDX_LABEL) {
      String v = InputTextAction::popup("Label", _pendingLabel.c_str());
      if (!InputTextAction::wasCancelled()) _pendingLabel = v;
      _updateAddLabels(); render();
    } else if (index == IDX_SOURCE) {
      if (!_waMasterAvailable()) {
        ShowStatusAction::show("Run Utility > Manage WebAuthn > BIP39 Generate", 2200);
        _pendingSource = SRC_LEGACY;
        _updateAddLabels(); render();
        return;
      }
      static constexpr InputSelectAction::Option srcOpts[] = {
        {"Local (master pw only)",     "0"},
        {"WebAuthn (pw + master.bin)", "1"},
      };
      const char* r = InputSelectAction::popup("Source", srcOpts, 2,
                        String(_pendingSource).c_str());
      if (r) _pendingSource = (uint8_t)atoi(r);
      _updateAddLabels(); render();
    } else if (index == IDX_TYPE) {
      static constexpr InputSelectAction::Option typeOpts[] = {
        {"Alphanumeric (letters+digits)", "0"},
        {"Alphabet only (letters)",       "1"},
        {"Alphanumeric + Symbols",        "2"},
      };
      const char* r = InputSelectAction::popup("Password Type", typeOpts, 3,
                        String(_pendingType).c_str());
      if (r) _pendingType = (uint8_t)atoi(r);
      _updateAddLabels(); render();
    } else if (index == IDX_CASE) {
      static constexpr InputSelectAction::Option caseOpts[] = {
        {"Lower case (a-z)", "0"},
        {"Upper case (A-Z)", "1"},
        {"Mixed case (A-Za-z)", "2"},
      };
      const char* r = InputSelectAction::popup("Case", caseOpts, 3,
                        String(_pendingCase).c_str());
      if (r) _pendingCase = (uint8_t)atoi(r);
      _updateAddLabels(); render();
    } else if (index == IDX_LEN) {
      int v = InputNumberAction::popup("Length (8-34)", 8, 34, _pendingLen);
      if (!InputNumberAction::wasCancelled()) _pendingLen = (uint8_t)v;
      _updateAddLabels(); render();
    } else if (index == IDX_SAVE) {
      _saveEntry();
    }
    return;
  }

  if (_state == STATE_MENU) {
    if (index < _entryCount) {
      _enterView(index);
    } else {
      _enterAdd();
    }
  }
}

void PasswordManagerScreen::onBack()
{
  if (_state == STATE_ADD || _state == STATE_VIEW) {
    _reloadMenu();
    render();
    return;
  }
  Screen.goBack();
}

// ── Unlock ──────────────────────────────────────────────────────────────────

void PasswordManagerScreen::_tryUnlock()
{
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    Screen.goBack();
    return;
  }
  Uni.Storage->makeDir(kDir);

  _firstRun = !Uni.Storage->exists(kMaster);
  const char* prompt = _firstRun ? "Set Master Password" : "Master Password";

  String pw = InputTextAction::popup(prompt, "");
  if (InputTextAction::wasCancelled() || pw.length() == 0) {
    Screen.goBack();
    return;
  }

  if (_firstRun) {
    String pw2 = InputTextAction::popup("Confirm Password", "");
    if (InputTextAction::wasCancelled() || pw2 != pw) {
      ShowStatusAction::show("Passwords do not match", 1500);
      Screen.goBack();
      return;
    }
    pw.toCharArray(_masterPw, sizeof(_masterPw));
    _setMaster(_masterPw);
  } else {
    pw.toCharArray(_masterPw, sizeof(_masterPw));
    if (!_verifyMaster(_masterPw)) {
      ShowStatusAction::show("Wrong password", 1500);
      memset(_masterPw, 0, sizeof(_masterPw));
      Screen.goBack();
      return;
    }
  }

  int n = Achievement.inc("pwd_mgr_unlock");
  if (n == 1) Achievement.unlock("pwd_mgr_unlock");

  _reloadMenu();
}

// ── Master password ─────────────────────────────────────────────────────────

bool PasswordManagerScreen::_verifyMaster(const char* pw)
{
  fs::File f = Uni.Storage->open(kMaster, "r");
  if (!f || f.size() != 32) { if (f) f.close(); return false; }

  uint8_t stored[32];
  if (f.read(stored, 32) != 32) { f.close(); return false; }
  f.close();

  uint8_t hash[32];
  _sha256str(String(pw) + "|VERIFY", hash);
  return memcmp(stored, hash, 32) == 0;
}

void PasswordManagerScreen::_setMaster(const char* pw)
{
  uint8_t hash[32];
  _sha256str(String(pw) + "|VERIFY", hash);

  fs::File f = Uni.Storage->open(kMaster, "w");
  if (!f) return;
  f.write(hash, 32);
  f.close();
}

// ── Vault ────────────────────────────────────────────────────────────────────

void PasswordManagerScreen::_loadVault()
{
  _entryCount = 0;
  if (!Uni.Storage->exists(kVault)) return;

  fs::File f = Uni.Storage->open(kVault, "r");
  if (!f) return;

  while (f.available() && _entryCount < kMaxEntries) {
    uint8_t recLen = 0;
    if (f.read(&recLen, 1) != 1 || recLen == 0) break;

    uint8_t enc[80] = {};
    if (recLen > sizeof(enc) || (size_t)f.read(enc, recLen) != recLen) break;

    uint8_t dec[80] = {};
    _xorCrypt(enc, recLen, dec);
    dec[recLen] = '\0';

    // format: label|type|caseMode|length[|source]
    // pre-2026-05 vaults omit |source — treat as SRC_LEGACY.
    char*   s  = (char*)dec;
    char*   p1 = strchr(s, '|');
    if (!p1) continue;
    char*   p2 = strchr(p1 + 1, '|');
    if (!p2) continue;
    char*   p3 = strchr(p2 + 1, '|');
    if (!p3) continue;
    char*   p4 = strchr(p3 + 1, '|');  // optional

    *p1 = *p2 = *p3 = '\0';
    if (p4) *p4 = '\0';
    uint8_t typ = (uint8_t)atoi(p1 + 1);
    uint8_t cm  = (uint8_t)atoi(p2 + 1);
    uint8_t len = (uint8_t)atoi(p3 + 1);
    uint8_t src = p4 ? (uint8_t)atoi(p4 + 1) : SRC_LEGACY;

    if (strlen(s) == 0 || len < 8 || len > 34 || typ > 2) continue;

    strncpy(_entries[_entryCount].label, s, sizeof(_entries[0].label) - 1);
    _entries[_entryCount].label[sizeof(_entries[0].label) - 1] = '\0';
    _entries[_entryCount].type     = typ;
    _entries[_entryCount].caseMode = cm < 3 ? cm : 0;
    _entries[_entryCount].length   = len;
    _entries[_entryCount].source   = (src == SRC_WEBAUTHN) ? SRC_WEBAUTHN : SRC_LEGACY;
    _entryCount++;
  }
  f.close();
}

void PasswordManagerScreen::_saveVault()
{
  fs::File f = Uni.Storage->open(kVault, "w");
  if (!f) return;

  for (uint8_t i = 0; i < _entryCount; i++) {
    String  plain   = String(_entries[i].label)    + "|" +
                      String(_entries[i].type)     + "|" +
                      String(_entries[i].caseMode) + "|" +
                      String(_entries[i].length)   + "|" +
                      String(_entries[i].source);
    uint8_t recLen  = (uint8_t)plain.length();
    uint8_t enc[80] = {};
    _xorCrypt((const uint8_t*)plain.c_str(), recLen, enc);
    f.write(&recLen, 1);
    f.write(enc, recLen);
  }
  f.close();
}

// ── Menu ─────────────────────────────────────────────────────────────────────

void PasswordManagerScreen::_reloadMenu()
{
  _loadVault();
  for (uint8_t i = 0; i < _entryCount; i++)
    _menuItems[i] = { _entries[i].label, nullptr };
  _menuItems[_entryCount] = { "Add New", nullptr };
  _state = STATE_MENU;
  setItems(_menuItems, _entryCount + 1);
}

// ── Add form ─────────────────────────────────────────────────────────────────

void PasswordManagerScreen::_enterAdd()
{
  _pendingLabel  = "";
  _pendingType   = 0;
  _pendingCase   = 0;
  _pendingLen    = 16;
  _pendingSource = _waMasterAvailable() ? SRC_WEBAUTHN : SRC_LEGACY;
  _updateAddLabels();

  uint8_t n = 0;
  _addItems[n++] = { "Label",  _addLblBuf };
#ifdef DEVICE_HAS_WEBAUTHN
  _addItems[n++] = { "Source", _addSrcBuf };
#endif
  _addItems[n++] = { "Type",   _addTypeBuf };
  _addItems[n++] = { "Case",   _addCaseBuf };
  _addItems[n++] = { "Length", _addLenBuf };
  _addItems[n++] = { "Save",   nullptr };

  _state = STATE_ADD;
  setItems(_addItems, n);
  render();
}

void PasswordManagerScreen::_updateAddLabels()
{
  static constexpr const char* typeLabels[] = {
    "Alphanumeric", "Alphabet", "Alpha+Sym"
  };
  static constexpr const char* caseLabels[] = {
    "Lower", "Upper", "Mixed"
  };

  if (_pendingLabel.length() == 0)
    strncpy(_addLblBuf, "-", sizeof(_addLblBuf));
  else
    strncpy(_addLblBuf, _pendingLabel.c_str(), sizeof(_addLblBuf) - 1);
  _addLblBuf[sizeof(_addLblBuf) - 1] = '\0';

  strncpy(_addTypeBuf, typeLabels[_pendingType < 3 ? _pendingType : 0],
          sizeof(_addTypeBuf) - 1);
  _addTypeBuf[sizeof(_addTypeBuf) - 1] = '\0';

  strncpy(_addCaseBuf, caseLabels[_pendingCase < 3 ? _pendingCase : 0],
          sizeof(_addCaseBuf) - 1);
  _addCaseBuf[sizeof(_addCaseBuf) - 1] = '\0';

  snprintf(_addLenBuf, sizeof(_addLenBuf), "%d", _pendingLen);

  const char* srcLabel = (_pendingSource == SRC_WEBAUTHN) ? "WebAuthn" : "Local";
  strncpy(_addSrcBuf, srcLabel, sizeof(_addSrcBuf) - 1);
  _addSrcBuf[sizeof(_addSrcBuf) - 1] = '\0';
}

void PasswordManagerScreen::_saveEntry()
{
  if (_pendingLabel.length() == 0) {
    ShowStatusAction::show("Label required", 1200);
    render();
    return;
  }
  if (_entryCount >= kMaxEntries) {
    ShowStatusAction::show("Max entries reached", 1200);
    render();
    return;
  }

  _pendingLabel.toCharArray(_entries[_entryCount].label, sizeof(_entries[0].label));
  _entries[_entryCount].type     = _pendingType;
  _entries[_entryCount].caseMode = _pendingCase;
  _entries[_entryCount].length   = _pendingLen;
  _entries[_entryCount].source   = _pendingSource;
  _entryCount++;
  _saveVault();

  int n = Achievement.inc("pwd_mgr_add");
  if (n == 1) Achievement.unlock("pwd_mgr_add");

  ShowStatusAction::show("Saved!", 800);
  _reloadMenu();
  render();
}

// ── View ──────────────────────────────────────────────────────────────────────

void PasswordManagerScreen::_enterView(uint8_t index)
{
  _viewIdx = index;
  if (!_generatePassword(_entries[index], _viewPw, sizeof(_viewPw) - 1)) {
#ifdef DEVICE_HAS_WEBAUTHN
    ShowStatusAction::show("WebAuthn master missing", 1500);
#else
    ShowStatusAction::show("WebAuthn not supported on this board", 2200);
#endif
    _reloadMenu();
    render();
    return;
  }
  _state = STATE_VIEW;
  _viewFirstRender = true;
  render();
}

void PasswordManagerScreen::_showEntryOptions(uint8_t index)
{
  static constexpr InputSelectAction::Option opts[] = {
    {"View",   "view"},
    {"Type",   "type"},
    {"Delete", "delete"},
  };
  const char* r = InputSelectAction::popup(_entries[index].label, opts, 3, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "view") == 0) {
    _enterView(index);
  } else if (strcmp(r, "type") == 0) {
    _enterView(index);
    if (_state != STATE_VIEW) return;  // _enterView aborted (e.g. webauthn master missing)
    _typePassword();
    render();
  } else if (strcmp(r, "delete") == 0) {
    _deleteEntry(index);
  }
}

void PasswordManagerScreen::_deleteEntry(uint8_t index)
{
  for (uint8_t i = index; i < _entryCount - 1; i++)
    _entries[i] = _entries[i + 1];
  _entryCount--;
  _saveVault();
  ShowStatusAction::show("Deleted", 800);
  _reloadMenu();
  render();
}

void PasswordManagerScreen::_typePassword()
{
  if (_mode == KeyboardScreen::MODE_BLE && !_keyboard->isConnected()) {
    ShowStatusAction::show("Not connected", 1500);
    return;
  }
  for (uint8_t i = 0; _viewPw[i] != '\0'; i++)
    _keyboard->write((uint8_t)_viewPw[i]);
  _keyboard->releaseAll();

  int n = Achievement.inc("pwd_mgr_type");
  if (n == 1) Achievement.unlock("pwd_mgr_type");

  ShowStatusAction::show("Typed!", 800);
}

void PasswordManagerScreen::_renderView()
{
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();

  if (_viewFirstRender) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    _viewFirstRender = false;
  }

  static constexpr int LABEL_H = 10;
  static constexpr int HINT_H  = 12;
  static constexpr int PAD     = 4;

  // Label
  {
    Sprite sp(&lcd);
    sp.createSprite(w, LABEL_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(_entries[_viewIdx].label, w / 2, 1);
    sp.pushSprite(x, y + PAD);
    sp.deleteSprite();
  }

  // Password — pick text size and split based on bodyW at runtime
  {
    String pw   = String(_viewPw);
    uint8_t len = pw.length();

    // At textSize 2: char = 12px wide → max per line = w/12
    // At textSize 1: char = 6px wide  → max per line = w/6
    int charsPerLineS2 = w / 12;
    int charsPerLineS1 = w / 6;

    uint8_t sz;
    bool    split;
    if (len <= (uint8_t)charsPerLineS2) {
      sz    = 2;
      split = false;
    } else if (len <= (uint8_t)charsPerLineS1) {
      sz    = 1;
      split = false;
    } else {
      sz    = 1;
      split = true;
    }

    int lineH  = (sz == 2) ? 16 : 8;
    int blockH = split ? lineH * 2 + 3 : lineH;
    int midH   = h - LABEL_H - PAD * 2 - HINT_H - PAD;
    int midY   = y + LABEL_H + PAD * 2;
    int blockY = midY + (midH - blockH) / 2;

    Sprite sp(&lcd);
    sp.createSprite(w, blockH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(sz);
    sp.setTextColor(TFT_WHITE);
    sp.setTextDatum(TC_DATUM);
    if (!split) {
      sp.drawString(pw.c_str(), w / 2, 0);
    } else {
      uint8_t half = len / 2;
      sp.drawString(pw.substring(0, half).c_str(), w / 2, 0);
      sp.drawString(pw.substring(half).c_str(),    w / 2, lineH + 3);
    }
    sp.pushSprite(x, blockY);
    sp.deleteSprite();
  }

  // Control footer
  {
    const int footerY = y + h - HINT_H - PAD;
    lcd.drawFastHLine(x, footerY - 1, w, TFT_DARKGREY);
    Sprite sp(&lcd);
    sp.createSprite(w, HINT_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString("[Press] Type  [Hold] Options", w / 2, 3);
    sp.pushSprite(x, footerY);
    sp.deleteSprite();
  }
}

// ── Password generation ──────────────────────────────────────────────────────

bool PasswordManagerScreen::_generatePassword(const Entry& e, char* out, uint8_t maxLen)
{
  // [type 0-2][case 0=lower 1=upper 2=mixed]
  static const char* charsets[3][3] = {
    // type 0: alphanumeric
    { "abcdefghijklmnopqrstuvwxyz0123456789",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" },
    // type 1: alphabet only
    { "abcdefghijklmnopqrstuvwxyz",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" },
    // type 2: alphanumeric + symbols
    { "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+",
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+" },
  };
  uint8_t     t       = e.type     < 3 ? e.type     : 0;
  uint8_t     c       = e.caseMode < 3 ? e.caseMode : 0;
  const char* charset = charsets[t][c];
  uint8_t     csLen   = (uint8_t)strlen(charset);
  uint8_t     len     = e.length < maxLen ? e.length : maxLen;

  uint8_t waKey[32] = {};
  bool    useWa     = false;
  if (e.source == SRC_WEBAUTHN) {
#ifdef DEVICE_HAS_WEBAUTHN
    if (webauthn::CredentialStore::hasMaster() &&
        webauthn::CredentialStore::getMasterKey(waKey)) {
      useWa = true;
    } else {
      return false;
    }
#else
    return false;
#endif
  }

  String  seed = String(_masterPw) + "|" + String(e.label) + "|" +
                 String(e.type)    + "|" + String(e.caseMode) + "|" + String(e.length);
  uint8_t hash[32];
  if (useWa) {
    _hmacSha256(waKey, 32, (const uint8_t*)seed.c_str(), seed.length(), hash);
  } else {
    _sha256str(seed, hash);
  }

  for (uint8_t i = 0; i < len; i++) {
    if (i > 0 && (i % 32) == 0) {
      String nextSeed = seed + "|" + String(i / 32);
      if (useWa) {
        _hmacSha256(waKey, 32, (const uint8_t*)nextSeed.c_str(),
                    nextSeed.length(), hash);
      } else {
        _sha256str(nextSeed, hash);
      }
    }
    out[i] = charset[hash[i % 32] % csLen];
  }
  out[len] = '\0';

  if (useWa) memset(waKey, 0, sizeof(waKey));
  return true;
}

// ── Crypto helpers ───────────────────────────────────────────────────────────

void PasswordManagerScreen::_sha256(const uint8_t* data, size_t len, uint8_t out[32])
{
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

void PasswordManagerScreen::_sha256str(const String& s, uint8_t out[32])
{
  _sha256((const uint8_t*)s.c_str(), s.length(), out);
}

void PasswordManagerScreen::_hmacSha256(const uint8_t* key, size_t keyLen,
                                        const uint8_t* data, size_t dataLen,
                                        uint8_t out[32])
{
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  key, keyLen, data, dataLen, out);
}

bool PasswordManagerScreen::_waMasterAvailable()
{
#ifdef DEVICE_HAS_WEBAUTHN
  return webauthn::CredentialStore::hasMaster();
#else
  return false;
#endif
}

void PasswordManagerScreen::_xorCrypt(const uint8_t* in, size_t len, uint8_t* out)
{
  uint8_t key[32];
  _sha256str(String(_masterPw) + "|ENC", key);
  for (size_t i = 0; i < len; i++)
    out[i] = in[i] ^ key[i % 32];
}