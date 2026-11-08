#include <new>
#include "ui/components/TagPrompt.h"
#include "PN532I2cScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/views/ProgressView.h"
#include "ui/views/DictionaryKeyProgressView.h"
#include "ui/views/DictionaryDiscoverySummary.h"
#include "ui/views/LogView.h"
#include "screens/utility/NfcMemoryViewScreen.h"
#include "../../utils/nfc/NdefBuilder.h"
#include "../../utils/nfc/NdefParser.h"
#include "../../utils/nfc/HfDumpBuilder.h"
#include "../../utils/nfc/HfDumpParser.h"

#include "utils/nfc/MfcKeyStore.h"
#include "utils/nfc/EmvReader.h"
#include "utils/IdentityFile.h"
// ── raw I2C helpers for Gen1a / Gen3 ──────────────────────────────────────
// Adafruit_PN532 exposes sendCommandCheckAck() publicly but readdata() is
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <esp_random.h>

// private. These helpers build commands in pn532_packetbuffer (the library's
// global) and read the response directly over Wire after the ACK is received.

extern byte pn532_packetbuffer[];

static bool _parseHexKeyI2c(const String& line, uint8_t out[6]);

static bool _parseHexBytesI2c(const String& text, uint8_t* out, size_t outCap, size_t& outLen) {
  outLen = 0;
  int hi = -1;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else if (c == ' ' || c == ':' || c == '-' || c == '\t' || c == '\r' || c == '\n') continue;
    else return false;
    if (hi < 0) hi = v;
    else {
      if (outLen >= outCap) return false;
      out[outLen++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  return hi < 0 && outLen > 0;
}

enum class _Pn532CmdFailure : uint8_t { None, InvalidArgs, AckFailed, ReadyTimeout, ShortRead, InvalidPreamble, InvalidLength, UnexpectedResponse, InvalidPayloadLength, InvalidChecksum };
static _Pn532CmdFailure _emvCmdFailure = _Pn532CmdFailure::None;
static int _emvI2cReadCount = -1;
static uint32_t _emvAckElapsedMs = 0;
static uint16_t _emvAckTimeoutMs = 0;
static bool _emvProbeDiscovery = false;
static bool _emvProbeAckOk = false;
static bool _emvFiniteRetriesTest = false;
static bool _emvRetryConfigOk = false;
static bool _emvRetryRestoreOk = false;
static bool _nfcSendCmdReadAckProbe(TwoWire*, const uint8_t*, uint8_t, uint32_t);
static const char* _emvApduTransportStage = "Not attempted";
static _Pn532CmdFailure _emvApduCommandFailure = _Pn532CmdFailure::None;
static int _emvApduI2cBytes = -1;
static uint32_t _emvApduCommandElapsedMs = 0;
static int _emvApduPn532Status = -1;
static size_t _emvApduPayloadBytes = 0;
static size_t _emvApduResponseCap = 0;

static bool _nfcCommandResponse(Adafruit_PN532* nfc, TwoWire* wire,
                                const uint8_t* cmd, uint8_t cmdLen,
                                uint8_t expectedResponse,
                                uint8_t* out, size_t outCap, size_t& outLen,
                                uint16_t timeoutMs = 800, bool largeEmvFrame = false) {
  _emvCmdFailure = _Pn532CmdFailure::None;
  _emvI2cReadCount = -1;
  _emvAckElapsedMs = 0;
  _emvAckTimeoutMs = timeoutMs;
  outLen = 0;
  if (!nfc || !wire || !cmd || !cmdLen) { _emvCmdFailure = _Pn532CmdFailure::InvalidArgs; return false; }
  for (uint8_t i = 0; i < cmdLen; ++i) pn532_packetbuffer[i] = cmd[i];
  const uint32_t ackStart = millis();
  const bool ackOk = _emvProbeDiscovery
      ? _nfcSendCmdReadAckProbe(wire, cmd, cmdLen, timeoutMs)
      : nfc->sendCommandCheckAck(pn532_packetbuffer, cmdLen, timeoutMs);
  _emvAckElapsedMs = millis() - ackStart;
  _emvProbeAckOk = ackOk;
  if (!ackOk) { _emvCmdFailure = _Pn532CmdFailure::AckFailed; return false; }

  constexpr size_t kLegacyFrameBytes = 80;
  constexpr size_t kEmvFrameBytes = 262;
  const size_t frameBytes = largeEmvFrame ? kEmvFrameBytes : kLegacyFrameBytes;

  uint32_t readyStart = millis();
  bool ready = false;
  while (millis() - readyStart < timeoutMs) {
    if (wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1) == 1) {
      ready = (wire->read() & 0x01U) != 0;
      if (ready) break;
    }
    delay(2);
  }
  if (!ready) { _emvCmdFailure = _Pn532CmdFailure::ReadyTimeout; return false; }

  uint8_t frame[kEmvFrameBytes] = {};
  const size_t want = frameBytes;
  const size_t got = wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, want + 1, true);
  _emvI2cReadCount = (int)got;
  if (got != want + 1) { _emvCmdFailure = _Pn532CmdFailure::ShortRead; return false; }
  wire->read();
  for (size_t i = 0; i < want; ++i) frame[i] = wire->available() ? wire->read() : 0;

  if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF) { _emvCmdFailure = _Pn532CmdFailure::InvalidPreamble; return false; }
  const uint8_t len = frame[3];
  if ((uint8_t)(len + frame[4]) != 0x00 || len < 2) { _emvCmdFailure = _Pn532CmdFailure::InvalidLength; return false; }
  if (frame[5] != PN532_PN532TOHOST || frame[6] != expectedResponse) { _emvCmdFailure = _Pn532CmdFailure::UnexpectedResponse; return false; }

  const size_t payloadLen = len - 2;
  const size_t dcsIndex = 5u + len;
  const size_t postambleIndex = dcsIndex + 1u;
  if (payloadLen > outCap || 7u + payloadLen > want || postambleIndex >= want) { _emvCmdFailure = _Pn532CmdFailure::InvalidPayloadLength; return false; }

  uint8_t sum = 0;
  for (size_t i = 5; i < dcsIndex; ++i) sum = (uint8_t)(sum + frame[i]);
  if ((uint8_t)(sum + frame[dcsIndex]) != 0x00 || frame[postambleIndex] != 0x00) { _emvCmdFailure = _Pn532CmdFailure::InvalidChecksum; return false; }

  if (payloadLen && out) memcpy(out, frame + 7, payloadLen);
  outLen = payloadLen;
  return true;
}

static bool _nfcSendCmdReadAckProbe(TwoWire* wire, const uint8_t* cmd, uint8_t cmdlen, uint32_t timeoutMs) {
  if (!wire || !cmd || !cmdlen) return false;
  const uint8_t len = cmdlen + 1;
  uint8_t packet[48] = {};
  if ((size_t)(8 + cmdlen) > sizeof(packet)) return false;
  packet[0]=0x00; packet[1]=0x00; packet[2]=0xFF; packet[3]=len; packet[4]=(uint8_t)(~len+1); packet[5]=0xD4;
  uint8_t sum=0xD4;
  for (uint8_t i=0;i<cmdlen;i++) { packet[6+i]=cmd[i]; sum=(uint8_t)(sum+cmd[i]); }
  packet[6+cmdlen]=(uint8_t)(~sum+1); packet[7+cmdlen]=0x00;
  wire->beginTransmission(PN532_I2C_ADDRESS); wire->write(packet,8+cmdlen);
  if (wire->endTransmission()!=0) return false;
  delay(1);
  uint32_t start=millis();
  while (millis()-start<timeoutMs) {
    if (wire->requestFrom((uint8_t)PN532_I2C_ADDRESS,(uint8_t)1)==1 && wire->available() && (wire->read()&1)) break;
    delay(5);
  }
  if (millis()-start>=timeoutMs) return false;
  uint8_t ack[7]={};
  if (wire->requestFrom((uint8_t)PN532_I2C_ADDRESS,(uint8_t)7)!=7) return false;
  wire->read();
  for (uint8_t i=0;i<6;i++) ack[i]=wire->available()?wire->read():0;
  return ack[0]==0x00 && ack[1]==0x00 && ack[2]==0xFF && ack[3]==0x00 && ack[4]==0xFF && ack[5]==0x00;
}

static void _nfcReadI2C(TwoWire* wire, uint8_t* buf, uint8_t n) {
  uint8_t total = n + 1;
  if (wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, total) != total) return;
  wire->read(); // status byte
  for (uint8_t i = 0; i < n; i++)
    buf[i] = wire->available() ? wire->read() : 0;
}

static bool _nfcWriteReg(Adafruit_PN532* nfc, TwoWire* wire, uint16_t reg, uint8_t val) {
  pn532_packetbuffer[0] = PN532_COMMAND_WRITEREGISTER;
  pn532_packetbuffer[1] = (reg >> 8) & 0xFF;
  pn532_packetbuffer[2] = reg & 0xFF;
  pn532_packetbuffer[3] = val;
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 4, 200)) return false;
  uint8_t resp[10];
  _nfcReadI2C(wire, resp, 10);
  return resp[5] == PN532_PN532TOHOST && resp[6] == (PN532_COMMAND_WRITEREGISTER + 1);
}

static bool _nfcCommThru(Adafruit_PN532* nfc, TwoWire* wire,
                          const uint8_t* data, uint8_t dlen,
                          uint8_t* resp, uint8_t& rlen,
                          uint16_t timeoutMs = 300) {
  pn532_packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
  for (uint8_t i = 0; i < dlen; i++) pn532_packetbuffer[1 + i] = data[i];
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 1 + dlen, timeoutMs)) return false;
  uint8_t buf[20];
  _nfcReadI2C(wire, buf, 20);
  if (buf[5] != PN532_PN532TOHOST || buf[6] != (PN532_COMMAND_INCOMMUNICATETHRU + 1)) return false;
  if (buf[7] & 0x3F) return false;
  uint8_t payLen = buf[3] > 3 ? buf[3] - 3 : 0;
  rlen = payLen < rlen ? payLen : rlen;
  if (rlen > 0) memcpy(resp, buf + 8, rlen);
  return true;
}

static bool _nfcDataExch(Adafruit_PN532* nfc, TwoWire* wire,
                          const uint8_t* data, uint8_t dlen,
                          uint8_t* resp, uint8_t& rlen,
                          uint16_t timeoutMs = 1000) {
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1; // Tg
  for (uint8_t i = 0; i < dlen; i++) pn532_packetbuffer[2 + i] = data[i];
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 2 + dlen, timeoutMs)) return false;
  uint8_t buf[24];
  _nfcReadI2C(wire, buf, 24);
  if (buf[5] != PN532_PN532TOHOST || buf[6] != PN532_RESPONSE_INDATAEXCHANGE) return false;
  if (buf[7] & 0x3F) return false;
  uint8_t payLen = buf[3] > 3 ? buf[3] - 3 : 0;
  rlen = payLen < rlen ? payLen : rlen;
  if (rlen > 0) memcpy(resp, buf + 8, rlen);
  return true;
}

static bool _pn532Type2ReadPage(Adafruit_PN532* nfc, TwoWire* wire,
                                   uint8_t page, uint8_t out[4]) {
  const uint8_t cmd[2] = {0x30, page};
  uint8_t rsp[18] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  if (len < 16) return false;
  memcpy(out, rsp, 4);
  return true;
}

static bool _pn532Type2ReadPageTailSafe(Adafruit_PN532* nfc, TwoWire* wire,
                                           uint16_t page, uint16_t totalPages,
                                           uint8_t out[4]) {
  if (!totalPages || page >= totalPages || totalPages > 256) return false;
  uint16_t start = page;
  uint8_t skip = 0;
  if (page + 3 >= totalPages) {
    start = totalPages >= 4 ? totalPages - 4 : 0;
    skip = (uint8_t)(page - start);
  }
  const uint8_t cmd[2] = {0x30, (uint8_t)start};
  uint8_t rsp[18] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  if (len < 16) return false;
  memcpy(out, rsp + skip * 4u, 4);
  return true;
}

static bool _pn532Type2WritePage(Adafruit_PN532* nfc, TwoWire* wire,
                                    uint8_t page, const uint8_t data[4]) {
  uint8_t cmd[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
  uint8_t rsp[4] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  return len == 0 || (len >= 1 && (rsp[0] & 0x0F) == 0x0A);
}

static bool _type2DefaultCc(const char* typeName, uint8_t cc[4]) {
  if (!typeName || !cc) return false;
  uint8_t size = 0;
  if (strcmp(typeName, "NTAG210") == 0 ||
      strcmp(typeName, "Ultralight EV1 11") == 0 ||
      strcmp(typeName, "Ultralight") == 0) size = 0x06;
  else if (strcmp(typeName, "NTAG212") == 0 ||
           strcmp(typeName, "Ultralight EV1 21") == 0) size = 0x10;
  else if (strcmp(typeName, "NTAG213") == 0 ||
           strcmp(typeName, "Ultralight C") == 0) size = 0x12;
  else if (strcmp(typeName, "NTAG215") == 0) size = 0x3F;
  else if (strcmp(typeName, "NTAG216") == 0) size = 0x6F;
  else return false;
  cc[0] = 0xE1; cc[1] = 0x10; cc[2] = size; cc[3] = 0x00;
  return true;
}

static bool _type2CcIsValid(const uint8_t cc[4]) {
  return cc && cc[0] == 0xE1 && (cc[1] & 0xF0) == 0x10 && cc[2] != 0;
}

static bool _type2CcCanProgramSafely(const uint8_t current[4], const uint8_t desired[4]) {
  for (uint8_t i = 0; i < 4; ++i)
    if ((current[i] & (uint8_t)~desired[i]) != 0) return false;
  return true;
}
static const char* _ultralightSensitivePageLabel(const char* typeName, uint16_t page) {
  if (!typeName) return nullptr;

  uint16_t dynamicLock = 0xFFFF;
  uint16_t config0 = 0xFFFF;

  if (strcmp(typeName, "NTAG210") == 0 ||
      strcmp(typeName, "Ultralight EV1 11") == 0) {
    config0 = 16;
  } else if (strcmp(typeName, "NTAG212") == 0 ||
             strcmp(typeName, "Ultralight EV1 21") == 0) {
    dynamicLock = 36; config0 = 37;
  } else if (strcmp(typeName, "NTAG213") == 0) {
    dynamicLock = 40; config0 = 41;
  } else if (strcmp(typeName, "NTAG215") == 0) {
    dynamicLock = 130; config0 = 131;
  } else if (strcmp(typeName, "NTAG216") == 0) {
    dynamicLock = 226; config0 = 227;
  } else if (strcmp(typeName, "Ultralight C") == 0) {
    if (page >= 40 && page <= 43) return "Security/config page";
    if (page >= 44 && page <= 47) return "3DES key page";
    return nullptr;
  } else {
    return nullptr;
  }

  if (page == dynamicLock) return "Dynamic lock page";
  if (page == config0 || page == config0 + 1) return "Configuration page";
  if (page == config0 + 2) return "Password page";
  if (page == config0 + 3) return "PACK page";
  return nullptr;
}

static bool _confirmUltralightSensitiveWrite(const char* typeName, uint16_t page) {
  const char* label = _ultralightSensitivePageLabel(typeName, page);
  if (!label) return true;

  static const InputSelectAction::Option opts[] = {
    {"Write anyway", "write"},
  };
  String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}

static uint16_t _ultralightConfig0(const char* typeName) {
  if (!typeName) return 0xFFFF;
  if (strcmp(typeName, "NTAG210") == 0 || strcmp(typeName, "Ultralight EV1 11") == 0) return 16;
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) return 37;
  if (strcmp(typeName, "NTAG213") == 0) return 41;
  if (strcmp(typeName, "NTAG215") == 0) return 131;
  if (strcmp(typeName, "NTAG216") == 0) return 227;
  return 0xFFFF;
}

static int _pn532UltralightNeedsAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                     const char* typeName, uint16_t pages,
                                     bool forRead) {
  const uint16_t cfg = _ultralightConfig0(typeName);
  if (cfg == 0xFFFF || cfg + 1 >= pages) return 0;
  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(nfc, wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(nfc, wire, cfg + 1, pages, c1)) return -1;
  const uint8_t auth0 = c0[3];
  if (auth0 == 0xFF || auth0 >= pages) return 0;
  if (!forRead) return 1;
  return (c1[0] & 0x80) ? 1 : 0;
}

static bool _ultralightPasswordFromText(const String& text, uint8_t pwd[4]) {
  if (!text.length()) return false;
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (!md) return false;
  uint8_t digest[16] = {};
  if (mbedtls_md(md, reinterpret_cast<const unsigned char*>(text.c_str()),
                 text.length(), digest) != 0) return false;
  memcpy(pwd, digest, 4);
  return true;
}

static bool _promptUltralightPassword(uint8_t pwd[4], const char* title = "Password") {
  String text = InputTextAction::popup(title, "", InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled()) return false;
  if (!_ultralightPasswordFromText(text, pwd)) {
    ShowStatusAction::show("Password required");
    return false;
  }
  return true;
}

static uint16_t _ultralightDynamicLockPage(const char* typeName) {
  if (!typeName) return 0xFFFF;
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) return 36;
  if (strcmp(typeName, "NTAG213") == 0) return 40;
  if (strcmp(typeName, "NTAG215") == 0) return 130;
  if (strcmp(typeName, "NTAG216") == 0) return 226;
  return 0xFFFF;
}

static bool _pn532UltralightPwdAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                     const uint8_t pwd[4]) {
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  if (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500))
    return false;

  const uint8_t cmd[5] = {0x1B, pwd[0], pwd[1], pwd[2], pwd[3]};
  uint8_t rsp[8] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  return len >= 2;
}

static bool _pn532EnsureUltralightAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                       const char* typeName, uint16_t pages,
                                       bool forRead) {
  const int need = _pn532UltralightNeedsAuth(nfc, wire, typeName, pages, forRead);
  if (need == 0) return true;
  if (typeName && strcmp(typeName, "Ultralight C") == 0) return false;
  uint8_t pwd[4] = {};
  if (!_promptUltralightPassword(pwd)) return false;
  if (!_pn532UltralightPwdAuth(nfc, wire, pwd)) {
    ShowStatusAction::show("Authentication failed", 1600);
    return false;
  }
  return true;
}

static bool _pn532Type2ReadPageSession(Adafruit_PN532* nfc, TwoWire* wire,
                                       uint8_t page, uint8_t out[4]) {
  const uint8_t cmd[2] = {0x30, page};
  uint8_t rsp[24] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  if (len < 4) return false;
  memcpy(out, rsp, 4);
  return true;
}

static bool _pn532ReadUltralightProtection(Adafruit_PN532* nfc, TwoWire* wire,
                                            const char* typeName, uint16_t pages,
                                            uint8_t& auth0, uint8_t& access) {
  const uint16_t cfg = _ultralightConfig0(typeName);
  if (cfg == 0xFFFF || cfg + 1 >= pages) return false;
  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(nfc, wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(nfc, wire, cfg + 1, pages, c1)) return false;
  auth0 = c0[3]; access = c1[0]; return true;
}

static bool _pn532EnsureUltralightAuthForRange(Adafruit_PN532* nfc, TwoWire* wire,
                                                const char* typeName, uint16_t pages,
                                                uint16_t firstPage, uint16_t lastPage,
                                                bool forRead,
                                                bool* authenticatedOut = nullptr) {
  if (authenticatedOut) *authenticatedOut = false;
  if (_ultralightConfig0(typeName) == 0xFFFF) return true;
  uint8_t auth0 = 0xFF, access = 0;
  const bool readable = _pn532ReadUltralightProtection(nfc, wire, typeName, pages, auth0, access);
  bool need = !readable;
  if (readable) {
    need = auth0 != 0xFF && auth0 < pages && lastPage >= auth0 &&
           (!forRead || (access & 0x80));
  }
  if (!need) return true;
  uint8_t pwd[4] = {};
  if (!_promptUltralightPassword(pwd)) return false;
  if (!_pn532UltralightPwdAuth(nfc, wire, pwd)) {
    ShowStatusAction::show("Authentication failed", 1600);
    return false;
  }
  if (authenticatedOut) *authenticatedOut = true;
  return true;
}

static bool _pn532BuildUltralightLockMasks(const char* typeName, uint16_t first, uint16_t last,
                                            uint8_t staticMask[2], uint8_t dynamicMask[3]) {
  staticMask[0] = staticMask[1] = 0;
  dynamicMask[0] = dynamicMask[1] = dynamicMask[2] = 0;
  if (!typeName || first > last || first < 4) return false;
  for (uint16_t p = first; p <= last && p <= 15; ++p) {
    if (p <= 7) staticMask[0] |= (uint8_t)(1u << p);
    else staticMask[1] |= (uint8_t)(1u << (p - 8));
  }
  if (last <= 15) return true;
  auto setGroup = [&](uint16_t start, uint16_t end, uint8_t byte, uint8_t bit) {
    if (last >= start && first <= end) dynamicMask[byte] |= (uint8_t)(1u << bit);
  };
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) {
    for (uint8_t i = 0; i < 10; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (strcmp(typeName, "NTAG213") == 0) {
    for (uint8_t i = 0; i < 12; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (strcmp(typeName, "NTAG215") == 0) {
    for (uint8_t i = 0; i < 7; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    setGroup(128, 129, 0, 7); return true;
  }
  if (strcmp(typeName, "NTAG216") == 0) {
    for (uint8_t i = 0; i < 8; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    for (uint8_t i = 0; i < 5; ++i) setGroup(144 + i * 16, 159 + i * 16, 1, i);
    setGroup(224, 225, 1, 5); return true;
  }
  if (strcmp(typeName, "NTAG210") == 0 || strcmp(typeName, "Ultralight EV1 11") == 0 ||
      strcmp(typeName, "Ultralight") == 0) return last <= 15;
  return false;
}

// ── title ──────────────────────────────────────────────────────────────────

static void renderTagPrompt(const char* message, int bx, int by, int bw, int bh) {
  TagPrompt::show(message, bx, by, bw, bh);
}

static void renderOperationTitle(const char* title) {
  Header header;
  header.render(title);
  StatusBar::refresh();
}

const char* PN532I2cScreen::title() {
  switch (_state) {
    case STATE_MAIN_MENU:       return "PN532 I2C";
    case STATE_FAMILIES_MENU:   return "Families & Protocols";
    case STATE_DEVICE_INFO:     return "Device Info";
    case STATE_SCAN_RESULT:     return "Tag Details";
    case STATE_SCAN_14A:        return "Scan Tag";
    case STATE_MIFARE_MENU:     return "MIFARE Classic";
    case STATE_MIFARE_TAG_MENU: return "Tag Operations";
    case STATE_MIFARE_ADVANCED_MENU:return "Advanced";
    case STATE_MIFARE_NDEF_MENU:return "NDEF Operations";
    case STATE_MIFARE_ATTACKS_MENU:return "Attacks";
    case STATE_MIFARE_KEYS_MENU:return "Keys";
    case STATE_MIFARE_KEY_DB_SELECT:return "Dictionaries";
    case STATE_MIFARE_KEY_DB_VIEW:return _keyDbViewTitle.length() ? _keyDbViewTitle.c_str() : "Dictionary";
    case STATE_MIFARE_DUMP:     return "Tag Details";
    case STATE_MIFARE_KEYS:     return "Check Known Keys";
    case STATE_MIFARE_DUMP_SELECT:return "Dump Files";
    case STATE_MIFARE_UID_SOURCE_FORM:return "Write UID to Tag";
    case STATE_MIFARE_UID_SELECT:return "Saved UIDs";
    case STATE_MIFARE_UID_DUMP_SELECT:return "Saved Dumps";
    case STATE_MIFARE_WRITE_PREVIEW:return "Write Dump to Tag";
    case STATE_MIFARE_UID_WRITE_PREVIEW:return "Write UID to Tag";
    case STATE_DICT_SELECT:     return "Dictionary Attack";
    case STATE_ULTRALIGHT_MENU: return "Ultralight / NTAG";
    case STATE_ULTRALIGHT_TAG_MENU:return "Tag Operations";
    case STATE_ULTRALIGHT_ADVANCED_MENU:return "Advanced";
    case STATE_ULTRALIGHT_NDEF_MENU:return "NDEF Operations";
    case STATE_TYPEB_MENU:       return "Type 4B (experimental)";
    case STATE_TYPEB_TAG_MENU:   return "Tag Operations";
    case STATE_TYPEB_ADVANCED_MENU:return "Advanced";
    case STATE_TYPEB_NDEF_MENU:  return "NDEF Operations";
    case STATE_TYPEB_RESULT:     return "Tag Details";
    case STATE_TYPE4A_MENU:       return "Type 4A (experimental)";
    case STATE_TYPE4A_TAG_MENU:   return "Tag Operations";
    case STATE_TYPE4A_ADVANCED_MENU:return "Advanced";
    case STATE_TYPE4A_NDEF_MENU:  return "NDEF Operations";
    case STATE_TYPE4A_RESULT:     return _emvResultActive ? (_emvDetailsVisible ? "Details" : "Read EMV (experimental)") : "Tag Details";
    case STATE_DESFIRE_MENU:       return "DESFire (experimental)";
    case STATE_DESFIRE_APPS_MENU:  return "Applications";
    case STATE_DESFIRE_FILES_MENU: return "Files";
    case STATE_DESFIRE_ADVANCED_MENU:return "Advanced";
    case STATE_DESFIRE_RESULT:     return _operationResultTitle.length() ? _operationResultTitle.c_str() : "DESFire";
    case STATE_FELICA_MENU:         return "FeliCa (experimental)";
    case STATE_FELICA_TAG_MENU:     return "Tag Operations";
    case STATE_FELICA_SYSTEMS_MENU: return "Systems";
    case STATE_FELICA_SERVICES_MENU:return "Services";
    case STATE_FELICA_ADVANCED_MENU:return "Advanced";
    case STATE_FELICA_NDEF_MENU:    return "NDEF Operations";
    case STATE_FELICA_RESULT:       return _operationResultTitle.length() ? _operationResultTitle.c_str() : "FeliCa";
    case STATE_TYPE1_MENU:           return "Type 1 / Jewel (experimental)";
    case STATE_TYPE1_TAG_MENU:       return "Tag Operations";
    case STATE_TYPE1_NDEF_MENU:      return "NDEF Operations";
    case STATE_TYPE1_RESULT:         return "Type 1 / Jewel";
    case STATE_MAGIC_DETECT:    return "Detect Magic";
    case STATE_RAW_RESULT:
      if (_rawResultTypeB) return _rawResultTypeBRaw ? "Raw Response" : "APDU Response";
      if (_rawResultType4A) return "APDU Response";
      return "Read Memory";
    case STATE_ULTRALIGHT_DUMP: return "Tag Details";
    case STATE_NDEF_WRITE_MENU: return "Write NDEF";
    case STATE_NDEF_RESULT:     return "NDEF Details";
    case STATE_NDEF_FILE_SELECT:return "NDEF Files";
  }
  return "PN532 I2C";
}

// ── lifecycle ──────────────────────────────────────────────────────────────

void PN532I2cScreen::onInit() {
  if (!_initModule()) return;
  _goMain();
}

void PN532I2cScreen::onUpdate() {
  if (_pendingRecoveryStep) {
    _pendingRecoveryStep = false;
    static const char* const kRecoveryDicts[] = {
      MfcKeyStore::kBuiltinDefaultId, MfcKeyStore::kDiscoveredDictionary, MfcKeyStore::kBuiltinExtendedId,
    };
    if (_recoverChainIndex < 3) {
      _doDictionaryAttackWithPath(kRecoveryDicts[_recoverChainIndex]);
    }
    return;
  }
  if (_readAfterRecovery) {
    _readAfterRecovery = false;
    _doDumpMemory();
    return;
  }
  if (_state == STATE_SCAN_14A) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) _goMain();
      else if (dir == INavigation::DIR_PRESS) _doScan14A();
    }
    return;
  }
  if (Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 700) {
    if (_state == STATE_SCAN_RESULT && _scanFamily >= 0) {
      Uni.Nav->suppressCurrentPress();
      _familyFromScan = true;
      _openFamily((uint8_t)_scanFamily);
      return;
    }
    if (_state == STATE_TYPEB_RESULT && _typeBFromMainScan && _typeBAttribLen) {
      Uni.Nav->suppressCurrentPress();
      _familyFromScan = true;
      _openFamily(3);
      return;
    }
  }
  if (_state == STATE_SCAN_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goMain();
      } else if (dir == INavigation::DIR_PRESS) {
        if (Uni.Nav->pressDuration() < 700) _doScan14A();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_TYPE4A_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_emvResultActive && _emvDetailsVisible) {
          _emvDetailsVisible = false;
          _resetRows();
          if (_emvSummaryBrand.length()) _pushWrappedRow("Brand", _emvSummaryBrand);
          if (_emvSummaryApplication.length()) _pushWrappedRow("Application", _emvSummaryApplication);
          if (_emvSummaryPan.length()) _pushWrappedRow("PAN", _emvSummaryPan);
          if (_emvSummaryExpiry.length()) _pushRow("Expires", _emvSummaryExpiry);
          if (_emvDetailsAvailable) _pushRow("[Press]", "Details");
          _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount); render();
        } else {
          if (_emvResultActive && _emvResultFromMain) _goMain();
          else _goType4ATag();
          _emvResultActive = false;
        }
      } else if (dir == INavigation::DIR_PRESS && _emvResultActive && !_emvDetailsVisible && _emvDetailsAvailable) {
        _emvDetailsVisible = true;
        _resetRows();
        _pushWrappedRow("AID", _emvDetailsAid);
        _pushWrappedRow("AIP", _emvDetailsAip);
        _pushWrappedRow("AFL", _emvDetailsAfl);
        if (_emvDetailsPdol.length()) _pushWrappedRow("PDOL", _emvDetailsPdol);
        _pushRow("Applications", _emvDetailsApplications);
        _pushRow("Records read", _emvDetailsRecords);
        _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount); render();
      } else if (dir == INavigation::DIR_PRESS && !_genericReadActive && !_emvResultActive) _doType4AReadTag();
      else _scrollView.onNav(dir);
    }
    return;
  }
  if (_state == STATE_TYPEB_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_typeBFromMainScan) _goMain(); else _goTypeBTag();
      } else if (dir == INavigation::DIR_PRESS) {
        if (_typeBFromMainScan) {
          if (Uni.Nav->pressDuration() < 700) _doScan14A();
        } else if (!_genericReadActive) _doTypeBReadTag();
      } else _scrollView.onNav(dir);
    }
    return;
  }
  if (_state == STATE_MAGIC_DETECT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK ||
          (dir == INavigation::DIR_PRESS && _magicUiPhase == MAGIC_RESULT)) {
        _goMifareTag();
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_DUMP) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _hasDump = false;
        _dumpComplete = false;
        _goMifareTag();
      } else if (dir == INavigation::DIR_PRESS && _state == STATE_MIFARE_DUMP && _hasDump) {
        _showDumpActions();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_WRITE_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_writePreviewFromFile) _doWriteDumpFromFilePicker();
        else _showTagDetails();
      } else if (dir == INavigation::DIR_PRESS) {
        if (_writePreviewSourceUidKnown) {
          static constexpr InputSelectAction::Option uidOpts[] = {
            {"Replace UID", "replace"},
            {"Preserve UID", "preserve"},
          };
          const char* choice = InputSelectAction::popup(
              "UID", uidOpts, 2, _writePreviewReplaceUid ? "replace" : "preserve");
          if (!choice) { render(); return; }
          _writePreviewReplaceUid = strcmp(choice, "replace") == 0;
          for (uint8_t i = 0; i < _rowCount; ++i) {
            if (_rowLabels[i] == "UID Action") {
              _rowValues[i] = _writePreviewReplaceUid ? "Replace" : "Preserve";
              _rows[i] = { _rowLabels[i].c_str(), _rowValues[i] };
              break;
            }
          }
          render();
        }
        uint8_t sourceContextUid[7] = {};
        const uint8_t sourceContextUidLen = _uidLen;
        const uint8_t sourceContextSak = _sak;
        const uint16_t sourceContextAtqa = _atqa;
        const auto sourceContextKeys = _mfKeys;
        if (sourceContextUidLen) memcpy(sourceContextUid, _uid, sourceContextUidLen);

        const bool ok = _doWriteDumpToTag(
            _dumpImg, _dumpLen,
            _writePreviewSourceUidKnown ? _writePreviewSourceUid : nullptr,
            _writePreviewSourceUidKnown ? _writePreviewSourceUidLen : 0);
        if (!_writePreviewFromFile) {
          memcpy(_uid, sourceContextUid, sourceContextUidLen);
          _uidLen = sourceContextUidLen;
          _sak = sourceContextSak;
          _atqa = sourceContextAtqa;
          _mfKeys = sourceContextKeys;
        }

        if (ok) {
          if (_writePreviewFromFile) _goMifareTag();
          else _showTagDetails();
        } else {
          render();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_UID_WRITE_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) _goMifareTag();
      else if (dir == INavigation::DIR_PRESS) { if (_doWriteUidToTag()) _goMifareTag(); else render(); }
      else _scrollView.onNav(dir);
    }
    return;
  }
  if (_state == STATE_NDEF_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_ndefWritePreview) {
          bool fromFile = _ndefWritePreviewFromFile;
          _ndefWritePreview = false;
          _ndefWritePreviewFromFile = false;
          if (fromFile) _doWriteNdefFromFile();
          else _goNdefWrite();
        } else {
          _goNdefParent();
        }
      } else if (dir == INavigation::DIR_PRESS && _hasNdef) {
        if (_ndefWritePreview) {
          bool ok = _writeNdefRecord(_ndefBuf, _ndefLen);
          if (ok) {
            _ndefWritePreview = false;
            _ndefWritePreviewFromFile = false;
            _ndefPickDir = "";
            _goNdefParent();
          } else {
            render();
          }
        } else {
          _showNdefActions();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  if (_state == STATE_ULTRALIGHT_DUMP) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goUltralightTag();
      } else if (dir == INavigation::DIR_PRESS && _state == STATE_ULTRALIGHT_DUMP) {
        _showUltralightDumpActions();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  if (_state == STATE_DEVICE_INFO || _state == STATE_MIFARE_KEYS ||
      _state == STATE_MIFARE_KEY_DB_VIEW || _state == STATE_RAW_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_state == STATE_MIFARE_KEYS) _goMifareKeys();
        else if (_state == STATE_MIFARE_KEY_DB_VIEW) _openKeyDatabases();
        else if (_state == STATE_RAW_RESULT) { if (_rawResultTypeB) _goTypeBAdvanced(); else if (_rawResultType4A) _goType4AAdvanced(); else if (_rawResultMifare) _goMifareAdvanced(); else _goUltralightAdvanced(); }
        else _goMain();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void PN532I2cScreen::onRender() {
  if (_state == STATE_MAGIC_DETECT) {
    if (_magicUiPhase == MAGIC_WAITING) {
      renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
    } else if (_magicUiPhase == MAGIC_RESULT) {
      auto& lcd = Uni.Lcd;
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextDatum(MC_DATUM);
      lcd.setTextSize(1);
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.drawString(_magicResult, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
    }
    return;
  }
  if (_state == STATE_DEVICE_INFO || _state == STATE_SCAN_RESULT ||
      _state == STATE_MIFARE_DUMP ||
      _state == STATE_MIFARE_WRITE_PREVIEW || _state == STATE_MIFARE_UID_WRITE_PREVIEW ||
      _state == STATE_MIFARE_KEYS || _state == STATE_MIFARE_KEY_DB_VIEW ||
      _state == STATE_RAW_RESULT || _state == STATE_ULTRALIGHT_DUMP || _state == STATE_NDEF_RESULT ||
      _state == STATE_TYPEB_RESULT || _state == STATE_TYPE4A_RESULT ||
      _state == STATE_TYPE1_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void PN532I2cScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_MAIN_MENU: _selMain = index; break;
    case STATE_MIFARE_MENU: _selMifare = index; break;
    case STATE_MIFARE_TAG_MENU: _selMifareTag = index; break;
    case STATE_MIFARE_ADVANCED_MENU: _selMifareAdvanced = index; break;
    case STATE_MIFARE_NDEF_MENU: _selMifareNdef = index; break;
    case STATE_MIFARE_ATTACKS_MENU: _selMifareAttacks = index; break;
    case STATE_MIFARE_KEYS_MENU: _selMifareKeys = index; break;
    case STATE_ULTRALIGHT_MENU: _selUltralight = index; break;
    case STATE_ULTRALIGHT_TAG_MENU: _selUltralightTag = index; break;
    case STATE_ULTRALIGHT_ADVANCED_MENU: _selUltralightAdvanced = index; break;
    case STATE_ULTRALIGHT_NDEF_MENU: _selUltralightNdef = index; break;
    case STATE_TYPEB_MENU: _selTypeB = index; break;
    case STATE_TYPEB_TAG_MENU: _selTypeBTag = index; break;
    case STATE_TYPEB_ADVANCED_MENU: _selTypeBAdvanced = index; break;
    case STATE_TYPEB_NDEF_MENU: _selTypeBNdef = index; break;
    case STATE_TYPE4A_MENU: _selType4A = index; break;
    case STATE_TYPE4A_TAG_MENU: _selType4ATag = index; break;
    case STATE_TYPE4A_ADVANCED_MENU: _selType4AAdvanced = index; break;
    case STATE_TYPE4A_NDEF_MENU: _selType4ANdef = index; break;
    case STATE_DESFIRE_MENU: _selDesfire = index; break;
    case STATE_DESFIRE_APPS_MENU: _selDesfireApps = index; break;
    case STATE_DESFIRE_FILES_MENU: _selDesfireFiles = index; break;
    case STATE_DESFIRE_ADVANCED_MENU: _selDesfireAdvanced = index; break;
    case STATE_FELICA_MENU: _selFelica = index; break;
    case STATE_FELICA_TAG_MENU: _selFelicaTag = index; break;
    case STATE_FELICA_SYSTEMS_MENU: _selFelicaSystems = index; break;
    case STATE_FELICA_SERVICES_MENU: _selFelicaServices = index; break;
    case STATE_FELICA_ADVANCED_MENU: _selFelicaAdvanced = index; break;
    case STATE_FELICA_NDEF_MENU: _selFelicaNdef = index; break;
    case STATE_TYPE1_MENU: _selType1 = index; break;
    case STATE_TYPE1_TAG_MENU: _selType1Tag = index; break;
    case STATE_TYPE1_NDEF_MENU: _selType1Ndef = index; break;
    case STATE_NDEF_WRITE_MENU: _selNdefWrite = index; break;
    default: break;
  }

  switch (_state) {
    case STATE_MAIN_MENU:
      switch (index) {
        case 0: _doScan14A();          break;
        case 1: _doGenericReadTag(); break;
        case 2: _doType4AReadEmv(); break;
        case 3: _doScanReader();      break;
        case 4: _goFamilies();        break;
        case 5: _showDeviceInfo();    break;
      }
      break;
    case STATE_FAMILIES_MENU:
      _selFamilies = index;
      _familyFromScan = false;
      _openFamily(index);
      break;
    case STATE_MIFARE_MENU:
      switch (index) {
        case 0: _goMifareTag(); break;
        case 1: _goMifareNdef(); break;
        case 2: _goMifareAttacks(); break;
        case 3: _goMifareKeys(); break;
      }
      break;
    case STATE_MIFARE_ATTACKS_MENU:
      if (index == 0) { _resumeReadAfterDict = false; _doDictionaryPicker(); }
      break;
    case STATE_MIFARE_KEYS_MENU:
      if (index == 0) _doShowKeys();
      else if (index == 1) { _keyDbPickDir = _dictPath; _openKeyDatabases(); }
      break;
    case STATE_MIFARE_KEY_DB_SELECT:
      _openKeyDatabase(index);
      break;
    case STATE_MIFARE_TAG_MENU:
      switch (index) {
        case 0: _goDetectMagic(); break;
        case 1: _doReadTag(); break;
        case 2: _doWriteUidSource(); break;
        case 3: _doWriteDumpFromFilePicker(); break;
        case 4: _doEraseTag(); break;
        case 5: _goMifareAdvanced(); break;
      }
      break;
    case STATE_MIFARE_ADVANCED_MENU:
      switch (index) {
        case 0: _doMifareReadMemory(); break;
        case 1: _doMifareEditMemory(); break;
        case 2: _doGen3LockUid(); break;
      }
      break;
    case STATE_MIFARE_NDEF_MENU:
      switch (index) {
        case 0:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _doReadClassicNdef();
          break;
        case 1:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _goNdefWrite();
          break;
        case 2:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          // Standalone Format NDEF must establish the same card context as
          // Read/Write NDEF before checking Classic dimensions.
          renderOperationTitle("Format NDEF");
          if (_scanCardOrShow(5000)) _formatClassic1kNdef();
          _goMifareNdef();
          break;
        case 3:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _doEraseClassicNdef();
          break;
      }
      break;
    case STATE_ULTRALIGHT_MENU:
      if (index == 0) _goUltralightTag();
      else if (index == 1) _goUltralightNdef();
      break;
    case STATE_ULTRALIGHT_TAG_MENU:
      if (index == 0) _doUltralightReadTag();
      else if (index == 1) _doUltralightWriteTag();
      else if (index == 2) _doUltralightEraseTag();
      else if (index == 3) _goUltralightAdvanced();
      break;
    case STATE_ULTRALIGHT_ADVANCED_MENU:
      if (index == 0) _doUltralightReadPages();
      else if (index == 1) _doUltralightWritePage();
      else if (index == 2) _doUltralightSetPassword();
      else if (index == 3) _doUltralightRemovePassword();
      else if (index == 4) _doUltralightLockTag();
      break;
    case STATE_ULTRALIGHT_NDEF_MENU:
      switch (index) {
        case 0:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doReadNdef();
          break;
        case 1:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _goNdefWrite();
          break;
        case 2:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doFormatNdef();
          break;
        case 3:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doEraseNdef();
          break;
      }
      break;
    case STATE_TYPEB_MENU:
      if (index == 0) _goTypeBTag();
      else if (index == 1) _goTypeBNdef();
      break;
    case STATE_TYPEB_TAG_MENU:
      if (index == 0) _doTypeBReadTag();
      else if (index == 1) _goTypeBAdvanced();
      break;
    case STATE_TYPEB_ADVANCED_MENU:
      if (index == 0) _doTypeBSendApdu(false);
      else if (index == 1) _doTypeBSendApdu(true);
      break;
    case STATE_TYPEB_NDEF_MENU:
      switch (index) {
        case 0: _ndefTarget = NDEF_TARGET_TYPE_B; _doTypeBReadNdef(); break;
        case 1: _ndefTarget = NDEF_TARGET_TYPE_B; _goNdefWrite(); break;
        case 2: _ndefTarget = NDEF_TARGET_TYPE_B; _doTypeBFormatNdef(); break;
        case 3: _ndefTarget = NDEF_TARGET_TYPE_B; _doTypeBEraseNdef(); break;
      }
      break;
    case STATE_TYPE4A_MENU:
      if (index == 0) _goType4ATag();
      else if (index == 1) _goType4ANdef();
      break;
    case STATE_TYPE4A_TAG_MENU:
      if (index == 0) _doType4AReadTag();
      else if (index == 1) _doType4AReadEmv();
      else if (index == 2) _goType4AAdvanced();
      break;
    case STATE_TYPE4A_ADVANCED_MENU:
      if (index == 0) _doType4ASendApdu();
      break;
    case STATE_TYPE4A_NDEF_MENU:
      switch (index) {
        case 0: _ndefTarget = NDEF_TARGET_TYPE_4A; _doType4AReadNdef(); break;
        case 1: _ndefTarget = NDEF_TARGET_TYPE_4A; _goNdefWrite(); break;
        case 2: _ndefTarget = NDEF_TARGET_TYPE_4A; _doType4AFormatNdef(); break;
        case 3: _ndefTarget = NDEF_TARGET_TYPE_4A; _doType4AEraseNdef(); break;
      }
      break;
    case STATE_DESFIRE_MENU:
      if (index == 0) _doDesfireReadTag();
      else if (index == 1) _goDesfireApps();
      else if (index == 2) _goDesfireFiles();
      else if (index == 3) _goDesfireAdvanced();
      break;
    case STATE_DESFIRE_APPS_MENU: _doDesfireAppAction(index); break;
    case STATE_DESFIRE_FILES_MENU: _doDesfireFileAction(index); break;
    case STATE_DESFIRE_ADVANCED_MENU: _doDesfireAdvancedAction(index); break;
    case STATE_FELICA_MENU:
      if (index == 0) _goFelicaTag(); else _goFelicaNdef();
      break;
    case STATE_FELICA_TAG_MENU: _doFelicaTagAction(index); break;
    case STATE_FELICA_SYSTEMS_MENU: _doFelicaSystemAction(index); break;
    case STATE_FELICA_SERVICES_MENU: _doFelicaServiceAction(index); break;
    case STATE_FELICA_ADVANCED_MENU: _doFelicaAdvancedAction(index); break;
    case STATE_FELICA_NDEF_MENU:
      if (index == 0) { _ndefTarget=NDEF_TARGET_FELICA; _doFelicaReadNdef(); }
      else if (index == 1) { _ndefTarget=NDEF_TARGET_FELICA; _goNdefWrite(); }
      else if (index == 2) _doFelicaFormatNdef();
      else if (index == 3) _doFelicaEraseNdef();
      break;
    case STATE_TYPE1_MENU:
      if (index == 0) _goType1Tag(); else _goType1Ndef();
      break;
    case STATE_TYPE1_TAG_MENU:
      if (index == 0) _doType1ReadTag(); else _doType1ReadMemory();
      break;
    case STATE_TYPE1_NDEF_MENU:
      if (index == 0) { _ndefTarget=NDEF_TARGET_TYPE_1; _doType1ReadNdef(); }
      break;
    case STATE_MIFARE_WRITE_PREVIEW:
      if (_writePreviewFromFile) _doWriteDumpFromFilePicker();
      else _showTagDetails();
      break;
    case STATE_MIFARE_DUMP_SELECT:
      _doWriteDumpFileSelected(index);
      break;
    case STATE_MIFARE_UID_SOURCE_FORM:
      if (index == 0) {
        static const InputSelectAction::Option sources[] = {{"Manual","manual"},{"UID File","uid"},{"Dump","dump"}};
        const char* r=InputSelectAction::popup("UID",sources,3,_uidWriteSourceMode==UID_WRITE_MANUAL?"manual":_uidWriteSourceMode==UID_WRITE_FILE?"uid":"dump");
        if(r){_uidWriteSourceMode=strcmp(r,"manual")==0?UID_WRITE_MANUAL:strcmp(r,"uid")==0?UID_WRITE_FILE:UID_WRITE_DUMP;_uidWriteSourceLen=0;_uidWriteFilePath="";_uidWriteDumpPath="";_rebuildWriteUidForm(0);} else render();
      } else if (index == 1) { if(_uidWriteSourceMode==UID_WRITE_MANUAL)_editWriteUidManual(); else if(_uidWriteSourceMode==UID_WRITE_FILE)_doWriteUidFromFilePicker(); else _doWriteUidFromDumpPicker(); }
      else if(index==2)_startWriteUidFromForm();
      break;
    case STATE_MIFARE_UID_SELECT:
      _doWriteUidFileSelected(index);
      break;
    case STATE_MIFARE_UID_DUMP_SELECT:
      _doWriteUidDumpSelected(index);
      break;
    case STATE_NDEF_WRITE_MENU:
      if (index == 0) _doWriteNdefText();
      else if (index == 1) _doWriteNdefUrl();
      else if (index == 2) _doWriteNdefPhone();
      else if (index == 3) _doWriteNdefEmail();
      else if (index == 4) _doWriteNdefVcard();
      else if (index == 5) _doWriteNdefFromFile();
      break;
    case STATE_NDEF_FILE_SELECT:
      _doWriteNdefFileSelected(index);
      break;
    case STATE_DICT_SELECT:
      _doDictionaryAttackWithFile(index);
      break;
    default: break;
  }
}

void PN532I2cScreen::onBack() {
  if (_genericReadActive) {
    switch (_state) {
      case STATE_MIFARE_DUMP:
      case STATE_ULTRALIGHT_DUMP:
      case STATE_TYPEB_RESULT:
      case STATE_TYPE4A_RESULT:
      case STATE_FELICA_RESULT:
      case STATE_TYPE1_RESULT:
        _goMain();
        return;
      default: break;
    }
  }
  switch (_state) {
    case STATE_MAIN_MENU:
      _cleanup();
      Screen.goBack();
      break;
    case STATE_FAMILIES_MENU:
      _goMain();
      break;
    case STATE_MIFARE_MENU:
    case STATE_ULTRALIGHT_MENU:
    case STATE_TYPEB_MENU:
    case STATE_TYPE4A_MENU:
    case STATE_DESFIRE_MENU:
    case STATE_FELICA_MENU:
    case STATE_TYPE1_MENU:
      _backFromFamily();
      break;
    case STATE_MAGIC_DETECT:
      _goMifareTag();
      break;
    case STATE_MIFARE_TAG_MENU:
    case STATE_MIFARE_NDEF_MENU:
    case STATE_MIFARE_ATTACKS_MENU:
    case STATE_MIFARE_KEYS_MENU:
      _goMifare();
      break;
    case STATE_ULTRALIGHT_TAG_MENU:
    case STATE_ULTRALIGHT_NDEF_MENU:
      _goUltralight();
      break;
    case STATE_TYPEB_TAG_MENU:
    case STATE_TYPEB_NDEF_MENU:
      _goTypeB();
      break;
    case STATE_TYPE4A_TAG_MENU:
    case STATE_TYPE4A_NDEF_MENU:
      _goType4A();
      break;
    case STATE_MIFARE_ADVANCED_MENU:
      _goMifareTag();
      break;
    case STATE_ULTRALIGHT_ADVANCED_MENU:
      _goUltralightTag();
      break;
    case STATE_TYPEB_ADVANCED_MENU:
      _goTypeBTag();
      break;
    case STATE_TYPE4A_ADVANCED_MENU:
      _goType4ATag();
      break;
    case STATE_DESFIRE_APPS_MENU:
    case STATE_DESFIRE_FILES_MENU:
    case STATE_DESFIRE_ADVANCED_MENU:
      _goDesfire();
      break;
    case STATE_DESFIRE_RESULT:
      if (_desfireResultReturn == STATE_DESFIRE_APPS_MENU) _goDesfireApps();
      else if (_desfireResultReturn == STATE_DESFIRE_FILES_MENU) _goDesfireFiles();
      else if (_desfireResultReturn == STATE_DESFIRE_ADVANCED_MENU) _goDesfireAdvanced();
      else _goDesfire();
      break;
    case STATE_FELICA_TAG_MENU:
    case STATE_FELICA_NDEF_MENU:
      _goFelica();
      break;
    case STATE_FELICA_SYSTEMS_MENU:
    case STATE_FELICA_SERVICES_MENU:
    case STATE_FELICA_ADVANCED_MENU:
      _goFelicaTag();
      break;
    case STATE_FELICA_RESULT:
      if (_felicaResultReturn == STATE_FELICA_SYSTEMS_MENU) _goFelicaSystems();
      else if (_felicaResultReturn == STATE_FELICA_SERVICES_MENU) _goFelicaServices();
      else if (_felicaResultReturn == STATE_FELICA_ADVANCED_MENU) _goFelicaAdvanced();
      else _goFelicaTag();
      break;
    case STATE_TYPE1_TAG_MENU:
    case STATE_TYPE1_NDEF_MENU:
      _goType1();
      break;
    case STATE_TYPE1_RESULT:
      _goType1Tag();
      break;
    case STATE_TYPEB_RESULT:
      if (_typeBFromMainScan) _goMain();
      else _goTypeBTag();
      break;
    case STATE_TYPE4A_RESULT:
      if (_emvResultActive && _emvResultFromMain) _goMain();
      else _goType4ATag();
      _emvResultActive = false;
      break;
    case STATE_MIFARE_UID_SOURCE_FORM:
      _goMifareTag();
      break;
    case STATE_MIFARE_UID_SELECT:
      if (_uidPickDir == "/unigeek/nfc/uids" || _uidPickDir.length() == 0) { _uidPickDir=""; _rebuildWriteUidForm(1); }
      else { int slash=_uidPickDir.lastIndexOf('/'); _uidPickDir=(slash>0)?_uidPickDir.substring(0,slash):String("/unigeek/nfc/uids"); _doWriteUidFromFilePicker(); }
      break;
    case STATE_MIFARE_UID_DUMP_SELECT:
      if (_dumpPickDir == _dumpPath || _dumpPickDir.length() == 0) { _dumpPickDir = ""; _rebuildWriteUidForm(1); }
      else { int slash = _dumpPickDir.lastIndexOf('/'); _dumpPickDir = (slash > 0) ? _dumpPickDir.substring(0, slash) : _dumpPath; _doWriteUidFromDumpPicker(); }
      break;
    case STATE_MIFARE_DUMP_SELECT:
      if (_dumpPickDir == _dumpPath || _dumpPickDir.length() == 0) {
        _dumpPickDir = "";
        _goMifareTag();
      } else {
        int slash = _dumpPickDir.lastIndexOf('/');
        _dumpPickDir = (slash > 0) ? _dumpPickDir.substring(0, slash) : _dumpPath;
        _doWriteDumpFromFilePicker();
      }
      break;
    case STATE_MIFARE_KEY_DB_SELECT:
      if (_keyDbPickDir == _dictPath || _keyDbPickDir.length() == 0) {
        _keyDbPickDir = "";
        _goMifareKeys();
      } else {
        int slash = _keyDbPickDir.lastIndexOf('/');
        _keyDbPickDir = (slash > 0) ? _keyDbPickDir.substring(0, slash) : _dictPath;
        _openKeyDatabases();
      }
      break;
    case STATE_DICT_SELECT:
      if (_dictPickDir == _dictPath || _dictPickDir.length() == 0) {
        _dictPickDir = "";
        _goMifareAttacks();
      } else {
        int slash = _dictPickDir.lastIndexOf('/');
        _dictPickDir = (slash > 0) ? _dictPickDir.substring(0, slash) : _dictPath;
        _doDictionaryPicker();
      }
      break;
    case STATE_RAW_RESULT:
      if (_rawResultTypeB) _goTypeBAdvanced();
      else if (_rawResultType4A) _goType4AAdvanced();
      else if (_rawResultMifare) _goMifareAdvanced();
      else _goUltralightAdvanced();
      break;
    case STATE_ULTRALIGHT_DUMP:
      _goUltralightTag();
      break;
    case STATE_NDEF_WRITE_MENU:
      _goNdefParent();
      break;
    case STATE_NDEF_RESULT:
      if (_ndefWritePreview) {
        bool fromFile = _ndefWritePreviewFromFile;
        _ndefWritePreview = false;
        _ndefWritePreviewFromFile = false;
        if (fromFile) _doWriteNdefFromFile();
        else _goNdefWrite();
      } else {
        _goNdefParent();
      }
      break;
    case STATE_NDEF_FILE_SELECT:
      if (_ndefPickDir == _ndefPath || _ndefPickDir.length() == 0) {
        _ndefPickDir = "";
        _goNdefWrite();
      } else {
        int slash = _ndefPickDir.lastIndexOf('/');
        String parent = (slash > 0) ? _ndefPickDir.substring(0, slash) : String(_ndefPath);
        if (!parent.startsWith(_ndefPath)) parent = _ndefPath;
        _ndefPickDir = parent;
        _doWriteNdefFromFile();
      }
      break;
    default:
      _goMain();
      break;
  }
}

void PN532I2cScreen::_doScanReader() {
  if (!_nfc || !_wire) return;
  _selMain = 1;
  renderOperationTitle("Scan NFC Reader");
  renderTagPrompt("Waiting for reader...", bodyX(), bodyY(), bodyW(), bodyH());

  bool cancelled = false;
  auto checkBack = [&]() -> bool {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      cancelled = true; return true;
    }
    return false;
  };

  struct ProbeProfile { uint8_t sak; bool isoDep; } profiles[] = {{0x08,false},{0x20,true}};
  const char* technology = "ISO 14443-A";
  String likely = "Unknown", confidence = "Low", action = "Activation";
  bool detected = false;

  const uint32_t probeStartedAt = millis();
  const uint32_t probeTimeoutMs = 15000;

  while (!detected && !cancelled && millis() - probeStartedAt < probeTimeoutMs) {
    for (const auto& profile : profiles) {
      if (cancelled || detected || millis() - probeStartedAt >= probeTimeoutMs) break;
    _nfc->SAMConfig();
    uint8_t target[38] = {};
    target[0] = 0x8C;
    target[1] = profile.isoDep ? 0x05 : 0x01;
    target[2] = 0x04; target[3] = 0x00;
    target[4] = 0x12; target[5] = 0x34; target[6] = 0x56;
    target[7] = profile.sak;
    if (!_nfcSendCmdReadAckProbe(_wire, target, sizeof(target), 800)) continue;

    bool activated=false;
    uint32_t start=millis();
    while (millis()-start<2500 && millis()-probeStartedAt<probeTimeoutMs) {
      if (checkBack()) break;
      if (_wire->requestFrom((uint8_t)PN532_I2C_ADDRESS,(uint8_t)1)==1 && _wire->available() && (_wire->read()&1)) {
        uint8_t buf[24]={}; _nfcReadI2C(_wire,buf,sizeof(buf));
        if (buf[5]==PN532_PN532TOHOST && buf[6]==0x8D) activated=true;
        break;
      }
      delay(15);
    }
    if (cancelled) break;
    if (!activated) { _nfc->SAMConfig(); continue; }

    uint8_t getCmd = profile.isoDep ? 0x86 : 0x88;
    if (!_nfcSendCmdReadAckProbe(_wire,&getCmd,1,500)) {
      detected=true; likely="Unknown"; confidence="Low"; action="Activated; probe command failed"; break;
    }
    start=millis();
    while (millis()-start<1800 && millis()-probeStartedAt<probeTimeoutMs) {
      if (checkBack()) break;
      if (_wire->requestFrom((uint8_t)PN532_I2C_ADDRESS,(uint8_t)1)==1 && _wire->available() && (_wire->read()&1)) {
        uint8_t buf[70]={}; _nfcReadI2C(_wire,buf,sizeof(buf));
        if (buf[5]==PN532_PN532TOHOST && buf[6]==(uint8_t)(getCmd+1)) {
          detected=true;
          if (profile.isoDep) {
            likely="ISO-DEP / Type 4"; confidence="High"; action="APDU exchange";
            if (buf[3]>5 && buf[9]==0xA4) action="SELECT application";
          } else {
            const uint8_t c=buf[8];
            if (c==0x60) { likely="MIFARE Classic"; confidence="High"; action="AUTH A"; }
            else if (c==0x61) { likely="MIFARE Classic"; confidence="High"; action="AUTH B"; }
            else if (c==0xA0) { likely="MIFARE Classic"; confidence="High"; action="WRITE"; }
            else if (c==0x30) { likely="MIFARE / Type 2"; confidence="Medium"; action="READ"; }
            else { likely="ISO 14443-A"; confidence="Medium"; action="Command 0x" + String(c, HEX); action.toUpperCase(); }
          }
        }
        break;
      }
      delay(15);
    }
      if (detected || cancelled) break;
      _nfc->SAMConfig();
    }
  }

  _nfc->SAMConfig();
  if (cancelled) { _goMain(); return; }
  if (!detected) {
    ShowStatusAction::show("Reader not detected", 1200);
    _goMain();
    return;
  }

  _rowCount=0;
  _rowLabels[_rowCount]="Technology"; _rowValues[_rowCount]=detected?technology:"Unknown"; _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; _rowCount++;
  _rowLabels[_rowCount]="Likely tag"; _rowValues[_rowCount]=detected?likely:"Unknown"; _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; _rowCount++;
  _rowLabels[_rowCount]="Confidence"; _rowValues[_rowCount]=detected?confidence:"Low"; _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; _rowCount++;
  _rowLabels[_rowCount]="Reader action"; _rowValues[_rowCount]=detected?action:"No command observed"; _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; _rowCount++;
  _scrollView.setRows(_rows,_rowCount);
  _state=STATE_DEVICE_INFO;
  renderOperationTitle("Scan NFC Reader");
  _scrollView.render(bodyX(),bodyY(),bodyW(),bodyH());

  while (true) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto d=Uni.Nav->readDirection();
      if (d==INavigation::DIR_BACK) { _goMain(); return; }
      _scrollView.onNav(d); _scrollView.render(bodyX(),bodyY(),bodyW(),bodyH());
    }
    delay(10);
  }
}

// ── init / cleanup ─────────────────────────────────────────────────────────

bool PN532I2cScreen::_initModule() {
  render();
  ProgressView::init();
  ProgressView::progress("Starting PN532 I2C...", 10);

  // Re-begin ExI2C on the currently-configured pins so runtime ext_sda/ext_scl
  // changes from Settings → Pin take effect without a reboot.
  if (Uni.ExI2C) {
    int sda = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
    int scl = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
    Uni.ExI2C->begin(sda, scl);

    _nfc = new Adafruit_PN532(255, 255, Uni.ExI2C);
    _nfc->begin(); // begin(false) internally — no Wire.begin()
    ProgressView::progress("Starting ExI2C...", 35);
    uint32_t fw = _nfc->getFirmwareVersion();
    if (fw) {
      _fwIc  = (fw >> 24) & 0xFF;
      _fwVer = (fw >> 16) & 0xFF;
      _fwRev = (fw >> 8)  & 0xFF;
      _fwSup =  fw        & 0xFF;
      _wire    = Uni.ExI2C;
      _busName = "ExI2C";
      ProgressView::finish();
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
    Uni.ExI2C->end();  // free pins for next consumer
  }

  // Fall back to InI2C
  if (Uni.InI2C) {
#ifdef DEVICE_PN532_REINIT_IN_I2C
    // This board requires a clean I2C restart before probing its on-board PN532.
    // Do not do this generically: InI2C may be shared with internal peripherals.
    Uni.InI2C->end();
    Uni.InI2C->begin();
#endif

#ifdef DEVICE_HAS_PN532_POWER_CONTROL
    // Wake the on-board PN532 only when we actually fall back to the internal bus.
    // RSTPDN release is quick; 10 ms leaves a conservative startup margin.
    digitalWrite(PN532_RESET_PIN, HIGH);
    delay(10);
#endif

    _nfc = new Adafruit_PN532(255, 255, Uni.InI2C);
    _nfc->begin();
    ProgressView::progress("Starting InI2C...", 35);
    uint32_t fw = _nfc->getFirmwareVersion();
    if (fw) {
      _fwIc  = (fw >> 24) & 0xFF;
      _fwVer = (fw >> 16) & 0xFF;
      _fwRev = (fw >> 8)  & 0xFF;
      _fwSup =  fw        & 0xFF;
      _wire    = Uni.InI2C;
      _busName = "InI2C";
      ProgressView::finish();
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
#ifdef DEVICE_HAS_PN532_POWER_CONTROL
    // A failed internal probe must not leave the on-board PN532 awake.
    digitalWrite(PN532_RESET_PIN, LOW);
#endif
  }

  ShowStatusAction::show("PN532 I2C not detected");
  Screen.goBack();
  return false;
}

void PN532I2cScreen::_cleanup() {
  delete _nfc; _nfc = nullptr;
  // Release ExI2C so a later screen can re-begin with a different pin set.
  // InI2C is shared with the PMIC / on-board peripherals — never end() it.
  if (_wire && _wire == Uni.ExI2C) _wire->end();
#ifdef DEVICE_HAS_PN532_POWER_CONTROL
  if (_wire && _wire == Uni.InI2C) digitalWrite(PN532_RESET_PIN, LOW);
#endif
  _wire    = nullptr;
  _busName = nullptr;
  _ready   = false;
  _hasCard = false;
  _hasTypeB = false;
  _typeBAttribLen = 0;
  _typeBRfConfigured = false;
  _rawResultTypeB = false;
  _rawResultTypeBRaw = false;
  _rawResultType4A = false;
  _hasType4A = false;
  _type4AAtsLen = 0;
  _hasFelica = false;
  _hasType1 = false;
  _desfireAidSelected = false;
  _desfireAidUidLen = 0;
  _operationResultTitle = "";
  _mfKeys.fill({});
}

// ── nav helpers ────────────────────────────────────────────────────────────

void PN532I2cScreen::_goMain() {
  _genericReadActive = false;
  _state = STATE_MAIN_MENU;
  setItems(_mainItems, 6, _selMain);
  render();
}

bool PN532I2cScreen::_genericIdentityMatches(uint8_t family, const uint8_t* id, uint8_t len) {
  if (!_genericReadActive) return true;
  if (_genericIdentityFamily == family && id && len == _genericIdentityLen &&
      memcmp(id, _genericIdentity, len) == 0) return true;
  ShowStatusAction::show("Different tag detected", 1200);
  _goMain();
  return false;
}

void PN532I2cScreen::_doGenericReadTag() {
  renderOperationTitle("Read Tag");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  int8_t family = -1;
  _genericIdentityLen = 0;
  _genericIdentityFamily = 0xFF;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goMain();
      return;
    }
    uint8_t uid[7] = {}; uint8_t uidLen = 0;
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      if (uidLen > sizeof(_uid)) break;
      memcpy(_uid, uid, uidLen);
      _uidLen = uidLen;
      _atqa = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak = pn532_packetbuffer[11];
      _hasCard = true;
      memcpy(_genericIdentity, uid, uidLen);
      _genericIdentityLen = uidLen;
      if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) family = 0;
      else if (_sak == 0x00) family = 1;
      else if (_sak & 0x20) family = 2;
      break;
    }
    if (_scanTypeB(150)) {
      if (_typeBAttribLen) {
        family = 3;
        const uint8_t off = (_typeBAtqb[0] == 0x50) ? 1 : 0;
        memcpy(_genericIdentity, _typeBAtqb + off, 4);
        _genericIdentityLen = 4;
      }
      break;
    }
    if (_scanFelica(150)) { family = 5; memcpy(_genericIdentity, _felicaIdm, 8); _genericIdentityLen = 8; break; }
    if (_scanType1(150)) { family = 6; memcpy(_genericIdentity, _type1JewelId, 4); _genericIdentityLen = 4; break; }
    delay(20);
  }
  if (family < 0) {
    ShowStatusAction::show("Tag not supported or not detected", 1200);
    _goMain();
    return;
  }
  _genericIdentityFamily = (uint8_t)family;
  _genericReadActive = true;
  switch (family) {
    case 0: _doReadTag(); break;
    case 1: _doUltralightReadTag(); break;
    case 2: _doType4AReadTag(); break;
    case 3: _doTypeBReadTag(); break;
    case 5: _doFelicaReadTag(); break;
    case 6: _doType1ReadTag(); break;
  }
}

void PN532I2cScreen::_goFamilies() {
  _state = STATE_FAMILIES_MENU;
  setItems(_familyItems, 7, _selFamilies);
  render();
}

void PN532I2cScreen::_openFamily(uint8_t index) {
  switch (index) {
    case 0: _goMifare(); break;
    case 1: _goUltralight(); break;
    case 2: _goType4A(); break;
    case 3: _goTypeB(); break;
    case 4: _goDesfire(); break;
    case 5: _goFelica(); break;
    case 6: _goType1(); break;
  }
}

void PN532I2cScreen::_backFromFamily() {
  if (_familyFromScan) { _familyFromScan = false; _goMain(); }
  else _goFamilies();
}

void PN532I2cScreen::_goMifare() {
  _state = STATE_MIFARE_MENU;
  setItems(_mfItems, 4, _selMifare);
  render();
}

void PN532I2cScreen::_goMifareAttacks() {
  _state = STATE_MIFARE_ATTACKS_MENU;
  setItems(_mfAttackItems, 1, _selMifareAttacks);
  render();
}

void PN532I2cScreen::_goMifareKeys() {
  _state = STATE_MIFARE_KEYS_MENU;
  setItems(_mfKeysItems, 2, _selMifareKeys);
  render();
}

void PN532I2cScreen::_goMifareTag() {
  if (_genericReadActive) { _goMain(); return; }
  _state = STATE_MIFARE_TAG_MENU;
  setItems(_mfTagItems, 6, _selMifareTag);
  render();
}

void PN532I2cScreen::_goMifareAdvanced() {
  _rawResultType4A = false;
  _rawResultTypeB = false;
  _state = STATE_MIFARE_ADVANCED_MENU;
  setItems(_mfAdvancedItems, 3, _selMifareAdvanced);
  render();
}

void PN532I2cScreen::_goMifareNdef() {
  _state = STATE_MIFARE_NDEF_MENU;
  setItems(_mfNdefItems, 4, _selMifareNdef);
  render();
}

void PN532I2cScreen::_goUltralight() {
  _state = STATE_ULTRALIGHT_MENU;
  setItems(_ulItems, 2, _selUltralight);
  render();
}

void PN532I2cScreen::_goUltralightTag() {
  if (_genericReadActive) { _goMain(); return; }
  _state = STATE_ULTRALIGHT_TAG_MENU;
  setItems(_ulTagItems, 4, _selUltralightTag);
  render();
}

void PN532I2cScreen::_goUltralightAdvanced() {
  _rawResultType4A = false;
  _rawResultTypeB = false;
  _state = STATE_ULTRALIGHT_ADVANCED_MENU;
  setItems(_ulAdvancedItems, 5, _selUltralightAdvanced);
  render();
}

void PN532I2cScreen::_goUltralightNdef() {
  _state = STATE_ULTRALIGHT_NDEF_MENU;
  setItems(_ulNdefItems, 4, _selUltralightNdef);
  render();
}

void PN532I2cScreen::_goTypeB() {
  _state = STATE_TYPEB_MENU;
  setItems(_typeBItems, 2, _selTypeB);
  render();
}

void PN532I2cScreen::_goTypeBTag() {
  if (_genericReadActive) { _goMain(); return; }
  _state = STATE_TYPEB_TAG_MENU;
  setItems(_typeBTagItems, 2, _selTypeBTag);
  render();
}

void PN532I2cScreen::_goTypeBAdvanced() {
  _rawResultType4A = false;
  _rawResultTypeB = true;
  _state = STATE_TYPEB_ADVANCED_MENU;
  setItems(_typeBAdvancedItems, 2, _selTypeBAdvanced);
  render();
}

void PN532I2cScreen::_goTypeBNdef() {
  _state = STATE_TYPEB_NDEF_MENU;
  setItems(_typeBNdefItems, 4, _selTypeBNdef);
  render();
}

void PN532I2cScreen::_goType4A() {
  _state = STATE_TYPE4A_MENU;
  setItems(_type4AItems, 2, _selType4A);
  render();
}

void PN532I2cScreen::_goType4ATag() {
  if (_genericReadActive) { _goMain(); return; }
  _state = STATE_TYPE4A_TAG_MENU;
  setItems(_type4ATagItems, 3, _selType4ATag);
  render();
}

void PN532I2cScreen::_goType4AAdvanced() {
  _rawResultType4A = false;
  _rawResultTypeB = false;
  _state = STATE_TYPE4A_ADVANCED_MENU;
  setItems(_type4AAdvancedItems, 1, _selType4AAdvanced);
  render();
}

void PN532I2cScreen::_goType4ANdef() {
  _state = STATE_TYPE4A_NDEF_MENU;
  setItems(_type4ANdefItems, 4, _selType4ANdef);
  render();
}

void PN532I2cScreen::_goDesfire() {
  _state = STATE_DESFIRE_MENU;
  setItems(_desfireItems, 4, _selDesfire);
  render();
}

void PN532I2cScreen::_goDesfireApps() { _state=STATE_DESFIRE_APPS_MENU; setItems(_desfireAppItems,3,_selDesfireApps); render(); }
void PN532I2cScreen::_goDesfireFiles() { _state=STATE_DESFIRE_FILES_MENU; setItems(_desfireFileItems,4,_selDesfireFiles); render(); }
void PN532I2cScreen::_goDesfireAdvanced() { _state=STATE_DESFIRE_ADVANCED_MENU; setItems(_desfireAdvancedItems,2,_selDesfireAdvanced); render(); }
void PN532I2cScreen::_goFelica() { _state=STATE_FELICA_MENU; setItems(_felicaItems,2,_selFelica); render(); }
void PN532I2cScreen::_goFelicaTag() {
  if (_genericReadActive) { _goMain(); return; } _state=STATE_FELICA_TAG_MENU; setItems(_felicaTagItems,4,_selFelicaTag); render(); }
void PN532I2cScreen::_goFelicaSystems() { _state=STATE_FELICA_SYSTEMS_MENU; setItems(_felicaSystemItems,1,_selFelicaSystems); render(); }
void PN532I2cScreen::_goFelicaServices() { _state=STATE_FELICA_SERVICES_MENU; setItems(_felicaServiceItems,3,_selFelicaServices); render(); }
void PN532I2cScreen::_goFelicaAdvanced() { _state=STATE_FELICA_ADVANCED_MENU; setItems(_felicaAdvancedItems,3,_selFelicaAdvanced); render(); }
void PN532I2cScreen::_goFelicaNdef() { _state=STATE_FELICA_NDEF_MENU; setItems(_felicaNdefItems,4,_selFelicaNdef); render(); }
void PN532I2cScreen::_goType1() { _state=STATE_TYPE1_MENU; setItems(_type1Items,2,_selType1); render(); }
void PN532I2cScreen::_goType1Tag() {
  if (_genericReadActive) { _goMain(); return; } _state=STATE_TYPE1_TAG_MENU; setItems(_type1TagItems,2,_selType1Tag); render(); }
void PN532I2cScreen::_goType1Ndef() { _state=STATE_TYPE1_NDEF_MENU; setItems(_type1NdefItems,1,_selType1Ndef); render(); }

void PN532I2cScreen::_goNdefWrite() {
  _ndefWritePreview = false;
  _ndefWritePreviewFromFile = false;
  _state = STATE_NDEF_WRITE_MENU;
  setItems(_ndefWriteItems, 6, _selNdefWrite);
  render();
}

void PN532I2cScreen::_goNdefParent() {
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) _goMifareNdef();
  else if (_ndefTarget == NDEF_TARGET_TYPE_B) _goTypeBNdef();
  else if (_ndefTarget == NDEF_TARGET_TYPE_4A) _goType4ANdef();
  else if (_ndefTarget == NDEF_TARGET_FELICA) _goFelicaNdef();
  else if (_ndefTarget == NDEF_TARGET_TYPE_1) _goType1Ndef();
  else _goUltralightNdef();
}

void PN532I2cScreen::_goDetectMagic() {
  _state = STATE_MAGIC_DETECT;
  _magicUiPhase = MAGIC_WAITING;
  _magicResult = "";
  render();
  _doDetectMagic();
}

// ── display helpers ────────────────────────────────────────────────────────

String PN532I2cScreen::_hexUid(const uint8_t* uid, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : ":", uid[i]);
    s += buf;
  }
  return s;
}

String PN532I2cScreen::_hexBlock(const uint8_t* data, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : " ", data[i]);
    s += buf;
  }
  return s;
}

void PN532I2cScreen::_resetRows() { _rowCount = 0; }

void PN532I2cScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;
}

void PN532I2cScreen::_pushWrappedRow(const String& label, const String& value) {
  if (value.length() == 0) {
    _pushRow(label, "");
    return;
  }

  // ScrollListView uses the built-in size-1 font. Keep enough room for the
  // label on the first row; continuation rows can use almost the full width.
  // Using conservative character widths avoids overlap/clipping without
  // changing ScrollListView globally.
  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;

  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;

  while (pos < (int)value.length() && _rowCount < MAX_ROWS) {
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) {
      pos++;
    }
    if (pos >= (int)value.length()) break;

    const int maxChars = first ? firstChars : totalChars;
    int end = pos + maxChars;
    if (end > (int)value.length()) end = value.length();

    // Respect explicit newlines.
    int newline = value.indexOf('\n', pos);
    if (newline >= pos && newline < end) {
      end = newline;
    } else if (end < (int)value.length()) {
      // Prefer a word boundary. For URLs / very long tokens there may be no
      // spaces, in which case the hard character boundary is used.
      int split = -1;
      for (int i = end; i > pos; --i) {
        if (value[i - 1] == ' ' || value[i - 1] == '\t') {
          split = i - 1;
          break;
        }
      }
      if (split > pos) end = split;
    }

    if (end <= pos) end = min(pos + maxChars, (int)value.length());

    String part = value.substring(pos, end);
    part.trim();
    _pushRow(first ? label : "", part);

    pos = end;
    first = false;

    // Skip whitespace/newline consumed at the wrap point.
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) {
      pos++;
    }
  }
}

std::pair<size_t, size_t> PN532I2cScreen::_mfDims(uint8_t sak) const {
  if (sak == 0x09) return {5,  20};
  if (sak == 0x08) return {16, 64};
  if (sak == 0x18) return {40, 256};
  return {0, 0};
}

const char* PN532I2cScreen::_inferType(uint8_t sak, uint16_t atqa) const {
  uint8_t atqaHi = (atqa >> 8) & 0xFF;
  if (sak == 0x09) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) return atqaHi == 0x03 ? "MIFARE DESFire" : "ISO14443-4";
  if (sak == 0x00) return (atqa & 0x00FF) == 0x44 ? "Ultralight / NTAG" : "ISO14443A T2";
  return "ISO14443A";
}

const char* PN532I2cScreen::_inferType2Variant() {
  if (_sak != 0x00 || !_nfc || !_wire) return nullptr;

  // Keep the PN532 path aligned with the Chameleon Type-2 detector:
  //   1) GET_VERSION for NTAG21x / Ultralight EV1
  //   2) AUTH probe for legacy Ultralight C
  //   3) page-boundary probe for original 16-page Ultralight
  //
  // InDataExchange can be unreliable for Type-2 proprietary commands on some
  // PN532/library combinations, so each probe may fall back to
  // InCommunicateThru.

  auto type2Exchange = [&](const uint8_t* tx, uint8_t txLen,
                           uint8_t* rx, uint8_t& rxLen,
                           uint32_t timeoutMs = 500) -> bool {
    uint8_t cap = rxLen;
    if (_nfcDataExch(_nfc, _wire, tx, txLen, rx, rxLen, timeoutMs))
      return true;

    rxLen = cap;
    memset(rx, 0, cap);
    return _nfcCommThru(_nfc, _wire, tx, txLen, rx, rxLen, timeoutMs);
  };

  auto readType2CcSize = [&]() -> uint8_t {
    const uint8_t readCc[2] = {0x30, 0x03};
    uint8_t data[18] = {};
    uint8_t len = sizeof(data);
    if (!type2Exchange(readCc, sizeof(readCc), data, len) || len < 4) return 0;
    if (data[0] != 0xE1 || (data[1] & 0xF0) != 0x10 || data[2] == 0) return 0;
    return data[2];
  };

  auto genericType2Name = [&]() -> const char* {
    const uint8_t ccSize = readType2CcSize();
    if (!ccSize) return nullptr;

    return "Ultralight / NTAG";
  };

  // NTAG21x / Ultralight EV1 GET_VERSION.
  const uint8_t getVersion = 0x60;
  uint8_t version[8] = {};
  bool gotVersion = false;

  for (uint8_t attempt = 0; attempt < 3 && !gotVersion; ++attempt) {
    uint8_t len = sizeof(version);
    memset(version, 0, sizeof(version));
    if (type2Exchange(&getVersion, 1, version, len) && len >= 8 &&
        version[0] == 0x00 && version[1] == 0x04)
      gotVersion = true;
    else
      delay(20);
  }

  if (gotVersion) {
    if (version[2] == 0x04) { // NTAG21x
      switch (version[6]) {
        case 0x0B: return "NTAG210";
        case 0x0E: return "NTAG212";
        case 0x0F: return "NTAG213";
        case 0x11: return "NTAG215";
        case 0x13: return "NTAG216";
        default:   return genericType2Name();
      }
    }

    if (version[2] == 0x03) { // MIFARE Ultralight EV1
      switch (version[6]) {
        case 0x0B: return "Ultralight EV1 11";
        case 0x0E: return "Ultralight EV1 21";
        default:   return "Ultralight EV1";
      }
    }

    // A real but unknown GET_VERSION response must not be forced into one of
    // the legacy Ultralight variants.
    return genericType2Name();
  }

  // Legacy Ultralight C has no GET_VERSION. AUTHENTICATE part 1 returns
  // AF + 8 encrypted bytes when the command is supported.
  const uint8_t authCmd[2] = {0x1A, 0x00};
  uint8_t authResp[16] = {};
  uint8_t authLen = sizeof(authResp);
  const bool isUltralightC =
      type2Exchange(authCmd, sizeof(authCmd), authResp, authLen) &&
      authLen >= 9 && authResp[0] == 0xAF;

  // The UL-C probe starts authentication. Re-select the card so the scan does
  // not leave it in a partial authentication state for the next PN532 action.
  uint8_t reselectUid[7] = {};
  uint8_t reselectUidLen = 0;
  _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,
                            reselectUid, &reselectUidLen, 200);

  if (isUltralightC) return "Ultralight C";

  const uint8_t read0[2] = {0x30, 0x00};
  uint8_t data[18] = {};
  uint8_t len = sizeof(data);
  if (!type2Exchange(read0, sizeof(read0), data, len) || len < 16)
    return genericType2Name();

  const uint8_t read41[2] = {0x30, 0x29};
  len = sizeof(data);
  memset(data, 0, sizeof(data));
  if (!type2Exchange(read41, sizeof(read41), data, len) || len < 16)
    return "Ultralight";

  return genericType2Name();
}

// ── scan helper ────────────────────────────────────────────────────────────

bool PN532I2cScreen::_scanCardOrShow(uint32_t timeoutMs) {
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) return false;
    }
    uint8_t uid[7]; uint8_t uidLen;
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      const bool sameCard =
        _hasCard &&
        uidLen == _uidLen &&
        memcmp(uid, _uid, uidLen) == 0;

      memcpy(_uid, uid, uidLen);
      _uidLen  = uidLen;
      _atqa    = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak     = pn532_packetbuffer[11];
      _hasCard = true;

      if (!sameCard) {
        _mfKeys.fill({});
      }
      return true;
    }
    delay(50);
  }
  ShowStatusAction::show("Tag not detected", 1200);
  return false;
}

bool PN532I2cScreen::_scanType4A(uint32_t timeoutMs) {
  if (!_nfc || !_wire) return false;
  _emvDiscoveryCaptured = true;
  _emvDiscoveryCommandOk = false;
  _emvDiscoveryLen = 0;
  memset(_emvDiscoveryResponse, 0, sizeof(_emvDiscoveryResponse));
  _hasType4A = false;
  _type4AAtsLen = 0;
  memset(_type4AAts, 0, sizeof(_type4AAts));

  if (_emvFiniteRetriesTest) {
    const uint8_t retries[] = {PN532_COMMAND_RFCONFIGURATION, 0x05, 0xFF, 0x01, 0x02};
    uint8_t configRsp[4] = {};
    size_t configLen = 0;
    _emvProbeDiscovery = true;
    _emvRetryConfigOk = _nfcCommandResponse(_nfc, _wire, retries, sizeof(retries),
        (uint8_t)(PN532_COMMAND_RFCONFIGURATION + 1),
        configRsp, sizeof(configRsp), configLen, 1000);
    _emvProbeDiscovery = false;
    if (!_emvRetryConfigOk) return false;
  }

  const uint8_t cmd[] = {PN532_COMMAND_INLISTPASSIVETARGET, 0x01, 0x00};
  uint8_t rsp[64] = {};
  size_t n = 0;
  _emvProbeDiscovery = true;
  _emvDiscoveryCommandOk = _nfcCommandResponse(_nfc, _wire, cmd, sizeof(cmd),
                           (uint8_t)(PN532_COMMAND_INLISTPASSIVETARGET + 1),
                           rsp, sizeof(rsp), n, timeoutMs);
  _emvProbeDiscovery = false;
  if (_emvFiniteRetriesTest) {
    const auto discoveryFailure = _emvCmdFailure;
    const int discoveryReadCount = _emvI2cReadCount;
    const uint32_t discoveryAckElapsed = _emvAckElapsedMs;
    const uint16_t discoveryAckTimeout = _emvAckTimeoutMs;
    const bool discoveryAckOk = _emvProbeAckOk;
    const uint8_t restore[] = {PN532_COMMAND_RFCONFIGURATION, 0x05, 0xFF, 0x01, 0xFF};
    uint8_t restoreRsp[4] = {};
    size_t restoreLen = 0;
    _emvProbeDiscovery = true;
    _emvRetryRestoreOk = _nfcCommandResponse(_nfc, _wire, restore, sizeof(restore),
        (uint8_t)(PN532_COMMAND_RFCONFIGURATION + 1),
        restoreRsp, sizeof(restoreRsp), restoreLen, 1000);
    _emvProbeDiscovery = false;
    _emvCmdFailure = discoveryFailure;
    _emvI2cReadCount = discoveryReadCount;
    _emvAckElapsedMs = discoveryAckElapsed;
    _emvAckTimeoutMs = discoveryAckTimeout;
    _emvProbeAckOk = discoveryAckOk;
  }
  if (!_emvDiscoveryCommandOk) return false;
  _emvDiscoveryLen = n;
  if (n) memcpy(_emvDiscoveryResponse, rsp, n);
  if (n < 6 || rsp[0] == 0) return false;

  _type4ATg = rsp[1];
  _atqa = ((uint16_t)rsp[2] << 8) | rsp[3];
  _sak = rsp[4];
  _uidLen = rsp[5];
  if (_uidLen == 0 || _uidLen > sizeof(_uid) || 6u + _uidLen > n) return false;
  memcpy(_uid, rsp + 6, _uidLen);

  if ((_sak & 0x20U) == 0) return false;

  size_t pos = 6u + _uidLen;
  if (pos < n) {
    const uint8_t atsFieldLen = rsp[pos++];
    if (atsFieldLen > 0) {
      const size_t atsBytes = (size_t)atsFieldLen - 1u;
      if (atsBytes > sizeof(_type4AAts) || pos + atsBytes > n) return false;
      _type4AAtsLen = (uint8_t)atsBytes;
      if (atsBytes) memcpy(_type4AAts, rsp + pos, atsBytes);
    }
  }

  _hasCard = true;
  _hasType4A = true;
  return true;
}

bool PN532I2cScreen::_type4AExchange(const uint8_t* tx, size_t txLen,
                                     uint8_t* rx, size_t rxCap, size_t& rxLen) {
  rxLen = 0;
  _emvApduTransportStage = "Arguments";
  _emvApduCommandFailure = _Pn532CmdFailure::None;
  _emvApduI2cBytes = -1;
  _emvApduCommandElapsedMs = 0;
  _emvApduPn532Status = -1;
  _emvApduPayloadBytes = 0;
  _emvApduResponseCap = rxCap;
  if (!_hasType4A || !tx || txLen == 0 || txLen > 60 || !rx || rxCap == 0) return false;
  uint8_t cmd[64] = {PN532_COMMAND_INDATAEXCHANGE, _type4ATg};
  memcpy(cmd + 2, tx, txLen);
  uint8_t rsp[256] = {};
  size_t n = 0;
  const uint32_t start = millis();
  const bool ok = _nfcCommandResponse(_nfc, _wire, cmd, (uint8_t)(txLen + 2),
                                      PN532_RESPONSE_INDATAEXCHANGE,
                                      rsp, sizeof(rsp), n, 1200, true);
  _emvApduCommandElapsedMs = millis() - start;
  _emvApduCommandFailure = _emvCmdFailure;
  _emvApduI2cBytes = _emvI2cReadCount;
  if (!ok) { _emvApduTransportStage = "PN532 command/frame"; return false; }
  _emvApduPayloadBytes = n;
  if (n < 1) { _emvApduTransportStage = "Empty PN532 payload"; return false; }
  _emvApduPn532Status = rsp[0];
  if ((rsp[0] & 0x3FU) != 0) { _emvApduTransportStage = "PN532 status error"; return false; }
  const size_t dataLen = n - 1;
  if (dataLen > rxCap) { _emvApduTransportStage = "APDU response > buffer"; return false; }
  if (dataLen) memcpy(rx, rsp + 1, dataLen);
  rxLen = dataLen;
  _emvApduTransportStage = "OK";
  return true;
}

void PN532I2cScreen::_showEmvDetectionDiagnostics() {
  _state = STATE_TYPE4A_RESULT;
  _resetRows();
  _pushRow("Type", "EMV detection diagnostics");
  _pushRow("EMV stage", "Before SELECT PPSE");
  _pushRow("APDU attempts", "0");
  _pushRow("Discovery timeout", "1000 ms (experimental)");
  _pushRow("RF retries configured", _emvRetryConfigOk ? "Yes (2 attempts)" : "No/failed");
  _pushRow("RF retries restored", _emvRetryRestoreOk ? "Yes (0xFF)" : "No/failed");
  if (!_nfc || !_wire) {
    _pushRow("Discovery", "PN532 not initialized");
  } else {
    const uint8_t* rsp = _emvDiscoveryResponse;
    const size_t n = _emvDiscoveryLen;
    const bool ok = _emvDiscoveryCaptured && _emvDiscoveryCommandOk;
    _pushRow("Discovery source", "Original attempt");
    const char* failure = "None";
    switch (_emvCmdFailure) {
      case _Pn532CmdFailure::None: break;
      case _Pn532CmdFailure::InvalidArgs: failure = "Invalid arguments"; break;
      case _Pn532CmdFailure::AckFailed: failure = "ACK/send failed"; break;
      case _Pn532CmdFailure::ReadyTimeout: failure = "Response ready timeout"; break;
      case _Pn532CmdFailure::ShortRead: failure = "I2C short read"; break;
      case _Pn532CmdFailure::InvalidPreamble: failure = "Invalid frame preamble"; break;
      case _Pn532CmdFailure::InvalidLength: failure = "Invalid frame length"; break;
      case _Pn532CmdFailure::UnexpectedResponse: failure = "Unexpected response"; break;
      case _Pn532CmdFailure::InvalidPayloadLength: failure = "Invalid payload length"; break;
      case _Pn532CmdFailure::InvalidChecksum: failure = "Invalid frame checksum"; break;
    }
    _pushRow("Command failure", failure);
    _pushRow("ACK path elapsed", String((unsigned long)_emvAckElapsedMs) + " ms");
    _pushRow("ACK path timeout", String((unsigned)_emvAckTimeoutMs) + " ms");
    _pushRow("ACK-only probe", _emvProbeAckOk ? "ACK received" : "ACK missing/invalid");
    _pushRow("Post-ACK stage", _emvProbeAckOk ?
        (_emvCmdFailure == _Pn532CmdFailure::ReadyTimeout ? "Response timeout" : "Response/frame processing") :
        "Not reached");
    if (_emvCmdFailure == _Pn532CmdFailure::AckFailed)
      _pushRow("ACK detail", "Library returned false; send vs ACK unknown");
    if (_emvI2cReadCount >= 0) {
      _pushRow("I2C requested", "81");
      _pushRow("I2C received", String(_emvI2cReadCount));
    }

    if (!ok) {
      _pushRow("Discovery", "Command/timeout error");
    } else {
      _pushRow("Response bytes", String((unsigned)n));
      if (n > 0) {
        const size_t shown = n < 32 ? n : 32;
        for (size_t offset = 0; offset < shown; offset += 8) {
          const size_t chunk = (shown - offset < 8) ? shown - offset : 8;
          const String label = String("Raw +") + String((unsigned)offset);
          _pushRow(label.c_str(), _hexBlock(rsp + offset, chunk));
        }
        if (n > shown) _pushRow("Raw payload", "First 32 bytes only");
      }
      if (n < 1) {
        _pushRow("Discovery", "Empty response");
      } else {
        _pushRow("Targets", String((unsigned)rsp[0]));
        if (rsp[0] == 0) {
          _pushRow("Discovery", "No Type A target");
        } else if (n < 6) {
          _pushRow("Discovery", "Truncated target header");
        } else {
          _pushRow("Target #", String((unsigned)rsp[1]));
          char hex[8];
          snprintf(hex, sizeof(hex), "%02X%02X", rsp[2], rsp[3]);
          _pushRow("ATQA", hex);
          snprintf(hex, sizeof(hex), "%02X", rsp[4]);
          _pushRow("SAK", hex);
          _pushRow("ISO-DEP bit", (rsp[4] & 0x20U) ? "Set" : "Not set");
          const size_t uidLen = rsp[5];
          if (!uidLen || uidLen > 10 || uidLen > n - 6) {
            _pushRow("Discovery", "Invalid/truncated UID");
          } else {
            _pushRow("UID", _hexBlock(rsp + 6, uidLen));
            const size_t pos = 6 + uidLen;
            if (pos < n) {
              const size_t tl = rsp[pos];
              if (tl && tl <= n - pos) {
                _pushRow("ATS", _hexBlock(rsp + pos, tl));
              } else {
                _pushRow("ATS", "Invalid/truncated");
              }
            } else {
              _pushRow("ATS", "Not provided");
            }
            _pushRow("Discovery", (rsp[4] & 0x20U) ? "Type A ISO-DEP candidate" : "Not Type A ISO-DEP");
          }
        }
      }
    }
  }
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doType4AReadEmv() {
  _emvResultFromMain = (_state == STATE_MAIN_MENU);
  _emvResultActive = true;
  _emvDetailsVisible = false;
  _emvDetailsAvailable = false;
  renderOperationTitle("Read EMV (experimental)");
  renderTagPrompt("Waiting for card...", bodyX(), bodyY(), bodyW(), bodyH());

  auto showEmvStatus = [this](const String& status) {
    _state = STATE_TYPE4A_RESULT;
    _resetRows();
    _pushWrappedRow("Status", status);
    _scrollView.setRows(_rows, _rowCount);
    render();
  };

  auto emvExchangeLogged = [this](const char*, uint8_t* tx,
                                  uint8_t txLen, uint8_t* rx,
                                  uint8_t* rxLen) -> bool {
    return _nfc->inDataExchange(tx, txLen, rx, rxLen);
  };

  if (!_wire || _wire->setBufferSize(256) < 256) {
    showEmvStatus("I2C buffer setup failed");
    return;
  }

  const bool emvSamOk = _nfc->SAMConfig();
  if (!emvSamOk) {
    showEmvStatus("SAM init failed");
    return;
  }
  delay(60);

  bool cancelled = false;
  const bool targetActive = _nfc->inListPassiveTarget(
      5000,
      []() -> bool {
        Uni.update();
        return Uni.Nav->wasPressed() &&
               Uni.Nav->readDirection() == INavigation::DIR_BACK;
      },
      &cancelled);
  if (cancelled) {
    _emvResultActive = false;
    if (_emvResultFromMain) _goMain();
    else _goType4ATag();
    return;
  }
  if (!targetActive) {
    _emvResultActive = false;
    ShowStatusAction::show("Card not detected", 1200);
    if (_emvResultFromMain) _goMain();
    else _goType4ATag();
    return;
  }

  uint8_t apdu[20] = {};
  const uint8_t apduLen = (uint8_t)EmvReader::buildSelectPpse(apdu);
  apdu[19] = 0x00;

  uint8_t response[220] = {};
  uint8_t responseLen = sizeof(response);
  if (!emvExchangeLogged("PPSE", apdu, apduLen, response, &responseLen)) {
    showEmvStatus("PPSE transport failed");
    return;
  }

  if (responseLen >= 2) {
    const uint8_t sw1 = response[responseLen - 2];
    const uint8_t sw2 = response[responseLen - 1];
    if (sw1 != 0x90 || sw2 != 0x00) {
      if (responseLen == sizeof(response)) {
        showEmvStatus("Response too long");
      } else {
        char msg[20];
        snprintf(msg, sizeof(msg), "APDU SW %02X%02X", sw1, sw2);
        showEmvStatus(msg);
      }
      return;
    }
  }

  EmvReader::Application apps[EmvReader::kMaxApps];
  const uint8_t appCount = EmvReader::parsePpse(response, responseLen, apps, EmvReader::kMaxApps);
  if (!appCount) {
    showEmvStatus("No EMV application found");
    return;
  }

  uint8_t selected = 0;
  uint8_t bestPriority = 0xFF;
  for (uint8_t i = 0; i < appCount; ++i) {
    const uint8_t priority = apps[i].priority & 0x0F;
    if (priority && priority < bestPriority) {
      selected = i;
      bestPriority = priority;
    }
  }

  uint8_t selectAid[22] = {};
  const uint8_t selectAidLen = (uint8_t)EmvReader::buildSelectAid(apps[selected], selectAid);
  if (!selectAidLen) {
    showEmvStatus("Invalid application AID");
    return;
  }

  uint8_t appResponse[220] = {};
  uint8_t appResponseLen = sizeof(appResponse);
  if (!emvExchangeLogged("SELECT_AID", selectAid, selectAidLen, appResponse, &appResponseLen)) {
    showEmvStatus("AID transport failed");
    return;
  }

  if (appResponseLen < 2) {
    showEmvStatus("Invalid AID response");
    return;
  }
  const uint8_t aidSw1 = appResponse[appResponseLen - 2];
  const uint8_t aidSw2 = appResponse[appResponseLen - 1];
  if (aidSw1 != 0x90 || aidSw2 != 0x00) {
    char msg[20];
    snprintf(msg, sizeof(msg), "AID SW %02X%02X", aidSw1, aidSw2);
    showEmvStatus(msg);
    return;
  }

  EmvReader::SelectedApplication selectedApp;
  EmvReader::parseSelectAid(appResponse, appResponseLen, selectedApp);

  uint8_t gpoApdu[160] = {};
  const size_t gpoApduLen = EmvReader::buildGpo(selectedApp, gpoApdu, sizeof(gpoApdu));
  if (!gpoApduLen || gpoApduLen > 255) { showEmvStatus("Invalid PDOL"); return; }

  uint8_t gpoResponse[220] = {};
  uint8_t gpoResponseLen = sizeof(gpoResponse);
  if (!emvExchangeLogged("GPO", gpoApdu, (uint8_t)gpoApduLen, gpoResponse, &gpoResponseLen)) { showEmvStatus("GPO transport failed"); return; }
  if (gpoResponseLen < 2) { showEmvStatus("Invalid GPO response"); return; }
  if (gpoResponse[gpoResponseLen-2]!=0x90 || gpoResponse[gpoResponseLen-1]!=0x00) {
    char msg[20]; snprintf(msg,sizeof(msg),"GPO SW %02X%02X",gpoResponse[gpoResponseLen-2],gpoResponse[gpoResponseLen-1]); showEmvStatus(msg); return;
  }

  EmvReader::GpoResult gpo;
  if (!EmvReader::parseGpo(gpoResponse,gpoResponseLen,gpo)) { showEmvStatus("Invalid GPO data"); return; }

  EmvReader::CardData card;
  EmvReader::findCardData(gpoResponse, gpoResponseLen - 2, card);
  uint8_t recordsRead = 0;

  bool recordTransportFailed = false;
  for (uint8_t i = 0; i + 3 < gpo.aflLen && !recordTransportFailed; i += 4) {
    const uint8_t sfi = gpo.afl[i] >> 3;
    const uint8_t firstRecord = gpo.afl[i + 1];
    const uint8_t lastRecord = gpo.afl[i + 2];
    if (!sfi || sfi > 30 || !firstRecord || lastRecord < firstRecord) continue;

    for (uint16_t record = firstRecord; record <= lastRecord; ++record) {
      uint8_t rrApdu[5] = {};
      if (!EmvReader::buildReadRecord((uint8_t)record, sfi, rrApdu)) break;

      uint8_t rrResponse[220] = {};
      uint8_t rrResponseLen = sizeof(rrResponse);
      if (!emvExchangeLogged("READ_RECORD", rrApdu, sizeof(rrApdu), rrResponse, &rrResponseLen)
          || rrResponseLen < 2) {
        recordTransportFailed = true;
        break;
      }

      const uint8_t sw1 = rrResponse[rrResponseLen - 2];
      const uint8_t sw2 = rrResponse[rrResponseLen - 1];
      if (sw1 == 0x90 && sw2 == 0x00) {
        ++recordsRead;
        EmvReader::findCardData(rrResponse, rrResponseLen - 2, card);
      }
    }
  }

  _emvSummaryBrand = "";
  if (apps[selected].aidLen >= 5) {
    const uint8_t* aid = apps[selected].aid;
    if (aid[0] == 0xA0 && aid[1] == 0x00 && aid[2] == 0x00) {
      if (aid[3] == 0x00 && aid[4] == 0x03) _emvSummaryBrand = "Visa";
      else if (aid[3] == 0x00 && aid[4] == 0x04) _emvSummaryBrand = "Mastercard";
      else if (aid[3] == 0x00 && aid[4] == 0x25) _emvSummaryBrand = "American Express";
      else if (aid[3] == 0x00 && aid[4] == 0x65) _emvSummaryBrand = "JCB";
      else if (aid[3] == 0x01 && aid[4] == 0x52) _emvSummaryBrand = "Discover";
    }
  }
  _emvSummaryApplication = selectedApp.preferredName.length() ? selectedApp.preferredName :
                           (selectedApp.label.length() ? selectedApp.label : apps[selected].label);
  _emvSummaryPan = card.pan;
  _emvSummaryExpiry = card.expiry;
  _emvDetailsAid = EmvReader::hex(apps[selected].aid, apps[selected].aidLen);
  _emvDetailsAip = EmvReader::hex(gpo.aip, sizeof(gpo.aip));
  _emvDetailsAfl = EmvReader::hex(gpo.afl, gpo.aflLen);
  _emvDetailsPdol = selectedApp.pdolLen ? EmvReader::hex(selectedApp.pdol, selectedApp.pdolLen) : String();
  _emvDetailsApplications = String(appCount);
  _emvDetailsRecords = String(recordsRead);
  _emvDetailsAvailable = true;
  _state = STATE_TYPE4A_RESULT;
  _resetRows();
  if (_emvSummaryBrand.length()) _pushWrappedRow("Brand", _emvSummaryBrand);
  if (_emvSummaryApplication.length()) _pushWrappedRow("Application", _emvSummaryApplication);
  if (_emvSummaryPan.length()) _pushWrappedRow("PAN", _emvSummaryPan);
  if (_emvSummaryExpiry.length()) _pushRow("Expires", _emvSummaryExpiry);
  _pushRow("[Press]", "Details");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_showType4ADetails(bool scanAgainHint) {
  _emvResultActive = false;
  _state = STATE_TYPE4A_RESULT;
  _resetRows();
  _pushRow("Type", "Type 4A");
  _pushRow("UID", _hexBlock(_uid, _uidLen));
  char buf[20];
  snprintf(buf, sizeof(buf), "0x%04X", _atqa); _pushRow("ATQA", buf);
  snprintf(buf, sizeof(buf), "0x%02X", _sak); _pushRow("SAK", buf);
  _pushRow("ISO-DEP", "Yes");
  if (_type4AAtsLen) _pushRow("ATS", _hexBlock(_type4AAts, _type4AAtsLen));
  if (scanAgainHint && !_genericReadActive) _pushRow("[Press]", "Scan again");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doType4AReadTag() {
  if (!_genericReadActive) { renderOperationTitle("Read Tag"); renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH()); }
  const uint32_t start = millis();
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goType4ATag(); return; }
    if (_scanType4A(250)) { if (!_genericIdentityMatches(2, _uid, _uidLen)) return; _showType4ADetails(false); return; }
    delay(30);
  }
  ShowStatusAction::show("No Type 4A tag detected", 1200);
  _goType4ATag();
}

void PN532I2cScreen::_doType4ASendApdu() {
  String h = InputTextAction::popup("APDU (hex)", "00A4040000", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goType4AAdvanced(); return; }
  uint8_t tx[60] = {}; size_t txLen = 0;
  if (!_parseHexBytesI2c(h, tx, sizeof(tx), txLen)) { ShowStatusAction::show("Invalid APDU"); _goType4AAdvanced(); return; }
  renderOperationTitle("APDU Response");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  const uint32_t start = millis(); bool found = false;
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goType4AAdvanced(); return; }
    if (_scanType4A(250)) { found = true; break; }
    delay(30);
  }
  if (!found) { ShowStatusAction::show("No Type 4A tag detected", 1200); _goType4AAdvanced(); return; }
  uint8_t rx[64] = {}; size_t rxLen = 0;
  if (!_type4AExchange(tx, txLen, rx, sizeof(rx), rxLen)) { ShowStatusAction::show("APDU failed"); _goType4AAdvanced(); return; }
  _state = STATE_RAW_RESULT;
  _rawResultMifare = false; _rawResultTypeB = false; _rawResultTypeBRaw = false; _rawResultType4A = true;
  _resetRows(); _pushWrappedRow("Response", rxLen ? _hexBlock(rx, (uint8_t)rxLen) : String("(empty)"));
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount); render();
}

bool PN532I2cScreen::_type4ASelectNdef(size_t& capacity, bool& writable) {
  capacity = 0; writable = false; _type4AMLe = 48; _type4AMLc = 40; _type4ANlenSize = 2;
  uint8_t rx[64] = {}; size_t n = 0;
  static const uint8_t selAppV2[] = {0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x01,0x00};
  static const uint8_t selAppV1[] = {0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x00,0x00};
  bool appSelected = _type4AExchange(selAppV2,sizeof(selAppV2),rx,sizeof(rx),n) && n>=2 && rx[n-2]==0x90 && rx[n-1]==0x00;
  if(!appSelected) appSelected = _type4AExchange(selAppV1,sizeof(selAppV1),rx,sizeof(rx),n) && n>=2 && rx[n-2]==0x90 && rx[n-1]==0x00;
  if(!appSelected) return false;
  static const uint8_t selCc[] = {0x00,0xA4,0x00,0x0C,0x02,0xE1,0x03};
  if (!_type4AExchange(selCc,sizeof(selCc),rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0x00) return false;
  static const uint8_t readLen[] = {0x00,0xB0,0x00,0x00,0x02};
  if (!_type4AExchange(readLen,sizeof(readLen),rx,sizeof(rx),n)||n<4||rx[n-2]!=0x90||rx[n-1]!=0x00) return false;
  const size_t ccLen=((size_t)rx[0]<<8)|rx[1]; if(ccLen<15||ccLen>48) return false;
  uint8_t readCc[]={0x00,0xB0,0x00,0x00,(uint8_t)ccLen};
  if(!_type4AExchange(readCc,sizeof(readCc),rx,sizeof(rx),n)||n!=ccLen+2||rx[n-2]!=0x90||rx[n-1]!=0x00) return false;
  _type4AMLe=((size_t)rx[3]<<8)|rx[4]; _type4AMLc=((size_t)rx[5]<<8)|rx[6];
  if(!_type4AMLe || !_type4AMLc) return false;
  uint16_t fileId=0; bool found=false;
  for(size_t pos=7;pos+1<ccLen;){
    const uint8_t type=rx[pos++]; const size_t len=rx[pos++]; if(pos+len>ccLen)return false;
    if(type==0x04&&len>=6){
      fileId=(uint16_t)((rx[pos]<<8)|rx[pos+1]); const size_t fileSize=((size_t)rx[pos+2]<<8)|rx[pos+3];
      if(fileSize<2)return false; capacity=fileSize-2; writable=rx[pos+5]==0x00; _type4ANlenSize=2; found=true; break;
    }
    if(type==0x06&&len>=8){
      fileId=(uint16_t)((rx[pos]<<8)|rx[pos+1]);
      const uint32_t fileSize=((uint32_t)rx[pos+2]<<24)|((uint32_t)rx[pos+3]<<16)|((uint32_t)rx[pos+4]<<8)|rx[pos+5];
      if(fileSize<4)return false; capacity=(size_t)fileSize-4; writable=rx[pos+7]==0x00; _type4ANlenSize=4; found=true; break;
    }
    pos+=len;
  }
  if(!found)return false;
  uint8_t selFile[]={0x00,0xA4,0x00,0x0C,0x02,(uint8_t)(fileId>>8),(uint8_t)fileId};
  return _type4AExchange(selFile,sizeof(selFile),rx,sizeof(rx),n)&&n>=2&&rx[n-2]==0x90&&rx[n-1]==0x00;
}

void PN532I2cScreen::_doType4AReadNdef() {
  renderOperationTitle("Read NDEF"); renderTagPrompt("Waiting for tag...",bodyX(),bodyY(),bodyW(),bodyH());
  const uint32_t start=millis(); bool found=false;
  while(millis()-start<3000){Uni.update();if(Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){_goType4ANdef();return;}if(_scanType4A(250)){found=true;break;}delay(30);}
  if(!found){ShowStatusAction::show("No Type 4A tag detected",1200);_goType4ANdef();return;}
  size_t cap=0;bool writable=false;if(!_type4ASelectNdef(cap,writable)){ShowStatusAction::show("NDEF not found / unsupported");_goType4ANdef();return;}
  uint8_t rx[64]={};size_t n=0;uint8_t readNlen[]={0x00,0xB0,0x00,0x00,_type4ANlenSize};
  if(!_type4AExchange(readNlen,sizeof(readNlen),rx,sizeof(rx),n)||n<(size_t)_type4ANlenSize+2||rx[n-2]!=0x90||rx[n-1]!=0x00){ShowStatusAction::show("Failed");_goType4ANdef();return;}
  size_t want=0;for(uint8_t i=0;i<_type4ANlenSize;i++)want=(want<<8)|rx[i];if(want>MAX_NDEF_BYTES||want>cap){ShowStatusAction::show("NDEF too large");_goType4ANdef();return;}
  size_t off=0;while(off<want){const size_t chunk=min(min((size_t)48,_type4AMLe),want-off);const uint16_t pos=(uint16_t)(off+_type4ANlenSize);uint8_t cmd[]={0x00,0xB0,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};if(!_type4AExchange(cmd,sizeof(cmd),rx,sizeof(rx),n)||n!=chunk+2||rx[n-2]!=0x90||rx[n-1]!=0x00){ShowStatusAction::show("Failed");_goType4ANdef();return;}memcpy(_ndefBuf+off,rx,chunk);off+=chunk;}
  _ndefLen=want;_ndefCapacity=cap;_hasNdef=true;_ndefTarget=NDEF_TARGET_TYPE_4A;_showNdefResult(_uid,_uidLen,_ndefBuf,_ndefLen);
}

bool PN532I2cScreen::_writeType4ANdefRecord(const uint8_t* ndef,size_t ndefLen) {
  if((!ndef&&ndefLen)||ndefLen>MAX_NDEF_BYTES||ndefLen>0xFFFFU){ShowStatusAction::show("NDEF too large");return false;}
  renderOperationTitle("Write NDEF");renderTagPrompt("Waiting for tag...",bodyX(),bodyY(),bodyW(),bodyH());
  const uint32_t start=millis();bool found=false;while(millis()-start<3000){Uni.update();if(Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK)return false;if(_scanType4A(250)){found=true;break;}delay(30);}
  if(!found){ShowStatusAction::show("No Type 4A tag detected",1200);return false;}
  size_t cap=0;bool writable=false;if(!_type4ASelectNdef(cap,writable)){ShowStatusAction::show("NDEF not found / unsupported");return false;}if(!writable){ShowStatusAction::show("NDEF is read-only");return false;}if(ndefLen>cap){ShowStatusAction::show("NDEF too large");return false;}
  uint8_t rx[64]={};size_t n=0;uint8_t zero[9]={0x00,0xD6,0x00,0x00,_type4ANlenSize};if(!_type4AExchange(zero,5+_type4ANlenSize,rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0x00)return false;
  size_t off=0;while(off<ndefLen){const size_t chunk=min(min((size_t)40,_type4AMLc),ndefLen-off);const uint16_t pos=(uint16_t)(off+_type4ANlenSize);uint8_t cmd[45]={0x00,0xD6,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};memcpy(cmd+5,ndef+off,chunk);if(!_type4AExchange(cmd,5+chunk,rx,sizeof(rx),n)||n<2||rx[n-2]!=0x90||rx[n-1]!=0x00)return false;off+=chunk;}
  uint8_t lenCmd[9]={0x00,0xD6,0x00,0x00,_type4ANlenSize};size_t v=ndefLen;for(int i=(int)_type4ANlenSize-1;i>=0;i--){lenCmd[5+i]=(uint8_t)v;v>>=8;}const bool ok=_type4AExchange(lenCmd,5+_type4ANlenSize,rx,sizeof(rx),n)&&n>=2&&rx[n-2]==0x90&&rx[n-1]==0x00;if(ok){_ndefCapacity=cap;ShowStatusAction::show("NDEF written");}else ShowStatusAction::show("Failed");return ok;
}

void PN532I2cScreen::_doType4AEraseNdef(){const uint8_t empty=0;const bool ok=_writeType4ANdefRecord(&empty,0);if(ok)ShowStatusAction::show("NDEF erased");_goType4ANdef();}
void PN532I2cScreen::_doType4AFormatNdef(){ShowStatusAction::show("Requires provisioned Type 4 NDEF",2000);_goType4ANdef();}

static uint16_t _felicaAttrChecksum(const uint8_t attr[16]) { uint16_t s=0; for(uint8_t i=0;i<14;i++) s=(uint16_t)(s+attr[i]); return s; }

bool PN532I2cScreen::_scanFelica(uint32_t timeoutMs) {
  if(!_nfc||!_wire) return false; _hasFelica=false;
  const uint8_t cmd[]={PN532_COMMAND_INLISTPASSIVETARGET,0x01,0x01,0xFF,0xFF,0x01,0x00};
  uint8_t rsp[48]={}; size_t n=0;
  if(!_nfcCommandResponse(_nfc,_wire,cmd,sizeof(cmd),(uint8_t)(PN532_COMMAND_INLISTPASSIVETARGET+1),rsp,sizeof(rsp),n,timeoutMs)) return false;
  if(n<18||rsp[0]==0) return false;
  _felicaTg=rsp[1]; memcpy(_felicaIdm,rsp+2,8); memcpy(_felicaPmm,rsp+10,8);
  _felicaSystemCode=(n>=20)?(uint16_t)((rsp[18]<<8)|rsp[19]):0xFFFF; _hasFelica=true; return true;
}

bool PN532I2cScreen::_felicaExchange(const uint8_t* body,size_t bodyLen,uint8_t* rx,size_t rxCap,size_t& rxLen){
  rxLen=0;if(!_hasFelica||!body||!bodyLen||bodyLen>58||!rx||!rxCap)return false;
  uint8_t cmd[64]={PN532_COMMAND_INDATAEXCHANGE,_felicaTg}; cmd[2]=(uint8_t)(bodyLen+1); memcpy(cmd+3,body,bodyLen);
  uint8_t rsp[68]={};size_t n=0;if(!_nfcCommandResponse(_nfc,_wire,cmd,(uint8_t)(bodyLen+3),PN532_RESPONSE_INDATAEXCHANGE,rsp,sizeof(rsp),n,1200))return false;
  if(n<2||(rsp[0]&0x3F)!=0)return false; const size_t dl=n-1;if(dl>rxCap)return false;memcpy(rx,rsp+1,dl);rxLen=dl;return dl>=2&&rx[0]==dl;
}

bool PN532I2cScreen::_felicaRequestSystemCodes(uint16_t* systems,size_t maxSystems,size_t& count){count=0;uint8_t b[9]={0x0C};memcpy(b+1,_felicaIdm,8);uint8_t r[64];size_t n=0;if(!_felicaExchange(b,sizeof(b),r,sizeof(r),n)||n<11||r[1]!=0x0D)return false;size_t c=r[10];if(11+c*2>n)return false;count=min(c,maxSystems);for(size_t i=0;i<count;i++)systems[i]=(uint16_t)((r[11+i*2]<<8)|r[12+i*2]);return true;}
bool PN532I2cScreen::_felicaSearchService(uint16_t idx,uint16_t& svc){uint8_t b[11]={0x0A};memcpy(b+1,_felicaIdm,8);b[9]=(uint8_t)idx;b[10]=(uint8_t)(idx>>8);uint8_t r[32];size_t n=0;if(!_felicaExchange(b,sizeof(b),r,sizeof(r),n)||n<12||r[1]!=0x0B)return false;svc=(uint16_t)(r[10]|(r[11]<<8));return svc!=0xFFFF;}
bool PN532I2cScreen::_felicaRequestService(uint16_t svc,uint16_t& keyVersion){uint8_t b[12]={0x02};memcpy(b+1,_felicaIdm,8);b[9]=1;b[10]=(uint8_t)svc;b[11]=(uint8_t)(svc>>8);uint8_t r[32];size_t n=0;if(!_felicaExchange(b,sizeof(b),r,sizeof(r),n)||n<13||r[1]!=0x03||r[10]!=1)return false;keyVersion=(uint16_t)(r[11]|(r[12]<<8));return true;}
bool PN532I2cScreen::_felicaReadBlock(uint16_t svc,uint16_t block,uint8_t data[16]){if(block>0xFF)return false;uint8_t b[15]={0x06};memcpy(b+1,_felicaIdm,8);b[9]=1;b[10]=(uint8_t)svc;b[11]=(uint8_t)(svc>>8);b[12]=1;b[13]=0x80;b[14]=(uint8_t)block;uint8_t r[48];size_t n=0;if(!_felicaExchange(b,sizeof(b),r,sizeof(r),n)||n<29||r[1]!=0x07||r[10]||r[11]||r[12]!=1)return false;memcpy(data,r+13,16);return true;}
bool PN532I2cScreen::_felicaWriteBlock(uint16_t svc,uint16_t block,const uint8_t data[16]){if(block>0xFF)return false;uint8_t b[31]={0x08};memcpy(b+1,_felicaIdm,8);b[9]=1;b[10]=(uint8_t)svc;b[11]=(uint8_t)(svc>>8);b[12]=1;b[13]=0x80;b[14]=(uint8_t)block;memcpy(b+15,data,16);uint8_t r[32];size_t n=0;return _felicaExchange(b,sizeof(b),r,sizeof(r),n)&&n>=12&&r[1]==0x09&&!r[10]&&!r[11];}
bool PN532I2cScreen::_felicaReadNdef(uint8_t*out,size_t max,size_t&len,size_t&cap){len=cap=0;uint8_t a[16];if(!_felicaReadBlock(0x000B,0,a)||_felicaAttrChecksum(a)!=((uint16_t)a[14]<<8|a[15])||a[9]!=0x00)return false;cap=(((size_t)a[3]<<8)|a[4])*16U;len=((size_t)a[11]<<16)|((size_t)a[12]<<8)|a[13];if(len>cap||len>max)return false;size_t off=0;for(uint16_t b=1;off<len;b++){uint8_t d[16];if(!_felicaReadBlock(0x000B,b,d))return false;size_t take=min((size_t)16,len-off);memcpy(out+off,d,take);off+=take;}return true;}
bool PN532I2cScreen::_felicaWriteNdef(const uint8_t*ndef,size_t len,size_t&cap){cap=0;uint8_t a[16];if(!_felicaReadBlock(0x0009,0,a)||_felicaAttrChecksum(a)!=((uint16_t)a[14]<<8|a[15])||a[10]!=1)return false;cap=(((size_t)a[3]<<8)|a[4])*16U;if(len>cap)return false;uint8_t w[16];memcpy(w,a,16);w[9]=0x0F;w[11]=w[12]=w[13]=0;uint16_t s=_felicaAttrChecksum(w);w[14]=s>>8;w[15]=s;if(!_felicaWriteBlock(0x0009,0,w))return false;size_t off=0;for(uint16_t b=1;off<len;b++){memset(w,0,16);size_t take=min((size_t)16,len-off);memcpy(w,ndef+off,take);if(!_felicaWriteBlock(0x0009,b,w))return false;off+=take;}memcpy(w,a,16);w[9]=0;w[11]=len>>16;w[12]=len>>8;w[13]=len;s=_felicaAttrChecksum(w);w[14]=s>>8;w[15]=s;return _felicaWriteBlock(0x0009,0,w);}

void PN532I2cScreen::_doFelicaReadTag(){if (!_genericReadActive) { renderOperationTitle("Read Tag"); renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH()); }if(!_scanFelica(2500)){ShowStatusAction::show("FeliCa tag not detected");_goFelicaTag();return;}if(!_genericIdentityMatches(5,_felicaIdm,8))return;_operationResultTitle="Tag Details";_felicaResultReturn=STATE_FELICA_TAG_MENU;_state=STATE_FELICA_RESULT;_resetRows();_pushRow("Type","FeliCa / NFC-F");_pushRow("IDm",_hexBlock(_felicaIdm,8));_pushRow("PMm",_hexBlock(_felicaPmm,8));char sc[5];snprintf(sc,sizeof(sc),"%04X",_felicaSystemCode);_pushRow("System",sc);_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();}
void PN532I2cScreen::_showFelicaHex(const char* title,const uint8_t* data,size_t len,State_e returnState){_operationResultTitle=title;_felicaResultReturn=returnState;_state=STATE_FELICA_RESULT;_resetRows();_pushWrappedRow(title,len?_hexBlock(data,(uint8_t)min(len,(size_t)255)):String("(empty)"));_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();}
void PN532I2cScreen::_doFelicaTagAction(uint8_t i){if(i==0){_doFelicaReadTag();return;}if(i==1){_goFelicaSystems();return;}if(i==2){_goFelicaServices();return;}_goFelicaAdvanced();}
void PN532I2cScreen::_doFelicaSystemAction(uint8_t){if(!_scanFelica(1200)){ShowStatusAction::show("FeliCa tag not detected");_goFelicaSystems();return;}uint16_t sys[16];size_t n=0;if(!_felicaRequestSystemCodes(sys,16,n)){ShowStatusAction::show("Request System Code failed");_goFelicaSystems();return;}_operationResultTitle="Systems";_felicaResultReturn=STATE_FELICA_SYSTEMS_MENU;_state=STATE_FELICA_RESULT;_resetRows();for(size_t k=0;k<n;k++){char v[7];snprintf(v,sizeof(v),"0x%04X",sys[k]);String l="System "+String((unsigned)k);_pushRow(l.c_str(),v);}_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();}
void PN532I2cScreen::_doFelicaServiceAction(uint8_t i){if(!_scanFelica(1200)){ShowStatusAction::show("FeliCa tag not detected");_goFelicaServices();return;}if(i==0){_operationResultTitle="Services";_felicaResultReturn=STATE_FELICA_SERVICES_MENU;_state=STATE_FELICA_RESULT;_resetRows();for(uint16_t k=0;k<32&&_rowCount<MAX_ROWS;k++){uint16_t svc;if(!_felicaSearchService(k,svc))break;char v[7];snprintf(v,sizeof(v),"0x%04X",svc);String l="Service "+String(k);_pushRow(l.c_str(),v);}_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();return;}String sh=InputTextAction::popup("Service code (4 hex)","000B",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goFelicaServices();return;}uint8_t sb[2];size_t sl=0;if(!_parseHexBytesI2c(sh,sb,2,sl)||sl!=2){ShowStatusAction::show("Need 4 hex chars");_goFelicaServices();return;}uint16_t svc=(uint16_t)((sb[0]<<8)|sb[1]);if(i==2){uint16_t kv=0;if(!_felicaRequestService(svc,kv)){ShowStatusAction::show("Failed");_goFelicaServices();return;}_operationResultTitle="Service Details";_felicaResultReturn=STATE_FELICA_SERVICES_MENU;_state=STATE_FELICA_RESULT;_resetRows();char sc[7],ks[7];snprintf(sc,sizeof(sc),"0x%04X",svc);snprintf(ks,sizeof(ks),"0x%04X",kv);_pushRow("Service Code",sc);_pushRow("Key Version",ks);_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();return;}int blk=InputNumberAction::popup("Block",0,255,0);if(InputNumberAction::wasCancelled()){_goFelicaServices();return;}uint8_t d[16];if(!_felicaReadBlock(svc,(uint16_t)blk,d)){ShowStatusAction::show("Read failed");_goFelicaServices();return;}_showFelicaHex("Service Data",d,16,STATE_FELICA_SERVICES_MENU);}
void PN532I2cScreen::_doFelicaAdvancedAction(uint8_t i){if(!_scanFelica(1200)){ShowStatusAction::show("FeliCa tag not detected");_goFelicaAdvanced();return;}if(i==2){String h=InputTextAction::popup("FeliCa command (hex)","",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goFelicaAdvanced();return;}uint8_t b[60],r[64];size_t bl=0,n=0;if(!_parseHexBytesI2c(h,b,sizeof(b),bl)||!bl){ShowStatusAction::show("Invalid hex data");_goFelicaAdvanced();return;}if(!_felicaExchange(b,bl,r,sizeof(r),n)){ShowStatusAction::show("Command failed");_goFelicaAdvanced();return;}_showFelicaHex("Raw Response",r,n,STATE_FELICA_ADVANCED_MENU);return;}String sh=InputTextAction::popup("Service code (4 hex)",i==0?"000B":"0009",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goFelicaAdvanced();return;}uint8_t sb[2];size_t sl=0;if(!_parseHexBytesI2c(sh,sb,2,sl)||sl!=2){ShowStatusAction::show("Need 4 hex chars");_goFelicaAdvanced();return;}uint16_t svc=(uint16_t)((sb[0]<<8)|sb[1]);int blk=InputNumberAction::popup("Block",0,255,0);if(InputNumberAction::wasCancelled()){_goFelicaAdvanced();return;}uint8_t d[16];if(!_felicaReadBlock(svc,(uint16_t)blk,d)){ShowStatusAction::show("Read failed");_goFelicaAdvanced();return;}if(i==0){_showFelicaHex("Memory",d,16,STATE_FELICA_ADVANCED_MENU);return;}String h=InputTextAction::popup("Block data (32 hex)",_hexBlock(d,16).c_str(),InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goFelicaAdvanced();return;}size_t dl=0;if(!_parseHexBytesI2c(h,d,16,dl)||dl!=16){ShowStatusAction::show("Need 32 hex chars");_goFelicaAdvanced();return;}ShowStatusAction::show(_felicaWriteBlock(svc,(uint16_t)blk,d)?"Block written":"Write failed");_goFelicaAdvanced();}
void PN532I2cScreen::_doFelicaReadNdef(){if(!_scanFelica(1200)){ShowStatusAction::show("FeliCa tag not detected");_goFelicaNdef();return;}size_t n=0,cap=0;if(!_felicaReadNdef(_ndefBuf,sizeof(_ndefBuf),n,cap)){ShowStatusAction::show("NDEF not found / unsupported");_goFelicaNdef();return;}_ndefLen=n;_ndefCapacity=cap;_hasNdef=true;_ndefTarget=NDEF_TARGET_FELICA;_showNdefResult(_felicaIdm,8,_ndefBuf,_ndefLen);}
bool PN532I2cScreen::_writeFelicaNdefRecord(const uint8_t* ndef,size_t len){if(!_scanFelica(1200)){ShowStatusAction::show("FeliCa tag not detected");return false;}size_t cap=0;bool ok=_felicaWriteNdef(ndef,len,cap);if(ok)_ndefCapacity=cap;ShowStatusAction::show(ok?"NDEF written":"NDEF write failed");return ok;}
void PN532I2cScreen::_doFelicaEraseNdef(){_ndefTarget=NDEF_TARGET_FELICA;_writeFelicaNdefRecord(nullptr,0);_goFelicaNdef();}
void PN532I2cScreen::_doFelicaFormatNdef(){ShowStatusAction::show("Requires provisioned Type 3 NDEF");_goFelicaNdef();}

bool PN532I2cScreen::_scanType1(uint32_t timeoutMs) {
  if (!_nfc || !_wire) return false;
  _hasType1 = false;
  const uint8_t cmd[] = {PN532_COMMAND_INLISTPASSIVETARGET, 0x01, 0x04};
  uint8_t rsp[24] = {}; size_t n = 0;
  if (!_nfcCommandResponse(_nfc, _wire, cmd, sizeof(cmd),
      (uint8_t)(PN532_COMMAND_INLISTPASSIVETARGET + 1), rsp, sizeof(rsp), n, timeoutMs)) return false;
  if (n < 8 || rsp[0] == 0) return false;
  _type1Tg = rsp[1];
  memcpy(_type1SensRes, rsp + 2, 2);
  memcpy(_type1JewelId, rsp + 4, 4);
  _hasType1 = true;
  return true;
}

bool PN532I2cScreen::_type1Exchange(const uint8_t* tx, size_t txLen,
                                    uint8_t* rx, size_t rxCap, size_t& rxLen) {
  rxLen = 0;
  if (!_hasType1 || !tx || !txLen || txLen > 60 || !rx || !rxCap) return false;
  uint8_t cmd[64] = {PN532_COMMAND_INDATAEXCHANGE, _type1Tg};
  memcpy(cmd + 2, tx, txLen);
  uint8_t rsp[68] = {}; size_t n = 0;
  if (!_nfcCommandResponse(_nfc, _wire, cmd, (uint8_t)(txLen + 2),
      PN532_RESPONSE_INDATAEXCHANGE, rsp, sizeof(rsp), n, 1000)) return false;
  if (n < 1 || (rsp[0] & 0x3F) != 0) return false;
  const size_t dl = n - 1;
  if (dl > rxCap) return false;
  if (dl) memcpy(rx, rsp + 1, dl);
  rxLen = dl;
  return true;
}

bool PN532I2cScreen::_type1Rid(uint8_t hr[2], uint8_t uid[4]) {
  const uint8_t tx[7] = {0x78, 0, 0, 0, 0, 0, 0};
  uint8_t rx[12] = {}; size_t n = 0;
  if (!_type1Exchange(tx, sizeof(tx), rx, sizeof(rx), n) || n < 6) return false;
  memcpy(hr, rx, 2); memcpy(uid, rx + 2, 4);
  memcpy(_type1Hr, hr, 2);
  memcpy(_type1JewelId, uid, 4);
  return true;
}

bool PN532I2cScreen::_type1ReadByte(uint8_t address, uint8_t& value) {
  uint8_t tx[7] = {0x01, address, 0};
  memcpy(tx + 3, _type1JewelId, 4);
  uint8_t rx[8] = {}; size_t n = 0;
  if (!_type1Exchange(tx, sizeof(tx), rx, sizeof(rx), n)) return false;
  if (n >= 2 && rx[0] == address) { value = rx[1]; return true; }
  if (n == 1) { value = rx[0]; return true; }
  return false;
}

bool PN532I2cScreen::_type1ReadStatic(uint8_t mem[120]) {
  if (!_hasType1 || !mem) return false;
  uint8_t hr[2], uid[4];
  if (!_type1Rid(hr, uid)) return false;
  for (uint8_t a = 0; a < 120; ++a) if (!_type1ReadByte(a, mem[a])) return false;
  return true;
}

void PN532I2cScreen::_doType1ReadTag() {
  if (!_genericReadActive) { renderOperationTitle("Read Tag"); renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH()); }
  if (!_scanType1(2500)) { ShowStatusAction::show("Type 1 tag not detected"); _goType1Tag(); return; }
  if (!_genericIdentityMatches(6, _type1JewelId, 4)) return;
  uint8_t hr[2] = {}, uid[4] = {};
  if (!_type1Rid(hr, uid)) { ShowStatusAction::show("RID failed"); _goType1Tag(); return; }
  _state = STATE_TYPE1_RESULT; _resetRows();
  _pushRow("Type", "NFC Forum Type 1 / Jewel");
  _pushRow("SENS_RES", _hexBlock(_type1SensRes, 2));
  _pushRow("HR", _hexBlock(hr, 2));
  _pushRow("Jewel ID", _hexBlock(uid, 4));
  const char* model = "Unknown";
  if (hr[0] == 0x11) model = "Topaz96 (static)";
  else if (hr[0] == 0x12) model = "Topaz512 (dynamic, limited)";
  else if ((hr[0] & 0xF0) == 0x10) model = "Type 1 (unknown model)";
  _pushRow("Model", model);
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount); render();
}

void PN532I2cScreen::_doType1ReadMemory() {
  if (!_scanType1(1500)) { ShowStatusAction::show("Type 1 tag not detected"); _goType1Tag(); return; }
  uint8_t mem[120] = {};
  if (!_type1ReadStatic(mem)) { ShowStatusAction::show("Memory read failed"); _goType1Tag(); return; }
  _state = STATE_TYPE1_RESULT; _resetRows();
  for (uint8_t block = 0; block < 15; ++block) {
    char label[12]; snprintf(label, sizeof(label), "Block %02X", block);
    _pushRow(label, _hexBlock(mem + block * 8, 8));
  }
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount); render();
}

void PN532I2cScreen::_doType1ReadNdef() {
  if (!_scanType1(1500)) { ShowStatusAction::show("Type 1 tag not detected"); _goType1Ndef(); return; }
  uint8_t mem[120] = {};
  if (!_type1ReadStatic(mem)) { ShowStatusAction::show("Memory read failed"); _goType1Ndef(); return; }
  if ((_type1Hr[0] & 0xF0) != 0x10 || mem[8] != 0xE1 || (mem[9] >> 4) != 1) {
    ShowStatusAction::show("NDEF not formatted"); _goType1Ndef(); return;
  }
  size_t physical = ((size_t)mem[10] + 1U) * 8U;
  if (physical > 120 || _type1Hr[0] == 0x12) { ShowStatusAction::show("Dynamic Type 1 NDEF unsupported"); _goType1Ndef(); return; }
  const size_t userEnd = min(physical, (size_t)104);
  size_t p = 12, end = userEnd; bool found = false; size_t nlen = 0, dataOff = 0;
  while (p < end) {
    uint8_t t = mem[p++];
    if (t == 0x00) continue;
    if (t == 0xFE) break;
    if (p >= end) break;
    size_t l = mem[p++];
    if (l == 0xFF) { if (p + 1 >= end) break; l = ((size_t)mem[p] << 8) | mem[p + 1]; p += 2; }
    if (p + l > end) break;
    if (t == 0x03) { found = true; nlen = l; dataOff = p; break; }
    p += l;
  }
  if (!found || nlen > sizeof(_ndefBuf)) { ShowStatusAction::show("NDEF not found / too large"); _goType1Ndef(); return; }
  memcpy(_ndefBuf, mem + dataOff, nlen); _ndefLen = nlen; _ndefCapacity = end > dataOff ? end - dataOff : 0; _hasNdef = true; _ndefTarget = NDEF_TARGET_TYPE_1;
  _showNdefResult(_type1JewelId, 4, _ndefBuf, _ndefLen);
}

static void _desfireRotateLeft16(const uint8_t in[16], uint8_t out[16]) { memcpy(out,in+1,15); out[15]=in[0]; }
static bool _desfireAesCbc(const uint8_t key[16], bool enc, const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len) {
  if(!key||!iv||!in||!out||!len||(len&15U)) return false; mbedtls_aes_context a; mbedtls_aes_init(&a);
  int rc=enc?mbedtls_aes_setkey_enc(&a,key,128):mbedtls_aes_setkey_dec(&a,key,128); if(rc){mbedtls_aes_free(&a);return false;}
  uint8_t w[16];memcpy(w,iv,16);rc=mbedtls_aes_crypt_cbc(&a,enc?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,len,w,in,out);mbedtls_aes_free(&a);return rc==0;
}

bool PN532I2cScreen::_desfireExchange(uint8_t ins,const uint8_t* data,size_t dataLen,uint8_t* out,size_t outMax,size_t& outLen,uint8_t& status){
  outLen=0;status=0xFF;if(dataLen>52)return false;uint8_t apdu[60]={0x90,ins,0,0,(uint8_t)dataLen};if(dataLen)memcpy(apdu+5,data,dataLen);size_t al=5+dataLen;apdu[al++]=0;
  for(uint8_t frame=0;frame<16;frame++){uint8_t rx[64]={};size_t n=0;if(!_type4AExchange(apdu,al,rx,sizeof(rx),n)||n<2||rx[n-2]!=0x91)return false;size_t pl=n-2;if(out&&pl){if(outLen+pl>outMax)return false;memcpy(out+outLen,rx,pl);outLen+=pl;}status=rx[n-1];if(status!=0xAF)return status==0x00;apdu[0]=0x90;apdu[1]=0xAF;apdu[2]=apdu[3]=apdu[4]=apdu[5]=0;al=6;}return false;
}

bool PN532I2cScreen::_desfireProbe(){uint8_t v[96]={};size_t n=0;uint8_t st=0xFF;return _desfireExchange(0x60,nullptr,0,v,sizeof(v),n,st)&&st==0&&n>=7;}
bool PN532I2cScreen::_desfireSelectCurrentAid(){if(!_desfireAidSelected)return true;uint8_t o[4];size_t n=0;uint8_t st;return _desfireExchange(0x5A,_desfireAid,3,o,sizeof(o),n,st);}

bool PN532I2cScreen::_desfireAuthenticateAes(uint8_t keyNo,const uint8_t key[16]){
  uint8_t rx[64]={};size_t n=0;const uint8_t first[]={0x90,0xAA,0x00,0x00,0x01,keyNo,0x00};
  if(!_type4AExchange(first,sizeof(first),rx,sizeof(rx),n)||n!=18||rx[16]!=0x91||rx[17]!=0xAF)return false;
  uint8_t iv0[16]={},rndB[16],rndBp[16];if(!_desfireAesCbc(key,false,iv0,rx,rndB,16))return false;_desfireRotateLeft16(rndB,rndBp);
  uint8_t rndA[16];for(uint8_t i=0;i<16;i+=4){uint32_t r=esp_random();memcpy(rndA+i,&r,4);}uint8_t plain[32];memcpy(plain,rndA,16);memcpy(plain+16,rndBp,16);uint8_t enc[32],iv1[16];memcpy(iv1,rx,16);if(!_desfireAesCbc(key,true,iv1,plain,enc,32))return false;
  uint8_t apdu[38]={0x90,0xAF,0,0,0x20};memcpy(apdu+5,enc,32);apdu[37]=0;if(!_type4AExchange(apdu,sizeof(apdu),rx,sizeof(rx),n)||n!=18||rx[16]!=0x91||rx[17]!=0)return false;
  uint8_t iv2[16],got[16],expected[16];memcpy(iv2,enc+16,16);if(!_desfireAesCbc(key,false,iv2,rx,got,16))return false;_desfireRotateLeft16(rndA,expected);return memcmp(got,expected,16)==0;
}

void PN532I2cScreen::_showDesfireHex(const char* title,const uint8_t* data,size_t len){_operationResultTitle=title;_state=STATE_DESFIRE_RESULT;_resetRows();_pushWrappedRow(title,len?_hexBlock(data,(uint8_t)min(len,(size_t)255)):String("(empty)"));_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();}

void PN532I2cScreen::_doDesfireReadTag(){renderOperationTitle("Read Tag");renderTagPrompt("Waiting for tag...",bodyX(),bodyY(),bodyW(),bodyH());uint32_t t=millis();bool f=false;while(millis()-t<3000){Uni.update();if(_scanType4A(250)){f=true;break;}delay(30);}if(!f||!_desfireProbe()){ShowStatusAction::show("DESFire tag not detected",1400);_goDesfire();return;}uint8_t v[96]={};size_t n=0;uint8_t st;if(!_desfireExchange(0x60,nullptr,0,v,sizeof(v),n,st)){ShowStatusAction::show("GetVersion failed");_goDesfire();return;}_operationResultTitle="Tag Details";_desfireResultReturn=STATE_DESFIRE_MENU;_state=STATE_DESFIRE_RESULT;_resetRows();_pushRow("Type","MIFARE DESFire");_pushRow("UID",_hexBlock(_uid,_uidLen));_pushRow("GetVersion",String((unsigned)n)+" bytes");if(n>=7){_pushRow("HW vendor",String(v[0],HEX));_pushRow("HW type",String(v[1],HEX));_pushRow("HW version",String(v[3])+"."+String(v[4]));}_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();}

void PN532I2cScreen::_doDesfireAppAction(uint8_t index){if(!_scanType4A(700)||!_desfireProbe()){ShowStatusAction::show("DESFire tag not detected");_goDesfireApps();return;}if(_desfireAidSelected&&(_desfireAidUidLen!=_uidLen||memcmp(_desfireAidUid,_uid,_uidLen)!=0)){_desfireAidSelected=false;_desfireAidUidLen=0;}uint8_t out[192]={};size_t n=0;uint8_t st=0xFF;if(index==0){if(!_desfireExchange(0x6A,nullptr,0,out,sizeof(out),n,st)){ShowStatusAction::show("Failed");_goDesfireApps();return;}_operationResultTitle="Applications";_desfireResultReturn=STATE_DESFIRE_APPS_MENU;_state=STATE_DESFIRE_RESULT;_resetRows();for(size_t i=0;i+2<n&&_rowCount<MAX_ROWS;i+=3){String l="AID "+String((unsigned)(i/3));char v[7];snprintf(v,sizeof(v),"%02X%02X%02X",out[i],out[i+1],out[i+2]);_pushRow(l.c_str(),v);}_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();return;}if(index==1){String h=InputTextAction::popup("Application AID (6 hex)",_desfireAidSelected?_hexBlock(_desfireAid,3).c_str():"",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goDesfireApps();return;}size_t al=0;if(!_parseHexBytesI2c(h,_desfireAid,3,al)||al!=3||!_desfireExchange(0x5A,_desfireAid,3,out,sizeof(out),n,st)){_desfireAidSelected=false;ShowStatusAction::show("Failed");_goDesfireApps();return;}_desfireAidSelected=true;_desfireAidUidLen=_uidLen;memcpy(_desfireAidUid,_uid,_uidLen);ShowStatusAction::show("Application selected");_goDesfireApps();return;}if(!_desfireSelectCurrentAid()||!_desfireExchange(0x45,nullptr,0,out,sizeof(out),n,st)){ShowStatusAction::show("Failed");_goDesfireApps();return;}_desfireResultReturn=STATE_DESFIRE_APPS_MENU;_showDesfireHex("Application Details",out,n);}

void PN532I2cScreen::_doDesfireFileAction(uint8_t index){if(!_desfireAidSelected){ShowStatusAction::show("Select application first");_goDesfireFiles();return;}if(!_scanType4A(700)||!_desfireProbe()){ShowStatusAction::show("DESFire tag not detected");_goDesfireFiles();return;}if(_desfireAidUidLen!=_uidLen||memcmp(_desfireAidUid,_uid,_uidLen)!=0){_desfireAidSelected=false;_desfireAidUidLen=0;ShowStatusAction::show("Select application for this tag");_goDesfireFiles();return;}if(!_desfireSelectCurrentAid()){ShowStatusAction::show("Selected application unavailable");_goDesfireFiles();return;}uint8_t out[192]={};size_t n=0;uint8_t st=0xFF;if(index==0){if(!_desfireExchange(0x6F,nullptr,0,out,sizeof(out),n,st)){ShowStatusAction::show("Failed");_goDesfireFiles();return;}_desfireResultReturn=STATE_DESFIRE_FILES_MENU;_showDesfireHex("File List",out,n);return;}int fileNo=InputNumberAction::popup("File number",0,31,0);if(InputNumberAction::wasCancelled()){_goDesfireFiles();return;}if(index==3){uint8_t f=(uint8_t)fileNo;if(!_desfireExchange(0xF5,&f,1,out,sizeof(out),n,st)){ShowStatusAction::show("Failed");_goDesfireFiles();return;}_desfireResultReturn=STATE_DESFIRE_FILES_MENU;_showDesfireHex("File Details",out,n);return;}uint8_t f=(uint8_t)fileNo;if(!_desfireExchange(0xF5,&f,1,out,sizeof(out),n,st)||n<1){ShowStatusAction::show("File settings failed");_goDesfireFiles();return;}if(out[0]!=0x00&&out[0]!=0x01){ShowStatusAction::show("Unsupported file type");_goDesfireFiles();return;}if(n<7){ShowStatusAction::show("Invalid file settings");_goDesfireFiles();return;}const uint32_t fileSize=(uint32_t)out[4]|((uint32_t)out[5]<<8)|((uint32_t)out[6]<<16);if(index==1){int off=InputNumberAction::popup("Offset",0,0xFFFF,0);if(InputNumberAction::wasCancelled()){_goDesfireFiles();return;}if((uint32_t)off>=fileSize){ShowStatusAction::show("Offset outside file");_goDesfireFiles();return;}const int maxRead=(int)min((uint32_t)48,fileSize-(uint32_t)off);int len=InputNumberAction::popup("Length",1,maxRead,min(16,maxRead));if(InputNumberAction::wasCancelled()){_goDesfireFiles();return;}uint8_t q[7]={(uint8_t)fileNo,(uint8_t)off,(uint8_t)(off>>8),(uint8_t)(off>>16),(uint8_t)len,(uint8_t)(len>>8),(uint8_t)(len>>16)};if(!_desfireExchange(0xBD,q,7,out,sizeof(out),n,st)){ShowStatusAction::show("Failed");_goDesfireFiles();return;}_desfireResultReturn=STATE_DESFIRE_FILES_MENU;_showDesfireHex("File Data",out,n);return;}String h=InputTextAction::popup("File data (hex)","",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goDesfireFiles();return;}uint8_t data[40];size_t dl=0;if(!_parseHexBytesI2c(h,data,sizeof(data),dl)){ShowStatusAction::show("Max 40 bytes / invalid hex");_goDesfireFiles();return;}int off=InputNumberAction::popup("Offset",0,0xFFFF,0);if(InputNumberAction::wasCancelled()){_goDesfireFiles();return;}if((uint32_t)off>fileSize||dl>fileSize-(uint32_t)off){ShowStatusAction::show("Write outside file");_goDesfireFiles();return;}uint8_t q[47]={(uint8_t)fileNo,(uint8_t)off,(uint8_t)(off>>8),(uint8_t)(off>>16),(uint8_t)dl,(uint8_t)(dl>>8),(uint8_t)(dl>>16)};memcpy(q+7,data,dl);bool ok=_desfireExchange(0x3D,q,7+dl,out,sizeof(out),n,st);ShowStatusAction::show(ok?"File written":"Failed");_goDesfireFiles();}

void PN532I2cScreen::_doDesfireAdvancedAction(uint8_t index){if(index==1){String h=InputTextAction::popup("APDU (hex)","906000000000",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goDesfireAdvanced();return;}uint8_t tx[60];size_t tl=0;if(!_parseHexBytesI2c(h,tx,sizeof(tx),tl)||!_scanType4A(700)||!_desfireProbe()){ShowStatusAction::show("Invalid APDU / no DESFire");_goDesfireAdvanced();return;}uint8_t rx[64];size_t n=0;if(!_type4AExchange(tx,tl,rx,sizeof(rx),n)){ShowStatusAction::show("APDU failed");_goDesfireAdvanced();return;}_desfireResultReturn=STATE_DESFIRE_ADVANCED_MENU;_showDesfireHex("APDU Response",rx,n);return;}if(!_scanType4A(700)||!_desfireProbe()){ShowStatusAction::show("DESFire tag not detected");_goDesfireAdvanced();return;}if(_desfireAidSelected&&(_desfireAidUidLen!=_uidLen||memcmp(_desfireAidUid,_uid,_uidLen)!=0)){_desfireAidSelected=false;_desfireAidUidLen=0;}if(!_desfireSelectCurrentAid()){ShowStatusAction::show("DESFire tag not detected");_goDesfireAdvanced();return;}int keyNo=InputNumberAction::popup("AES key number",0,13,0);if(InputNumberAction::wasCancelled()){_goDesfireAdvanced();return;}String h=InputTextAction::popup("AES-128 key (32 hex)","00000000000000000000000000000000",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_goDesfireAdvanced();return;}uint8_t key[16];size_t kl=0;if(!_parseHexBytesI2c(h,key,sizeof(key),kl)||kl!=16){ShowStatusAction::show("Need 32 hex chars");_goDesfireAdvanced();return;}ShowStatusAction::show(_desfireAuthenticateAes((uint8_t)keyNo,key)?"Authenticated":"Authentication failed");_goDesfireAdvanced();}

bool PN532I2cScreen::_scanTypeB(uint32_t timeoutMs) {
  if (!_nfc || !_wire) return false;

  _hasTypeB = false;
  _typeBAttribLen = 0;
  memset(_typeBAtqb, 0, sizeof(_typeBAtqb));
  memset(_typeBAttrib, 0, sizeof(_typeBAttrib));

  if (!_typeBRfConfigured) {
    uint8_t dummy[4] = {};
    size_t dummyLen = 0;

    const uint8_t retriesCmd[] = {PN532_COMMAND_RFCONFIGURATION, 0x05, 0xFF, 0x01, 0x00};
    if (!_nfcCommandResponse(_nfc, _wire, retriesCmd, sizeof(retriesCmd),
                             (uint8_t)(PN532_COMMAND_RFCONFIGURATION + 1),
                             dummy, sizeof(dummy), dummyLen, 300)) return false;

    const uint8_t typeBAnalogCmd[] = {PN532_COMMAND_RFCONFIGURATION, 0x0C, 0xFF, 0x10, 0x85};
    if (!_nfcCommandResponse(_nfc, _wire, typeBAnalogCmd, sizeof(typeBAnalogCmd),
                             (uint8_t)(PN532_COMMAND_RFCONFIGURATION + 1),
                             dummy, sizeof(dummy), dummyLen, 300)) return false;

    _typeBRfConfigured = true;
  }

  const uint8_t cmd[] = {PN532_COMMAND_INLISTPASSIVETARGET, 0x01, 0x03, 0x00};
  uint8_t rsp[48] = {};
  size_t n = 0;
  if (!_nfcCommandResponse(_nfc, _wire, cmd, sizeof(cmd),
                           (uint8_t)(PN532_COMMAND_INLISTPASSIVETARGET + 1),
                           rsp, sizeof(rsp), n, timeoutMs)) return false;
  if (n < 1 || rsp[0] == 0 || n < 15) return false;

  _typeBTg = rsp[1];
  memcpy(_typeBAtqb, rsp + 2, sizeof(_typeBAtqb));
  const uint8_t attribLen = rsp[14];
  if ((size_t)15 + attribLen > n || attribLen > sizeof(_typeBAttrib)) return false;
  _typeBAttribLen = attribLen;
  memset(_typeBAttrib, 0, sizeof(_typeBAttrib));
  if (attribLen) memcpy(_typeBAttrib, rsp + 15, attribLen);
  _hasTypeB = true;
  _hasCard = false;
  return true;
}

bool PN532I2cScreen::_isoATagPresent(uint32_t timeoutMs) {
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  return _nfc && _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, timeoutMs);
}

void PN532I2cScreen::_showTypeBDetails(bool scanAgainHint) {
  _state = STATE_TYPEB_RESULT;
  _resetRows();

  const uint8_t pupiOff = (_typeBAtqb[0] == 0x50) ? 1 : 0;
  _pushRow("Type", _typeBAttribLen ? "Type 4B" : "ISO14443B");
  _pushRow("PUPI / NFCID", _hexBlock(_typeBAtqb + pupiOff, 4));
  _pushRow("ISO-DEP", _typeBAttribLen ? "Yes" : "No / unknown");
  _pushRow("Protocol", "ISO14443B");
  _pushRow("ATQB", _hexBlock(_typeBAtqb, sizeof(_typeBAtqb)));
  if (_typeBAtqb[0] == 0x50) {
    _pushRow("Application Data", _hexBlock(_typeBAtqb + 5, 4));
    _pushRow("Protocol Info", _hexBlock(_typeBAtqb + 9, 3));
  }
  if (_typeBAttribLen) _pushRow("ATTRIB_RES", _hexBlock(_typeBAttrib, _typeBAttribLen));
  if (scanAgainHint) {
    _pushRow("[Press]", "Scan again");
    if (_typeBAttribLen) _pushRow("[Hold]", "Type 4B Menu");
  }

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doTypeBReadTag() {
  if (!_genericReadActive) { renderOperationTitle("Read Tag"); renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH()); }
  const uint32_t start = millis();
  bool otherTagDetected = false;
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goTypeBTag();
      return;
    }
    if (_scanTypeB(250)) {
      const uint8_t off = (_typeBAtqb[0] == 0x50) ? 1 : 0;
      if (!_genericIdentityMatches(3, _typeBAtqb + off, 4)) return;
      _typeBFromMainScan = false;
      _showTypeBDetails(false);
      return;
    }
    if (_isoATagPresent()) { otherTagDetected = true; break; }
    delay(30);
  }
  ShowStatusAction::show(otherTagDetected ? "No Type B tag detected" : "Tag not detected", 1200);
  _goTypeBTag();
}

bool PN532I2cScreen::_typeBExchange(const uint8_t* tx, size_t txLen,
                                    uint8_t* rx, size_t rxCap, size_t& rxLen) {
  rxLen = 0;
  if (!_hasTypeB || !tx || txLen == 0 || txLen > 60 || !rx || rxCap == 0) return false;
  uint8_t cmd[64] = {PN532_COMMAND_INDATAEXCHANGE, _typeBTg};
  memcpy(cmd + 2, tx, txLen);
  uint8_t rsp[68] = {};
  size_t n = 0;
  if (!_nfcCommandResponse(_nfc, _wire, cmd, (uint8_t)(txLen + 2),
                           PN532_RESPONSE_INDATAEXCHANGE,
                           rsp, sizeof(rsp), n, 1200)) return false;
  if (n < 1 || (rsp[0] & 0x3FU) != 0) return false;
  const size_t dataLen = n - 1;
  if (dataLen > rxCap) return false;
  if (dataLen) memcpy(rx, rsp + 1, dataLen);
  rxLen = dataLen;
  return true;
}

void PN532I2cScreen::_doTypeBSendApdu(bool rawMode) {
  const char* titleText = rawMode ? "Raw command (hex)" : "APDU (hex)";
  const char* initial = rawMode ? "" : "00A4040000";
  String h = InputTextAction::popup(titleText, initial, InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goTypeBAdvanced(); return; }
  uint8_t tx[60] = {};
  size_t txLen = 0;
  if (!_parseHexBytesI2c(h, tx, sizeof(tx), txLen)) {
    ShowStatusAction::show(rawMode ? "Invalid command" : "Invalid APDU");
    _goTypeBAdvanced();
    return;
  }

  renderOperationTitle(rawMode ? "Raw Response" : "APDU Response");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  const uint32_t start = millis();
  bool found = false;
  bool otherTagDetected = false;
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goTypeBAdvanced();
      return;
    }
    if (_scanTypeB(250)) { found = true; break; }
    if (_isoATagPresent()) { otherTagDetected = true; break; }
    delay(30);
  }
  if (!found) { ShowStatusAction::show(otherTagDetected ? "No Type B tag detected" : "Tag not detected", 1200); _goTypeBAdvanced(); return; }

  uint8_t rx[64] = {};
  size_t rxLen = 0;
  if (!_typeBExchange(tx, txLen, rx, sizeof(rx), rxLen)) {
    ShowStatusAction::show(rawMode ? "Command failed" : "APDU failed");
    _goTypeBAdvanced();
    return;
  }

  _state = STATE_RAW_RESULT;
  _rawResultMifare = false;
  _rawResultTypeB = true;
  _rawResultType4A = false;
  _rawResultTypeBRaw = rawMode;
  _resetRows();
  _pushWrappedRow("Response", rxLen ? _hexBlock(rx, (uint8_t)rxLen) : String("(empty)"));
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

bool PN532I2cScreen::_typeBSelectNdef(size_t& capacity, bool& writable) {
  capacity = 0;
  writable = false;
  _typeBMLe = 48;
  _typeBMLc = 40;
  _typeBNlenSize = 2;
  uint8_t rx[64] = {};
  size_t n = 0;

  static const uint8_t selAppV2[] = {0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x01,0x00};
  static const uint8_t selAppV1[] = {0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x00,0x00};
  bool appSelected = _typeBExchange(selAppV2, sizeof(selAppV2), rx, sizeof(rx), n) &&
                     n >= 2 && rx[n-2] == 0x90 && rx[n-1] == 0x00;
  if (!appSelected) {
    appSelected = _typeBExchange(selAppV1, sizeof(selAppV1), rx, sizeof(rx), n) &&
                  n >= 2 && rx[n-2] == 0x90 && rx[n-1] == 0x00;
  }
  if (!appSelected) return false;

  static const uint8_t selCc[] = {0x00,0xA4,0x00,0x0C,0x02,0xE1,0x03};
  if (!_typeBExchange(selCc, sizeof(selCc), rx, sizeof(rx), n) || n < 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) return false;
  static const uint8_t readLen[] = {0x00,0xB0,0x00,0x00,0x02};
  if (!_typeBExchange(readLen, sizeof(readLen), rx, sizeof(rx), n) || n < 4 || rx[n-2] != 0x90 || rx[n-1] != 0x00) return false;
  const size_t ccLen = ((size_t)rx[0] << 8) | rx[1];
  if (ccLen < 15 || ccLen > 48) return false;
  uint8_t readCc[] = {0x00,0xB0,0x00,0x00,(uint8_t)ccLen};
  if (!_typeBExchange(readCc, sizeof(readCc), rx, sizeof(rx), n) || n != ccLen + 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) return false;

  _typeBMLe = ((size_t)rx[3] << 8) | rx[4];
  _typeBMLc = ((size_t)rx[5] << 8) | rx[6];
  if (!_typeBMLe || !_typeBMLc) return false;

  uint16_t fileId = 0;
  bool found = false;
  for (size_t pos = 7; pos + 1 < ccLen;) {
    const uint8_t type = rx[pos++];
    const size_t len = rx[pos++];
    if (pos + len > ccLen) return false;
    if (type == 0x04 && len >= 6) {
      fileId = (uint16_t)((rx[pos] << 8) | rx[pos+1]);
      const size_t fileSize = ((size_t)rx[pos+2] << 8) | rx[pos+3];
      if (fileSize < 2) return false;
      capacity = fileSize - 2;
      writable = rx[pos+5] == 0x00;
      _typeBNlenSize = 2;
      found = true;
      break;
    }
    if (type == 0x06 && len >= 8) {
      fileId = (uint16_t)((rx[pos] << 8) | rx[pos+1]);
      const uint32_t fileSize = ((uint32_t)rx[pos+2] << 24) | ((uint32_t)rx[pos+3] << 16) |
                                ((uint32_t)rx[pos+4] << 8) | rx[pos+5];
      if (fileSize < 4) return false;
      capacity = (size_t)fileSize - 4;
      writable = rx[pos+7] == 0x00;
      _typeBNlenSize = 4;
      found = true;
      break;
    }
    pos += len;
  }
  if (!found) return false;
  uint8_t selFile[] = {0x00,0xA4,0x00,0x0C,0x02,(uint8_t)(fileId>>8),(uint8_t)fileId};
  return _typeBExchange(selFile, sizeof(selFile), rx, sizeof(rx), n) && n >= 2 && rx[n-2] == 0x90 && rx[n-1] == 0x00;
}

void PN532I2cScreen::_doTypeBReadNdef() {
  renderOperationTitle("Read NDEF");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  const uint32_t start = millis();
  bool found = false;
  bool otherTagDetected = false;
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goTypeBNdef(); return; }
    if (_scanTypeB(250)) { found = true; break; }
    if (_isoATagPresent()) { otherTagDetected = true; break; }
    delay(30);
  }
  if (!found) { ShowStatusAction::show(otherTagDetected ? "No Type B tag detected" : "Tag not detected", 1200); _goTypeBNdef(); return; }

  size_t cap = 0; bool writable = false;
  if (!_typeBSelectNdef(cap, writable)) { ShowStatusAction::show("NDEF not found / unsupported"); _goTypeBNdef(); return; }
  uint8_t rx[64] = {}; size_t n = 0;
  uint8_t readNlen[] = {0x00,0xB0,0x00,0x00,_typeBNlenSize};
  if (!_typeBExchange(readNlen, sizeof(readNlen), rx, sizeof(rx), n) || n < (size_t)_typeBNlenSize + 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) {
    ShowStatusAction::show("Failed"); _goTypeBNdef(); return;
  }
  size_t want = 0;
  for (uint8_t i = 0; i < _typeBNlenSize; ++i) want = (want << 8) | rx[i];
  if (want > MAX_NDEF_BYTES || want > cap) { ShowStatusAction::show("NDEF too large"); _goTypeBNdef(); return; }
  size_t off = 0;
  while (off < want) {
    const size_t chunk = min(min((size_t)48, _typeBMLe), want - off);
    const uint16_t pos = (uint16_t)(off + _typeBNlenSize);
    uint8_t cmd[] = {0x00,0xB0,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};
    if (!_typeBExchange(cmd, sizeof(cmd), rx, sizeof(rx), n) || n != chunk + 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) {
      ShowStatusAction::show("Failed"); _goTypeBNdef(); return;
    }
    memcpy(_ndefBuf + off, rx, chunk);
    off += chunk;
  }
  _ndefLen = want;
  _ndefCapacity = cap;
  _hasNdef = true;
  _ndefTarget = NDEF_TARGET_TYPE_B;
  const uint8_t pupiOff = (_typeBAtqb[0] == 0x50) ? 1 : 0;
  _showNdefResult(_typeBAtqb + pupiOff, 4, _ndefBuf, _ndefLen);
}

bool PN532I2cScreen::_writeTypeBNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  if ((!ndef && ndefLen) || ndefLen > MAX_NDEF_BYTES || ndefLen > 0xFFFFU) { ShowStatusAction::show("NDEF too large"); return false; }
  renderOperationTitle("Write NDEF");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  const uint32_t start = millis(); bool found = false; bool otherTagDetected = false;
  while (millis() - start < 3000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) return false;
    if (_scanTypeB(250)) { found = true; break; }
    if (_isoATagPresent()) { otherTagDetected = true; break; }
    delay(30);
  }
  if (!found) { ShowStatusAction::show(otherTagDetected ? "No Type B tag detected" : "Tag not detected", 1200); return false; }
  size_t cap = 0; bool writable = false;
  if (!_typeBSelectNdef(cap, writable)) { ShowStatusAction::show("NDEF not found / unsupported"); return false; }
  if (!writable) { ShowStatusAction::show("NDEF is read-only"); return false; }
  if (ndefLen > cap) { ShowStatusAction::show("NDEF too large"); return false; }
  uint8_t rx[64] = {}; size_t n = 0;
  uint8_t zero[9] = {0x00,0xD6,0x00,0x00,_typeBNlenSize};
  if (!_typeBExchange(zero, 5 + _typeBNlenSize, rx, sizeof(rx), n) || n < 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) return false;
  size_t off = 0;
  while (off < ndefLen) {
    const size_t chunk = min(min((size_t)40, _typeBMLc), ndefLen - off);
    const uint16_t pos = (uint16_t)(off + _typeBNlenSize);
    uint8_t cmd[45] = {0x00,0xD6,(uint8_t)(pos>>8),(uint8_t)pos,(uint8_t)chunk};
    memcpy(cmd + 5, ndef + off, chunk);
    if (!_typeBExchange(cmd, 5 + chunk, rx, sizeof(rx), n) || n < 2 || rx[n-2] != 0x90 || rx[n-1] != 0x00) return false;
    off += chunk;
  }
  uint8_t lenCmd[9] = {0x00,0xD6,0x00,0x00,_typeBNlenSize};
  size_t v = ndefLen;
  for (int i = (int)_typeBNlenSize - 1; i >= 0; --i) { lenCmd[5+i] = (uint8_t)v; v >>= 8; }
  const bool ok = _typeBExchange(lenCmd, 5 + _typeBNlenSize, rx, sizeof(rx), n) && n >= 2 && rx[n-2] == 0x90 && rx[n-1] == 0x00;
  if (ok) { _ndefCapacity = cap; ShowStatusAction::show("NDEF written"); }
  else ShowStatusAction::show("Failed");
  return ok;
}

void PN532I2cScreen::_doTypeBEraseNdef() {
  const uint8_t empty = 0;
  const bool ok = _writeTypeBNdefRecord(&empty, 0);
  if (ok) ShowStatusAction::show("NDEF erased");
  _goTypeBNdef();
}

void PN532I2cScreen::_doTypeBFormatNdef() {
  ShowStatusAction::show("Requires provisioned Type 4 NDEF", 2000);
  _goTypeBNdef();
}

// ── actions ────────────────────────────────────────────────────────────────

void PN532I2cScreen::_showDeviceInfo() {
  _state = STATE_DEVICE_INFO;
  _resetRows();

  char buf[24];
  _pushRow("Device", _fwIc == 0x32 ? "PN532" : "PN5xx");

  snprintf(buf, sizeof(buf), "0x%02X", _fwIc);
  _pushRow("IC Identity", buf);

  snprintf(buf, sizeof(buf), "%u.%u", _fwVer, _fwRev);
  _pushRow("Firmware", buf);

  _pushRow("Interface", "I2C");
  _pushRow("I2C Address", "0x24");
  if (_busName) _pushRow("Bus", _busName);

  String support;
  if (_fwSup & 0x01) support += "ISO14443A";
  if (_fwSup & 0x02) { if (support.length()) support += " / "; support += "ISO14443B"; }
  if (_fwSup & 0x04) { if (support.length()) support += " / "; support += "ISO18092"; }
  if (!support.length()) support = "None reported";
  _pushRow("Supported", support);

  snprintf(buf, sizeof(buf), "0x%02X", _fwSup);
  _pushRow("Support Mask", buf);

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doScan14A() {
  _scanFamily = -1;
  _state = STATE_SCAN_14A;
  renderOperationTitle("Scan Tag");

  // Match the Chameleon HF reader scan presentation.
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  TagPrompt::show("Waiting for tag...", bx, by, bw, bh);

  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMain(); return; }
    }
    uint8_t uid[7]; uint8_t uidLen;
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      memcpy(_uid, uid, uidLen);
      _uidLen  = uidLen;
      _atqa    = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak     = pn532_packetbuffer[11];
      _hasCard = true;
      _mfKeys.fill({});
      ok = true;
      break;
    }
    if (_scanTypeB(250)) {
      _typeBFromMainScan = true;
      int n = Achievement.inc("nfc_uid_first");
      if (n == 1) Achievement.unlock("nfc_uid_first");
      if (n == 10) Achievement.unlock("nfc_uid_10");
      _showTypeBDetails(true);
      return;
    }
    delay(30);
  }
  if (!ok) { ShowStatusAction::show("Tag not detected", 1200); _goMain(); return; }

  int n = Achievement.inc("nfc_uid_first");
  if (n == 1)  Achievement.unlock("nfc_uid_first");
  if (n == 10) Achievement.unlock("nfc_uid_10");

  _state = STATE_SCAN_RESULT;
  _resetRows();
  char buf[24];
  _pushRow("UID",  _hexUid(_uid, _uidLen));
  const char* typeName = _inferType(_sak, _atqa);
  if (_sak == 0x00) {
    const char* concreteType = _inferType2Variant();
    if (concreteType) typeName = concreteType;
  }
  _pushRow("Type", typeName);
  if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) _scanFamily = 0;
  else if (_sak == 0x00) _scanFamily = 1;
  else if (_sak & 0x20) _scanFamily = 2;
  if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) {
    const MagicCardType magic = _detectMagicType();
    _pushRow("Magic", magicCardTypeName(magic));
  }
  const bool supported = (_sak == 0x09 || _sak == 0x08 || _sak == 0x18 ||
                          _sak == 0x00 || (_sak & 0x20) != 0);
  if (!supported) _pushRow("Status", "Tag not supported");
  snprintf(buf, sizeof(buf), "%02X:%02X", (_atqa >> 8) & 0xFF, _atqa & 0xFF);
  _pushRow("ATQA", buf);
  snprintf(buf, sizeof(buf), "%02X", _sak);
  _pushRow("SAK", buf);
  snprintf(buf, sizeof(buf), "%d bytes", _uidLen);
  _pushRow("UID Len", buf);
  _pushRow("Protocol", "ISO14443A");
  _pushRow("[Press]", "Scan again");
  if (_scanFamily >= 0) _pushRow("[Hold]", String(_scanFamily == 0 ? "MIFARE Classic" : _scanFamily == 1 ? "Ultralight / NTAG" : "Type 4A") + " Menu");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_loadSavedKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_hasCard) return;

  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  String path = String("/unigeek/nfc/keys/") + uid + ".txt";
  String content = Uni.Storage->readFile(path.c_str());
  if (content.length() == 0) return;

  auto dims = _mfDims(_sak);
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();

    int sector = -1;
    char keyType = 0;
    char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sector, &keyType, hex) == 3 &&
        sector >= 0 && sector < (int)dims.first) {
      uint8_t raw[6];
      if (_parseHexKeyI2c(String(hex), raw)) {
        NFCUtility::MIFARE_Key key(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
        if (keyType == 'A' || keyType == 'a') _mfKeys[sector].first = key;
        else if (keyType == 'B' || keyType == 'b') _mfKeys[sector].second = key;
      }
    }
    start = nl + 1;
  }
}

void PN532I2cScreen::_saveKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_hasCard) return;
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return;

  Uni.Storage->makeDir("/unigeek/nfc/keys");
  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  String path = String("/unigeek/nfc/keys/") + uid + ".txt";
  String buf;

  for (size_t sector = 0; sector < dims.first; ++sector) {
    for (uint8_t kt = 0; kt < 2; ++kt) {
      auto& slot = kt ? _mfKeys[sector].second : _mfKeys[sector].first;
      if (!slot) continue;
      const auto kv = slot.value();
      char line[48];
      snprintf(line, sizeof(line), "S%02u %c %02X%02X%02X%02X%02X%02X\n",
               (unsigned)sector, kt ? 'B' : 'A',
               kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]);
      buf += line;
    }
  }
  if (buf.length() > 0) {
    Uni.Storage->writeFile(path.c_str(), buf.c_str());
    MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, buf);
  }
}

bool PN532I2cScreen::_discoverDefaultKeys(bool checkingProgress) {
  _keyCheckCancelled = false;
  if (!_hasCard) return false;
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;

  const size_t totalSectors = dims.first;
  _mfKeys.fill({});

  _loadSavedKeys();
  ProgressView::init();
  bool keyFound = false;

  for (size_t sector = 0; sector < totalSectors; sector++) {
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      Uni.update();
      if (Uni.Nav && Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
        _keyCheckCancelled = true; ProgressView::finish(); return false;
      }
      bool useKeyB = (kt == 1);
      auto& slot = useKeyB ? _mfKeys[sector].second : _mfKeys[sector].first;
      char msg[48];
      const size_t keyIndex = sector * 2u + kt + 1u;
      const size_t totalKeys = totalSectors * 2u;
      if (checkingProgress) {
        snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...",
                 (unsigned)keyIndex, (unsigned)totalKeys);
      } else {
        snprintf(msg, sizeof(msg), "Authenticating sectors (%u/%u)...",
                 (unsigned)(sector + 1), (unsigned)totalSectors,
                 useKeyB ? "B" : "A");
      }
      int pct = checkingProgress
                  ? (int)((keyIndex - 1u) * 100u / totalKeys)
                  : (int)(sector * 100u / totalSectors);
      ProgressView::progress(msg, pct);

      // Persisted per-UID keys are tried first, but never trusted blindly.
      if (slot) {
        const auto kv = slot.value();
        if (checkingProgress) {
          snprintf(msg, sizeof(msg), "S%u %c %02X%02X%02X%02X%02X%02X",
                   (unsigned)sector, useKeyB ? 'B' : 'A',
                   kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]);
          ProgressView::progress(msg, pct);
        }
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          keyFound = true;
          uint8_t rUid[7]; uint8_t rLen;
          _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
          continue;
        }
        slot.reset();
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }

      for (const auto& key : NFCUtility::getDefaultKeys()) {
        if (!checkingProgress) {
          const auto quick = key.value();
          static const uint8_t quickKeys[5][6] = {
            {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0x00,0x00,0x00,0x00,0x00,0x00},
            {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
            {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}
          };
          bool isQuick = false; for (const auto& qk : quickKeys) if (memcmp(quick.data(), qk, 6) == 0) { isQuick=true; break; }
          if (!isQuick) continue;
        }
        Uni.update();
        if (Uni.Nav && Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
          _keyCheckCancelled = true; ProgressView::finish(); return false;
        }
        const auto kv = key.value();
        if (checkingProgress) {
          snprintf(msg, sizeof(msg), "S%u %c %02X%02X%02X%02X%02X%02X",
                   (unsigned)sector, useKeyB ? 'B' : 'A',
                   kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]);
          ProgressView::progress(msg, pct);
        }
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          slot = key;
          if (!keyFound) {
            keyFound = true;
            int n = Achievement.inc("nfc_key_found");
            if (n == 1) Achievement.unlock("nfc_key_found");
          }
          break;
        }
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
    }
  }

  ProgressView::finish();
  if (keyFound) _saveKeys();
  return keyFound;
}

bool PN532I2cScreen::_hasReadableKeyForEverySector() const {
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;
  for (size_t sector = 0; sector < dims.first; ++sector) {
    if (!_mfKeys[sector].first && !_mfKeys[sector].second) return false;
  }
  return true;
}

void PN532I2cScreen::_doReadTag() {
  _resumeReadAfterDict = false;
  renderOperationTitle("Read Tag");
  if (!_scanCardOrShow(5000)) { _goMifareTag(); return; }
  if (!_genericIdentityMatches(0, _uid, _uidLen)) return;
  if (_mfDims(_sak).first == 0) { ShowStatusAction::show("Tag not supported"); _goMifareTag(); return; }

  _discoverDefaultKeys();
  if (_keyCheckCancelled) { ShowStatusAction::show("Cancelled",1000); _goMifareTag(); return; }
  _doDumpMemory();
}

void PN532I2cScreen::_doDumpMemory() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Tag not supported"); _goMifare(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _hasDump = false;
  _dumpComplete = false;
  _dumpReadBlocks = 0;
  memset(_dumpValidBlocks, 0, sizeof(_dumpValidBlocks));
  const size_t totalSectors = dims.first;
  const size_t totalBlocks = dims.second;
  _dumpLen = totalBlocks * 16u;

  memset(_dumpImg, 0x00, sizeof(_dumpImg));
  static constexpr uint8_t kTrailer[16] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0x07,0x80,0x69, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  for (size_t sector = 0; sector < totalSectors; ++sector) {
    const size_t trailerBlock =
        (sector < 32) ? (sector * 4 + 3)
                      : (128 + (sector - 32) * 16 + 15);
    memcpy(&_dumpImg[trailerBlock * 16], kTrailer, 16);
  }

  // Keep block 0 as raw memory. SAK and ATQA are anticollision metadata and
  // are not part of the MIFARE Classic manufacturer block. The normal read
  // loop below fills all 16 bytes when block 0 is readable; otherwise the
  // zero-initialized bytes correctly remain unknown instead of fabricating
  // a non-standard manufacturer block.

  int readCount = 0;
  ProgressView::init();

  for (size_t blk = 0; blk < totalBlocks; blk++) {
    size_t sector  = (blk < 128) ? (blk / 4) : ((blk - 128) / 16 + 32);
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    int pct = (int)(blk * 100 / totalBlocks);
    char msg[40];
    snprintf(msg, sizeof(msg), "Reading blocks (%u/%u)...",
             (unsigned)(blk + 1), (unsigned)totalBlocks);
    ProgressView::progress(msg, pct);

    auto& slotA = _mfKeys[sector].first;
    auto& slotB = _mfKeys[sector].second;
    if (!slotA && !slotB) continue;

    uint8_t data[16];
    bool blockRead = false;
    for (int keyType = 0; keyType < 2 && !blockRead; ++keyType) {
      auto& slot = (keyType == 0) ? slotA : slotB;
      if (!slot) continue;

      const auto kv = slot.value();
      bool authenticated = _nfc->mifareclassic_AuthenticateBlock(
          _uid, _uidLen, trailer, keyType, (uint8_t*)kv.data());
      if (!authenticated) {
        uint8_t rUid[7]; uint8_t rLen;
        if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200)) {
          authenticated = _nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, keyType, (uint8_t*)kv.data());
        }
      }
      if (!authenticated) continue;

      blockRead = _nfc->mifareclassic_ReadDataBlock((uint8_t)blk, data);
    }
    if (!blockRead) continue;

    readCount++;
    _dumpValidBlocks[blk] = 1;
    memcpy(&_dumpImg[blk * 16], data, 16);
  }

  // A MIFARE Classic trailer read does not expose Key A as ordinary memory,
  // and Key B may also be hidden by the access conditions. Reinsert keys that
  // UniGeek actually discovered so the saved raw image is useful as a complete
  // Classic dump, matching the Chameleon Ultra dump path. Preserve the access
  // bits and GPB exactly as read.
  for (size_t sector = 0; sector < totalSectors; ++sector) {
    const size_t trailerBlock =
        (sector < 32) ? (sector * 4 + 3)
                      : (128 + (sector - 32) * 16 + 15);
    uint8_t* trailer = &_dumpImg[trailerBlock * 16];
    if (_mfKeys[sector].first) {
      const auto& keyA = _mfKeys[sector].first.value();
      memcpy(trailer, keyA.data(), 6);
    }
    if (_mfKeys[sector].second) {
      const auto& keyB = _mfKeys[sector].second.value();
      memcpy(trailer + 10, keyB.data(), 6);
    }
  }

  _dumpReadBlocks = (size_t)readCount;
  _dumpComplete = (_dumpReadBlocks == totalBlocks);
  _hasDump = true;

  int n = Achievement.inc("nfc_dump_memory");
  if (n == 1) Achievement.unlock("nfc_dump_memory");

  ProgressView::finish();
  _showTagDetails();
}

void PN532I2cScreen::_showTagDetails() {
  if (!_hasDump) { _goMifareTag(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { _goMifareTag(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _pushRow("Type", _inferType(_sak, _atqa));
  _pushRow("UID", _hexUid(_uid, _uidLen));

  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%04X", _atqa);
  _pushRow("ATQA", atqa);
  char sak[6];
  snprintf(sak, sizeof(sak), "%02X", _sak);
  _pushRow("SAK", sak);

  _pushRow("Size", String((unsigned)_dumpLen) + " bytes");
  _pushRow("Sectors", String((unsigned)dims.first));
  _pushRow("Blocks", String((unsigned)_dumpReadBlocks) + "/" + String((unsigned)dims.second));

  size_t sectorsWithKey = 0;
  for (size_t s = 0; s < dims.first; s++) {
    if (_mfKeys[s].first || _mfKeys[s].second) sectorsWithKey++;
  }
  _pushRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)dims.first) + " sectors");
  _pushRow("Status", _dumpComplete ? "Complete" : "Partial");
  _appendDumpNdefDetails();
  _pushRow("[Press]", "Actions");

  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_appendDumpNdefDetails() {
  if (!_hasDump) return;

  auto dims = _mfDims(_sak);
  if (dims.first == 0) return;
  _appendDumpNdefDetails(_dumpImg, _dumpLen, dims.first);
}

void PN532I2cScreen::_appendDumpNdefDetails(const uint8_t* dump, size_t dumpLen,
                                              size_t totalSectors) {
  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  if (!NdefParser::extractMifareClassicNdef(dump, dumpLen, totalSectors,
                                             &ndef, &ndefLen)) {
    _pushRow("NDEF", "Not found");
    return;
  }
  if (ndefLen == 0) {
    _pushRow("NDEF", "Empty");
    delete[] ndef;
    return;
  }

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
    delete[] ndef;
    return;
  }

  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _pushRow("NDEF", "Text");
      if (parsed.language.length()) _pushRow("Language", parsed.language);
      if (parsed.encoding == "UTF-16") _pushRow("Text", "(UTF-16 raw)");
      else if (parsed.text.length()) _pushWrappedRow("Text", parsed.text);
      break;
    case NdefParser::RECORD_URL:
      _pushRow("NDEF", "URL");
      _pushWrappedRow("URL", parsed.uri);
      break;
    case NdefParser::RECORD_PHONE:
      _pushRow("NDEF", "Phone");
      _pushWrappedRow("Phone", parsed.phone);
      break;
    case NdefParser::RECORD_EMAIL:
      _pushRow("NDEF", "Email");
      _pushWrappedRow("Email", parsed.email);
      break;
    case NdefParser::RECORD_VCARD:
      _pushRow("NDEF", "vCard");
      if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
      if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
      if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
      if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
      if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
      if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
      break;
    default:
      _pushRow("NDEF", "Unsupported");
      break;
  }

  delete[] ndef;
}

void PN532I2cScreen::_doShowKeys() {
  // No authentication or attack is needed to inspect the saved results.
  if (!_scanCardOrShow(5000)) { _goMifareKeys(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Tag not supported"); _goMifareKeys(); return; }
  _mfKeys.fill({});
  _loadSavedKeys();

  _state = STATE_MIFARE_KEYS;
  _resetRows();
  _pushRow("UID", _hexUid(_uid, _uidLen));
  char sak[8]; snprintf(sak, sizeof(sak), "%02X", _sak);
  _pushRow("SAK", sak);
  size_t known = 0;
  for (size_t sector = 0; sector < dims.first; ++sector) {
    if (_mfKeys[sector].first) ++known;
    if (_mfKeys[sector].second) ++known;
  }
  char count[16]; snprintf(count, sizeof(count), "%u / %u", (unsigned)known, (unsigned)(dims.first * 2u));
  _pushRow("Keys", count);
  for (size_t sector = 0; sector < dims.first; ++sector) {
    String a = "---", b = "---";
    if (_mfKeys[sector].first) a = String(_mfKeys[sector].first.c_str().c_str());
    if (_mfKeys[sector].second) b = String(_mfKeys[sector].second.c_str().c_str());
    _pushRow("S" + String((int)sector) + " A", a);
    _pushRow("S" + String((int)sector) + " B", b);
  }
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_openKeyDatabases() {
  _state = STATE_MIFARE_KEY_DB_SELECT;
  if (!_keyDbPickDir.length()) _keyDbPickDir = _dictPath;
  _browser.root = _dictPath;
  uint8_t n = _browser.load(this, _keyDbPickDir, ".txt", nullptr, BrowseFileView::STEM,
                            nullptr);
  if (_keyDbPickDir == _dictPath) {
    uint8_t out=0; _dictItems[out++]={"Default"};
    _dictItems[out++]={"Discovered"};
    _dictItems[out++]={"Extended"};
    for(uint8_t i=0;i<n;++i) if(_browser.entry(i).path!=MfcKeyStore::kDiscoveredDictionary) _dictItems[out++]=_browser.items()[i];
    setItems(_dictItems,out);
  } else setItems(_browser.items(), n);
  render();
}

void PN532I2cScreen::_openKeyDatabase(uint8_t index) {
  int fi = -1;
  if (_keyDbPickDir == _dictPath) {
    int discovered=-1; for(uint8_t i=0;i<_browser.count();++i) if(_browser.entry(i).path==MfcKeyStore::kDiscoveredDictionary){discovered=i;break;}
    const uint8_t extendedIndex=2;
    if(index==0 || index==extendedIndex){
      const uint8_t (*keys)[6]=nullptr; size_t count=0; const char* label=nullptr;
      MfcKeyStore::builtinDictionary(index==0?MfcKeyStore::kBuiltinDefaultId:MfcKeyStore::kBuiltinExtendedId,&keys,&count,&label);
      _resetRows(); for(size_t i=0;i<count && _rowCount<MAX_ROWS;++i)_pushRow(String((unsigned)(i+1)),MfcKeyStore::keyString(keys[i]));
      _keyDbViewTitle=label; _state=STATE_MIFARE_KEY_DB_VIEW; _scrollView.resetScroll(); _scrollView.setRows(_rows,_rowCount); render(); return;
    }
    if(index==1) {
      if(discovered<0){ ShowStatusAction::show("No keys in file"); return; }
      fi=discovered;
    } else { uint8_t wanted=index-3; for(uint8_t i=0,seen=0;i<_browser.count();++i){if((int)i==discovered)continue;if(seen++==wanted){fi=i;break;}} }
  } else fi=index;
  if (fi < 0 || fi >= _browser.count()) return;
  const auto& e = _browser.entry(fi);
  if (e.isDir) { _keyDbPickDir = e.path; _openKeyDatabases(); return; }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable"); return; }
  String content = Uni.Storage->readFile(e.path.c_str());
  _resetRows();
  int pos = 0;
  while (pos < (int)content.length() && _rowCount < MAX_ROWS) {
    int nl = content.indexOf('\n', pos);
    if (nl < 0) nl = content.length();
    String line = content.substring(pos, nl); line.trim();
    if (line.length() && !line.startsWith("#")) {
      _pushRow(String((unsigned)(_rowCount + 1u)), line);
    }
    pos = nl + 1;
  }
  if (!_rowCount) { ShowStatusAction::show("No keys in file"); return; }
  _keyDbViewTitle = e.label;
  _state = STATE_MIFARE_KEY_DB_VIEW;
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doDictionaryPicker() {

  _state = STATE_DICT_SELECT;
  if (_dictPickDir.length() == 0) _dictPickDir = _dictPath;
  _browser.root = _dictPath;
  uint8_t n = _browser.load(this, _dictPickDir, ".txt", nullptr, BrowseFileView::NAME);
  if (_dictPickDir == _dictPath) {
    uint8_t out=0; _dictItems[out++]={"Default"};
    _dictItems[out++]={"Discovered"};
    _dictItems[out++]={"Extended"};
    for(uint8_t i=0;i<n;++i) if(_browser.entry(i).path!=MfcKeyStore::kDiscoveredDictionary) _dictItems[out++]=_browser.items()[i];
    setItems(_dictItems,out);
  } else setItems(_browser.items(), n);
}

static bool _parseHexKeyI2c(const String& line, uint8_t out[6]) {
  String s = line; s.trim();
  if (s.length() == 0 || s.startsWith("#")) return false;
  s.replace(":", "");
  if (s.length() != 12) return false;
  for (int i = 0; i < 6; i++) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end;
    unsigned long val = strtoul(hex, &end, 16);
    if (*end != 0) return false;
    out[i] = (uint8_t)val;
  }
  return true;
}

void PN532I2cScreen::_doDictionaryAttackWithFile(uint8_t fileIndex) {
  int fi=-1;
  if(_dictPickDir==_dictPath){
    int discovered=-1;for(uint8_t i=0;i<_browser.count();++i)if(_browser.entry(i).path==MfcKeyStore::kDiscoveredDictionary){discovered=i;break;}
    const uint8_t extendedIndex=2;
    if(fileIndex==0 || fileIndex==extendedIndex){_doDictionaryAttackWithPath(fileIndex==0?MfcKeyStore::kBuiltinDefaultId:MfcKeyStore::kBuiltinExtendedId);return;}
    if(fileIndex==1){_doDictionaryAttackWithPath(MfcKeyStore::kDiscoveredDictionary);return;}
    else{uint8_t wanted=fileIndex-3;for(uint8_t i=0,seen=0;i<_browser.count();++i){if((int)i==discovered)continue;if(seen++==wanted){fi=i;break;}}}
  } else fi=fileIndex;
  if(fi<0 || fi>=_browser.count())return;
  const auto& e=_browser.entry((uint8_t)fi);
  if(e.isDir){_dictPickDir=e.path;_doDictionaryPicker();return;}
  _doDictionaryAttackWithPath(e.path);
}

void PN532I2cScreen::_startRecoverKeys() {
  _resumeReadAfterDict = true;
  _recoverChainActive = true;
  _recoverContinueMissing = false;
  _recoverPromptShown = false;
  _recoverStopRequested = false;
  _recoverChainIndex = 0;
  _recoverChainNewKeys = 0;
  _recoverySummary.reset();
  _pendingRecoveryStep = true;
}

void PN532I2cScreen::_doDictionaryAttackWithPath(const String& filePath) {
  const size_t sourceKeyCount = MfcKeyStore::dictionaryKeyCount(Uni.Storage, filePath);
  if (!sourceKeyCount && !_recoverChainActive) {
    ShowStatusAction::show("No valid keys");
    _doDictionaryPicker();
    return;
  }
  if (!_recoverChainActive && !_resumeReadAfterDict) {
    if (!_scanCardOrShow(5000)) { _doDictionaryPicker(); return; }
    if (_mfDims(_sak).first == 0) { ShowStatusAction::show("Tag not supported"); _doDictionaryPicker(); return; }
    _mfKeys.fill({});
    _loadSavedKeys();
  }
  if (!sourceKeyCount) {
    if (_recoverChainActive) {
      ++_recoverChainIndex;
      if (_recoverChainIndex < 3) _pendingRecoveryStep = true;
      else {
        _recoverChainActive = false;
        char finalMsg[80]; auto dims = _mfDims(_sak); unsigned covered = 0;
        for (size_t sec=0; sec<dims.first; ++sec) if (_mfKeys[sec].first || _mfKeys[sec].second) ++covered;
        unsigned found=0;
        for(size_t sec=0;sec<dims.first;++sec){if(_mfKeys[sec].first)++found;if(_mfKeys[sec].second)++found;}
        _recoverySummary.format(finalMsg,sizeof(finalMsg), (unsigned)_recoverySummary.count, (unsigned)_recoverySummary.count,covered,(unsigned)dims.first);
        ShowStatusAction::show(finalMsg,1400); _readAfterRecovery=true;
      }
      return;
    }
    ShowStatusAction::show("No valid keys"); return;
  }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) {
    if (_recoverChainActive) {
      _recoverChainActive = false;
      _resumeReadAfterDict = false;
      _pendingRecoveryStep = false;
      _readAfterRecovery = false;
    }
    ShowStatusAction::show("Tag not supported");
    _goMifare();
    return;
  }

  size_t totalSectors = dims.first;
  if (!_recoverChainActive) _recoverySummary.reset();
  int recovered = 0;
  auto formatSummary = [&](char* out, size_t size) {
    unsigned covered = 0;
    for (size_t sec = 0; sec < totalSectors; ++sec)
      if (_mfKeys[sec].first || _mfKeys[sec].second) ++covered;
    unsigned found = 0;
    for (size_t sec = 0; sec < totalSectors; ++sec) { if (_mfKeys[sec].first) ++found; if (_mfKeys[sec].second) ++found; }
    _recoverySummary.format(out, size, (unsigned)_recoverySummary.count, (unsigned)_recoverySummary.count, covered, (unsigned)totalSectors);
  };
  bool objectiveMet = false;
  bool promptShown = _recoverChainActive && _recoverPromptShown;

  // Match the Chameleon Ultra dictionary-attack UX: live scrolling key
  // attempts with a status/progress bar, rather than a progress-only screen.
  LogView actionLog;
  actionLog.clear();
  struct DictUiCtx { const char* status; const char* attempt; int pct; size_t keyIndex; size_t keyTotal; uint8_t stage; } ui = {"Starting...", "", 0, 0, sourceKeyCount, 0};
  auto statusCb = [](Sprite& sp, int barY, int width, void* userData) {
    auto* ctx = static_cast<DictUiCtx*>(userData);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_WHITE);
    char label[64];
    if (ctx->stage) snprintf(label, sizeof(label), "%s (%u/3)", ctx->status, (unsigned)ctx->stage);
    else snprintf(label, sizeof(label), "%s", ctx->status);
    sp.drawString(label, 2, barY);
    char keyBuf[32];
    snprintf(keyBuf, sizeof(keyBuf), "%u/%u keys", (unsigned)ctx->keyIndex, (unsigned)ctx->keyTotal);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(keyBuf, width - 2, barY);
  };
  char liveStatus[80] = "Starting...";
  if (!_recoverChainActive) {
    if (filePath == MfcKeyStore::kBuiltinDefaultId) snprintf(liveStatus, sizeof(liveStatus), "Default");
    else if (filePath == MfcKeyStore::kDiscoveredDictionary) snprintf(liveStatus, sizeof(liveStatus), "Discovered");
    else if (filePath == MfcKeyStore::kBuiltinExtendedId) snprintf(liveStatus, sizeof(liveStatus), "Extended");
    else { String label=filePath; int slash=label.lastIndexOf('/'); if(slash>=0) label=label.substring(slash+1); snprintf(liveStatus,sizeof(liveStatus),"%s",label.c_str()); }
  }
  if (_recoverChainActive) {
    static const char* const kStageNames[] = {"Default", "Discovered", "Extended"};
    renderOperationTitle("Dictionary Attack");
    snprintf(liveStatus, sizeof(liveStatus), "%s", kStageNames[_recoverChainIndex]);
  }
  ui.status = liveStatus; ui.stage=_recoverChainActive ? (uint8_t)(_recoverChainIndex + 1U) : 0;
  actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);

  if (!_recoverChainActive) {
    for (size_t sector = 0; sector < totalSectors; ++sector) {
      const uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
      for (uint8_t kt = 0; kt < 2; ++kt) {
        auto& slot = kt ? _mfKeys[sector].second : _mfKeys[sector].first;
        if (!slot) continue;
        Uni.update();
        if (Uni.Nav && Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
          ShowStatusAction::show("Cancelled", 1000); _doDictionaryPicker(); return;
        }
        const auto kv = slot.value();
        if (!_nfc->mifareclassic_AuthenticateBlock(_uid,_uidLen,trailer,kt?1:0,const_cast<uint8_t*>(kv.data()))) slot.reset();
        uint8_t rUid[7]; uint8_t rLen; _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,rUid,&rLen,200);
      }
    }
  }

  if (!_recoverChainActive) {
    ui.pct = 0;
    actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);
  }

  bool cancelled = false;
  const size_t totalSlots = totalSectors * 2U;
  auto authenticate = [&](size_t sector, uint8_t kt, const uint8_t key[6]) -> bool {
    const uint32_t trailer=(sector<32)?(sector*4+3):(128+(sector-32)*16+15);
    const bool ok=_nfc->mifareclassic_AuthenticateBlock(_uid,_uidLen,trailer,kt?1:0,const_cast<uint8_t*>(key));
    uint8_t rUid[7]; uint8_t rLen; _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,rUid,&rLen,200);
    return ok;
  };
  auto propagate = [&](const uint8_t key[6], size_t originSector, uint8_t originKt) {
    size_t pending=0, done=0;
    for(size_t s=0;s<totalSectors;++s) for(uint8_t kt=0;kt<2;++kt) if(!(s==originSector&&kt==originKt) && !(kt?_mfKeys[s].second:_mfKeys[s].first)) ++pending;
    for(size_t s=0;s<totalSectors && !cancelled;++s) for(uint8_t kt=0;kt<2;++kt) {
      if(s==originSector&&kt==originKt) continue;
      auto& slot=kt?_mfKeys[s].second:_mfKeys[s].first; if(slot) continue;
      Uni.update(); if(Uni.Nav&&Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){cancelled=true;break;}
      ++done; ui.pct=pending?(int)(done*100U/pending):100;
      char current[40]; snprintf(current,sizeof(current),"S%u %c %02X%02X%02X%02X%02X%02X",(unsigned)s,kt?'B':'A',key[0],key[1],key[2],key[3],key[4],key[5]); ui.attempt=current;
      actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);
      if(authenticate(s,kt,key)){
        slot=NFCUtility::MIFARE_Key(key[0],key[1],key[2],key[3],key[4],key[5]);
        ++recovered;
        _recoverySummary.add(key);
        actionLog.addLine(current, TFT_GREEN);
      }
    }
  };
  DictionaryDiscoverySummary discoveredKeys;
  auto offerContinuation = [&](const uint8_t key[6], char keyType) -> bool {
    discoveredKeys.note(key,keyType);
    size_t covered=0,known=0;
    for(size_t sec=0;sec<totalSectors;++sec){
      const auto& a=_mfKeys[sec].first;
      const auto& b=_mfKeys[sec].second;
      if(a||b)++covered;
      if(a)++known;
      if(b)++known;
    }
    const bool complete=known==totalSectors*2U;
    const bool canStop=covered==totalSectors && !complete;
    discoveredKeys.render(actionLog,totalSectors,[&](const String& hex)->size_t{
      uint8_t bytes[6];
      for(int j=0;j<6;++j){char pair[3]={hex[j*2],hex[j*2+1],0};bytes[j]=(uint8_t)strtoul(pair,nullptr,16);}
      size_t n=0;
      for(size_t sec=0;sec<totalSectors;++sec){
        const auto& a=_mfKeys[sec].first;const auto& b=_mfKeys[sec].second;
        if((a&&memcmp(a.value().data(),bytes,6)==0)||(b&&memcmp(b.value().data(),bytes,6)==0))++n;
      }
      return n;
    },canStop,complete);
    ui.attempt="";
    actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);
    {
      if(canStop){promptShown=true;if(_recoverChainActive)_recoverPromptShown=true;}
      Uni.update();delay(80);Uni.update();
      while(true){
        Uni.update();
        if(Uni.Nav&&Uni.Nav->wasPressed()){
          const auto dir=Uni.Nav->readDirection();
          if(dir==INavigation::DIR_PRESS){
            if(_recoverChainActive)_recoverContinueMissing=true;
            return true;
          }
          if(canStop&&dir==INavigation::DIR_BACK){
            if(_recoverChainActive)_recoverStopRequested=true;
            return false;
          }
        }
        delay(15);
      }
    }
    return true;
  };
  DictionaryKeyProgressView::begin(liveStatus, sourceKeyCount, _recoverChainActive ? (uint8_t)(_recoverChainIndex + 1U) : 0);
  for(size_t sector=0;sector<totalSectors && !cancelled && !objectiveMet;++sector) for(uint8_t kt=0;kt<2 && !cancelled && !objectiveMet;++kt) {
    auto& slot=kt?_mfKeys[sector].second:_mfKeys[sector].first; if(slot) continue;
    bool found=false;
    MfcKeyStore::forEachDictionaryKey(Uni.Storage,filePath,[&](const uint8_t key[6],size_t keyIndex,size_t){
      Uni.update(); if(Uni.Nav&&Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){cancelled=true;return false;}
      ui.keyIndex=keyIndex+1U; ui.keyTotal=sourceKeyCount;
      DictionaryKeyProgressView::draw(liveStatus,(uint8_t)sector,kt?'B':'A',key,keyIndex+1U,sourceKeyCount,sector*2U+kt+1U,totalSlots,false,true);
      if(!authenticate(sector,kt,key)) return true;
      slot=NFCUtility::MIFARE_Key(key[0],key[1],key[2],key[3],key[4],key[5]); ++recovered; _recoverySummary.add(key); found=true;
      ui.keyIndex=keyIndex+1U; ui.keyTotal=sourceKeyCount;
      DictionaryKeyProgressView::draw(liveStatus,(uint8_t)sector,kt?'B':'A',key,keyIndex+1U,sourceKeyCount,sector*2U+kt+1U,totalSlots,true,true);
      delay(2000);
      propagate(key,sector,kt);
      if(cancelled)return false;
      if(!offerContinuation(key,kt?'B':'A')){objectiveMet=true;return false;}
      bool allKnown=true;
      for(size_t x=0;x<totalSectors;++x)
        if(!_mfKeys[x].first || !_mfKeys[x].second){allKnown=false;break;}
      if(allKnown)objectiveMet=true;
      if(!objectiveMet){
        actionLog.clear();
        ui.attempt="";
        ui.pct=0;
        DictionaryKeyProgressView::begin(liveStatus, sourceKeyCount, _recoverChainActive ? (uint8_t)(_recoverChainIndex + 1U) : 0);
      }
      return false;
    });
  }
  if(cancelled){
    if(_recoverChainActive){_recoverChainActive=false;_resumeReadAfterDict=false;_pendingRecoveryStep=false;_readAfterRecovery=false;ShowStatusAction::show("Cancelled",1000);_goMifareTag();return;}
    ShowStatusAction::show("Cancelled",1000);_doDictionaryPicker();return;
  }
  ui.pct = 100;
  if (recovered > 0) {
    _saveKeys();
    int n = Achievement.inc("nfc_dict_attack");
    if (n == 1) Achievement.unlock("nfc_dict_attack");
  }
  char msg[80];
  formatSummary(msg, sizeof(msg));

  if (_recoverChainActive) {
    _recoverChainNewKeys += recovered;
    const bool covered = _hasReadableKeyForEverySector();
    bool allKeys=true;
    for(size_t sec=0;sec<totalSectors;++sec)
      if(!_mfKeys[sec].first || !_mfKeys[sec].second){allKeys=false;break;}
    ++_recoverChainIndex;
    if ((!covered || (_recoverContinueMissing && !allKeys && !_recoverStopRequested)) && _recoverChainIndex < 3) {
      Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      _pendingRecoveryStep = true;
    } else {
      _recoverChainActive = false;
      char finalMsg[80];
      formatSummary(finalMsg, sizeof(finalMsg));
      ShowStatusAction::show(finalMsg, 1400);
      _readAfterRecovery = true;
    }
  } else if (_resumeReadAfterDict) {
    _resumeReadAfterDict = false;
    _readAfterRecovery = true;
  } else {
    ShowStatusAction::show(msg, 1400);
    _doDictionaryPicker();
  }
}

bool PN532I2cScreen::_detectUltralightTag(uint16_t& pages, const char*& typeName) {
  pages = 0;
  typeName = nullptr;
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  bool found = false;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      found = true;
      break;
    }
    delay(40);
  }
  if (!found) return false;

  memcpy(_uid, uid, uidLen);
  _uidLen = uidLen;
  _atqa = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
  _sak = pn532_packetbuffer[11];
  _hasCard = true;
  if (_sak != 0x00) return false;

  typeName = _inferType2Variant();
  if (!typeName) return false;

  if (strcmp(typeName, "NTAG210") == 0) pages = 20;
  else if (strcmp(typeName, "NTAG212") == 0) pages = 41;
  else if (strcmp(typeName, "NTAG213") == 0) pages = 45;
  else if (strcmp(typeName, "NTAG215") == 0) pages = 135;
  else if (strcmp(typeName, "NTAG216") == 0) pages = 231;
  else if (strcmp(typeName, "Ultralight EV1 11") == 0) pages = 20;
  else if (strcmp(typeName, "Ultralight EV1 21") == 0) pages = 41;
  else if (strcmp(typeName, "Ultralight C") == 0) pages = 48;
  else if (strcmp(typeName, "Ultralight") == 0) pages = 16;
  else if (strcmp(typeName, "Ultralight / NTAG") == 0) {
    uint8_t cc[4] = {};
    if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc) ||
        cc[0] != 0xE1 || (cc[1] & 0xF0) != 0x10 || cc[2] == 0) return false;
    const uint16_t advertisedPages = (uint16_t)(4u + (uint16_t)cc[2] * 2u);
    if (advertisedPages <= 4 || advertisedPages > 256) return false;
    pages = advertisedPages;
  }
  return pages != 0;
}

bool PN532I2cScreen::_readUltralightDump(uint16_t pages, const char* typeName) {
  const size_t bytes = (size_t)pages * 4u;
  if (!pages || bytes > sizeof(_dumpImg)) return false;
  memset(_dumpImg, 0, bytes);

  ProgressView::init();
  for (uint16_t page = 0; page < pages; ++page) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(page + 1), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100u / pages));
    uint8_t data[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, page, pages, data)) {
      if (typeName && strcmp(typeName, "Ultralight C") == 0 && page >= 44 && page < 48) continue;
      ProgressView::finish();
      _dumpLen = 0;
      _hasDump = false;
      return false;
    }
    memcpy(_dumpImg + page * 4u, data, 4);
  }
  ProgressView::finish();
  _dumpLen = bytes;
  _hasDump = true;
  _dumpComplete = true;
  return true;
}

void PN532I2cScreen::_showUltralightTagDetails(const char* typeName, uint16_t pages) {
  _ulTypeName = typeName ? typeName : "Ultralight / NTAG";
  _ulPages = pages;
  _state = STATE_ULTRALIGHT_DUMP;
  _resetRows();
  _pushRow("Type", _ulTypeName);
  _pushRow("UID", _hexUid(_uid, _uidLen));
  _pushRow("Pages", String(pages));
  _pushRow("Dump", String(_dumpLen) + " bytes");

  const uint16_t cfg = _ultralightConfig0(_ulTypeName.c_str());
  if (cfg != 0xFFFF && (size_t)(cfg + 1) * 4u + 3u < _dumpLen) {
    const uint8_t auth0 = _dumpImg[(size_t)cfg * 4u + 3u];
    const uint8_t access = _dumpImg[(size_t)(cfg + 1) * 4u];
    if (auth0 == 0xFF || auth0 >= pages) {
      _pushRow("Protection", "None");
    } else {
      _pushRow("Protection", (access & 0x80) ? "Read & Write" : "Write Only");
      _pushRow("From Page", String(auth0));
    }
  }

  bool locked = false;
  if (_dumpLen >= 12) locked = (_dumpImg[10] != 0 || _dumpImg[11] != 0);
  uint16_t dyn = 0xFFFF;
  if (_ulTypeName == "NTAG212" || _ulTypeName == "Ultralight EV1 21") dyn = 36;
  else if (_ulTypeName == "NTAG213") dyn = 40;
  else if (_ulTypeName == "NTAG215") dyn = 130;
  else if (_ulTypeName == "NTAG216") dyn = 226;
  if (dyn != 0xFFFF && (size_t)dyn * 4u + 2u < _dumpLen) {
    locked = locked || _dumpImg[(size_t)dyn * 4u] ||
             _dumpImg[(size_t)dyn * 4u + 1u] || _dumpImg[(size_t)dyn * 4u + 2u];
  }
  _pushRow("Lock Status", locked ? "Locked pages" : "Unlocked");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (NdefParser::extractType2Ndef(_dumpImg, _dumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        _pushRow("NDEF", "Text");
        if (parsed.language.length()) _pushRow("Language", parsed.language);
        if (parsed.encoding == "UTF-16") _pushRow("Text", "(UTF-16 raw)");
        else if (parsed.text.length()) _pushWrappedRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        _pushRow("NDEF", "URL");
        _pushWrappedRow("URL", parsed.uri);
        break;
      case NdefParser::RECORD_PHONE:
        _pushRow("NDEF", "Phone");
        _pushWrappedRow("Phone", parsed.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        _pushRow("NDEF", "Email");
        _pushWrappedRow("Email", parsed.email);
        break;
      case NdefParser::RECORD_VCARD:
        _pushRow("NDEF", "vCard");
        if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
        if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
        if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
        break;
      default: _pushRow("NDEF", "Unsupported"); break;
    }
  } else {
    _pushRow("NDEF", "Not found");
  }
  _pushRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_saveUltralightDump(const char* typeName) {
  if (!_hasDump || !_dumpLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir(_dumpPath);
  String uid = _hexUid(_uid, _uidLen); uid.replace(":", "");
  String safeType = typeName ? String(typeName) : String("Ultralight");
  safeType.replace(" / ", "-"); safeType.replace(" ", "-");
  String name = InputTextAction::popup("Save Dump", safeType + "_" + uid);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";
  fs::File f = Uni.Storage->open((String(_dumpPath) + "/" + filename).c_str(), "w");
  const bool ok = f && f.write(_dumpImg, _dumpLen) == _dumpLen;
  if (f) f.close();
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

bool PN532I2cScreen::_writeUltralightNtag215Dump(const uint8_t* dump, size_t len) {
  static constexpr uint16_t kBytes = 135u * 4u;
  static constexpr uint8_t kFirst = 4;
  static constexpr uint8_t kLast = 129;
  if (!dump || len != kBytes) return false;

  renderOperationTitle("Write Dump to Tag");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || pages != 135 ||
      !typeName || strcmp(typeName, "NTAG215") != 0) {
    ShowStatusAction::show("Tag must be NTAG215");
    return false;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) return false;

  ProgressView::init();
  bool ok = true;
  for (uint16_t page = kFirst; page <= kLast; ++page) {
    char msg[36];
    const uint16_t done = page - kFirst + 1;
    snprintf(msg, sizeof(msg), "Writing pages (%u/126)...", (unsigned)done);
    ProgressView::progress(msg, (int)((uint32_t)(done - 1) * 100u / 126u));
    if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)page, dump + page * 4u)) {
      ok = false;
      break;
    }
  }
  ProgressView::finish();
  render();
  ShowStatusAction::show(ok ? "Written" : "Failed", 1600);
  return ok;
}

void PN532I2cScreen::_saveUid(const char* typeName) {
  if (!_uidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/uids");
  String uid = _hexUid(_uid, _uidLen); uid.replace(":", "");
  String safeType = typeName ? String(typeName) : String("NFC");
  safeType.replace(" / ", "-"); safeType.replace(" ", "-");
  String name = InputTextAction::popup("Save UID", safeType + "_" + uid);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".uid")) name.remove(name.length() - 4);
  const String filename = name + ".uid";
  const String path = String("/unigeek/nfc/uids/") + filename;
  const bool ok = IdentityFile::saveNfcUid(path, _uid, _uidLen);
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

void PN532I2cScreen::_showUltralightDumpActions() {
  static const InputSelectAction::Option opts[] = {
    {"Read Memory", "view"},
    {"Save UID", "uid"},
    {"Save Dump", "save"},
    {"Write to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 4, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "view") == 0) {
    uint8_t valid[256];
    const size_t units = min<size_t>(_dumpLen / 4u, sizeof(valid));
    memset(valid, 1, units);
    if (_ulTypeName == "Ultralight C") {
      for (size_t page = 44; page < units && page < 48; ++page) valid[page] = 0;
    }
    Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::TYPE2, _ulTypeName,
                                        _uid, _uidLen, _dumpImg, _dumpLen, valid));
    return;
  }
  render();
  if (strcmp(r, "uid") == 0) {
    _saveUid(_ulTypeName.c_str());
  } else if (strcmp(r, "save") == 0) {
    _saveUltralightDump(_ulTypeName.c_str());
  } else {
    if (_ulTypeName != "NTAG215" || _ulPages != 135 || _dumpLen != 540) {
      ShowStatusAction::show("Write supports NTAG215");
      render();
      return;
    }
    uint8_t sourceUid[7] = {};
    const uint8_t sourceUidLen = _uidLen;
    const uint16_t sourceAtqa = _atqa;
    const uint8_t sourceSak = _sak;
    if (sourceUidLen) memcpy(sourceUid, _uid, sourceUidLen);
    _writeUltralightNtag215Dump(_dumpImg, _dumpLen);
    memcpy(_uid, sourceUid, sourceUidLen);
    _uidLen = sourceUidLen;
    _atqa = sourceAtqa;
    _sak = sourceSak;
    _showUltralightTagDetails("NTAG215", 135);
  }
}

void PN532I2cScreen::_doUltralightReadTag() {
  if (!_genericReadActive) { renderOperationTitle("Read Tag"); renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH()); }
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported", 1600);
    _goUltralightTag();
    return;
  }
  if (!_genericIdentityMatches(1, _uid, _uidLen)) return;
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, true)) {
    _goUltralightTag();
    return;
  }
  if (!_readUltralightDump(pages, typeName)) {
    ShowStatusAction::show("Failed");
    _goUltralightTag();
    return;
  }
  _showUltralightTagDetails(typeName, pages);
}

void PN532I2cScreen::_doUltralightWriteTag() {
  const uint8_t n = _browser.load(this, _dumpPath,
      BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (!n) { ShowStatusAction::show("No NTAG215 .bin"); _goUltralightTag(); return; }
  const uint8_t count = min((uint8_t)10, n);
  InputSelectAction::Option opts[10];
  String vals[10];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("NTAG215 dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count || !Uni.Storage) { render(); return; }
  fs::File f = Uni.Storage->open(_browser.entry(idx).path.c_str(), "r");
  if (!f || f.size() != 540) { if (f) f.close(); ShowStatusAction::show("Invalid NTAG215 dump"); render(); return; }
  uint8_t dump[540];
  const bool loaded = f.read(dump, sizeof(dump)) == sizeof(dump);
  f.close();
  if (!loaded) { ShowStatusAction::show("Failed to read dump"); render(); return; }
  _writeUltralightNtag215Dump(dump, sizeof(dump));
  _goUltralightTag();
}

void PN532I2cScreen::_doUltralightEraseTag() {
  renderOperationTitle("Erase Tag");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || pages != 135 ||
      !typeName || strcmp(typeName, "NTAG215") != 0) {
    ShowStatusAction::show("Tag must be NTAG215");
    _goUltralightTag();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightTag();
    return;
  }

  uint8_t image[HfDumpBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!HfDumpBuilder::buildNtag215(_uid, nullptr, 0, image, imageLen,
                                    HfDumpBuilder::NTAG215_SIZE) || imageLen != 540) {
    ShowStatusAction::show("Cannot build empty tag");
    _goUltralightTag();
    return;
  }

  ProgressView::init();
  bool ok = true;
  for (uint16_t page = 4; page <= 129; ++page) {
    char msg[36];
    const uint16_t done = page - 3;
    snprintf(msg, sizeof(msg), "Erasing pages (%u/126)...", (unsigned)done);
    ProgressView::progress(msg, (int)((uint32_t)(done - 1) * 100u / 126u));
    if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)page, image + page * 4u)) {
      ok = false;
      break;
    }
  }
  ProgressView::finish();
  _hasDump = false; _dumpLen = 0;
  render();
  ShowStatusAction::show(ok ? "Tag erased" : "Failed", 1600);
  _goUltralightTag();
}

void PN532I2cScreen::_doMifareReadMemory() {
  renderOperationTitle("Read Memory");
  if (!_scanCardOrShow(5000)) { _goMifareAdvanced(); return; }
  auto dims = _mfDims(_sak);
  if (!dims.first || !dims.second) { ShowStatusAction::show("Tag not supported", 1600); _goMifareAdvanced(); return; }

  _discoverDefaultKeys(true);
  if (_keyCheckCancelled) { ShowStatusAction::show("Cancelled",1000); _goMifareAdvanced(); return; }
  const uint16_t blocks = dims.second;
  uint8_t* dump = new(std::nothrow) uint8_t[(size_t)blocks * 16u]();
  uint8_t* valid = new(std::nothrow) uint8_t[blocks]();
  if (!dump || !valid) {
    delete[] dump; delete[] valid;
    ShowStatusAction::show("Out of memory", 1200); _goMifareAdvanced(); return;
  }

  ProgressView::init();
  for (uint16_t block=0; block<blocks; ++block) {
    const uint8_t sector = block < 128 ? block/4 : 32 + (block-128)/16;
    auto& keyA=_mfKeys[sector].first; auto& keyB=_mfKeys[sector].second;
    const bool useKeyB=!keyA && (bool)keyB; auto& slot=useKeyB?keyB:keyA;
    char msg[36]; snprintf(msg,sizeof(msg),"Reading blocks (%u/%u)...",(unsigned)(block+1),(unsigned)blocks);
    ProgressView::progress(msg,(int)((uint32_t)block*100u/blocks));
    uint8_t* data = dump + (size_t)block * 16u; bool ok=false;
    if (slot) {
      const auto key=slot.value(); uint8_t uid[7]={},ul=0;
      if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,uid,&ul,250) && ul==_uidLen && memcmp(uid,_uid,ul)==0 &&
          _nfc->mifareclassic_AuthenticateBlock(_uid,_uidLen,block,useKeyB?1:0,const_cast<uint8_t*>(key.data())))
        ok=_nfc->mifareclassic_ReadDataBlock(block,data);
    }
    valid[block] = ok ? 1 : 0;
  }
  ProgressView::finish();
  const String type = _inferType(_sak, _atqa);
  _goMifareAdvanced();
  Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::MIFARE_CLASSIC, type,
                                      _uid, _uidLen, dump, (size_t)blocks * 16u, valid));
  delete[] dump; delete[] valid;
}
void PN532I2cScreen::_doMifareEditMemory() {
  renderOperationTitle("Edit Memory");
  if (!_scanCardOrShow(5000)) { _goMifareAdvanced(); return; }
  auto dims=_mfDims(_sak); if (!dims.first) { ShowStatusAction::show("Tag not supported"); _goMifareAdvanced(); return; }
  int block=InputNumberAction::popup((String("Block (1..")+String(dims.second-1)+")").c_str(),1,dims.second-1,1);
  if (InputNumberAction::wasCancelled()) { _goMifareAdvanced(); return; }
  String hex=InputTextAction::popup("Block data (32 hex)","",InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goMifareAdvanced(); return; }
  hex.replace(" ",""); hex.replace(":",""); if (hex.length()!=32) { _goMifareAdvanced(); ShowStatusAction::show("Need 32 hex chars"); return; }
  uint8_t data[16]={}; for(int i=0;i<16;++i){ char b[3]={hex[i*2],hex[i*2+1],0}; char*e=nullptr; unsigned long v=strtoul(b,&e,16); if(!e||*e){_goMifareAdvanced();ShowStatusAction::show("Bad hex");return;} data[i]=(uint8_t)v; }
  static const uint8_t keys[][6] = {
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, {0x00,0x00,0x00,0x00,0x00,0x00},
    {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}, {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
    {0x1A,0x98,0x2C,0x7E,0x45,0x9A}, {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  };
  bool ok=false; for(uint8_t kt=0;kt<2 && !ok;++kt) for(auto &key:keys){ uint8_t uid[7]={},ul=0; if(!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,uid,&ul,250)||ul!=_uidLen||memcmp(uid,_uid,ul)!=0) continue; if(_nfc->mifareclassic_AuthenticateBlock(_uid,_uidLen,block,kt,const_cast<uint8_t*>(key))) ok=_nfc->mifareclassic_WriteDataBlock(block,data); if(ok) break; }
  _goMifareAdvanced(); ShowStatusAction::show(ok?"Block written":"Key not available",1600);
}

void PN532I2cScreen::_doUltralightReadPages() {
  renderOperationTitle("Read Memory");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported", 1600); _goUltralightAdvanced(); return;
  }

  bool authenticated = false;
  if (!_pn532EnsureUltralightAuthForRange(
          _nfc, _wire, typeName, pages, 0, pages - 1, true, &authenticated)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t* dump = new(std::nothrow) uint8_t[(size_t)pages * 4u]();
  uint8_t* valid = new(std::nothrow) uint8_t[pages]();
  if (!dump || !valid) {
    delete[] dump; delete[] valid;
    ShowStatusAction::show("Out of memory", 1200); _goUltralightAdvanced(); return;
  }

  renderOperationTitle("Read Memory");
  ProgressView::init();
  for (uint16_t page = 0; page < pages; ++page) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...", (unsigned)(page + 1), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100u / pages));
    uint8_t* data = dump + (size_t)page * 4u;
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, page, pages, data)) {
      valid[page] = 0;
      if (!(strcmp(typeName, "Ultralight C") == 0 && page >= 44)) break;
      continue;
    }
    valid[page] = 1;
  }
  ProgressView::finish();
  const String type = typeName;
  _goUltralightAdvanced();
  Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::TYPE2, type,
                                      _uid, _uidLen, dump, (size_t)pages * 4u, valid));
  delete[] dump; delete[] valid;
}
void PN532I2cScreen::_doUltralightWritePage() {
  renderOperationTitle("Edit Memory");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported", 1600);
    _goUltralightAdvanced();
    return;
  }
  if (pages <= 4) { ShowStatusAction::show("No writable pages"); _goUltralightAdvanced(); return; }

  const int page = InputNumberAction::popup(
      (String("Page (4..") + String(pages - 1) + ")").c_str(), 4, pages - 1, 4);
  if (InputNumberAction::wasCancelled()) { _goUltralightAdvanced(); return; }

  if (!_confirmUltralightSensitiveWrite(typeName, (uint16_t)page)) {
    _goUltralightAdvanced();
    return;
  }

  String hex = InputTextAction::popup("Page data (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goUltralightAdvanced(); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8) { renderOperationTitle("Edit Memory"); ShowStatusAction::show("Need 8 hex chars", 1600); _goUltralightAdvanced(); return; }

  uint8_t data[4] = {};
  for (int i = 0; i < 4; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* e = nullptr; unsigned long v = strtoul(b, &e, 16);
    if (!e || *e) { renderOperationTitle("Edit Memory"); ShowStatusAction::show("Bad hex", 1200); _goUltralightAdvanced(); return; }
    data[i] = (uint8_t)v;
  }
  if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages, page, page, false)) {
    _goUltralightAdvanced();
    return;
  }
  renderOperationTitle("Edit Memory");
  const bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)page, data);
  ShowStatusAction::show(ok ? "Page written" : "Failed", 1600);
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightLockTag() {
  renderOperationTitle("Lock Tag");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || !typeName ||
      strcmp(typeName, "Ultralight C") == 0 ||
      strcmp(typeName, "Ultralight / NTAG") == 0) {
    ShowStatusAction::show("Lock not supported", 1600); _goUltralightAdvanced(); return;
  }

  uint16_t lastUser = _ultralightDynamicLockPage(typeName);
  if (lastUser != 0xFFFF) --lastUser; else lastUser = 15;
  if (lastUser < 4) { ShowStatusAction::show("No lockable memory", 1600); _goUltralightAdvanced(); return; }

  static const InputSelectAction::Option warn[] = {{"Lock permanently", "lock"}};
  if (!InputSelectAction::popup("Make tag read-only?", warn, 1, nullptr)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t sm[2] = {}, dm[3] = {};
  if (!_pn532BuildUltralightLockMasks(typeName, 4, lastUser, sm, dm)) {
    ShowStatusAction::show("Lock not supported", 1600); _goUltralightAdvanced(); return;
  }
  const uint16_t dyn = _ultralightDynamicLockPage(typeName);
  const bool needsDyn = dyn != 0xFFFF && (dm[0] || dm[1] || dm[2]);
  const uint16_t authPage = needsDyn ? dyn : 2;
  if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages,
                                           authPage, authPage, false)) {
    _goUltralightAdvanced(); return;
  }

  renderOperationTitle("Lock Tag");
  bool ok = true;
  if (sm[0] || sm[1]) {
    uint8_t p2[4] = {0, 0, sm[0], sm[1]};
    ok = _pn532Type2WritePage(_nfc, _wire, 2, p2);
  }
  if (ok && needsDyn) {
    uint8_t cur[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, dyn, pages, cur)) ok = false;
    else {
      cur[0] |= dm[0]; cur[1] |= dm[1]; cur[2] |= dm[2];
      ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)dyn, cur);
    }
  }
  ShowStatusAction::show(ok ? "Tag locked" : "Failed", 1600);
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightSetPassword() {
  renderOperationTitle("Set Password");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || _ultralightConfig0(typeName) == 0xFFFF) {
    ShowStatusAction::show("Password not supported", 1600); _goUltralightAdvanced(); return;
  }
  const uint16_t cfg = _ultralightConfig0(typeName);

  uint8_t auth0 = 0xFF, access = 0;
  const bool protectionReadable = _pn532ReadUltralightProtection(
      _nfc, _wire, typeName, pages, auth0, access);
  const bool wasProtected = !protectionReadable || (auth0 != 0xFF && auth0 < pages);

  if (wasProtected && !_pn532EnsureUltralightAuthForRange(
          _nfc, _wire, typeName, pages, cfg, cfg + 3, false)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg + 1, pages, c1)) {
    ShowStatusAction::show("Failed", 1600); _goUltralightAdvanced(); return;
  }

  uint8_t newPwd[4] = {};
  if (!_promptUltralightPassword(newPwd, "New Password")) {
    _goUltralightAdvanced(); return;
  }
  static const InputSelectAction::Option modes[] = {
    {"Write Only", "w"}, {"Read & Write", "rw"},
  };
  const char* mode = InputSelectAction::popup("Protection", modes, 2, nullptr);
  if (!mode) { _goUltralightAdvanced(); return; }
  const bool protectRead = strcmp(mode, "rw") == 0;

  const uint8_t desiredAccess = (uint8_t)((c1[0] & ~0x87u) | (protectRead ? 0x80u : 0u));
  const bool configLocked = (c1[0] & 0x40u) != 0;
  if (configLocked &&
      (c0[3] != 4 || (c1[0] & 0x87u) != (desiredAccess & 0x87u))) {
    renderOperationTitle("Set Password"); ShowStatusAction::show("Configuration locked", 2000); _goUltralightAdvanced(); return;
  }

  if (!configLocked) {
    c1[0] = desiredAccess;
    c0[3] = 4;
    if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 1), c1)) {
      renderOperationTitle("Set Password"); ShowStatusAction::show("Failed", 1600);
      _goUltralightAdvanced(); return;
    }
  }

  if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 2), newPwd)) {
    renderOperationTitle("Set Password"); ShowStatusAction::show("Failed", 1600);
    _goUltralightAdvanced(); return;
  }

  if (!configLocked &&
      !_pn532Type2WritePage(_nfc, _wire, (uint8_t)cfg, c0)) {
    renderOperationTitle("Set Password"); ShowStatusAction::show("Failed", 1600);
    _goUltralightAdvanced(); return;
  }

  if (!_pn532UltralightPwdAuth(_nfc, _wire, newPwd)) {
    renderOperationTitle("Set Password"); ShowStatusAction::show("Authentication failed", 1600);
    _goUltralightAdvanced(); return;
  }

  renderOperationTitle("Set Password");
  ShowStatusAction::show("Password set", 1600);
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightRemovePassword() {
  renderOperationTitle("Remove Password");
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || _ultralightConfig0(typeName) == 0xFFFF) {
    ShowStatusAction::show("Password not supported", 1600); _goUltralightAdvanced(); return;
  }
  const uint16_t cfg = _ultralightConfig0(typeName);

  uint8_t auth0 = 0xFF, access = 0;
  const bool readable = _pn532ReadUltralightProtection(
      _nfc, _wire, typeName, pages, auth0, access);
  const bool protectedNow = !readable || (auth0 != 0xFF && auth0 < pages);
  if (readable && !protectedNow) {
    ShowStatusAction::show("Password not set");
    _goUltralightAdvanced(); return;
  }
  if (protectedNow && !_pn532EnsureUltralightAuthForRange(
          _nfc, _wire, typeName, pages, cfg, cfg + 3, false)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg + 1, pages, c1)) {
    renderOperationTitle("Remove Password"); ShowStatusAction::show("Failed", 1600); _goUltralightAdvanced(); return;
  }
  if (c1[0] & 0x40) {
    renderOperationTitle("Remove Password"); ShowStatusAction::show("Configuration locked", 2000); _goUltralightAdvanced(); return;
  }

  c1[0] &= (uint8_t)~0x87u;
  c0[3] = 0xFF;
  const uint8_t defaultPwd[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t defaultPack[4] = {0x00, 0x00, 0x00, 0x00};

  bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 1), c1);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)cfg, c0);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 2), defaultPwd);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 3), defaultPack);

  renderOperationTitle("Remove Password");
  ShowStatusAction::show(ok ? "Password removed" : "Failed", 1600);
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doReadNdef() {
  renderOperationTitle("Read NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported");
    _goUltralightNdef();
    return;
  }

  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, true)) {
    _goUltralightNdef();
    return;
  }

  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    _goUltralightNdef();
    return;
  }
  if (cc[0] != 0xE1 || cc[2] == 0) {
    ShowStatusAction::show("Not NDEF formatted");
    _goUltralightNdef();
    return;
  }

  _ndefCapacity = (size_t)cc[2] * 8u;
  const size_t maxUserBytes = pages > 4 ? ((size_t)pages - 4u) * 4u : 0u;
  const size_t userBytes = (_ndefCapacity < maxUserBytes) ? _ndefCapacity : maxUserBytes;
  if (!userBytes) {
    ShowStatusAction::show("Invalid NDEF capacity");
    _goUltralightNdef();
    return;
  }

  uint8_t* user = new uint8_t[userBytes];
  if (!user) {
    ShowStatusAction::show("Out of memory");
    _goUltralightNdef();
    return;
  }
  memset(user, 0, userBytes);

  const size_t totalPages = (userBytes + 3u) / 4u;
  size_t userLen = 0;
  bool readOk = true;
  renderOperationTitle("Read NDEF");
  ProgressView::init();
  for (size_t i = 0; i < totalPages; ++i) {
    const uint16_t page16 = 4u + (uint16_t)i;
    if (page16 >= pages || page16 > 0xFFu) break;

    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(i + 1u), (unsigned)totalPages);
    ProgressView::progress(msg, (int)(i * 100u / totalPages));

    uint8_t data[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, (uint8_t)page16,
                                     pages, data)) {
      readOk = false;
      break;
    }
    const size_t copy = (userBytes - userLen < 4u) ? userBytes - userLen : 4u;
    memcpy(user + userLen, data, copy);
    userLen += copy;
  }
  ProgressView::finish();

  if (!readOk) {
    delete[] user;
    render();
    ShowStatusAction::show("Failed");
    _goUltralightNdef();
    return;
  }

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;
  while (pos < userLen) {
    const uint8_t tlv = user[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= userLen) break;

    size_t len = user[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= userLen) break;
      len = ((size_t)user[pos] << 8) | user[pos + 1];
      pos += 2;
    }
    if (pos + len > userLen) break;
    if (tlv == 0x03) {
      ndef = user + pos;
      ndefLen = len;
      break;
    }
    pos += len;
  }

  _showNdefResult(_uid, _uidLen, ndef, ndefLen);
  delete[] user;
}

void PN532I2cScreen::_showNdefResult(const uint8_t* uid, uint8_t uidLen,
                                         const uint8_t* ndef, size_t ndefLen) {
  _state = STATE_NDEF_RESULT;
  _resetRows();

  if (!_ndefWritePreview && uid && uidLen > 0) {
    memcpy(_uid, uid, uidLen);
    _uidLen = uidLen;
  }
  _hasNdef = false;
  _ndefLen = 0;

  if (uid && uidLen > 0) {
    _pushRow("UID", _hexUid(uid, uidLen));
  }

  if (!ndef) {
    _pushRow("NDEF", "Not found");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  if (ndefLen <= MAX_NDEF_BYTES) {
    memcpy(_ndefBuf, ndef, ndefLen);
    _ndefLen = ndefLen;
    _hasNdef = true;
  }

  char lenBuf[24];
  char capBuf[24];
  char freeBuf[24];

  snprintf(lenBuf, sizeof(lenBuf), "%u bytes", (unsigned)ndefLen);
  _pushRow("NDEF Size", lenBuf);

  if (_ndefCapacity > 0) {
    const size_t freeBytes =
        (_ndefCapacity > ndefLen) ? (_ndefCapacity - ndefLen) : 0;

    snprintf(capBuf, sizeof(capBuf), "%u bytes", (unsigned)_ndefCapacity);
    snprintf(freeBuf, sizeof(freeBuf), "%u bytes", (unsigned)freeBytes);

    _pushRow("Capacity", capBuf);
    _pushRow("Free", freeBuf);
  }

  if (ndefLen == 0) {
    _pushRow("NDEF", "Empty");
    _pushRow("[Press]", "Actions");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _pushRow("Record", "Text");
      if (parsed.language.length()) _pushRow("Language", parsed.language);
      _pushWrappedRow("Text", parsed.text);
      break;

    case NdefParser::RECORD_URL:
      _pushRow("Record", "URL");
      _pushWrappedRow("URL", parsed.uri);
      break;

    case NdefParser::RECORD_PHONE:
      _pushRow("Record", "Phone");
      _pushWrappedRow("Phone", parsed.phone);
      break;

    case NdefParser::RECORD_EMAIL:
      _pushRow("Record", "Email");
      _pushWrappedRow("Email", parsed.email);
      break;

    case NdefParser::RECORD_VCARD:
      _pushRow("Record", "vCard");
      if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
      if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
      if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
      if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
      if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
      if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
      break;

    default:
      _pushRow("Record", "Unsupported");
      break;
  }

  if (_hasNdef) {
    _pushRow("[Press]", _ndefWritePreview ? "Write to Tag" : "Actions");
  }

  _scrollView.setRows(_rows, _rowCount);
  render();
}

namespace {
static uint8_t _classicMadCrc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xC7;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
  }
  return crc;
}
}

// ── MIFARE Classic NDEF ─────────────────────────────────────────────────────
//
// NFC Forum formatted MIFARE Classic uses MAD AID 0x03E1 to mark NFC sectors.
// MAD sectors are readable with public Key A A0:A1:A2:A3:A4:A5 and
// non-proprietary NFC sectors use public Key A D3:F7:D3:F7:D3:F7.
//
// This first implementation intentionally operates on cards that are already
// NFC/NDEF formatted. It never rewrites MAD entries, sector trailers, keys or
// access bits.

bool PN532I2cScreen::_classicAuthSector(uint8_t sector, const uint8_t key[6]) {
  uint16_t block = (sector < 32)
                     ? (uint16_t)sector * 4
                     : (uint16_t)(128 + (sector - 32) * 16);

  if (_nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, block, 0, const_cast<uint8_t*>(key))) {
    return true;
  }

  // A failed MIFARE authentication can leave the PICC halted. Re-select once.
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 250)) {
    return false;
  }
  if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;

  return _nfc->mifareclassic_AuthenticateBlock(
      _uid, _uidLen, block, 0, const_cast<uint8_t*>(key));
}

bool PN532I2cScreen::_classicNdefSectors(uint8_t* sectors,
                                         size_t maxSectors,
                                         size_t& count) {
  count = 0;
  if (!sectors || maxSectors == 0) return false;

  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };

  auto readMadBlock = [&](uint16_t block, uint8_t out[16]) {
    const uint8_t sector = (block < 128) ? (uint8_t)(block / 4u)
                                         : (uint8_t)(32u + (block - 128u) / 16u);
    for (const auto& key : madKeys) {
      if (_classicAuthSector(sector, key) && _nfc->mifareclassic_ReadDataBlock(block, out))
        return true;
    }
    return false;
  };

  uint8_t b1[16] = {};
  uint8_t b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) return false;

  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= dims.first || count >= maxSectors) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03))
      sectors[count++] = sector;
  };

  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2u + (size_t)(sec - 1u) * 2u;
    addIfNdef(sec, b1[off], b1[off + 1u]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8u) * 2u;
    addIfNdef(sec, b2[off], b2[off + 1u]);
  }

  if (dims.first > 16) {
    uint8_t m0[16] = {};
    uint8_t m1[16] = {};
    uint8_t m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2u + (size_t)(sec - 17u) * 2u;
        addIfNdef(sec, m0[off], m0[off + 1u]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24u) * 2u;
        addIfNdef(sec, m1[off], m1[off + 1u]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32u) * 2u;
        addIfNdef(sec, m2[off], m2[off + 1u]);
      }
    }
  }

  return count > 0;
}

bool PN532I2cScreen::_classicReadNdefArea(const uint8_t* sectors,
                                           size_t sectorCount,
                                           uint8_t*& area,
                                           size_t& areaLen) {
  area = nullptr;
  areaLen = 0;
  if (!sectors || sectorCount == 0) return false;

  size_t capacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    capacity += (sectors[i] < 32) ? 48 : 240;
  }

  area = new uint8_t[capacity];
  if (!area) return false;

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  ProgressView::init();
  size_t out = 0;

  for (size_t si = 0; si < sectorCount; si++) {
    uint8_t sector = sectors[si];
    if (!_classicAuthSector(sector, NFC_KEY_A)) {
      ProgressView::finish();
      delete[] area;
      area = nullptr;
      areaLen = 0;
      return false;
    }

    uint16_t firstBlock = (sector < 32)
                            ? (uint16_t)sector * 4
                            : (uint16_t)(128 + (sector - 32) * 16);
    uint8_t dataBlocks = (sector < 32) ? 3 : 15;

    for (uint8_t bi = 0; bi < dataBlocks; bi++) {
      char msg[32];
      snprintf(msg, sizeof(msg), "Reading S%u B%u",
               (unsigned)sector, (unsigned)bi);
      ProgressView::progress(msg, (int)(out * 100 / capacity));

      uint8_t data[16] = {};
      if (!_nfc->mifareclassic_ReadDataBlock((uint8_t)(firstBlock + bi), data)) {
        ProgressView::finish();
        delete[] area;
        area = nullptr;
        areaLen = 0;
        return false;
      }
      memcpy(area + out, data, 16);
      out += 16;
    }
  }

  ProgressView::finish();
  areaLen = out;
  return true;
}

void PN532I2cScreen::_doReadClassicNdef() {
  renderOperationTitle("Read NDEF");
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;

  if (!_scanCardOrShow(5000)) {
    _goMifareNdef();
    return;
  }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) {
    ShowStatusAction::show("Tag not supported");
    _goMifareNdef();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _goMifareNdef();
    return;
  }

  _ndefCapacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    _ndefCapacity += (sectors[i] < 32) ? 48 : 240;
  }

  uint8_t* area = nullptr;
  size_t areaLen = 0;
  if (!_classicReadNdefArea(sectors, sectorCount, area, areaLen)) {
    ShowStatusAction::show("Failed to read NDEF sectors");
    _goMifareNdef();
    return;
  }

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;

  while (pos < areaLen) {
    uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= areaLen) break;

    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= areaLen) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1];
      pos += 2;
    }
    if (pos + len > areaLen) break;

    if (tlv == 0x03) {
      ndef = area + pos;
      ndefLen = len;
      break;
    }
    pos += len;
  }

  // _showNdefResult copies short NDEF data into _ndefBuf before area is freed.
  _showNdefResult(_uid, _uidLen, ndef, ndefLen);
  delete[] area;
}

bool PN532I2cScreen::_formatClassic1kNdef() {
  auto dims = _mfDims(_sak);
  if (dims.first != 16) {
    ShowStatusAction::show("Format supports Classic 1K");
    return false;
  }

  // NFC Forum INITIALISED layout (AN1305): sectors 1 and 2 are NFC
  // sectors, all remaining sectors stay free.
  uint8_t madPayload[31] = {};
  madPayload[0] = 0x01;
  madPayload[1] = 0x03; madPayload[2] = 0xE1;
  madPayload[3] = 0x03; madPayload[4] = 0xE1;

  uint8_t mad1[16] = {};
  uint8_t mad2[16] = {};
  mad1[0] = _classicMadCrc8(madPayload, sizeof(madPayload));
  memcpy(mad1 + 1, madPayload, 15);
  memcpy(mad2, madPayload + 15, 16);

  static const uint8_t madTrailer[16] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,
    0x78,0x77,0x88,0xC1,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  static const uint8_t nfcTrailer[16] = {
    0xD3,0xF7,0xD3,0xF7,0xD3,0xF7,
    0x7F,0x07,0x88,0x40,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  // Keep formatter authentication aligned with the physically validated
  // Chameleon Ultra path instead of limiting it to the four NFC defaults.
  static const uint8_t candidates[][6] = {
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, {0x00,0x00,0x00,0x00,0x00,0x00},
    {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}, {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
    {0x1A,0x98,0x2C,0x7E,0x45,0x9A}, {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
    {0x71,0x4C,0x5C,0x88,0x6E,0x97}, {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
    {0xA0,0x47,0x8C,0xC3,0x90,0x91}, {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
    {0x8F,0xD0,0xA4,0xF2,0x56,0xE9}, {0x00,0x00,0x00,0x00,0x00,0x01},
    {0x11,0x22,0x33,0x44,0x55,0x66}, {0x26,0x97,0x34,0x3B,0x00,0x00},
    {0x12,0x34,0x56,0x78,0x9A,0xBC}, {0xBD,0x49,0x3A,0x39,0x62,0xB6},
  };

  // same candidate set used by Chameleon Ultra. Formatting touches only
  // sectors 0, 1 and 2, so validate one credential per sector up front.
  _mfKeys.fill({});
  _loadSavedKeys();

  auto authSectorKey = [&](uint8_t block, const uint8_t key[6], bool useKeyB) {
    // Failed MIFARE authentication can halt the PICC. Start every probe from
    // a fresh selection, just like _tryWriteMifareBlock() does for writes.
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
      return false;
    }
    if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;
    return _nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, block, useKeyB ? 1 : 0, const_cast<uint8_t*>(key)) != 0;
  };

  uint8_t sectorKey[3][6] = {};
  bool sectorKeyB[3] = {};
  for (uint8_t sector = 0; sector < 3; ++sector) {
    const uint8_t block = (uint8_t)(sector * 4u);
    bool found = false;

    auto& savedA = _mfKeys[sector].first;
    auto& savedB = _mfKeys[sector].second;
    if (savedA) {
      const auto ka = savedA.value();
      if (authSectorKey(block, (const uint8_t*)ka.data(), false)) {
        memcpy(sectorKey[sector], ka.data(), 6);
        sectorKeyB[sector] = false;
        found = true;
      }
    }
    if (!found && savedB) {
      const auto kb = savedB.value();
      if (authSectorKey(block, (const uint8_t*)kb.data(), true)) {
        memcpy(sectorKey[sector], kb.data(), 6);
        sectorKeyB[sector] = true;
        found = true;
      }
    }

    for (uint8_t kt = 0; kt < 2 && !found; ++kt) {
      for (const auto& key : candidates) {
        if (authSectorKey(block, key, kt != 0)) {
          memcpy(sectorKey[sector], key, 6);
          sectorKeyB[sector] = (kt != 0);
          found = true;
          break;
        }
      }
    }
    if (!found) {
      ShowStatusAction::show("Format: unknown sector key");
      return false;
    }
  }

  uint8_t zero[16] = {};
  uint8_t emptyNdef[16] = {0x03,0x00,0xFE};
  uint16_t done = 0;
  static constexpr uint16_t total = 11;
  ProgressView::init();

  auto writeWithKnownKey = [&](uint16_t block, const uint8_t data[16]) {
    char msg[40];
    snprintf(msg, sizeof(msg), "Formatting blocks (%u/%u)...",
             (unsigned)(done + 1u), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100u / total));

    const uint8_t sector = (uint8_t)(block / 4u); // Classic 1K only here.
    auto& keyA = _mfKeys[sector].first;
    auto& keyB = _mfKeys[sector].second;
    const bool madDataBlock = (sector == 0 && (block == 1 || block == 2));

    if (madDataBlock) {
      if (keyB) {
        const auto kb = keyB.value();
        if (_tryWriteMifareBlock(block, data, (const uint8_t*)kb.data(), true)) {
          ++done;
          return true;
        }
      }
      if (sectorKeyB[sector] &&
          _tryWriteMifareBlock(block, data, sectorKey[sector], true)) {
        ++done;
        return true;
      }
      for (const auto& key : candidates) {
        if (_tryWriteMifareBlock(block, data, key, true)) {
          ++done;
          return true;
        }
      }
    }

    if (keyA) {
      const auto ka = keyA.value();
      if (_tryWriteMifareBlock(block, data, (const uint8_t*)ka.data(), false)) {
        ++done;
        return true;
      }
    }
    if (!madDataBlock && keyB) {
      const auto kb = keyB.value();
      if (_tryWriteMifareBlock(block, data, (const uint8_t*)kb.data(), true)) {
        ++done;
        return true;
      }
    }
    if (!madDataBlock &&
        _tryWriteMifareBlock(block, data, sectorKey[sector], sectorKeyB[sector])) {
      ++done;
      return true;
    }

    for (const auto& key : candidates) {
      if (_tryWriteMifareBlock(block, data, key, false) ||
          (!madDataBlock && _tryWriteMifareBlock(block, data, key, true))) {
        ++done;
        return true;
      }
    }
    return false;
  };

  bool ok = writeWithKnownKey(1, mad1) && writeWithKnownKey(2, mad2);

  for (uint8_t sector = 1; sector <= 2 && ok; ++sector) {
    const uint16_t first = (uint16_t)sector * 4u;
    for (uint8_t bi = 0; bi < 3 && ok; ++bi) {
      const uint8_t* data = (sector == 1 && bi == 0) ? emptyNdef : zero;
      ok = writeWithKnownKey(first + bi, data);
    }
  }

  if (ok) ok = writeWithKnownKey(3, madTrailer);
  if (ok) ok = writeWithKnownKey(7, nfcTrailer);
  if (ok) ok = writeWithKnownKey(11, nfcTrailer);

  if (ok) ProgressView::progress("Format complete", 100);
  ProgressView::finish();
  render();
  ShowStatusAction::show(ok ? "NDEF formatted" : "Failed", 1600);

  if (ok) {
    // Trailer writes change authentication state. Force a fresh select before
    // the caller verifies MAD and continues with the requested NDEF write.
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 400);
  }
  return ok;
}

bool PN532I2cScreen::_writeClassicNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  if (!ndef || ndefLen == 0 || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("NDEF too large");
    return false;
  }

  if (!_scanCardOrShow(5000)) return false;
  if (_mfDims(_sak).first == 0) {
    ShowStatusAction::show("Tag not supported");
    return false;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    static const InputSelectAction::Option opts[] = {
      {"Format NDEF", "format"},
    };
    const char* choice = InputSelectAction::popup("Not NDEF formatted", opts, 1, nullptr);
    render();
    if (!choice || strcmp(choice, "format") != 0 || !_formatClassic1kNdef()) {
      return false;
    }
    // Formatting changes the sector trailers and therefore invalidates the
    // PN532 Crypto1/authentication state. Re-select the tag and then return to
    // the same MAD discovery + write path used by the original, physically
    // validated Write NDEF implementation.
    if (!_scanCardOrShow(5000) ||
        !_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
      ShowStatusAction::show("Failed to verify format");
      return false;
    }
  }

  size_t capacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    capacity += (sectors[i] < 32) ? 48 : 240;
  }

  // Initial implementation targets the NFC Forum Simple Configuration:
  // mandatory NDEF Message TLV begins at byte 0 of the first NFC sector.
  const size_t payloadLen = ndefLen + 3; // 03 LEN <NDEF> FE
  if (payloadLen > capacity) {
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  uint8_t* payload = new uint8_t[payloadLen];
  if (!payload) {
    ShowStatusAction::show("Out of memory");
    return false;
  }
  payload[0] = 0x03;
  payload[1] = (uint8_t)ndefLen;
  memcpy(payload + 2, ndef, ndefLen);
  payload[2 + ndefLen] = 0xFE;

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  // First block is committed last with the real length. Until then the tag
  // advertises an empty NDEF TLV, reducing the chance of a torn write exposing
  // a partially-written message.
  uint8_t firstFinal[16] = {};
  uint8_t firstStaged[16] = {};
  bool firstPrepared = false;
  bool success = true;
  size_t offset = 0;

  ProgressView::init();

  for (size_t si = 0; si < sectorCount && offset < payloadLen; si++) {
    uint8_t sector = sectors[si];
    if (!_classicAuthSector(sector, NFC_KEY_A)) {
      success = false;
      break;
    }

    uint16_t firstBlock = (sector < 32)
                            ? (uint16_t)sector * 4
                            : (uint16_t)(128 + (sector - 32) * 16);
    uint8_t dataBlocks = (sector < 32) ? 3 : 15;

    for (uint8_t bi = 0; bi < dataBlocks && offset < payloadLen; bi++) {
      uint8_t blockNo = (uint8_t)(firstBlock + bi);
      uint8_t block[16] = {};
      if (!_nfc->mifareclassic_ReadDataBlock(blockNo, block)) {
        success = false;
        break;
      }

      size_t take = payloadLen - offset;
      if (take > 16) take = 16;
      memcpy(block, payload + offset, take);

      char msg[32];
      snprintf(msg, sizeof(msg), "Writing S%u B%u",
               (unsigned)sector, (unsigned)bi);
      ProgressView::progress(msg, (int)(offset * 100 / payloadLen));

      if (!firstPrepared) {
        memcpy(firstFinal, block, 16);
        memcpy(firstStaged, block, 16);
        firstStaged[1] = 0x00;  // empty NDEF until final commit

        if (!_nfc->mifareclassic_WriteDataBlock(blockNo, firstStaged)) {
          success = false;
          break;
        }
        firstPrepared = true;
      } else {
        if (!_nfc->mifareclassic_WriteDataBlock(blockNo, block)) {
          success = false;
          break;
        }
      }

      offset += take;
    }
    if (!success) break;
  }

  // Re-authenticate first NFC sector and commit the real length last.
  if (success && firstPrepared) {
    if (!_classicAuthSector(sectors[0], NFC_KEY_A) ||
        !_nfc->mifareclassic_WriteDataBlock(
            (uint8_t)((sectors[0] < 32)
                        ? sectors[0] * 4
                        : 128 + (sectors[0] - 32) * 16),
            firstFinal)) {
      success = false;
    }
  }

  ProgressView::finish();
  delete[] payload;
  render();

  ShowStatusAction::show(success ? "NDEF written" : "Failed", 1600);
  return success;
}

void PN532I2cScreen::_doEraseClassicNdef() {
  renderOperationTitle("Erase NDEF");
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;

  if (!_scanCardOrShow(5000)) {
    _goMifareNdef();
    return;
  }
  if (_mfDims(_sak).first == 0) {
    ShowStatusAction::show("Tag not supported");
    _goMifareNdef();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("Not NDEF formatted");
    _goMifareNdef();
    return;
  }

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  if (!_classicAuthSector(sectors[0], NFC_KEY_A)) {
    ShowStatusAction::show("NDEF sector locked");
    _goMifareNdef();
    return;
  }

  uint8_t blockNo = (uint8_t)((sectors[0] < 32)
                                ? sectors[0] * 4
                                : 128 + (sectors[0] - 32) * 16);
  uint8_t block[16] = {};
  if (!_nfc->mifareclassic_ReadDataBlock(blockNo, block)) {
    ShowStatusAction::show("Failed");
    _goMifareNdef();
    return;
  }

  // Logical erase defined for NDEF: empty NDEF Message TLV + Terminator TLV.
  // Bytes after FE are intentionally left untouched; they are no longer active.
  block[0] = 0x03;
  block[1] = 0x00;
  block[2] = 0xFE;

  bool success = _nfc->mifareclassic_WriteDataBlock(blockNo, block);
  _hasNdef = false;
  _ndefLen = 0;
  ShowStatusAction::show(success ? "NDEF erased" : "Failed", 1600);
  _goMifareNdef();
}

bool PN532I2cScreen::_writeNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  renderOperationTitle("Write NDEF");
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) return _writeClassicNdefRecord(ndef, ndefLen);
  if (_ndefTarget == NDEF_TARGET_TYPE_B) return _writeTypeBNdefRecord(ndef, ndefLen);
  if (_ndefTarget == NDEF_TARGET_TYPE_4A) return _writeType4ANdefRecord(ndef, ndefLen);
  if (_ndefTarget == NDEF_TARGET_FELICA) return _writeFelicaNdefRecord(ndef, ndefLen);
  if (_ndefTarget == NDEF_TARGET_TYPE_1) { ShowStatusAction::show("Type 1 is read-only"); return false; }
  return _writeUltralightNdefRecord(ndef, ndefLen);
}

bool PN532I2cScreen::_writeUltralightNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  renderOperationTitle("Write NDEF");
  if (!ndef || ndefLen == 0 || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("NDEF too large");
    return false;
  }

  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());

  uint8_t uid[7];
  uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;

  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      return false;
    }

    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    ShowStatusAction::show("Tag not detected", 1200);
    return false;
  }

  uint16_t authPages = 0; const char* authType = nullptr;
  if (!_detectUltralightTag(authPages, authType)) {
    ShowStatusAction::show("Tag not supported", 1600);
    return false;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, authType, authPages, false)) {
    return false;
  }

  // Read the NFC Forum Type 2 Capability Container from page 3.
  // CC byte 2 gives the data area size in multiples of 8 bytes.
  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    return false;
  }

  if (cc[0] != 0xE1) {
    ShowStatusAction::show("Not NDEF formatted");
    return false;
  }

  size_t capacity = (size_t)cc[2] * 8u;
  size_t physicalCapacity = authPages > 4 ? ((size_t)authPages - 4u) * 4u : 0u;
  uint8_t maxCc[4] = {};
  if (_type2DefaultCc(authType, maxCc)) {
    const size_t knownCapacity = (size_t)maxCc[2] * 8u;
    if (knownCapacity < physicalCapacity) physicalCapacity = knownCapacity;
  }
  if (capacity == 0 || physicalCapacity == 0) {
    ShowStatusAction::show("Invalid NDEF capacity");
    return false;
  }
  if (capacity > physicalCapacity) capacity = physicalCapacity;

  // Type 2 Tag memory layout:
  const size_t lenBytes = (ndefLen < 0xFFu) ? 1u : 3u;
  size_t tlvLen = 1u + lenBytes + ndefLen + 1u;
  size_t paddedLen = (tlvLen + 3) & ~((size_t)3);

  if (paddedLen > capacity) {
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  uint8_t* payload = new uint8_t[paddedLen];
  if (!payload) {
    ShowStatusAction::show("Out of memory");
    return false;
  }

  memset(payload, 0x00, paddedLen);
  size_t pos = 0;
  payload[pos++] = 0x03;
  if (ndefLen < 0xFFu) {
    payload[pos++] = (uint8_t)ndefLen;
  } else {
    payload[pos++] = 0xFF;
    payload[pos++] = (uint8_t)((ndefLen >> 8) & 0xFFu);
    payload[pos++] = (uint8_t)(ndefLen & 0xFFu);
  }
  memcpy(payload + pos, ndef, ndefLen);
  pos += ndefLen;
  payload[pos] = 0xFE;

  renderOperationTitle("Write NDEF");
  ProgressView::init();
  bool success = true;

  const size_t totalPages = paddedLen / 4;
  for (size_t offset = 0; offset < paddedLen; offset += 4) {
    uint8_t page = 4 + (offset / 4);
    const size_t currentPage = offset / 4 + 1;

    char msg[32];
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...",
             (unsigned)currentPage, (unsigned)totalPages);
    ProgressView::progress(msg, (int)(offset * 100 / paddedLen));

    if (!_pn532Type2WritePage(_nfc, _wire, page, &payload[offset])) {
      success = false;
      break;
    }
  }

  ProgressView::finish();
  delete[] payload;
  render();

  ShowStatusAction::show(success ? "NDEF written" : "Failed", 1600);
  return success;
}

void PN532I2cScreen::_doWriteNdefText() {
  String text = InputTextAction::popup("Enter text", "");
  if (InputTextAction::wasCancelled() || text.length() == 0) {
    _goNdefWrite();
    return;
  }

  // Short-record NDEF Text:
  // D1 01 <payloadLen> 54 02 'e' 'n' <text>
  size_t payloadLen = 1 + 2 + text.length();
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > 254 || payloadLen > 255) {
    _goNdefWrite();
    ShowStatusAction::show("Text too long");
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    _goNdefWrite();
    ShowStatusAction::show("Out of memory");
    return;
  }

  ndef[0] = 0xD1;                    // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                    // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'T';
  ndef[4] = 0x02;                    // UTF-8, language code length 2
  ndef[5] = 'e';
  ndef[6] = 'n';
  memcpy(&ndef[7], text.c_str(), text.length());

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefUrl() {
  String url = InputTextAction::popup("Enter URL", "https://");
  if (InputTextAction::wasCancelled() || url.length() == 0) {
    _goNdefWrite();
    return;
  }

  uint8_t prefix = 0x00;
  const char* body = url.c_str();

  if      (url.startsWith("https://www.")) { prefix = 0x02; body += 12; }
  else if (url.startsWith("http://www."))  { prefix = 0x01; body += 11; }
  else if (url.startsWith("https://"))     { prefix = 0x04; body += 8; }
  else if (url.startsWith("http://"))      { prefix = 0x03; body += 7; }

  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > 254 || payloadLen > 255) {
    _goNdefWrite();
    ShowStatusAction::show("URL too long");
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    _goNdefWrite();
    ShowStatusAction::show("Out of memory");
    return;
  }

  ndef[0] = 0xD1;                    // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                    // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefEmail() {
  String email = InputTextAction::popup("Enter email", "");
  if (InputTextAction::wasCancelled() || email.length() == 0) {
    _goNdefWrite();
    return;
  }

  String uri = email.startsWith("mailto:") ? email : ("mailto:" + email);

  uint8_t prefix = 0x06;  // "mailto:"
  const char* body = uri.c_str() + 7;
  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > MAX_NDEF_BYTES || payloadLen > 255) {
    _goNdefWrite();
    ShowStatusAction::show("Email too long");
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    _goNdefWrite();
    ShowStatusAction::show("Out of memory");
    return;
  }

  ndef[0] = 0xD1;                  // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                  // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';                   // URI record
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefPhone() {
  String phone = InputTextAction::popup("Enter phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled() || phone.length() == 0) {
    _goNdefWrite();
    return;
  }

  String uri = phone.startsWith("tel:") ? phone : ("tel:" + phone);

  uint8_t prefix = 0x05;  // "tel:"
  const char* body = uri.c_str() + 4;
  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > MAX_NDEF_BYTES || payloadLen > 255) {
    _goNdefWrite();
    ShowStatusAction::show("Phone too long");
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    _goNdefWrite();
    ShowStatusAction::show("Out of memory");
    return;
  }

  ndef[0] = 0xD1;                  // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                  // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';                   // URI record
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_showNdefActions() {
  if (!_hasNdef) return;

  static const InputSelectAction::Option opts[] = {
    {"Write to Tag", "write"},
    {"Save to File", "save"},
  };
  static const InputSelectAction::Option readOnlyOpts[] = {
    {"Save to File", "save"},
  };

  const bool readOnly = (_ndefTarget == NDEF_TARGET_TYPE_1);
  const char* choice = InputSelectAction::popup("NDEF Actions", readOnly ? readOnlyOpts : opts, readOnly ? 1 : 2, nullptr);
  if (!choice) {
    render();
    return;
  }

  render();

  if (strcmp(choice, "write") == 0) {
    _doWriteCurrentNdef();
  } else if (strcmp(choice, "save") == 0) {
    _doSaveNdef();
  }
}

void PN532I2cScreen::_doWriteCurrentNdef() {
  if (!_hasNdef || _ndefLen == 0) {
    ShowStatusAction::show("No NDEF loaded");
    render();
    return;
  }

  if (_ndefTarget == NDEF_TARGET_TYPE_B || _ndefTarget == NDEF_TARGET_TYPE_4A || _ndefTarget == NDEF_TARGET_FELICA) {
    bool ok = _writeNdefRecord(_ndefBuf, _ndefLen);
    if (ok) _goNdefParent();
    else { _state = STATE_NDEF_RESULT; render(); }
    return;
  }
  if (_ndefTarget == NDEF_TARGET_TYPE_1) {
    ShowStatusAction::show("Type 1 is read-only");
    _state = STATE_NDEF_RESULT; render();
    return;
  }

  static const InputSelectAction::Option targets[] = {
    {"Ultralight / NTAG", "ul"},
    {"MIFARE Classic",    "mfc"},
  };

  const char* choice =
      InputSelectAction::popup("Write to Tag", targets, 2, nullptr);

  if (!choice) {
    render();
    return;
  }

  render();

  bool ok = false;
  bool wroteUltralight = false;
  if (strcmp(choice, "ul") == 0) {
    wroteUltralight = true;
    ok = _writeUltralightNdefRecord(_ndefBuf, _ndefLen);
  } else if (strcmp(choice, "mfc") == 0) {
    ok = _writeClassicNdefRecord(_ndefBuf, _ndefLen);
  }

  if (ok) {
    if (wroteUltralight) _goUltralight();
    else _goMifare();
    return;
  }

  // Failure keeps the preview/result available for another attempt.
  _state = STATE_NDEF_RESULT;
  render();
}

void PN532I2cScreen::_doSaveNdef() {
  if (!_hasNdef || _ndefLen == 0) {
    ShowStatusAction::show(_hasNdef ? "NDEF is empty" : "No NDEF to save");
    render();
    return;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  String uid;
  const char* suffix = "_ntag";
  if (_ndefTarget == NDEF_TARGET_FELICA) { uid = _hexBlock(_felicaIdm, 8); suffix = "_felica"; }
  else if (_ndefTarget == NDEF_TARGET_TYPE_1) { uid = _hexBlock(_type1JewelId, 4); suffix = "_type1"; }
  else if (_ndefTarget == NDEF_TARGET_TYPE_B) { uid = _hexBlock(_typeBAtqb + 1, 4); suffix = "_type4b"; }
  else { uid = _hexUid(_uid, _uidLen); if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) suffix = "_mifare"; else if (_ndefTarget == NDEF_TARGET_TYPE_4A) suffix = "_type4a"; }
  uid.replace(":", ""); uid.replace(" ", "");
  if (uid.length() == 0) uid = "unknown";
  String baseName = uid + suffix;

  String name = InputTextAction::popup("File name", baseName.c_str());
  if (InputTextAction::wasCancelled()) { render(); return; }

  name.trim();
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);
  if (name.length() == 0) { render(); return; }

  for (int i = 0; i < (int)name.length(); i++) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }
  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) { render(); return; }

  String path = String(_ndefPath) + "/" + name + ".ndef";
  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_ndefPath) + "/" + name + "_(" + n + ").ndef";
      if (!Uni.Storage->exists(candidate.c_str())) { path = candidate; break; }
    }
  }

  render();
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) { ShowStatusAction::show("Failed"); render(); return; }

  const size_t written = f.write(_ndefBuf, _ndefLen);
  f.close();
  if (written == _ndefLen) {
    const int slash = path.lastIndexOf('/');
    const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
    ShowStatusAction::show(("Saved: " + saved).c_str(), 1600);

    _goNdefParent();
    return;
  }

  // Failure keeps the preview/result available for another attempt.
  ShowStatusAction::show("Failed");
  _state = STATE_NDEF_RESULT;
  render();
}

void PN532I2cScreen::_doWriteNdefVcard() {
  String contact = InputTextAction::popup("Contact name", "");
  if (InputTextAction::wasCancelled() || contact.length() == 0) {
    _goNdefWrite();
    return;
  }

  String company = InputTextAction::popup("Company", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String address = InputTextAction::popup("Address", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String email = InputTextAction::popup("Mail", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String website = InputTextAction::popup("Website", "https://");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;

  if (!NdefBuilder::buildVcard(contact,
                               company,
                               address,
                               phone,
                               email,
                               website,
                               ndef,
                               ndefLen,
                               MAX_NDEF_BYTES)) {
    _goNdefWrite();
    ShowStatusAction::show("vCard too large");
    return;
  }

  _showNdefWritePreview(ndef, ndefLen, false);
}

void PN532I2cScreen::_showNdefWritePreview(const uint8_t* ndef,
                                               size_t ndefLen,
                                               bool fromFile) {
  _ndefWritePreview = true;
  _ndefWritePreviewFromFile = fromFile;
  _ndefCapacity = 0;
  _showNdefResult(nullptr, 0, ndef, ndefLen);
}

void PN532I2cScreen::_doWriteNdefFromFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    _goNdefWrite();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  _state = STATE_NDEF_FILE_SELECT;
  if (_ndefPickDir.length() == 0 || !_ndefPickDir.startsWith(_ndefPath))
    _ndefPickDir = _ndefPath;
  _browser.root = _ndefPath;

  uint8_t n = _browser.load(this, _ndefPickDir, ".ndef");
  setItems(_browser.items(), n);
  render();
}

void PN532I2cScreen::_doWriteNdefFileSelected(uint8_t fileIndex) {
  if (fileIndex >= _browser.count()) return;

  const auto& e = _browser.entry(fileIndex);
  if (e.isDir) {
    _ndefPickDir = e.path;
    _doWriteNdefFromFile();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Failed");
    _doWriteNdefFromFile();
    return;
  }

  size_t len = f.size();
  if (len == 0 || len > MAX_NDEF_BYTES) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large");
    _doWriteNdefFromFile();
    return;
  }

  uint8_t buf[MAX_NDEF_BYTES];
  size_t got = f.read(buf, len);
  f.close();

  if (got != len) {
    ShowStatusAction::show("Failed");
    _doWriteNdefFromFile();
    return;
  }

  // Preview the selected NDEF using the exact same parser/layout as Read NDEF.
  // _ndefTarget remains unchanged, so confirmation writes to the correct
  // destination: MIFARE Classic or Ultralight / NTAG.
  _showNdefWritePreview(buf, len, true);
}

void PN532I2cScreen::_doEraseNdef() {
  renderOperationTitle("Erase NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported");
    _goUltralightNdef();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightNdef();
    return;
  }

  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    _goUltralightNdef();
    return;
  }
  if (cc[0] != 0xE1) {
    ShowStatusAction::show("Not NDEF formatted");
    _goUltralightNdef();
    return;
  }

  uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};
  const bool success = _pn532Type2WritePage(_nfc, _wire, 4, emptyNdef);

  renderOperationTitle("Erase NDEF");
  ShowStatusAction::show(success ? "NDEF erased" : "Failed", 1600);
  _goUltralightNdef();
}

void PN532I2cScreen::_doFormatNdef() {
  renderOperationTitle("Format NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Tag not supported");
    _goUltralightNdef();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightNdef();
    return;
  }

  uint8_t cc[4] = {}, desired[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc) || !_type2DefaultCc(typeName, desired)) {
    ShowStatusAction::show("Format not supported");
    _goUltralightNdef();
    return;
  }

  if (!_type2CcIsValid(cc)) {
    if (!_type2CcCanProgramSafely(cc, desired)) {
      ShowStatusAction::show("CC cannot be safely formatted");
      _goUltralightNdef();
      return;
    }
    if (!_pn532Type2WritePage(_nfc, _wire, 3, desired)) {
      ShowStatusAction::show("Failed to write CC");
      _goUltralightNdef();
      return;
    }
  }

  const uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};
  const bool ok = _pn532Type2WritePage(_nfc, _wire, 4, emptyNdef);
  renderOperationTitle("Format NDEF");
  ShowStatusAction::show(ok ? "NDEF formatted" : "Failed", 1600);
  _goUltralightNdef();
}

bool PN532I2cScreen::_resetAndReselect() {
  if (!_nfc || !_nfc->SAMConfig()) return false;
  uint8_t uid[7] = {}, uidLen = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 250))
      return true;
    delay(30);
  }
  return false;
}

MagicCardType PN532I2cScreen::_detectMagicType(void (*progress)(uint8_t)) {
  auto report = [&](uint8_t percent) { if (progress) progress(percent); };
  report(10);
  // Probe Gen3 first. Gen3 / APDU cards accept a direct READ of block 0
  // without MIFARE authentication; a prior Gen1A unlock would make the same
  // read possible and could cause a false Gen3 positive.
  {
    report(20);
    static const uint8_t readBlock0[] = {0x30, 0x00};
    uint8_t resp[20] = {};
    uint8_t rlen = sizeof(resp);
    if (_nfcDataExch(_nfc, _wire, readBlock0, sizeof(readBlock0), resp, rlen, 500) &&
        rlen >= 16) {
      report(90);
      _resetAndReselect();
      report(100);
      return MagicCardType::GEN3;
    }
  }

  report(40);
  if (!_resetAndReselect()) { report(100); return MagicCardType::NONE; }

  // Gen1A backdoor sequence. After normal ISO14443A activation the PICC is
  // ACTIVE, but the 0x40(7-bit) wakeup is expected from HALT. Put the card in
  // HALT first, with CRC handled explicitly, then send 0x40 / 0x43 with CRC
  // disabled. This mirrors the established PN532 Gen1A raw-command sequence.
  bool gen1a = false;
  uint8_t resp[8] = {};
  uint8_t rlen = sizeof(resp);

  const bool rawMode =
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x00) && // TxMode: CRC off, 106A
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x00);   // RxMode: CRC off, 106A

  report(55);
  if (rawMode) {
    // HLTA has CRC_A 0x57CD. A halted card intentionally sends no response;
    // _nfcCommThru still consumes the PN532 response/status, so ignore its
    // boolean result here.
    static const uint8_t halt[] = {0x50, 0x00, 0x57, 0xCD};
    (void)_nfcCommThru(_nfc, _wire, halt, sizeof(halt), resp, rlen, 200);

    if (_nfcWriteReg(_nfc, _wire, 0x633D, 0x07)) {
      static const uint8_t wake[] = {0x40};
      rlen = sizeof(resp);
      const bool ack1 = _nfcCommThru(_nfc, _wire, wake, sizeof(wake),
                                      resp, rlen, 250) &&
                        rlen >= 1 && (resp[0] & 0x0F) == 0x0A;

      _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);

      report(75);
      if (ack1) {
        static const uint8_t unlock[] = {0x43};
        rlen = sizeof(resp);
        gen1a = _nfcCommThru(_nfc, _wire, unlock, sizeof(unlock),
                             resp, rlen, 250) &&
                rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
      }
    }
  }

  // Restore normal ISO14443A CRC/framing before leaving the raw probe.
  _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);
  _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
  _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
  report(90);
  _resetAndReselect();
  report(100);
  return gen1a ? MagicCardType::GEN1A : MagicCardType::NONE;
}

bool PN532I2cScreen::_writeMagicUid(MagicCardType type, const uint8_t* sourceUid,
                                       uint8_t sourceUidLen, const uint8_t block0[16]) {
  if (!sourceUid || !_nfc || !_wire ||
      (sourceUidLen != 4 && sourceUidLen != 7)) return false;
  if (type == MagicCardType::GEN1A && (!block0 || sourceUidLen != 4)) return false;

  if (!_resetAndReselect()) return false;

  if (type == MagicCardType::GEN3) {
    // Reuse the same command family already used by Magic -> Gen3 Set UID.
    uint8_t cmd[13] = {0x90, 0xFB, 0xCC, 0xCC, sourceUidLen};
    memcpy(cmd + 5, sourceUid, sourceUidLen);
    cmd[5 + sourceUidLen] = 0x00;
    const uint8_t cmdLen = (uint8_t)(6u + sourceUidLen);
    uint8_t resp[8] = {};
    uint8_t rlen = sizeof(resp);
    if (!_nfcDataExch(_nfc, _wire, cmd, cmdLen, resp, rlen, 500)) return false;
  } else if (type == MagicCardType::GEN1A) {
    const uint8_t bcc = (uint8_t)(sourceUid[0] ^ sourceUid[1] ^ sourceUid[2] ^ sourceUid[3]);

    // Enter the Gen1A backdoor from HALT, using the same raw sequence as
    // detection. Gen1A 0x40 must be sent as 7 bits with CRC disabled.
    uint8_t resp[8] = {};
    uint8_t rlen = sizeof(resp);
    const bool rawMode =
        _nfcWriteReg(_nfc, _wire, 0x6302, 0x00) &&
        _nfcWriteReg(_nfc, _wire, 0x6303, 0x00);
    if (!rawMode) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      return false;
    }

    static const uint8_t halt[] = {0x50, 0x00, 0x57, 0xCD};
    (void)_nfcCommThru(_nfc, _wire, halt, sizeof(halt), resp, rlen, 200);

    if (!_nfcWriteReg(_nfc, _wire, 0x633D, 0x07)) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      return false;
    }
    static const uint8_t wake[] = {0x40};
    rlen = sizeof(resp);
    const bool ack1 = _nfcCommThru(_nfc, _wire, wake, sizeof(wake),
                                    resp, rlen, 250) &&
                      rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);
    if (!ack1) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    static const uint8_t unlock[] = {0x43};
    rlen = sizeof(resp);
    const bool ack2 = _nfcCommThru(_nfc, _wire, unlock, sizeof(unlock),
                                    resp, rlen, 250) &&
                      rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    // MIFARE WRITE command/data frames need CRC-A on TX, but the PICC ACKs
    // are only 4 bits and carry no CRC. Keep RX CRC disabled until both ACKs
    // have been consumed; otherwise the PN532 can report a CRC/framing error
    // even though the card accepted the transmitted frame.
    _nfcWriteReg(_nfc, _wire, 0x6302, 0x80); // TxMode: CRC-A enabled
    _nfcWriteReg(_nfc, _wire, 0x6303, 0x00); // RxMode: raw 4-bit ACK
    if (!ack2) {
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    // MIFARE WRITE is a two-frame exchange. Sending A0 00 and the 16-byte
    // manufacturer block in one InDataExchange frame is not equivalent and
    // leaves block 0 unchanged on Gen1A cards. Keep the backdoor session open
    // and perform both RF exchanges explicitly.
    uint8_t write0[2] = {0xA0, 0x00};
    rlen = sizeof(resp);
    const bool writeAck = _nfcCommThru(_nfc, _wire, write0, sizeof(write0),
                                       resp, rlen, 400) &&
                          rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    if (!writeAck) {
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    uint8_t safeBlock0[16];
    memcpy(safeBlock0, block0, sizeof(safeBlock0));
    memcpy(safeBlock0, sourceUid, 4);
    safeBlock0[4] = bcc;
    rlen = sizeof(resp);
    const bool dataAck = _nfcCommThru(_nfc, _wire, safeBlock0, sizeof(safeBlock0),
                                      resp, rlen, 700) &&
                         rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    // Restore normal CRC handling before leaving the raw MIFARE exchange.
    _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
    if (!dataAck) {
      _resetAndReselect();
      return false;
    }
  } else {
    return false;
  }

  // Force a fresh activation and verify that the source UID is now presented.
  if (!_nfc->SAMConfig()) return false;
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300))
      return uidLen == sourceUidLen && memcmp(uid, sourceUid, sourceUidLen) == 0;
    delay(30);
  }
  return false;
}

void PN532I2cScreen::_doDetectMagic() {
  uint8_t uid[7]; uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goMifareTag();
      return;
    }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    _magicResult = "Tag not detected";
    _magicUiPhase = MAGIC_RESULT;
    render();
    return;
  }

  const uint8_t sak = pn532_packetbuffer[11];
  if (sak != 0x09 && sak != 0x08 && sak != 0x18) {
    _magicResult = "Magic not detected";
    _magicUiPhase = MAGIC_RESULT;
    render();
    return;
  }

  _magicUiPhase = MAGIC_SCANNING;
  ProgressView::init();
  ProgressView::progress("Scanning...", 0);
  const MagicCardType magic = _detectMagicType([](uint8_t p) { ProgressView::progress("Scanning...", p); });
  ProgressView::finish();

  _magicResult = magic == MagicCardType::GEN1A ? "Gen1A" :
                 magic == MagicCardType::GEN3 ? "Gen3" : "Magic not detected";
  if (magic != MagicCardType::NONE) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }
  _magicUiPhase = MAGIC_RESULT;
  render();
}

void PN532I2cScreen::_doGen3LockUid() {
  ShowStatusAction::show("Place Gen3 tag...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMifareAdvanced(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("Tag not detected", 1200); _goMifareAdvanced(); return; }

  if (_detectMagicType() != MagicCardType::GEN3) {
    ShowStatusAction::show("Tag is not Gen3");
    _goMifareAdvanced();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"Lock UID permanently", "lock"},
  };
  const char* choice = InputSelectAction::popup("Permanent UID lock", opts, 1, nullptr);
  render();
  if (!choice || strcmp(choice, "lock") != 0) { _goMifareAdvanced(); return; }

  // The confirmation overlay may leave the card idle; use a fresh activation
  // before issuing the irreversible Gen3 lock command.
  if (!_resetAndReselect()) {
    ShowStatusAction::show("Tag lost");
    _goMifareAdvanced();
    return;
  }

  static const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[8]; uint8_t rlen = sizeof(resp);
  bool locked = _nfcDataExch(_nfc, _wire, cmd, sizeof(cmd), resp, rlen);
  ShowStatusAction::show(locked ? "Gen3 UID locked" : "Failed", 1600);
  _goMifareAdvanced();
}

void PN532I2cScreen::_showDumpActions() {
  if (!_dumpComplete) {
    static const InputSelectAction::Option missingOpts[] = {
      {"Recover Keys", "dict"},
      {"Partial Read", "partial"},
    };
    const char* choice = InputSelectAction::popup("Missing sector keys", missingOpts, 2, nullptr);
    render();
    if (!choice) return;
    if (strcmp(choice, "dict") == 0) { _startRecoverKeys(); return; }
  }
  static const InputSelectAction::Option opts[] = {
    {"Read Memory",    "view"},
    {"Save UID",     "uid"},
    {"Save Dump",    "save"},
    {"Write UID to Tag", "writeuid"},
    {"Write Dump to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 5, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "view") == 0) {
    Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::MIFARE_CLASSIC,
                                        _inferType(_sak, _atqa), _uid, _uidLen,
                                        _dumpImg, _dumpLen, _dumpValidBlocks));
    return;
  }

  // Restore Tag Details before opening another modal/progress screen. The
  // popup only clears its own rectangle, so launching Save/Write immediately
  // could leave fragments of Dump Actions visible underneath the next UI.
  render();
  if (strcmp(r, "uid") == 0) {
    const char* typeName = (_sak == 0x09) ? "MF-Mini" : (_sak == 0x18) ? "MF-4K" : "MF-1K";
    _saveUid(typeName);
  } else if (strcmp(r, "save") == 0) _doSaveDump();
  else if (strcmp(r, "writeuid") == 0) _showWriteUidPreview(_uid, _uidLen);
  else _showWriteDumpPreview(_dumpImg, _dumpLen, _uid, _uidLen, false);
}

void PN532I2cScreen::_doWriteUidSource() {
  _uidWriteSourceMode = UID_WRITE_MANUAL;
  _uidWriteSourceLen = 0;
  _uidWriteFilePath = "";
  _uidWriteDumpPath = "";
  _rebuildWriteUidForm(0);
}

void PN532I2cScreen::_rebuildWriteUidForm(uint8_t selected) {
  _state = STATE_MIFARE_UID_SOURCE_FORM;
  _resetRows();
  const char* source = _uidWriteSourceMode == UID_WRITE_MANUAL ? "Manual" :
                       _uidWriteSourceMode == UID_WRITE_FILE ? "UID File" : "Dump";
  _pushRow("UID", source);
  if (_uidWriteSourceMode == UID_WRITE_MANUAL) {
    _pushRow("Value", _uidWriteSourceLen ? _hexUid(_uidWriteSource, _uidWriteSourceLen) : "-");
  } else if (_uidWriteSourceMode == UID_WRITE_FILE) {
    String name = _uidWriteFilePath.length() ? _uidWriteFilePath.substring(_uidWriteFilePath.lastIndexOf('/') + 1) : "-";
    _pushRow("UID File", name);
  } else {
    String name = _uidWriteDumpPath.length() ? _uidWriteDumpPath.substring(_uidWriteDumpPath.lastIndexOf('/') + 1) : "-";
    _pushRow("Dump File", name);
  }
  _pushRow("Write", "");
  for (uint8_t i=0;i<_rowCount;++i) _uidWriteItems[i] = {_rowLabels[i].c_str(), _rowValues[i].c_str()};
  setItems(_uidWriteItems, _rowCount, min<uint8_t>(selected, _rowCount-1));
}

void PN532I2cScreen::_editWriteUidManual() {
  String initial = _uidWriteSourceLen ? _hexUid(_uidWriteSource, _uidWriteSourceLen) : "";
  String hex = InputTextAction::popup("UID (8 or 14 hex)", initial, InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _rebuildWriteUidForm(1); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length()!=8 && hex.length()!=14) { _rebuildWriteUidForm(1); ShowStatusAction::show("UID must be 4 or 7 bytes",1600); return; }
  uint8_t uid[7]={}; const uint8_t len=(uint8_t)(hex.length()/2u);
  for(uint8_t i=0;i<len;++i){char b[3]={hex[i*2],hex[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(b,&e,16);if(!e||*e){_rebuildWriteUidForm(1);ShowStatusAction::show("Bad hex",1200);return;}uid[i]=(uint8_t)v;}
  memcpy(_uidWriteSource,uid,len); _uidWriteSourceLen=len; _rebuildWriteUidForm(1);
}

void PN532I2cScreen::_startWriteUidFromForm() {
  if (_uidWriteSourceLen != 4 && _uidWriteSourceLen != 7) {
    ShowStatusAction::show(_uidWriteSourceMode == UID_WRITE_MANUAL ? "Enter UID" : "Select source file", 1600);
    render(); return;
  }
  _showWriteUidPreview(_uidWriteSource, _uidWriteSourceLen);
}

void PN532I2cScreen::_doWriteUidFromFilePicker() {
  _state = STATE_MIFARE_UID_SELECT;
  const String root = "/unigeek/nfc/uids";
  if (_uidPickDir.length() == 0) _uidPickDir = root;
  _browser.root = root;
  uint8_t n = _browser.load(this, _uidPickDir, BrowseFileView::Mode(".uid"));
  if (n == 0 && _uidPickDir == root) { _rebuildWriteUidForm(1); ShowStatusAction::show("No saved UIDs",1600); return; }
  setItems(_browser.items(), n);
}
void PN532I2cScreen::_doWriteUidFileSelected(uint8_t i) {
  if (i >= _browser.count()) return; const auto& e=_browser.entry(i);
  if (e.isDir) { _uidPickDir=e.path; _doWriteUidFromFilePicker(); return; }
  size_t n=0; uint8_t uid[7]={};
  if (!IdentityFile::loadNfcUid(e.path,uid,sizeof(uid),n) || (n!=4&&n!=7)) { ShowStatusAction::show("Invalid UID file",1600); return; }
  memcpy(_uidWriteSource,uid,n); _uidWriteSourceLen=(uint8_t)n; _uidWriteFilePath=e.path; _rebuildWriteUidForm(1);
}
void PN532I2cScreen::_doWriteUidFromDumpPicker() {
  _state=STATE_MIFARE_UID_DUMP_SELECT; if(_dumpPickDir.length()==0)_dumpPickDir=_dumpPath; _browser.root=_dumpPath;
  uint8_t n=_browser.load(this,_dumpPickDir,BrowseFileView::Mode(".bin",320,1024,4096));
  if(n==0&&_dumpPickDir==_dumpPath){_rebuildWriteUidForm(1);ShowStatusAction::show("No saved dumps",1600);return;} setItems(_browser.items(),n);
}
void PN532I2cScreen::_doWriteUidDumpSelected(uint8_t i) {
  if(i>=_browser.count())return;const auto&e=_browser.entry(i);if(e.isDir){_dumpPickDir=e.path;_doWriteUidFromDumpPicker();return;}
  fs::File f=Uni.Storage->open(e.path.c_str(),"r");if(!f||f.size()==0||f.size()>4096){if(f)f.close();ShowStatusAction::show("Invalid dump",1600);return;}size_t len=f.size();uint8_t* d=new uint8_t[len];if(!d){f.close();ShowStatusAction::show("Out of memory",1600);return;}size_t got=f.read(d,len);f.close();
  if(got!=len){delete[] d;ShowStatusAction::show("Invalid dump",1600);return;}auto info=HfDumpParser::inspect(d,len);delete[] d;if(!HfDumpParser::isMifareClassic(info.type)||!info.uidValid||(info.uidLen!=4&&info.uidLen!=7)){ShowStatusAction::show("Dump UID not supported",1600);return;}memcpy(_uidWriteSource,info.uid,info.uidLen);_uidWriteSourceLen=info.uidLen;_uidWriteDumpPath=e.path;_rebuildWriteUidForm(1);
}

void PN532I2cScreen::_showWriteUidPreview(const uint8_t* uid,uint8_t n) {
  if(!uid||(n!=4&&n!=7))return; memcpy(_uidWriteSource,uid,n);_uidWriteSourceLen=n;
  _state=STATE_MIFARE_UID_WRITE_PREVIEW;_resetRows();_pushRow("UID",_hexUid(uid,n));_pushRow("Target","Magic Gen1A/Gen3");_pushRow("[Press]","Write to Tag");_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();
}
bool PN532I2cScreen::_readGen1aBlock0(uint8_t out[16]) {
  if(!_resetAndReselect())return false;uint8_t resp[20]={};uint8_t n=sizeof(resp);bool raw=_nfcWriteReg(_nfc,_wire,0x6302,0)&&_nfcWriteReg(_nfc,_wire,0x6303,0);bool ok=false;
  if(raw){static const uint8_t halt[]={0x50,0,0x57,0xCD};(void)_nfcCommThru(_nfc,_wire,halt,sizeof(halt),resp,n,200);if(_nfcWriteReg(_nfc,_wire,0x633D,7)){static const uint8_t wake[]={0x40};n=sizeof(resp);bool a1=_nfcCommThru(_nfc,_wire,wake,1,resp,n,250)&&n>=1&&(resp[0]&0x0F)==0x0A;_nfcWriteReg(_nfc,_wire,0x633D,0);if(a1){static const uint8_t unlock[]={0x43};n=sizeof(resp);bool a2=_nfcCommThru(_nfc,_wire,unlock,1,resp,n,250)&&n>=1&&(resp[0]&0x0F)==0x0A;if(a2){_nfcWriteReg(_nfc,_wire,0x6302,0x80);_nfcWriteReg(_nfc,_wire,0x6303,0x80);static const uint8_t rd[]={0x30,0};n=sizeof(resp);if(_nfcCommThru(_nfc,_wire,rd,2,resp,n,500)&&n>=16){memcpy(out,resp,16);ok=true;}}}}}
  _nfcWriteReg(_nfc,_wire,0x633D,0);_nfcWriteReg(_nfc,_wire,0x6302,0x80);_nfcWriteReg(_nfc,_wire,0x6303,0x80);_resetAndReselect();return ok;
}
bool PN532I2cScreen::_doWriteUidToTag() {
  Header h;h.render("Write UID to Tag");renderTagPrompt("Place target tag...",bodyX(),bodyY(),bodyW(),bodyH());uint8_t u[7]={},n=0;bool found=false;uint32_t start=millis();
  while(millis()-start<5000){Uni.update();if(Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){render();return false;}if(_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,u,&n,200)){found=true;break;}delay(50);}if(!found){ShowStatusAction::show("Tag not detected",1200);render();return false;}
  MagicCardType m=_detectMagicType();if(m!=MagicCardType::GEN1A&&m!=MagicCardType::GEN3){ShowStatusAction::show("Tag not Gen1A/Gen3",2000);render();return false;}if(m==MagicCardType::GEN1A&&_uidWriteSourceLen!=4){ShowStatusAction::show("Gen1A UID must be 4 bytes",1600);render();return false;}
  uint8_t b0[16]={};bool ok=m==MagicCardType::GEN3||_readGen1aBlock0(b0);if(ok)ok=_writeMagicUid(m,_uidWriteSource,_uidWriteSourceLen,b0);
  if(ok)Uni.Lcd.fillRect(bodyX(),bodyY(),bodyW(),bodyH(),TFT_BLACK);
  ShowStatusAction::show(ok?"UID written":"Failed",1600);render();return ok;
}

void PN532I2cScreen::_doWriteDumpFromFilePicker() {
  _state = STATE_MIFARE_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = _dumpPath;
  _browser.root = _dumpPath;

  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(".bin", 320, 1024, 4096));
  if (n == 0 && _dumpPickDir == _dumpPath) {
    ShowStatusAction::show("No compatible Classic .bin");
    _goMifareTag();
    return;
  }
  setItems(_browser.items(), n);
}

void PN532I2cScreen::_doWriteDumpFileSelected(uint8_t fileIndex) {
  if (fileIndex >= _browser.count()) return;
  const auto& e = _browser.entry(fileIndex);
  if (e.isDir) {
    _dumpPickDir = e.path;
    _doWriteDumpFromFilePicker();
    return;
  }
  if (!Uni.Storage) { ShowStatusAction::show("Storage unavailable"); _goMifareTag(); return; }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Failed to open file"); _goMifareTag(); return; }
  size_t len = f.size();
  if (len != 320 && len != 1024 && len != 4096) {
    f.close(); ShowStatusAction::show("Dump size not supported"); _goMifareTag(); return;
  }
  uint8_t* dump = (uint8_t*)malloc(len);
  if (!dump) { f.close(); ShowStatusAction::show("Out of memory"); _goMifareTag(); return; }
  size_t got = f.read(dump, len); f.close();
  if (got != len) { free(dump); ShowStatusAction::show("Failed"); _goMifareTag(); return; }

  // A raw MIFARE Classic dump carries the original 4-byte UID in block 0.
  // Trust it only when the manufacturer-block BCC is valid; otherwise keep
  // the conservative file-source behaviour (UID Unknown / target preserved).
  uint8_t sourceUid[4] = { dump[0], dump[1], dump[2], dump[3] };
  const bool sourceUidKnown =
      dump[4] == (uint8_t)(sourceUid[0] ^ sourceUid[1] ^ sourceUid[2] ^ sourceUid[3]);

  _showWriteDumpPreview(dump, len,
                        sourceUidKnown ? sourceUid : nullptr,
                        sourceUidKnown ? 4 : 0,
                        true);
  free(dump);
}

void PN532I2cScreen::_showWriteDumpPreview(const uint8_t* dump, size_t len,
                                             const uint8_t* sourceUid, uint8_t sourceUidLen,
                                             bool fromFile) {
  if (!dump || (len != 320 && len != 1024 && len != 4096)) {
    ShowStatusAction::show("Invalid dump");
    return;
  }

  memcpy(_dumpImg, dump, len);
  _dumpLen = len;
  _writePreviewFromFile = fromFile;
  _writePreviewSourceUidKnown = sourceUid && (sourceUidLen == 4 || sourceUidLen == 7);
  _writePreviewSourceUidLen = _writePreviewSourceUidKnown ? sourceUidLen : 0;
  _writePreviewReplaceUid = _writePreviewSourceUidKnown;
  memset(_writePreviewSourceUid, 0, sizeof(_writePreviewSourceUid));
  if (_writePreviewSourceUidKnown) memcpy(_writePreviewSourceUid, sourceUid, sourceUidLen);

  _state = STATE_MIFARE_WRITE_PREVIEW;
  _resetRows();
  _pushRow("Source", fromFile ? "File" : "Read Tag");
  const char* type = len == 320 ? "MIFARE Classic Mini" :
                     (len == 4096 ? "MIFARE Classic 4K" : "MIFARE Classic 1K");
  _pushRow("Type", type);
  _pushRow("UID", _writePreviewSourceUidKnown ?
                    _hexUid(_writePreviewSourceUid, _writePreviewSourceUidLen) : String("Unknown"));
  _pushRow("UID Action", _writePreviewSourceUidKnown ? (_writePreviewReplaceUid ? "Replace" : "Preserve") : "Preserved");
  _pushRow("Blocks", String((unsigned)(len / 16u)));
  _pushRow("Dump", String((unsigned)len) + " bytes");

  const size_t totalSectors = len == 320 ? 5u : (len == 4096 ? 40u : 16u);
  _appendDumpNdefDetails(_dumpImg, len, totalSectors);

  _pushRow("[Press]", "Write Dump to Tag");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

bool PN532I2cScreen::_tryWriteMifareBlock(uint16_t block, const uint8_t data[16],
                                           const uint8_t key[6], bool useKeyB) {
  if (!data || !key) return false;

  // Start every write attempt from a freshly selected PICC.  A previous
  // authenticate/write leaves the card in an active Crypto1 session; trying
  // to authenticate a second block without re-selecting is unreliable on the
  // PN532.  Re-selecting also gives us a clean retry path with Key B when the
  // access conditions do not permit writes with Key A.
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
    return false;
  }
  if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;

  if (!_nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, (uint8_t)block, useKeyB ? 1 : 0,
        const_cast<uint8_t*>(key))) {
    return false;
  }

  return _nfc->mifareclassic_WriteDataBlock(
      (uint8_t)block, const_cast<uint8_t*>(data));
}

bool PN532I2cScreen::_doWriteDumpToTag(const uint8_t* dump, size_t len,
                                         const uint8_t* sourceUid, uint8_t sourceUidLen) {
  // Copy source identity before scanning the destination: _scanCardOrShow()
  // updates the screen's _uid/_uidLen members with the target tag.
  uint8_t sourceUidCopy[7] = {};
  const bool sourceUidKnown = sourceUid && (sourceUidLen == 4 || sourceUidLen == 7);
  if (sourceUidKnown) memcpy(sourceUidCopy, sourceUid, sourceUidLen);

  renderOperationTitle("Write Dump to Tag");
  if (!dump || (len != 320 && len != 1024 && len != 4096)) {
    ShowStatusAction::show("Invalid dump"); return false;
  }
  renderTagPrompt("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
  if (!_scanCardOrShow(5000)) { render(); return false; }
  auto dims = _mfDims(_sak);
  if (dims.second * 16u != len) { ShowStatusAction::show("Tag size mismatch"); render(); return false; }

  MagicCardType magic = MagicCardType::NONE;
  bool restoreUid = false;
  bool uidDiffers = false;
  const bool replaceUidRequested = sourceUidKnown && _writePreviewReplaceUid;
  if (replaceUidRequested) {
    magic = _detectMagicType();
    uidDiffers = _uidLen != sourceUidLen ||
                 (_uidLen == sourceUidLen && memcmp(_uid, sourceUidCopy, _uidLen) != 0);
    restoreUid = uidDiffers &&
                 ((magic == MagicCardType::GEN1A && sourceUidLen == 4) ||
                  (magic == MagicCardType::GEN3 && (sourceUidLen == 4 || sourceUidLen == 7)));
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t zeroKey[6] = {};
  ProgressView::init();
  size_t written = 0;
  const size_t totalWritableBlocks = dims.second - 1u;

  for (size_t sector = 0; sector < dims.first; ++sector) {
    const uint16_t first = (sector < 32) ? (uint16_t)(sector * 4) : (uint16_t)(128 + (sector - 32) * 16);
    const uint16_t trailer = (sector < 32) ? (uint16_t)(first + 3) : (uint16_t)(first + 15);
    const uint8_t* srcTrailer = dump + (size_t)trailer * 16u;

    for (uint16_t block = first; block <= trailer; ++block) {
      if (block == 0) continue; // manufacturer block is not normally writable
      const size_t currentWrite = written + 1u;
      int pct = (int)((currentWrite - 1u) * 100u / totalWritableBlocks);
      char msg[40];
      snprintf(msg, sizeof(msg), "Writing blocks (%u/%u)...",
               (unsigned)currentWrite, (unsigned)totalWritableBlocks);
      ProgressView::progress(msg, pct);

      const uint8_t* blockData = dump + (size_t)block * 16u;
      bool ok = false;

      const bool madWrite = (block == 1u || block == 2u) ||
                            (dims.first > 16 && block >= 64u && block <= 66u);

      auto tryDefaults = [&](bool useKeyB) {
        for (const auto& k : defaults) {
          auto kv = k.value();
          const uint8_t* key = (const uint8_t*)kv.data();
          if (_tryWriteMifareBlock(block, blockData, key, useKeyB)) return true;
        }
        return false;
      };

      if (madWrite) {
        ok = _tryWriteMifareBlock(block, blockData, srcTrailer + 10, true) ||
             tryDefaults(true) ||
             _tryWriteMifareBlock(block, blockData, srcTrailer, false) ||
             tryDefaults(false);
      } else {
        ok = _tryWriteMifareBlock(block, blockData, srcTrailer, false) ||
             _tryWriteMifareBlock(block, blockData, srcTrailer + 10, true) ||
             tryDefaults(false) ||
             tryDefaults(true);
      }

      if (!ok) {
        ProgressView::finish();
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        char err[32]; snprintf(err, sizeof(err), "Failed to write block %u", (unsigned)block);
        ShowStatusAction::show(err);
        render();
        return false;
      }
      written++;
    }
  }
  ProgressView::finish();

  // Magic Gen1A/Gen3 can reproduce the source identity as well as blocks
  // 1..N. Do this last: a failed data write must never change the target UID.
  if (restoreUid) {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    ShowStatusAction::show("Writing source UID...", 0);
    if (!_writeMagicUid(magic, sourceUidCopy, sourceUidLen, dump)) {
      ShowStatusAction::show("Failed to write UID");
      render();
      return false;
    }
  }

  // Do not leave the completed progress view behind the modal status box.
  // ShowStatusAction wipes only its own rectangle on dismissal, which made
  // remnants of the progress UI briefly visible during Write to Tag.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  const char* status = restoreUid ? "Tag + UID written" :
                       (replaceUidRequested && uidDiffers ? "Written; UID preserved" : "Written");
  ShowStatusAction::show(status, 1600);
  render();
  return true;
}

void PN532I2cScreen::_doEraseTag() {
  renderOperationTitle("Erase Tag");
  if (!_scanCardOrShow(5000)) { _goMifareTag(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Tag not supported"); _goMifareTag(); return; }
  _discoverDefaultKeys(true);
  if (_keyCheckCancelled) { ShowStatusAction::show("Cancelled",1000); _goMifareTag(); return; }
  if (!_hasReadableKeyForEverySector()) {
    ShowStatusAction::show("Key not available"); _goMifareTag(); return;
  }

  uint8_t zero[16] = {};
  size_t erased = 0;
  const size_t totalDataBlocks = dims.second - dims.first - 1u;
  ProgressView::init();
  for (size_t sector = 0; sector < dims.first; ++sector) {
    uint16_t first = (sector < 32) ? (uint16_t)(sector * 4) : (uint16_t)(128 + (sector - 32) * 16);
    uint16_t trailer = (sector < 32) ? (uint16_t)(first + 3) : (uint16_t)(first + 15);
    auto& a = _mfKeys[sector].first;
    auto& b = _mfKeys[sector].second;
    for (uint16_t block = first; block < trailer; ++block) {
      if (block == 0) continue;
      const size_t currentErase = erased + 1u;
      char msg[40];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...",
               (unsigned)currentErase, (unsigned)totalDataBlocks);
      ProgressView::progress(
          msg, (int)((currentErase - 1u) * 100u / totalDataBlocks));

      bool ok = false;
      // NFC Forum MAD1 uses access bits 78 77 88.  On formatted cards Key A
      // can authenticate/read the MAD data blocks, while write permission is
      // normally granted through Key B.  The CU command path reports denied
      // writes reliably; the PN532/Adafruit path can acknowledge the command
      // after a Key-A authentication without the MAD bytes actually changing.
      // Prefer Key B specifically for MAD1 blocks 1/2, then fall back to A.
      const bool madWrite = (sector == 0 && (block == 1 || block == 2));
      if (madWrite && b) {
        auto kb = b.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)kb.data(), true);
      }
      if (!ok && a) {
        auto ka = a.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)ka.data(), false);
      }
      if (!ok && !madWrite && b) {
        auto kb = b.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)kb.data(), true);
      }
      if (!ok) {
        ProgressView::finish();
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        char err[32]; snprintf(err, sizeof(err), "Failed to erase block %u", (unsigned)block);
        ShowStatusAction::show(err);
        _goMifareTag();
        return;
      }
      erased++;
    }
  }
  ProgressView::finish();

  // The CU path validated on hardware defines Erase Tag as clearing every
  // data block while preserving block 0 and all sector trailers.  Blocks 1
  // and 2 are MAD1 on Classic 1K; if either one is not actually zero after
  // the writes above, PN532 Read NDEF can still see the old NDEF allocation.
  // Read them back before reporting success so a transport-level "write OK"
  // cannot leave a silently formatted tag behind.
  if (dims.first == 16) {
    auto verifyZeroBlock = [&](uint8_t block) -> bool {
      auto& a = _mfKeys[0].first;
      auto& b = _mfKeys[0].second;
      uint8_t data[16] = {};

      auto tryRead = [&](const NFCUtility::mfKey& key, bool useKeyB) -> bool {
        uint8_t uid[7] = {};
        uint8_t uidLen = 0;
        if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300))
          return false;
        if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;
        if (!_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, block, useKeyB ? 1 : 0,
              const_cast<uint8_t*>(key.data())))
          return false;
        return _nfc->mifareclassic_ReadDataBlock(block, data);
      };

      bool read = false;
      if (a) read = tryRead(a.value(), false);
      if (!read && b) read = tryRead(b.value(), true);
      if (!read) return false;
      for (uint8_t v : data) if (v != 0) return false;
      return true;
    };

    if (!verifyZeroBlock(1) || !verifyZeroBlock(2)) {
      Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      ShowStatusAction::show("Failed: MAD remains");
      _goMifareTag();
      return;
    }
  }

  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  _hasCard = false;
  _mfKeys.fill({});
  ShowStatusAction::show("Tag erased", 1600);
  _goMifareTag();
}

void PN532I2cScreen::_doSaveDump() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir(_dumpPath);

  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");

  // Match the Chameleon Classic dump naming convention exactly:
  // MF-Mini_<UID>, MF-1K_<UID>, or MF-4K_<UID>.
  const char* typeName = (_sak == 0x09) ? "MF-Mini"
                       : (_sak == 0x18) ? "MF-4K"
                                        : "MF-1K";
  String suggested = String(typeName) + "_" + uid;

  String name = InputTextAction::popup("Save Dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }

  // Keep the editor focused on the basename, as in the Chameleon dump flow.
  // The storage format remains a raw .bin file.
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";
  String path = String(_dumpPath) + "/" + filename;

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) { render(); ShowStatusAction::show("Failed"); return; }
  if (_dumpLen == 0 || _dumpLen > sizeof(_dumpImg)) {
    f.close();
    render();
    ShowStatusAction::show("Invalid dump size");
    return;
  }
  const bool ok = f.write(_dumpImg, _dumpLen) == _dumpLen;
  f.close();

  render();
  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
  } else {
    ShowStatusAction::show("Failed", 1600);
  }
  render();
}
