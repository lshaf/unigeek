#pragma once
#include "utils/ble/ChameleonClient.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

namespace ChameleonMfuAuthUtils {
inline uint16_t config0(uint16_t type) {
  switch (type) {
    case ChameleonClient::MFU_NTAG210:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_11: return 16;
    case ChameleonClient::MFU_NTAG212:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_21: return 37;
    case ChameleonClient::MFU_NTAG213: return 41;
    case ChameleonClient::MFU_NTAG215: return 131;
    case ChameleonClient::MFU_NTAG216: return 227;
    default: return 0xFFFF;
  }
}

inline bool promptPassword(uint8_t pwd[4]) {
  String hex = InputTextAction::popup("Password (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) return false;
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8) { ShowStatusAction::show("Need 8 hex chars"); return false; }
  for (uint8_t i = 0; i < 4; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* e = nullptr; unsigned long v = strtoul(b, &e, 16);
    if (!e || *e) { ShowStatusAction::show("Bad password"); return false; }
    pwd[i] = (uint8_t)v;
  }
  return true;
}

inline bool prepare(ChameleonClient& c, const ChameleonClient::MfuTagInfo& info,
                    bool forRead, uint8_t pwd[4], bool& usePassword) {
  usePassword = false;
  const uint16_t cfg = config0(info.type);
  if (cfg == 0xFFFF || cfg + 1 >= info.pages) return true;
  uint8_t c0[4] = {}, c1[4] = {};
  const bool readable = c.mfuReadPage((uint8_t)cfg, c0) &&
                        c.mfuReadPage((uint8_t)(cfg + 1), c1);
  bool need = !readable;
  if (readable) {
    const uint8_t auth0 = c0[3];
    if (auth0 != 0xFF && auth0 < info.pages)
      need = !forRead || ((c1[0] & 0x80) != 0);
  }
  if (!need) return true;
  if (!promptPassword(pwd)) return false;
  if (!c.mfuPwdAuth(pwd, nullptr)) {
    ShowStatusAction::show("Authentication failed");
    return false;
  }
  usePassword = true;
  return true;
}
} // namespace ChameleonMfuAuthUtils
