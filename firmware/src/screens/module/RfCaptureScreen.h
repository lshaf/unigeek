//
// RfCaptureScreen — shared base for radio capture/replay/jam screens
// (SubGHzScreen via CC1101, M5RF433Screen via single-pin OOK).
//
// Derived classes plug in the radio backend via the _radio* virtuals and
// supply their own menu (item count + dispatch). The base owns:
//   - The capture buffer (15 slots) + waiting/list UI + filter toggle
//   - The Send file browser (folder nav, popup, rename, delete, info)
//   - The jammer loop (timer + chrome + back-handling)
//   - The Signal Info text view + the popup-return path from info → list/browse
//   - All the persistence helpers (timestamp gen, unique path, .sub save)
//
// Derived adds extra states (e.g. SubGHz STATE_SCANNING) by extending _state
// past STATE_USER_BASE and implementing the _on*Extra() hooks.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/TextScrollView.h"
#include "utils/rf/CC1101Util.h"

class RfCaptureScreen : public ListScreen {
public:
  using Signal   = CC1101Util::Signal;
  using RxFilter = CC1101Util::RxFilter;

  // Common state IDs. Derived states start from STATE_USER_BASE.
  static constexpr int STATE_MENU        = 0;
  static constexpr int STATE_RECEIVING   = 1;
  static constexpr int STATE_SEND_BROWSE = 2;
  static constexpr int STATE_JAMMING     = 3;
  static constexpr int STATE_SIGNAL_INFO = 4;
  static constexpr int STATE_USER_BASE   = 100;

  const char* title() override { return _titleBuf; }
  bool inhibitPowerSave() override {
    return _state == STATE_RECEIVING || _inhibitExtra();
  }
  bool inhibitPowerOff() override {
    return _state == STATE_RECEIVING || _state == STATE_JAMMING || _inhibitExtra();
  }

  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

protected:
  enum InfoSource { INFO_FROM_CAPTURE, INFO_FROM_BROWSE };

  // ── Shared state ───────────────────────────────────────────────────────
  int   _state        = STATE_MENU;
  char  _titleBuf[32] = "";
  bool  _chromeDrawn  = false;

  static constexpr uint8_t kMaxCapture = 15;
  Signal   _capturedSignals[kMaxCapture];
  String   _capturedTimes[kMaxCapture];
  bool     _capturedSaved[kMaxCapture]{};
  String   _capturedSubLabels[kMaxCapture];
  ListItem _capturedItems[kMaxCapture]{};
  uint8_t  _capturedCount = 0;
  uint32_t _lastRender    = 0;
  uint32_t _jamStart      = 0;

  TextScrollView _textView;
  InfoSource     _infoSource = INFO_FROM_CAPTURE;
  uint8_t        _infoIdx    = 0;

  static constexpr const char* kRootPath = "/unigeek/rf";
  BrowseFileView _browser;
  String         _browsePath;
  bool           _holdFired = false;

  // ── Radio adapter — derived must implement ─────────────────────────────
  virtual bool        _radioBeginReceive()                = 0;
  virtual void        _radioEndReceive()                  = 0;
  virtual bool        _radioPollReceive(Signal& out)      = 0;
  virtual bool        _radioSendFromBrowse(const Signal& sig) = 0; // begin/send/end
  virtual void        _radioSendCaptured(const Signal& sig)   = 0; // detach RX, send
  virtual bool        _radioStartJam()                    = 0;
  virtual void        _radioStopJam()                     = 0;
  virtual void        _radioJamBurst()                    = 0;
  virtual RxFilter    _radioGetRxFilter()                 = 0;
  virtual void        _radioSetRxFilter(RxFilter f)       = 0;
  virtual void        _radioFreqLabel(char* buf, size_t n) = 0;    // e.g. "433.92 MHz"
  virtual void        _radioShutdown()                    = 0;    // called on leave-from-MENU
  virtual const char* _titlePrefix()                      = 0;    // e.g. "Sub-GHz"
  virtual void        _showMenu()                         = 0;    // derived builds + setItems
  virtual void        _onMenuSelected(uint8_t index)      = 0;

  // ── Optional extension hooks ───────────────────────────────────────────
  // Return true if the hook consumed the event.
  virtual bool _onUpdateExtra()             { return false; }
  virtual bool _onRenderExtra()             { return false; }
  virtual bool _onBackExtra()               { return false; }
  virtual bool _onItemSelectedExtra(uint8_t /*idx*/) { return false; }
  virtual bool _inhibitExtra() const        { return false; }
  // Active jammer mode name, shown under "Jamming..." — nullptr hides the line.
  virtual const char* _radioJamModeName()   { return nullptr; }
  // Called after the jam is stopped by BACK. Return true if the derived screen
  // took over navigation (e.g. reopened its mode menu); false lets the base fall
  // back to the main menu. Radio is already stopped when this runs.
  virtual bool _onJamStopped()              { return false; }

  // ── Shared helpers (called by base + reusable by derived) ──────────────
  void   _enterReceiveMode();
  void   _enterJammingMode();
  void   _enterBrowseMode() { _loadBrowseDir(kRootPath); }

  void   _showReceiveList();
  void   _handleCaptureSelection(uint8_t index);
  void   _rebuildCapturedItems();
  void   _sendCapturedSignal(uint8_t index);
  void   _replayStepKeeloqSignal(uint8_t index);   // counter+1 + re-encrypt
  void   _saveSignal(uint8_t index, const String& name);
  bool   _isDuplicate(const Signal& sig) const;
  String _generateTimestampName();
  String _makeUniquePath(const String& name);
  void   _loadBrowseDir(const String& path);
  void   _sendBrowseFile(uint8_t index);
  void   _showBrowseOptions(uint8_t index);
  void   _showBrowseFileInfo(uint8_t index);
  void   _showSignalInfo(const Signal& sig, InfoSource src, uint8_t idx);
  void   _toggleRxFilter();
};
