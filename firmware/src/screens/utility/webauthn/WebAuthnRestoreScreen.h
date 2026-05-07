#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_WEBAUTHN is defined

#ifdef DEVICE_HAS_WEBAUTHN

#include "ui/templates/BaseScreen.h"

// Restore the FIDO master key from a 24-word BIP-39 mnemonic.
//
// Flow:
//   1. Warning   — restore wipes ALL existing creds (old credentialIds were
//                  bound to the old master and become uncrackable garbage).
//   2. Collect   — InputBipAction::popup × 24, prefix-narrowing picker on
//                  non-keyboard boards, free-form typing on keyboard boards.
//   3. Verify    — Bip39::decode + SHA-256 checksum.
//   4. Confirm   — show first/last 2 words for visual cross-check.
//   5. Restore   — CredentialStore::restoreMaster (wipe + write new master).
//   6. Done      — suggest reboot for a clean storage state.
class WebAuthnRestoreScreen : public BaseScreen {
public:
  const char* title() override;
  bool inhibitPowerOff() override { return true; }

  void onInit()    override;
  void onUpdate()  override;
  void onRender()  override;

private:
  enum State : uint8_t {
    ST_WARNING,
    ST_BAD_CHECKSUM,
    ST_CONFIRM,
    ST_DONE,
    ST_ERROR,
  };

  static constexpr uint8_t kWordCount = 24;

  State       _state = ST_WARNING;
  uint16_t    _words[kWordCount];
  uint8_t     _entropy[32];
  bool        _entropyReady = false;
  const char* _err          = nullptr;

  enum CollectResult : uint8_t {
    COLLECT_OK,
    COLLECT_CANCELLED,
    COLLECT_BAD_CSUM,
  };
  CollectResult _collectAndDecode();
  void          _wipeBuffers();

  void _drawWarning();
  void _drawBadChecksum();
  void _drawConfirm();
  void _drawDone();
  void _drawError();
};

#endif  // DEVICE_HAS_WEBAUTHN
