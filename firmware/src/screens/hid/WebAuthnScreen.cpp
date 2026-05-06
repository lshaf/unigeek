#include "WebAuthnScreen.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "utils/webauthn/Ctap2.h"
#include "utils/webauthn/U2f.h"
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
#ifdef WEBAUTHN_LOG_BEGIN
  // Bring up the board-defined debug log Stream once. On cardputer_adv this
  // is Serial1 wired to the Grove port (also = GPS pins) — Serial/HWCDC is
  // dead once USB.begin() runs below.
  static bool s_logInited = false;
  if (!s_logInited) {
    s_logInited = true;
    WEBAUTHN_LOG_BEGIN();
  }
#endif

  webauthn::WebAuthnCrypto::init();
  // Ephemeral ECDH key — regenerated each time the WebAuthn screen opens;
  // hosts negotiate it via authenticatorClientPIN.getKeyAgreement before
  // every hmac-secret call so a fresh keypair per screen-entry is fine.
  webauthn::WebAuthnCrypto::initEphemeralEcdh();
  webauthn::Ctap2::initPinAuthToken();
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
  webauthn::U2f::setUserPresenceFn  (&WebAuthnScreen::_onUserPresence, this);

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
      webauthn::U2f::setUserPresenceFn  (nullptr, nullptr);
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
    _lastTxDrawn   = (uint32_t)-1;
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

  // Layout of idle/active body (no log ring — debug goes to Grove Serial1):
  //   ┌──── bodyY ──────────────────────────┐
  //   │                                     │
  //   │            [ Active ]      ← size 3 │
  //   │            Tx: N           ← size 1 │
  //   │                                     │
  //   │            BACK: Stop      ← footer │
  //   └─────────────────────────────────────┘
  const int centerX = bodyX() + bodyW() / 2;
  const int statusY = bodyY() + bodyH() / 2 - 12;
  const int txY     = bodyY() + bodyH() / 2 + 16;

  // ── Status banner ────────────────────────────────────────────────────
  if (_lastConnected != connected) {
    _lastConnected = connected;
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 28);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(3);
    sp.setTextColor(connected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    sp.drawString(connected ? "Active" : "Idle", bodyW() / 2, 14);
    sp.pushSprite(bodyX(), statusY - 14);
    sp.deleteSprite();
  }

  // ── Tx counter line ──────────────────────────────────────────────────
  if (_lastTxDrawn != _txCount) {
    _lastTxDrawn = _txCount;
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), 12);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "Tx: %lu", (unsigned long)_txCount);
    sp.drawString(buf, bodyW() / 2, 6);
    sp.pushSprite(bodyX(), txY - 6);
    sp.deleteSprite();
  }
  (void)centerX;  // reserved for future indicators (e.g. UV/PIN status)
}

bool WebAuthnScreen::_onUserPresence(const char* rpId, void* user)
{
  auto* self = static_cast<WebAuthnScreen*>(user);
  self->_state         = ST_PROMPTING;
  self->_promptRpId    = rpId;
  self->_promptStartMs = (uint32_t)millis();
  self->_chromeDrawn   = false;

  // Wake the LCD if it auto-dimmed during idle. Refresh `lastActiveMs` here
  // and on every loop tick below so power-saving can't cut the display while
  // the user is being prompted to confirm.
  Uni.lastActiveMs = millis();
  if (Uni.lcdOff) {
    Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS,
                                              APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
    Uni.lcdOff = false;
  }
  if (Uni.Speaker) Uni.Speaker->beep();

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
    // Main loop's Device::update isn't running while we block here, so the
    // keyboard FIFO + nav state would freeze. Pump it explicitly. Also
    // refresh `lastActiveMs` so power-save can't dim/sleep the screen while
    // the user is being prompted.
    Uni.update();
    Uni.lastActiveMs = millis();
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
