#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/LogView.h"
#include "utils/network/ResponderUtil.h"

// Responder screen — LLMNR/NBT-NS poisoning + SMB NTLMv2 hash capture.
// Lives under WiFi → Network (requires an active STA connection). Hashes are
// saved to /unigeek/wifi/responder/ntlm_hashes.txt (hashcat -m 5600).
class ResponderScreen : public BaseScreen {
public:
  const char* title()    override { return "Responder"; }
  bool inhibitPowerOff() override { return true; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  ResponderUtil _resp;
  LogView       _log;
  bool          _running  = false;
  unsigned long _lastDraw = 0;

  static void _logCb(void* ctx, const char* msg, uint16_t color);
  static void _hitCb(void* ctx);
  static void _statusCb(Sprite& sp, int barY, int width, void* userData);
};
