#include "WebAuthnRestoreScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputBipAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/webauthn/CredentialStore.h"
#include "utils/crypto/Bip39.h"
#include "utils/crypto/Bip39Wordlist.h"

#include <string.h>
#include <stdio.h>

const char* WebAuthnRestoreScreen::title()
{
  switch (_state) {
    case ST_BAD_CHECKSUM: return "Checksum Failed";
    case ST_CONFIRM:      return "Confirm Restore";
    case ST_DONE:         return "Restored";
    case ST_ERROR:        return "Error";
    case ST_WARNING:
    default:              return "Restore BIP39";
  }
}

void WebAuthnRestoreScreen::onInit()
{
  webauthn::CredentialStore::init();
  _state = ST_WARNING;
  render();
}

void WebAuthnRestoreScreen::_wipeBuffers()
{
  memset(_words,   0, sizeof(_words));
  memset(_entropy, 0, sizeof(_entropy));
  _entropyReady = false;
}

WebAuthnRestoreScreen::CollectResult WebAuthnRestoreScreen::_collectAndDecode()
{
  for (uint8_t i = 0; i < kWordCount; i++) {
    char t[18];
    snprintf(t, sizeof(t), "Word %u / %u", (unsigned)(i + 1), (unsigned)kWordCount);
    int idx = InputBipAction::popup(t);
    if (idx < 0) {
      _wipeBuffers();
      return COLLECT_CANCELLED;
    }
    _words[i] = (uint16_t)idx;
  }
  if (!unigeek::crypto::Bip39::decode(_words, kWordCount, _entropy)) {
    _entropyReady = false;
    return COLLECT_BAD_CSUM;
  }
  _entropyReady = true;
  return COLLECT_OK;
}

void WebAuthnRestoreScreen::onUpdate()
{
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    _wipeBuffers();
    Screen.goBack();
    return;
  }

  if (_state == ST_WARNING) {
    if (dir == INavigation::DIR_PRESS) {
      // Blocking: 24 InputBipAction popups in sequence, then checksum.
      switch (_collectAndDecode()) {
        case COLLECT_OK:        _state = ST_CONFIRM;     render(); break;
        case COLLECT_BAD_CSUM:  _state = ST_BAD_CHECKSUM; render(); break;
        case COLLECT_CANCELLED: Screen.goBack();                    break;
      }
    }
    return;
  }

  if (_state == ST_CONFIRM) {
    if (dir == INavigation::DIR_PRESS) {
      ShowStatusAction::show("Writing master.bin...", 0);
      bool ok = webauthn::CredentialStore::restoreMaster(_entropy,
                                                          sizeof(_entropy));
      _wipeBuffers();
      if (ok) { _state = ST_DONE; }
      else    { _err = "Restore failed"; _state = ST_ERROR; }
      render();
    }
    return;
  }

  if (_state == ST_BAD_CHECKSUM || _state == ST_DONE || _state == ST_ERROR) {
    if (dir == INavigation::DIR_PRESS) {
      _wipeBuffers();
      Screen.goBack();
    }
    return;
  }
}

void WebAuthnRestoreScreen::onRender()
{
  switch (_state) {
    case ST_WARNING:      _drawWarning();       break;
    case ST_BAD_CHECKSUM: _drawBadChecksum();   break;
    case ST_CONFIRM:      _drawConfirm();       break;
    case ST_DONE:         _drawDone();          break;
    case ST_ERROR:        _drawError();         break;
  }
}

// Body-sized sprite + single pushSprite per state transition. Each state
// only paints once when entered (no hot-loop redraw), so the per-render
// alloc is fine — same exemption WebAuthnBackupScreen relies on.

void WebAuthnRestoreScreen::_drawWarning()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  int cx = bodyW() / 2;
  int y  = 6;

  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString("RESTORE WARNING", cx, y); y += 16;
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString("This wipes the existing", cx, y); y += 12;
  sp.drawString("master + every passkey.", cx, y); y += 16;
  sp.setTextColor(TFT_CYAN, TFT_BLACK);
  sp.drawString("Have your 24-word backup", cx, y); y += 12;
  sp.drawString("ready to enter.",          cx, y);

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_GREEN, TFT_BLACK);
  sp.drawString("PRESS: continue / BACK: cancel", cx, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnRestoreScreen::_drawBadChecksum()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  int cx = bodyW() / 2;
  int y  = 6;

  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString("Checksum mismatch", cx, y); y += 16;
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString("One or more words are wrong.", cx, y); y += 12;
  sp.drawString("Master key NOT changed.",      cx, y); y += 16;
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Re-check your backup,",        cx, y); y += 12;
  sp.drawString("then try again.",              cx, y);

  sp.setTextDatum(BC_DATUM);
  sp.drawString("PRESS: exit", cx, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnRestoreScreen::_drawConfirm()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  int cx = bodyW() / 2;
  int y  = 6;

  sp.setTextColor(TFT_GREEN, TFT_BLACK);
  sp.drawString("Checksum OK", cx, y); y += 14;
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString("Spot-check vs backup:", cx, y); y += 14;

  sp.setTextDatum(TL_DATUM);
  int lx = 10;
  char line[40];
  uint8_t showIdx[4] = { 0, 1, (uint8_t)(kWordCount - 2), (uint8_t)(kWordCount - 1) };
  for (int j = 0; j < 4; j++) {
    if (j == 2) {
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("...", lx, y); y += 12;
    }
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(line, sizeof(line), "%2u. %s", (unsigned)(showIdx[j] + 1),
             unigeek::crypto::kBip39EnglishWordlist[_words[showIdx[j]]]);
    sp.drawString(line, lx, y); y += 12;
  }

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawString("PRESS: write / BACK: cancel", cx, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnRestoreScreen::_drawDone()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(MC_DATUM);
  int cx = bodyW() / 2;
  int cy = bodyH() / 2;

  sp.setTextColor(TFT_GREEN, TFT_BLACK);
  sp.drawString("Master key restored.", cx, cy - 16);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString("Reboot the device for",  cx, cy);
  sp.drawString("a clean storage state.", cx, cy + 12);

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("PRESS: exit", cx, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WebAuthnRestoreScreen::_drawError()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(MC_DATUM);
  int cx = bodyW() / 2;
  int cy = bodyH() / 2;
  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString(_err ? _err : "Failed", cx, cy);
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("PRESS: exit", cx, bodyH() - 4);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

#endif  // DEVICE_HAS_WEBAUTHN
