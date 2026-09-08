#include "WebAuthnGenerateScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/RandomSeed.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/webauthn/CredentialStore.h"
#include "utils/webauthn/WebAuthnCrypto.h"
#include "utils/crypto/Bip39.h"
#include "utils/crypto/Bip39Wordlist.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>
#include <string.h>

namespace {
constexpr uint32_t kNtpTimeoutMs    = 15000;
constexpr time_t   kNtpSanityEpoch  = 1700000000;  // 2023-11-14
}

const char* WebAuthnGenerateScreen::title()
{
  switch (_state) {
    case ST_WIFI_LIST:   return "Pick WiFi";
    case ST_NTP_SYNC:    return "Syncing Time";
    case ST_GENERATING:  return "Generating";
    case ST_WORDS:       return _isRegen ? "Regenerated Seed" : "New Seed";
    case ST_DONE:        return "Done";
    case ST_ERROR:       return "Error";
    case ST_WARNING:
    default:             return _isRegen ? "Regenerate BIP39" : "Generate BIP39";
  }
}

void WebAuthnGenerateScreen::onInit()
{
  webauthn::CredentialStore::init();
  _isRegen = webauthn::CredentialStore::hasMaster();
  _state   = ST_WARNING;
  setItems(nullptr, 0);   // ListScreen body inactive in this state
  render();
}

void WebAuthnGenerateScreen::onBack()
{
  // Drop the WiFi link if we're the ones who brought it up. Mirrors
  // NetworkMenuScreen::onBack so we don't leak a STA association after this
  // screen exits.
  if (_state >= ST_WIFI_LIST && _state != ST_WARNING) {
    WiFi.disconnect(true);
  }
  if (_state == ST_WORDS || _state == ST_DONE) {
    memset(_wordIdx, 0, sizeof(_wordIdx));
    _wordsReady = false;
  }
  Screen.goBack();
}

// ── State entry ────────────────────────────────────────────────────────

void WebAuthnGenerateScreen::_enterWifiList()
{
  WiFi.mode(WIFI_MODE_STA);
  ShowStatusAction::show("Scanning...", 0);

  _scannedCount = WifiUtility::scan(_scanned, kMaxScans);

  int ns = Achievement.inc("wifi_first_scan");
  if (ns == 1) Achievement.unlock("wifi_first_scan");

  for (uint8_t i = 0; i < _scannedCount; i++) {
    _scannedItems[i] = { _scanned[i].ssid, _scanned[i].bssid, _scanned[i].rssi, true, true };
  }

  _state = ST_WIFI_LIST;
  setItems(_scannedItems, _scannedCount);
}

bool WebAuthnGenerateScreen::_connectAndAdvance(uint8_t index)
{
  if (index >= _scannedCount) return false;
  auto rc = WifiUtility::connectWithPrompt(_scanned[index].bssid,
                                           _scanned[index].ssid);
  if (rc == WifiUtility::CONNECT_OK) {
    int nc = Achievement.inc("wifi_first_connect");
    if (nc == 1)  Achievement.unlock("wifi_first_connect");
    if (nc == 5)  Achievement.unlock("wifi_connect_5");
    if (nc == 20) Achievement.unlock("wifi_connect_20");

    // Start NTP and switch to polling state.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    _ntpStartMs = (uint32_t)millis();
    _state      = ST_NTP_SYNC;
    setItems(nullptr, 0);
    render();
    return true;
  }
  if (rc == WifiUtility::CONNECT_CANCELLED) {
    // Stay on the list — user can pick again or BACK.
    render();
    return false;
  }
  ShowStatusAction::show("Connection Failed!", 1500);
  // Re-scan so the list reflects current air conditions.
  _enterWifiList();
  return false;
}

bool WebAuthnGenerateScreen::_runGenerate()
{
  // RTC set + RF active. Refresh both RNG layers before pulling the master.
  RandomSeed::reseed();
  if (!webauthn::WebAuthnCrypto::reseed()) {
    _err = "DRBG reseed failed"; return false;
  }
  if (!webauthn::CredentialStore::generateMaster(_isRegen)) {
    _err = _isRegen ? "Regenerate failed" : "Generate failed";
    return false;
  }

  uint8_t master[webauthn::CredentialStore::kMasterKeySize];
  if (!webauthn::CredentialStore::getMasterKey(master)) {
    _err = "Master read-back failed"; return false;
  }
  bool ok = unigeek::crypto::Bip39::encode(master, sizeof(master), _wordIdx);
  memset(master, 0, sizeof(master));
  if (!ok) { _err = "BIP-39 encode failed"; return false; }
  _wordsReady = true;
  return true;
}

// ── Input + ticking ────────────────────────────────────────────────────

void WebAuthnGenerateScreen::onUpdate()
{
  // Long-running phases driven by ticks (no input expected).
  if (_state == ST_NTP_SYNC) {
    uint32_t now = (uint32_t)millis();
    if (now - _ntpStartMs > kNtpTimeoutMs) {
      _err = "NTP timeout"; _state = ST_ERROR; render(); return;
    }
    time_t t = 0;
    time(&t);
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && t > kNtpSanityEpoch) {
      _state = ST_GENERATING;
      render();
    }
    return;
  }

  if (_state == ST_GENERATING) {
    if (!_generateRan) {
      _generateRan = true;
      if (_runGenerate()) { _state = ST_WORDS; _page = 0; }
      else                { _state = ST_ERROR; }
      render();
    }
    return;
  }

  // ST_WIFI_LIST → defer to ListScreen for scrolling/selection.
  if (_state == ST_WIFI_LIST) {
    ListScreen::onUpdate();
    return;
  }

  // Custom-rendered states (warning/words/done/error) handle their own input.
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) { onBack(); return; }

  if (_state == ST_WARNING) {
    if (dir == INavigation::DIR_PRESS) _enterWifiList();
    return;
  }

  if (_state == ST_WORDS) {
    Layout L = _layout();
    if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
      if (_page + 1 < L.pageCount) _page++;
      else                         _state = ST_DONE;
      render();
    } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
      if (_page > 0) { _page--; render(); }
    }
    return;
  }

  if (_state == ST_DONE || _state == ST_ERROR) {
    if (dir == INavigation::DIR_PRESS) onBack();
    return;
  }
}

void WebAuthnGenerateScreen::onItemSelected(uint8_t index)
{
  if (_state == ST_WIFI_LIST) {
    _connectAndAdvance(index);
  }
}

// ── Render dispatch ────────────────────────────────────────────────────

void WebAuthnGenerateScreen::onRender()
{
  switch (_state) {
    case ST_WARNING:    _drawWarning();                                     break;
    case ST_WIFI_LIST:  ListScreen::onRender();                             break;
    case ST_NTP_SYNC:   _drawSync("Syncing clock", "via NTP...");           break;
    case ST_GENERATING: _drawSync("Generating",   "BIP-39 seed...");        break;
    case ST_WORDS:      _drawWords();                                        break;
    case ST_DONE:       _drawDone();                                         break;
    case ST_ERROR:      _drawError();                                        break;
  }
}

WebAuthnGenerateScreen::Layout WebAuthnGenerateScreen::_layout()
{
  Layout L;
  L.rowH    = 14;
  L.headerH = 18;
  L.footerH = 18;
  int16_t avail = (int16_t)bodyH() - L.headerH - L.footerH;
  if (avail < L.rowH) avail = L.rowH;
  uint8_t rows = (uint8_t)(avail / L.rowH);
  if (rows < 1) rows = 1;
  L.rows = rows;
  L.cols = (bodyW() >= 200) ? 2 : 1;
  uint16_t wpp = (uint16_t)L.rows * L.cols;
  if (wpp < 2)          wpp = 2;
  if (wpp > kWordCount) wpp = kWordCount;
  L.wordsPerPage = (uint8_t)wpp;
  L.pageCount    = (uint8_t)((kWordCount + L.wordsPerPage - 1) / L.wordsPerPage);
  return L;
}

void WebAuthnGenerateScreen::_drawWarning()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int y  = bodyY() + 6;

  lcd.setTextColor(_isRegen ? TFT_RED : TFT_YELLOW, TFT_BLACK);
  lcd.drawString(_isRegen ? "REGENERATE WARNING" : "FIRST-TIME GENERATE", cx, y);
  y += 16;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (_isRegen) {
    lcd.drawString("This wipes the existing master",  cx, y); y += 12;
    lcd.drawString("key + every passkey on device.",  cx, y); y += 16;
  } else {
    lcd.drawString("Creates the FIDO master key.",    cx, y); y += 12;
    lcd.drawString("Required before any registration.", cx, y); y += 16;
  }

  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString("Needs WiFi + internet for NTP",     cx, y); y += 12;
  lcd.drawString("(better RNG entropy).",             cx, y);

  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.drawString("[Press] Continue / [Back] Cancel",
                 cx, bodyY() + bodyH() - 4);
}

void WebAuthnGenerateScreen::_drawSync(const char* line1, const char* line2)
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString(line1, cx, cy - 8);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString(line2, cx, cy + 8);
}

void WebAuthnGenerateScreen::_drawWords()
{
  // Sprite-blit pattern matches WebAuthnBackupScreen — flicker-free page swap.
  Layout L = _layout();
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  int16_t colW    = bodyW() / L.cols;
  int16_t leftPad = 8;
  int16_t numPad  = 22;
  int16_t top     = L.headerH;

  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "Page %u / %u",
           (unsigned)(_page + 1), (unsigned)L.pageCount);
  sp.drawString(hdr, bodyW() / 2, 2);

  uint8_t base = _page * L.wordsPerPage;
  for (uint8_t i = 0; i < L.wordsPerPage && (base + i) < kWordCount; i++) {
    uint8_t  row = (L.cols == 1) ? i : (i / L.cols);
    uint8_t  col = (L.cols == 1) ? 0 : (i %  L.cols);
    int16_t  x   = col * colW + leftPad;
    int16_t  y   = top + row * L.rowH;

    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char num[6];
    snprintf(num, sizeof(num), "%2u.", (unsigned)(base + i + 1));
    sp.drawString(num, x, y);

    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(unigeek::crypto::kBip39EnglishWordlist[_wordIdx[base + i]],
                  x + numPad, y);
  }

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(_page + 1 < L.pageCount ? "[Press] Next"
                                        : "[Press] Done",
                bodyW() / 2, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnGenerateScreen::_drawDone()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.drawString("Master key generated.", cx, cy - 16);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("Verify your written copy", cx, cy);
  lcd.drawString("matches before exiting.",  cx, cy + 12);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
}

void WebAuthnGenerateScreen::_drawError()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;
  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.drawString(_err ? _err : "Failed", cx, cy);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
}

#endif  // DEVICE_HAS_WEBAUTHN
