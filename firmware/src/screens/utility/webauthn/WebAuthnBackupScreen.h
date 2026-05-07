#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_WEBAUTHN is defined

#ifdef DEVICE_HAS_WEBAUTHN

#include "ui/templates/BaseScreen.h"

// On-device BIP-39 backup of the FIDO master key.
//
// Flow:
//   1. PIN gate (skipped if no PIN is set, with a stronger warning instead)
//   2. Warning page — "Anyone who sees these words can clone your key"
//   3. 4 pages of 6 words = 24 BIP-39 words total
//   4. Done page — "have you written them down?"
//
// The master key never leaves the device — only the BIP-39 representation
// is shown on screen. No host channel involvement.
class WebAuthnBackupScreen : public BaseScreen {
public:
  const char* title()    override;
  bool inhibitPowerOff() override { return true; }   // don't dim while reading words

  void onInit()    override;
  void onUpdate()  override;
  void onRender()  override;

private:
  enum State : uint8_t {
    ST_NO_MASTER,     // no master.bin — render "Generate BIP39 first" hint
    ST_PIN_PROMPT,    // ask for PIN if one is set
    ST_WARNING,       // pre-display warning + UP confirm
    ST_WORDS,         // showing words (paged)
    ST_DONE,          // "have you written them down?" exit prompt
    ST_DENIED,        // technical failure — render error and exit on press
  };

  static constexpr uint8_t kWordCount    = 24;

  // Layout computed per render from bodyW()/bodyH() — caches words-per-page +
  // page count so the same numbers drive both rendering and page advancement.
  struct Layout {
    uint8_t  cols;          // 1 or 2 columns
    uint8_t  rows;          // word rows per page
    uint8_t  wordsPerPage;  // cols * rows, clamped to [2, kWordCount]
    uint8_t  pageCount;     // ceil(kWordCount / wordsPerPage)
    int16_t  rowH;          // pixels per row
    int16_t  headerH;       // top reserve (page indicator)
    int16_t  footerH;       // bottom reserve (hint line)
  };
  Layout _layout();

  State    _state       = ST_PIN_PROMPT;
  uint8_t  _page        = 0;
  uint16_t _wordIdx[kWordCount];                       // BIP-39 wordlist indices
  bool     _wordsReady  = false;
  const char* _err      = nullptr;

  bool _generate();          // pull master key, encode to _wordIdx
  bool _checkPin();          // prompt + verify against stored hash
  void _drawWarning();
  void _drawWords();
  void _drawDone();
  void _drawError();
  void _drawNoMaster();
};

#endif  // DEVICE_HAS_WEBAUTHN
