#include "WebAuthnScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "utils/webauthn/Ctap2.h"
#include "utils/webauthn/CredentialStore.h"
#include "utils/webauthn/WebAuthnCrypto.h"
#include "utils/webauthn/WebAuthnConfig.h"

namespace {
constexpr uint32_t kPromptTimeoutMs = 30000;
constexpr uint32_t kKeepaliveMs     = 100;
}  // namespace

void WebAuthnScreen::_onReportThunk(const uint8_t* buf64, void* user)
{
  static_cast<WebAuthnScreen*>(user)->_onReport(buf64);
}

void WebAuthnScreen::_onReport(const uint8_t* buf64)
{
  _txCount++;
  _ctaphid.onReport(buf64);
}

void WebAuthnScreen::onInit()
{
  webauthn::WebAuthnCrypto::init();
  webauthn::CredentialStore::init();

  webauthn::fido().begin();
  webauthn::fido().setOnReport(&WebAuthnScreen::_onReportThunk, this);
  _ctaphid.setSender(&webauthn::fido());
  _ctaphid.setHandler(&webauthn::Ctap2::dispatch, nullptr);

  webauthn::Ctap2::setUserPresenceFn(&WebAuthnScreen::_onUserPresence, this);

  int n = Achievement.inc("webauthn_first_use");
  if (n == 1) Achievement.unlock("webauthn_first_use");
}

void WebAuthnScreen::onUpdate()
{
  // Drain inbound USB reports → CTAPHID assembly → CTAP2 dispatch.
  webauthn::fido().poll();
  _ctaphid.tick((uint32_t)millis());

  // Back exits the screen and tears down the USB FIDO presence (the FIDO
  // descriptor stays advertised since arduino-esp32 USB can't be torn down,
  // but we stop forwarding reports).
  if (_state == ST_ACTIVE && Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      webauthn::fido().setOnReport(nullptr, nullptr);
      webauthn::Ctap2::setUserPresenceFn(nullptr, nullptr);
      Screen.goBack();
      return;
    }
  }
}

void WebAuthnScreen::onRender()
{
  auto& lcd = Uni.Lcd;
  bool connected = webauthn::fido().isConnected();

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
    _chromeDrawn   = true;
    _lastConnected = !connected;  // force first status paint
  }

  if (_state == ST_PROMPTING) {
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 64);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(2);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString("Confirm:", bodyW() / 2, 12);
    sp.setTextSize(1);
    sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString(_promptRpId ? _promptRpId : "(unknown)", bodyW() / 2, 36);
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.drawString("PRESS to allow / BACK to deny", bodyW() / 2, 52);
    sp.pushSprite(bodyX(), bodyY() + (bodyH() - 64) / 2 - 6);
    sp.deleteSprite();
    return;
  }

  // ST_ACTIVE — only repaint when status changes
  if (_lastConnected == connected) {
    // Refresh transaction counter line every render call (cheap).
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 14);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "Tx: %lu", (unsigned long)_txCount);
    sp.drawString(buf, bodyW() / 2, 7);
    sp.pushSprite(bodyX(), bodyY() + (bodyH() / 2) + 14);
    sp.deleteSprite();
    return;
  }
  _lastConnected = connected;

  Sprite sp(&lcd);
  sp.createSprite(bodyW(), 28);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(connected ? TFT_GREEN : TFT_RED, TFT_BLACK);
  sp.drawString(connected ? "Active" : "Idle", bodyW() / 2, 14);
  sp.pushSprite(bodyX(), bodyY() + (bodyH() / 2) - 14);
  sp.deleteSprite();
}

bool WebAuthnScreen::_onUserPresence(const char* rpId, void* user)
{
  auto* self = static_cast<WebAuthnScreen*>(user);
  self->_state         = ST_PROMPTING;
  self->_promptRpId    = rpId;
  self->_promptStartMs = (uint32_t)millis();
  self->_chromeDrawn   = false;
  self->render();

  uint32_t lastKa = 0;
  while (true) {
    uint32_t now = (uint32_t)millis();
    if (now - self->_promptStartMs >= kPromptTimeoutMs) {
      self->_state = ST_ACTIVE;
      self->_chromeDrawn = false;
      self->render();
      return false;
    }
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_PRESS) {
        self->_state = ST_ACTIVE;
        self->_chromeDrawn = false;
        self->render();
        int n = Achievement.inc("webauthn_first_passkey");
        if (n == 1) Achievement.unlock("webauthn_first_passkey");
        return true;
      }
      if (dir == INavigation::DIR_BACK) {
        self->_state = ST_ACTIVE;
        self->_chromeDrawn = false;
        self->render();
        return false;
      }
    }
    // CTAPHID keepalive: tell the host we're still alive and need user input.
    if (now - lastKa >= kKeepaliveMs) {
      self->_ctaphid.sendKeepalive(self->_ctaphid.currentCid(), 0x02 /* UPNEEDED */);
      lastKa = now;
    }
    delay(5);
  }
}

#endif  // DEVICE_HAS_WEBAUTHN
