#include "WebAuthnScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "utils/webauthn/Ctap2.h"
#include "utils/webauthn/CredentialStore.h"
#include "utils/webauthn/WebAuthnCrypto.h"
#include "utils/webauthn/WebAuthnConfig.h"
#include "utils/webauthn/UsbProfile.h"
#include "utils/webauthn/WebAuthnLog.h"

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

  // Construct the singleton — this attempts to claim the WEBAUTHN profile.
  // If keyboard/mouse already grabbed USB this boot, registration fails
  // and we render a "reboot to switch" message instead of starting up.
  webauthn::fido();
  if (webauthn::activeUsbProfile() != webauthn::UsbProfile::WEBAUTHN
      || !webauthn::fido().isRegistered()) {
    _profileMismatch = true;
    return;
  }

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
  if (_profileMismatch) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        Screen.goBack();
      }
    }
    return;
  }

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

  if (_profileMismatch) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("USB busy", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString("Keyboard/Mouse already",
                   bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 4);
    lcd.drawString("claimed USB this boot.",
                   bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 16);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("Reboot, then open WebAuthn first.",
                   bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 32);
    return;
  }

  bool connected = webauthn::fido().isConnected();

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
    _chromeDrawn   = true;
    _lastConnected = !connected;  // force first status paint
    _logSerial     = (uint32_t)-1;
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

  // ── Status banner (top of body) ──────────────────────────────────────
  if (_lastConnected != connected) {
    _lastConnected = connected;
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 20);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(2);
    sp.setTextColor(connected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    sp.drawString(connected ? "Active" : "Idle", bodyW() / 2, 10);
    sp.pushSprite(bodyX(), bodyY() + 2);
    sp.deleteSprite();
  }

  // ── Tx counter line ──────────────────────────────────────────────────
  {
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 12);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "Tx: %lu", (unsigned long)_txCount);
    sp.drawString(buf, bodyW() / 2, 6);
    sp.pushSprite(bodyX(), bodyY() + 24);
    sp.deleteSprite();
  }

  // ── Log ring viewer (bottom of body) ─────────────────────────────────
  // Repainted only when something new was logged; sized to fill below the
  // status banner and tx counter.
#if WEBAUTHN_DEBUG
  auto& ring = webauthn_log::ring();
  if (_logSerial == ring.serial) return;
  _logSerial = ring.serial;

  const int logTop  = bodyY() + 40;
  const int logH    = (bodyY() + bodyH() - 12) - logTop;  // leave footer space
  if (logH <= 0) return;

  Sprite sp(&lcd);
  sp.createSprite(bodyW(), logH);
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_GREEN, TFT_BLACK);

  uint8_t lineH = sp.fontHeight() + 1;
  uint8_t maxLines = (uint8_t)(logH / lineH);
  uint8_t shown = ring.count < maxLines ? ring.count : maxLines;
  // Walk back `shown` slots from head, oldest first.
  uint8_t start = (uint8_t)((ring.head + webauthn_log::kLines - shown) % webauthn_log::kLines);
  for (uint8_t i = 0; i < shown; i++) {
    uint8_t idx = (uint8_t)((start + i) % webauthn_log::kLines);
    sp.drawString(ring.lines[idx], 0, i * lineH);
  }
  sp.pushSprite(bodyX(), logTop);
  sp.deleteSprite();
#endif
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
