#include "WebAuthnBackupScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/webauthn/CredentialStore.h"
#include "utils/webauthn/WebAuthnCrypto.h"
#include "utils/crypto/Bip39.h"
#include "utils/crypto/Bip39Wordlist.h"

#include <string.h>

const char* WebAuthnBackupScreen::title()
{
  switch (_state) {
    case ST_NO_MASTER: return "Backup Unavailable";
    case ST_WARNING:   return "Backup: Warning";
    case ST_WORDS:     return "Backup Seed";
    case ST_DONE:      return "Backup Done";
    case ST_DENIED:    return "Backup Denied";
    default:           return "Backup";
  }
}

void WebAuthnBackupScreen::onInit()
{
  webauthn::CredentialStore::init();
  if (!webauthn::CredentialStore::hasMaster()) {
    // No master.bin yet — point the user at the Generate flow instead of
    // emitting a generic "load failed" error.
    _state = ST_NO_MASTER;
  } else if (webauthn::CredentialStore::isPinSet()) {
    _state = ST_PIN_PROMPT;
  } else {
    // No PIN — go straight to warning, but the warning copy is sterner.
    _state = ST_WARNING;
  }
  render();
}

bool WebAuthnBackupScreen::_checkPin()
{
  // PIN per CTAP2 spec is 4..63 bytes UTF-8; INPUT_TEXT covers it.
  String pin = InputTextAction::popup("Enter PIN", "", InputTextAction::INPUT_TEXT);
  if (pin.length() == 0) return false;

  uint8_t hostHash[32];
  webauthn::WebAuthnCrypto::sha256(
      reinterpret_cast<const uint8_t*>(pin.c_str()),
      pin.length(), hostHash);

  uint8_t stored[16], storedLen, retries;
  if (!webauthn::CredentialStore::getPinHash(stored, &storedLen, &retries)) {
    return false;
  }
  // Constant-time compare on the 16-byte prefix.
  uint8_t diff = 0;
  for (size_t i = 0; i < 16; i++) diff |= (uint8_t)(stored[i] ^ hostHash[i]);
  return diff == 0;
}

bool WebAuthnBackupScreen::_generate()
{
  uint8_t master[webauthn::CredentialStore::kMasterKeySize];
  if (!webauthn::CredentialStore::getMasterKey(master)) {
    _err = "Master key load failed";
    return false;
  }
  bool ok = unigeek::crypto::Bip39::encode(master, sizeof(master), _wordIdx);
  memset(master, 0, sizeof(master));   // never linger
  if (!ok) { _err = "BIP-39 encode failed"; return false; }
  _wordsReady = true;
  return true;
}

void WebAuthnBackupScreen::onUpdate()
{
  if (_state == ST_PIN_PROMPT) {
    if (!_checkPin()) {
      // Cancel or wrong PIN — silently bail back to the menu rather than
      // burning a screen on a denial message the user already understands.
      Screen.goBack();
      return;
    }
    _state = ST_WARNING;
    render();
    return;
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    Screen.goBack();
    return;
  }

  if (_state == ST_WARNING) {
    if (dir == INavigation::DIR_PRESS) {
      if (!_wordsReady && !_generate()) { _state = ST_DENIED; render(); return; }
      _page = 0;
      _state = ST_WORDS;
      render();
    }
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

  if (_state == ST_DONE || _state == ST_DENIED || _state == ST_NO_MASTER) {
    if (dir == INavigation::DIR_PRESS) {
      // Wipe the displayed words from memory before exit.
      memset(_wordIdx, 0, sizeof(_wordIdx));
      _wordsReady = false;
      Screen.goBack();
    }
    return;
  }
}

void WebAuthnBackupScreen::onRender()
{
  switch (_state) {
    case ST_PIN_PROMPT: /* InputTextAction handles its own UI */ return;
    case ST_NO_MASTER:  _drawNoMaster(); break;
    case ST_WARNING:    _drawWarning();  break;
    case ST_WORDS:      _drawWords();    break;
    case ST_DONE:       _drawDone();     break;
    case ST_DENIED:     _drawError();    break;
  }
}

void WebAuthnBackupScreen::_drawWarning()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int y  = bodyY() + 6;

  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.drawString("WARNING", cx, y); y += 16;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("24 words below back up the",         cx, y); y += 12;
  lcd.drawString("FIDO master key.",                    cx, y); y += 16;

  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Anyone who sees them can clone",     cx, y); y += 12;
  lcd.drawString("every passkey on this device.",      cx, y); y += 16;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("Write them down on paper.",          cx, y); y += 12;
  lcd.drawString("Do NOT photograph or share.",        cx, y); y += 16;

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("PRESS: continue / BACK: cancel",
                 cx, bodyY() + bodyH() - 4);
}

WebAuthnBackupScreen::Layout WebAuthnBackupScreen::_layout()
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

  // Two columns need ~180 px usable: " NN. interactive " is ~88 px at size 1
  // (worst-case BIP-39 word len ≈ 11 + "NN. " = ~14 chars × 6 px).
  L.cols = (bodyW() >= 200) ? 2 : 1;

  uint16_t wpp = (uint16_t)L.rows * L.cols;
  if (wpp < 2)            wpp = 2;
  if (wpp > kWordCount)   wpp = kWordCount;
  L.wordsPerPage = (uint8_t)wpp;

  L.pageCount = (uint8_t)((kWordCount + L.wordsPerPage - 1) / L.wordsPerPage);
  return L;
}

void WebAuthnBackupScreen::_drawWords()
{
  // Render off-screen into a body-sized sprite, then push in one blit so the
  // page transition is flicker-free. Page changes are rare (user button
  // press) so the per-render sprite alloc is fine — see screen-patterns
  // exemption for "whole body genuinely repaints" cases.
  Layout L = _layout();
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  int16_t colW    = bodyW() / L.cols;
  int16_t leftPad = 8;
  int16_t numPad  = 22;       // pixels for "NN." prefix at size-1
  int16_t top     = L.headerH;

  // Page indicator (sprite-relative coords from here on)
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "Page %u / %u",
           (unsigned)(_page + 1), (unsigned)L.pageCount);
  sp.drawString(hdr, bodyW() / 2, 2);

  // Word grid (column-major within row → visual order 1,2 / 3,4 / ...)
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
    const char* w = unigeek::crypto::kBip39EnglishWordlist[_wordIdx[base + i]];
    sp.drawString(w, x + numPad, y);
  }

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(_page + 1 < L.pageCount ? "PRESS: next  /  BACK: exit"
                                        : "PRESS: done  /  BACK: exit",
                bodyW() / 2, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnBackupScreen::_drawDone()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.drawString("All 24 words shown.", cx, cy - 16);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("Verify your written copy", cx, cy);
  lcd.drawString("matches before exiting.",  cx, cy + 12);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("PRESS: exit",
                 bodyX() + bodyW() / 2, bodyY() + bodyH() - 4);
}

void WebAuthnBackupScreen::_drawError()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;
  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.drawString(_err ? _err : "Denied", cx, cy);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("PRESS: exit",
                 bodyX() + bodyW() / 2, bodyY() + bodyH() - 4);
}

void WebAuthnBackupScreen::_drawNoMaster()
{
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  int cx = bodyX() + bodyW() / 2;
  int y  = bodyY() + 8;

  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("No master key yet", cx, y); y += 16;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("Nothing to back up.",     cx, y); y += 14;
  lcd.drawString("Create one first via:",   cx, y); y += 14;

  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString("Manage WebAuthn",         cx, y); y += 12;
  lcd.drawString(">  Generate BIP39",       cx, y);

  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("PRESS / BACK: exit",
                 bodyX() + bodyW() / 2, bodyY() + bodyH() - 4);
}

#endif  // DEVICE_HAS_WEBAUTHN
