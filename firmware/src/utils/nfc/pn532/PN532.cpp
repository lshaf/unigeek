#include "PN532.h"

using R = IPN532Transport::Result;

bool PN532::init() {
  _t.wakeup();
  // SAMConfiguration: 0x14 + mode(0x01 normal) + timeout(0x14) + use_irq(0x01)
  uint8_t cfg[3] = { 0x01, 0x14, 0x01 };
  uint8_t resp[8];
  size_t respLen = 0;
  R r = _t.transceive(CMD_SAM_CONFIGURATION, cfg, sizeof(cfg),
                      resp, sizeof(resp), respLen, 500);
  return _ok(r);
}

bool PN532::getFirmwareVersion(FirmwareInfo& out) {
  uint8_t resp[8];
  size_t  respLen = 0;
  R r = _t.transceive(CMD_GET_FIRMWARE_VERSION, nullptr, 0,
                      resp, sizeof(resp), respLen, 500);
  if (!_ok(r) || respLen < 4) return false;
  out.ic       = resp[0];
  out.version  = resp[1];
  out.revision = resp[2];
  out.support  = resp[3];
  out.valid    = true;
  return true;
}

bool PN532::isPN532Killer(uint8_t& killerCode) {
  uint8_t resp[8];
  size_t  respLen = 0;
  R r = _t.transceive(CMD_KILLER_DETECT, nullptr, 0,
                      resp, sizeof(resp), respLen, 200);
  if (!_ok(r) || respLen < 1) return false;
  killerCode = resp[0];
  return respLen > 2;  // stock PN532 returns ERR_BAD_FRAME; killer returns a multi-byte payload
}

bool PN532::listPassiveTarget14A(Target14A& out, uint32_t timeoutMs) {
  uint8_t param[2] = { 0x01, BR_106_TYPE_A };
  uint8_t resp[64];
  size_t  respLen = 0;
  R r = _t.transceive(CMD_IN_LIST_PASSIVE_TARGET, param, sizeof(param),
                      resp, sizeof(resp), respLen, timeoutMs);
  if (!_ok(r)) return false;
  if (respLen < 1 || resp[0] == 0) return false;       // NbTg
  // resp: NbTg | TgN | SENS_RES(2) | SEL_RES | NFCIDLen | NFCID... | [ATSLen | ATS...]
  if (respLen < 6) return false;
  out.atqa   = ((uint16_t)resp[2] << 8) | resp[3];
  out.sak    = resp[4];
  out.uidLen = resp[5];
  if (out.uidLen > sizeof(out.uid) || respLen < (size_t)6 + out.uidLen) return false;
  memcpy(out.uid, &resp[6], out.uidLen);
  size_t idx = 6 + out.uidLen;
  out.atsLen = 0;
  if (idx < respLen) {
    uint8_t atsLen = resp[idx++];
    if (atsLen > 0 && atsLen <= sizeof(out.ats) && idx + (size_t)(atsLen - 1) <= respLen) {
      // ATS field starts with TL (atsLen) followed by atsLen-1 bytes
      out.atsLen = atsLen - 1;
      memcpy(out.ats, &resp[idx], out.atsLen);
    }
  }
  return true;
}

bool PN532::listPassiveTarget15(Target15& out, uint32_t timeoutMs) {
  uint8_t param[2] = { 0x01, BR_ISO15693 };
  uint8_t resp[32];
  size_t  respLen = 0;
  R r = _t.transceive(CMD_IN_LIST_PASSIVE_TARGET, param, sizeof(param),
                      resp, sizeof(resp), respLen, timeoutMs);
  if (!_ok(r)) return false;
  if (respLen < 1 || resp[0] == 0) return false;
  // resp: NbTg | TgN | DSFID | UID(8 bytes, big-endian per PN532)
  if (respLen < 11) return false;
  out.dsfid = resp[2];
  // pn532-python reverses to little-endian for display; keep big-endian here, caller can flip
  for (int i = 0; i < 8; i++) out.uid[i] = resp[3 + i];
  out.valid = true;
  return true;
}

bool PN532::listPassiveTargetEM4100(TargetLF& out, uint32_t timeoutMs) {
  uint8_t param[2] = { 0x01, BR_EM4100 };
  uint8_t resp[16];
  size_t  respLen = 0;
  R r = _t.transceive(CMD_IN_LIST_PASSIVE_TARGET, param, sizeof(param),
                      resp, sizeof(resp), respLen, timeoutMs);
  if (!_ok(r)) return false;
  if (respLen < 1 || resp[0] == 0) return false;
  // resp shape: NbTg | TgN | UID(5)
  if (respLen < 7) return false;
  out.uidLen = 5;
  memcpy(out.uid, &resp[2], 5);
  return true;
}

bool PN532::inRelease(uint8_t target) {
  uint8_t resp[4]; size_t respLen = 0;
  R r = _t.transceive(CMD_IN_RELEASE, &target, 1, resp, sizeof(resp), respLen, 200);
  return _ok(r) && respLen >= 1 && resp[0] == 0x00;
}

bool PN532::inDeselect(uint8_t target) {
  uint8_t resp[4]; size_t respLen = 0;
  R r = _t.transceive(CMD_IN_DESELECT, &target, 1, resp, sizeof(resp), respLen, 200);
  return _ok(r) && respLen >= 1 && resp[0] == 0x00;
}

bool PN532::inDataExchange(const uint8_t* payload, size_t payloadLen,
                           uint8_t* out, size_t outCap, size_t& outLen,
                           uint32_t timeoutMs) {
  R r = _t.transceive(CMD_IN_DATA_EXCHANGE, payload, payloadLen,
                      out, outCap, outLen, timeoutMs);
  if (!_ok(r) || outLen < 1) return false;
  // First byte is status: 0x00 = success.
  if (out[0] != 0x00) return false;
  // Strip status byte from returned data.
  if (outLen > 1) memmove(out, out + 1, outLen - 1);
  outLen -= 1;
  return true;
}

bool PN532::inCommunicateThru(const uint8_t* payload, size_t payloadLen,
                              uint8_t* out, size_t outCap, size_t& outLen,
                              uint32_t timeoutMs) {
  R r = _t.transceive(CMD_IN_COMMUNICATE_THRU, payload, payloadLen,
                      out, outCap, outLen, timeoutMs);
  if (!_ok(r) || outLen < 1) return false;
  if (out[0] != 0x00) return false;
  if (outLen > 1) memmove(out, out + 1, outLen - 1);
  outLen -= 1;
  return true;
}

bool PN532::mifareAuth(uint8_t block, bool useKeyB,
                       const uint8_t key[6], const uint8_t uid[4]) {
  uint8_t buf[1 + 1 + 1 + 6 + 4];
  buf[0] = 0x01;                        // logical target
  buf[1] = useKeyB ? MF_AUTH_B : MF_AUTH_A;
  buf[2] = block;
  memcpy(&buf[3], key, 6);
  memcpy(&buf[9], uid, 4);
  uint8_t resp[8]; size_t respLen = 0;
  return inDataExchange(buf, sizeof(buf), resp, sizeof(resp), respLen, 500);
}

bool PN532::mifareRead(uint8_t block, uint8_t out16[16]) {
  uint8_t buf[3] = { 0x01, MF_READ, block };
  uint8_t resp[24]; size_t respLen = 0;
  if (!inDataExchange(buf, sizeof(buf), resp, sizeof(resp), respLen, 500)) return false;
  if (respLen < 16) return false;
  memcpy(out16, resp, 16);
  return true;
}

bool PN532::mifareWrite(uint8_t block, const uint8_t data[16]) {
  uint8_t buf[3 + 16];
  buf[0] = 0x01;
  buf[1] = MF_WRITE_16;
  buf[2] = block;
  memcpy(&buf[3], data, 16);
  uint8_t resp[8]; size_t respLen = 0;
  return inDataExchange(buf, sizeof(buf), resp, sizeof(resp), respLen, 500);
}

bool PN532::ultralightRead(uint8_t page, uint8_t out16[16]) {
  uint8_t buf[3] = { 0x01, MF_READ, page };
  uint8_t resp[24]; size_t respLen = 0;
  if (!inDataExchange(buf, sizeof(buf), resp, sizeof(resp), respLen, 500)) return false;
  if (respLen < 16) return false;
  memcpy(out16, resp, 16);
  return true;
}

bool PN532::ultralightWrite4(uint8_t page, const uint8_t data[4]) {
  uint8_t buf[3 + 4];
  buf[0] = 0x01;
  buf[1] = MF_WRITE_4;
  buf[2] = page;
  memcpy(&buf[3], data, 4);
  uint8_t resp[8]; size_t respLen = 0;
  return inDataExchange(buf, sizeof(buf), resp, sizeof(resp), respLen, 500);
}

bool PN532::tgInitAsTarget(uint16_t atqa, uint8_t sak,
                            const uint8_t* uid, uint8_t uidLen,
                            uint32_t timeoutMs) {
  // Mode(1) + MifareParams(6) + FeliCaParams(18) + NFCIDTarget(10) = 35
  uint8_t params[35];
  memset(params, 0, sizeof(params));
  params[0] = 0x00;                         // passive target, no DEP/FeliCa
  params[1] = (atqa >> 8) & 0xFF;           // SENS_RES[0]
  params[2] = atqa & 0xFF;                  // SENS_RES[1]
  uint8_t n = uidLen >= 3 ? 3 : uidLen;
  memcpy(&params[3], uid, n);               // NFCID1 (3 bytes)
  params[6] = sak;                          // SEL_RES
  // params[7..24]: FeliCaParams (zeros = skip FeliCa)
  // params[25..34]: NFCIDTarget (zeros = no DEP)

  uint8_t resp[4]; size_t respLen = 0;
  R r = _t.transceive(CMD_TG_INIT_AS_TARGET, params, sizeof(params),
                      resp, sizeof(resp), respLen, timeoutMs);
  return _ok(r);
}

bool PN532::isGen1a() {
  // Gen1a unlock: 7-bit 0x40 then 0x43 — both via InCommunicateThru with bit-frame mode.
  // pn532-python triggers this by raw 14a; we rely on InCommunicateThru after a fresh select.
  // Send 0x40 with WriteRegister to set bit framing to 7 bits, then send 0x40, then 0x43.
  // Simpler heuristic: try the dual-byte unlock and see if both ACKs succeed.
  uint8_t resp[8]; size_t respLen = 0;
  // 0x40 unlock (only first 7 bits matter, but PN532 still handles it)
  uint8_t u1[] = { 0x40 };
  if (!inCommunicateThru(u1, sizeof(u1), resp, sizeof(resp), respLen, 200)) return false;
  if (respLen < 1 || (resp[0] & 0x0F) != 0x0A) return false;
  uint8_t u2[] = { 0x43 };
  if (!inCommunicateThru(u2, sizeof(u2), resp, sizeof(resp), respLen, 200)) return false;
  return respLen >= 1 && (resp[0] & 0x0F) == 0x0A;
}

bool PN532::gen3SetUid(const uint8_t uid[7], uint8_t uidLen) {
  // Magic Gen3 UID setter: 90 FB CC CC <len> <uid...>
  if (uidLen != 4 && uidLen != 7) return false;
  uint8_t buf[5 + 7];
  buf[0] = 0x90;
  buf[1] = 0xFB;
  buf[2] = 0xCC;
  buf[3] = 0xCC;
  buf[4] = uidLen;
  memcpy(&buf[5], uid, uidLen);
  uint8_t resp[16]; size_t respLen = 0;
  return inCommunicateThru(buf, 5 + uidLen, resp, sizeof(resp), respLen, 500);
}

bool PN532::gen3LockUid() {
  uint8_t buf[5] = { 0x90, 0xFD, 0x11, 0x11, 0x00 };
  uint8_t resp[8]; size_t respLen = 0;
  return inCommunicateThru(buf, sizeof(buf), resp, sizeof(resp), respLen, 500);
}
