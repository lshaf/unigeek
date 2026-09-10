#pragma once
#include "utils/ble/ChameleonClient.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <mbedtls/md.h>

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

inline uint16_t dynamicLockPage(uint16_t type) {
  switch (type) {
    case ChameleonClient::MFU_NTAG212:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_21: return 36;
    case ChameleonClient::MFU_NTAG213: return 40;
    case ChameleonClient::MFU_NTAG215: return 130;
    case ChameleonClient::MFU_NTAG216: return 226;
    default: return 0xFFFF;
  }
}

inline bool supportsPwd(uint16_t type) {
  return config0(type) != 0xFFFF;
}

inline bool passwordFromText(const String& text, uint8_t pwd[4]) {
  if (!text.length()) return false;
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (!md) return false;
  uint8_t digest[16] = {};
  if (mbedtls_md(md, reinterpret_cast<const unsigned char*>(text.c_str()),
                 text.length(), digest) != 0) return false;
  memcpy(pwd, digest, 4);
  return true;
}

inline bool promptPassword(uint8_t pwd[4], const char* title = "Password") {
  String text = InputTextAction::popup(title, "", InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled()) return false;
  if (!passwordFromText(text, pwd)) {
    ShowStatusAction::show("Password required");
    return false;
  }
  return true;
}

inline bool readProtection(ChameleonClient& c, const ChameleonClient::MfuTagInfo& info,
                           uint8_t& auth0, uint8_t& access) {
  const uint16_t cfg = config0(info.type);
  if (cfg == 0xFFFF || cfg + 1 >= info.pages) return false;
  uint8_t c0[4] = {}, c1[4] = {};
  if (!c.mfuReadPage((uint8_t)cfg, c0) || !c.mfuReadPage((uint8_t)(cfg + 1), c1)) return false;
  auth0 = c0[3];
  access = c1[0];
  return true;
}

inline bool pageNeedsAuth(uint8_t auth0, uint8_t access, uint16_t pages,
                          uint16_t page, bool forRead) {
  if (auth0 == 0xFF || auth0 >= pages || page < auth0) return false;
  if (!forRead) return true;
  return (access & 0x80) != 0; // PROT=1 protects reads as well as writes
}

inline bool rangeNeedsAuth(uint8_t auth0, uint8_t access, uint16_t pages,
                           uint16_t firstPage, uint16_t lastPage, bool forRead) {
  if (lastPage < firstPage) return false;
  return pageNeedsAuth(auth0, access, pages, lastPage, forRead);
}

inline bool ensureForRange(ChameleonClient& c, const ChameleonClient::MfuTagInfo& info,
                           uint16_t firstPage, uint16_t lastPage, bool forRead,
                           uint8_t pwd[4], bool& usePassword) {
  usePassword = false;
  if (!supportsPwd(info.type)) return true;

  uint8_t auth0 = 0xFF, access = 0;
  const bool readable = readProtection(c, info, auth0, access);
  bool need = !readable;
  if (readable) need = rangeNeedsAuth(auth0, access, info.pages, firstPage, lastPage, forRead);
  if (!need) return true;

  if (!promptPassword(pwd)) return false;
  if (!c.mfuPwdAuth(pwd, nullptr)) {
    ShowStatusAction::show("Authentication failed");
    return false;
  }
  usePassword = true;
  return true;
}

inline bool prepare(ChameleonClient& c, const ChameleonClient::MfuTagInfo& info,
                    bool forRead, uint8_t pwd[4], bool& usePassword) {
  return ensureForRange(c, info, 0, info.pages ? (uint16_t)(info.pages - 1) : 0,
                        forRead, pwd, usePassword);
}
} // namespace ChameleonMfuAuthUtils
