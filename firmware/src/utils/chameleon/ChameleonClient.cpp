#include "utils/chameleon/ChameleonClient.h"
#include <Arduino.h>
#include <string.h>

volatile bool ChameleonClient::_notifyReady = false;
uint8_t       ChameleonClient::_notifyBuf[1100] = {};
uint16_t      ChameleonClient::_notifyLen = 0;

ChameleonClient& ChameleonClient::get() {
  static ChameleonClient inst;
  return inst;
}

// ── Protocol helpers ─────────────────────────────────────────────────────────

uint8_t ChameleonClient::_lrc(const uint8_t* d, uint16_t n) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < n; i++) sum += d[i];
  return (uint8_t)((0x100u - (sum & 0xFF)) & 0xFF);
}

void ChameleonClient::_buildFrame(uint16_t cmd, const uint8_t* d, uint16_t n,
                                   uint8_t* out, uint16_t* outLen) {
  out[0] = 0x11;
  out[1] = _lrc(out, 1);
  out[2] = (uint8_t)(cmd >> 8);
  out[3] = (uint8_t)(cmd & 0xFF);
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = (uint8_t)(n >> 8);
  out[7] = (uint8_t)(n & 0xFF);
  out[8] = _lrc(out + 2, 6);
  if (n > 0 && d) memcpy(out + 9, d, n);
  out[9 + n] = _lrc(out, 9 + n);
  *outLen = 10 + n;
}

static const uint8_t kChamNoOpFrame[10] = {
  0x11, 0xEF, 0x03, 0xFB, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00
};

bool ChameleonClient::_parseFrame(const uint8_t* d, uint16_t n,
                                   uint16_t* cmd, uint16_t* status,
                                   uint8_t* payload, uint16_t* payLen) {
  if (n < 10) return false;
  if (d[0] != 0x11) return false;
  if (_lrc(d, 1) != d[1]) return false;
  *cmd    = ((uint16_t)d[2] << 8) | d[3];
  *status = ((uint16_t)d[4] << 8) | d[5];
  uint16_t dl = ((uint16_t)d[6] << 8) | d[7];
  if (_lrc(d + 2, 6) != d[8]) return false;
  if (n < (uint16_t)(10 + dl)) return false;
  if (payload && dl > 0) memcpy(payload, d + 9, dl);
  *payLen = dl;
  if (_lrc(d, 9 + dl) != d[9 + dl]) return false;
  return true;
}

void ChameleonClient::_onNotify(NimBLERemoteCharacteristic*, uint8_t* d,
                                 size_t n, bool) {
  if (n > sizeof(_notifyBuf)) n = sizeof(_notifyBuf);
  memcpy(_notifyBuf, d, n);
  _notifyLen   = (uint16_t)n;
  _notifyReady = true;
}

// ── Connection ───────────────────────────────────────────────────────────────

bool ChameleonClient::isConnected() const {
  return _client && _client->isConnected();
}

bool ChameleonClient::connect(const NimBLEAddress& addr) {
  disconnect();

  _client = NimBLEDevice::createClient();
  if (!_client->connect(addr)) {
    NimBLEDevice::deleteClient(_client);
    _client = nullptr;
    return false;
  }

  NimBLERemoteService* svc = _client->getService(kSvcUUID);
  if (!svc) { disconnect(); return false; }

  _rxChar = svc->getCharacteristic(kRxUUID);
  _txChar = svc->getCharacteristic(kTxUUID);
  if (!_rxChar || !_txChar) { disconnect(); return false; }

  if (!_txChar->subscribe(true, _onNotify)) { disconnect(); return false; }

  // Handshake no-op frame (fixes MTU negotiation quirks on nRF stack).
  _notifyReady = false;
  _rxChar->writeValue(kChamNoOpFrame, sizeof(kChamNoOpFrame), false);
  uint32_t t0 = millis();
  while (!_notifyReady && (millis() - t0) < 500) delay(10);

  // Initial ping: getVersion
  uint8_t frame[10];
  uint16_t frameLen = 0;
  _buildFrame(CMD_GET_VERSION, nullptr, 0, frame, &frameLen);
  _notifyReady = false;
  _rxChar->writeValue(frame, frameLen, false);
  uint32_t t = millis();
  while (!_notifyReady && (millis() - t) < 1500) delay(10);

  return true;
}

void ChameleonClient::disconnect() {
  _rxChar = nullptr;
  _txChar = nullptr;
  if (_client) {
    if (_client->isConnected()) _client->disconnect();
    NimBLEDevice::deleteClient(_client);
    _client = nullptr;
  }
}

// ── Command transport ────────────────────────────────────────────────────────

bool ChameleonClient::sendCommand(uint16_t cmd, const uint8_t* data, uint16_t dataLen,
                                   uint8_t* respBuf, uint16_t* respLen, uint16_t* respStatus,
                                   uint32_t timeoutMs, uint16_t respBufSize) {
  if (!isConnected() || !_rxChar) return false;

  // Build frame on heap to keep stack small; 10 header + payload + LRC.
  uint16_t frameLen = 10 + dataLen;
  uint8_t* frame    = (uint8_t*)malloc(frameLen + 4);
  if (!frame) return false;
  _buildFrame(cmd, data, dataLen, frame, &frameLen);

  _notifyReady = false;
  bool wrote = _rxChar->writeValue(frame, frameLen, false);
  free(frame);
  if (!wrote) return false;

  uint32_t start = millis();
  while (!_notifyReady) {
    if ((millis() - start) >= timeoutMs) return false;
    delay(10);
  }

  if (_notifyLen < 10) return false;
  if (_lrc(_notifyBuf, 1) != _notifyBuf[1]) return false;
  uint16_t parsedCmd = ((uint16_t)_notifyBuf[2] << 8) | _notifyBuf[3];
  uint16_t st        = ((uint16_t)_notifyBuf[4] << 8) | _notifyBuf[5];
  uint16_t dl        = ((uint16_t)_notifyBuf[6] << 8) | _notifyBuf[7];
  if (_lrc(_notifyBuf + 2, 6) != _notifyBuf[8]) return false;
  if (_notifyLen < (uint16_t)(10 + dl)) return false;
  if (_lrc(_notifyBuf, 9 + dl) != _notifyBuf[9 + dl]) return false;
  (void)parsedCmd;

  if (respStatus) *respStatus = st;
  uint16_t copy = dl;
  if (respBuf && copy > 0) {
    if (copy > respBufSize) copy = respBufSize;
    memcpy(respBuf, _notifyBuf + 9, copy);
  }
  if (respLen) *respLen = dl;
  return true;
}

// ── High-level commands ──────────────────────────────────────────────────────

bool ChameleonClient::getVersion(char* out, uint8_t maxLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_VERSION, nullptr, 0, buf, &len, &st)) return false;
  if (len >= 3)
    snprintf(out, maxLen, "%d.%d.%d", buf[0], buf[1], buf[2]);
  else if (len >= 2)
    snprintf(out, maxLen, "%d.%d", buf[0], buf[1]);
  else { buf[len] = 0; strncpy(out, (char*)buf, maxLen); out[maxLen-1] = 0; }
  return true;
}

bool ChameleonClient::getBattery(uint8_t* pct, uint16_t* mV) {
  uint8_t buf[8] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_BATTERY, nullptr, 0, buf, &len, &st)) return false;
  if (len < 3) return false;
  if (mV)  *mV  = ((uint16_t)buf[0] << 8) | buf[1];
  if (pct) *pct = buf[2];
  return true;
}

bool ChameleonClient::getDeviceType(uint8_t* type) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_DEV_TYPE, nullptr, 0, buf, &len, &st)) return false;
  if (len < 1) return false;
  *type = buf[0];
  return true;
}

bool ChameleonClient::getChipId(char* out, uint8_t maxLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_CHIP_ID, nullptr, 0, buf, &len, &st)) return false;
  char tmp[48] = {};
  for (uint16_t i = 0; i < len && (i * 2 + 2) < (int)sizeof(tmp); i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", buf[i]);
    strcat(tmp, hex);
  }
  strncpy(out, tmp, maxLen);
  out[maxLen - 1] = 0;
  return true;
}

bool ChameleonClient::getActiveSlot(uint8_t* slot) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_ACT_SLOT, nullptr, 0, buf, &len, &st)) return false;
  if (len < 1) return false;
  *slot = buf[0];
  return true;
}

bool ChameleonClient::setActiveSlot(uint8_t slot) {
  uint16_t st = 0;
  return sendCommand(CMD_SET_SLOT, &slot, 1, nullptr, nullptr, &st);
}

bool ChameleonClient::getMode(uint8_t* mode) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_MODE, nullptr, 0, buf, &len, &st)) return false;
  if (len < 1) return false;
  *mode = buf[0];
  return true;
}

bool ChameleonClient::setMode(uint8_t mode) {
  uint16_t st = 0;
  return sendCommand(CMD_CHANGE_MODE, &mode, 1, nullptr, nullptr, &st);
}

bool ChameleonClient::getSlotTypes(SlotTypes types[8]) {
  uint8_t buf[64] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_SLOT_INFO, nullptr, 0, buf, &len, &st)) return false;
  if (len < 32) {
    for (int i = 0; i < 8; i++) { types[i].hfType = 0; types[i].lfType = 0; }
    return false;
  }
  for (int i = 0; i < 8; i++) {
    types[i].hfType = ((uint16_t)buf[i*4 + 0] << 8) | buf[i*4 + 1];
    types[i].lfType = ((uint16_t)buf[i*4 + 2] << 8) | buf[i*4 + 3];
  }
  return true;
}

bool ChameleonClient::getEnabledSlots(bool hfEn[8], bool lfEn[8]) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_EN_SLOTS, nullptr, 0, buf, &len, &st)) return false;
  if (len < 16) {
    for (int i = 0; i < 8; i++) { hfEn[i] = false; lfEn[i] = false; }
    return false;
  }
  for (int i = 0; i < 8; i++) {
    hfEn[i] = buf[i*2 + 0] != 0;
    lfEn[i] = buf[i*2 + 1] != 0;
  }
  return true;
}

bool ChameleonClient::scan14A(uint8_t uid[7], uint8_t* uidLen,
                               uint8_t atqa[2], uint8_t* sak) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_14A, nullptr, 0, buf, &len, &st, 3000)) return false;
  if (st != 0 || len < 5) return false;
  uint8_t ul = buf[0];
  if (ul > 7) ul = 7;
  *uidLen = ul;
  memcpy(uid, buf + 1, ul);
  atqa[0] = buf[1 + ul];
  atqa[1] = buf[2 + ul];
  *sak    = buf[3 + ul];
  return true;
}

bool ChameleonClient::scanEM410X(uint8_t uid[5]) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_EM410X, nullptr, 0, buf, &len, &st, 3000)) return false;
  if (st != 0 || len < 5) return false;
  memcpy(uid, buf, 5);
  return true;
}

bool ChameleonClient::setEM410XSlot(const uint8_t uid[5]) {
  uint16_t st = 0;
  return sendCommand(CMD_SET_EM410X_ID, uid, 5, nullptr, nullptr, &st);
}

const char* ChameleonClient::tagTypeName(uint16_t type) {
  switch (type) {
    case 100:  return "EM4100";
    case 101:  return "EM410Xx2";
    case 200:  return "HIDProx";
    case 1000: return "MF-Mini";
    case 1001: return "MF-1K";
    case 1002: return "MF-2K";
    case 1003: return "MF-4K";
    case 1100: return "NTAG213";
    case 1101: return "NTAG215";
    case 1102: return "NTAG216";
    case 1103: return "ULight";
    case 1107: return "NTAG210";
    case 1108: return "NTAG212";
    case 0:    return "Empty";
    default:   return "Unknown";
  }
}

uint16_t ChameleonClient::inferHFTagType(uint8_t sak, const uint8_t atqa[2]) {
  if (sak == 0x01) return 1000; // MF Classic Mini
  if (sak == 0x08) return 1001; // MF Classic 1K
  if (sak == 0x18) return 1003; // MF Classic 4K
  if (sak == 0x00) return 1100; // default NTAG/UltraLight → ntag213
  return 1001;
}

bool ChameleonClient::cloneHF(uint8_t slot, uint16_t tagType,
                               const uint8_t* uid, uint8_t uidLen,
                               const uint8_t atqa[2], uint8_t sak) {
  uint16_t st = 0;

  uint8_t typePayload[3] = { slot, (uint8_t)(tagType >> 8), (uint8_t)(tagType & 0xFF) };
  if (!sendCommand(CMD_SET_SLOT_TAG_TYPE, typePayload, 3, nullptr, nullptr, &st)) return false;

  // ATQA bytes are reversed in the anti-collision payload
  uint8_t acoPayload[11] = {};
  acoPayload[0] = uidLen;
  memcpy(acoPayload + 1, uid, uidLen);
  acoPayload[1 + uidLen] = atqa[1];
  acoPayload[2 + uidLen] = atqa[0];
  acoPayload[3 + uidLen] = sak;
  if (!sendCommand(CMD_MF1_SET_ANTI_COLL, acoPayload, 4 + uidLen, nullptr, nullptr, &st)) return false;

  uint8_t enPayload[3] = { slot, 2, 1 }; // freq=HF(2), enabled
  if (!sendCommand(CMD_SET_SLOT_ENABLE, enPayload, 3, nullptr, nullptr, &st)) return false;

  return setMode(0); // emulator mode
}

// ── Git version ──────────────────────────────────────────────────────────────
bool ChameleonClient::getGitVersion(char* out, uint8_t maxLen) {
  uint8_t buf[64] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_GIT_VERSION, nullptr, 0, buf, &len, &st)) return false;
  if (len >= maxLen) len = maxLen - 1;
  memcpy(out, buf, len);
  out[len] = 0;
  return true;
}

// ── Device settings ──────────────────────────────────────────────────────────
bool ChameleonClient::getDeviceSettings(DeviceSettings* out) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_DEV_SETTINGS, nullptr, 0, buf, &len, &st)) return false;
  if (len < 13) return false;
  out->settingsVersion   = buf[0];
  out->animation         = buf[1];
  out->btnAShort         = buf[2];
  out->btnBShort         = buf[3];
  out->btnALong          = buf[4];
  out->btnBLong          = buf[5];
  out->blePairingEnabled = buf[6];
  memcpy(out->pairingPin, buf + 7, 6);
  out->pairingPin[6] = 0;
  return true;
}

bool ChameleonClient::saveSettings() {
  uint16_t st = 0;
  return sendCommand(CMD_SAVE_SETTINGS, nullptr, 0, nullptr, nullptr, &st);
}

bool ChameleonClient::resetSettings() {
  uint16_t st = 0;
  return sendCommand(CMD_RESET_SETTINGS, nullptr, 0, nullptr, nullptr, &st);
}

bool ChameleonClient::setAnimation(uint8_t mode) {
  uint16_t st = 0;
  return sendCommand(CMD_SET_ANIMATION, &mode, 1, nullptr, nullptr, &st);
}

bool ChameleonClient::getAnimation(uint8_t* mode) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_ANIMATION, nullptr, 0, buf, &len, &st)) return false;
  if (len < 1) return false;
  *mode = buf[0];
  return true;
}

// buttonIdx: 'A' or 'B' ASCII. Upstream uses ASCII codes for the button index.
bool ChameleonClient::setButtonConfig(uint8_t buttonIdx, bool longPress, uint8_t action) {
  uint16_t st = 0;
  uint8_t payload[2] = { buttonIdx, action };
  uint16_t cmd = longPress ? CMD_SET_LBTN_PRESS : CMD_SET_BTN_PRESS;
  return sendCommand(cmd, payload, 2, nullptr, nullptr, &st);
}

bool ChameleonClient::getButtonConfig(uint8_t buttonIdx, bool longPress, uint8_t* action) {
  uint16_t cmd = longPress ? CMD_GET_LBTN_PRESS : CMD_GET_BTN_PRESS;
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(cmd, &buttonIdx, 1, buf, &len, &st)) return false;
  if (len < 1) return false;
  *action = buf[0];
  return true;
}

bool ChameleonClient::setBlePairingEnabled(bool on) {
  uint16_t st = 0;
  uint8_t v = on ? 1 : 0;
  return sendCommand(CMD_BLE_SET_PAIR, &v, 1, nullptr, nullptr, &st);
}

bool ChameleonClient::clearBleBonds() {
  uint16_t st = 0;
  return sendCommand(CMD_BLE_CLEAR_BONDS, nullptr, 0, nullptr, nullptr, &st, 500);
}

// ── Slot edit ────────────────────────────────────────────────────────────────
bool ChameleonClient::setSlotTagType(uint8_t slot, uint16_t tagType) {
  uint16_t st = 0;
  uint8_t p[3] = { slot, (uint8_t)(tagType >> 8), (uint8_t)(tagType & 0xFF) };
  return sendCommand(CMD_SET_SLOT_TAG_TYPE, p, 3, nullptr, nullptr, &st);
}

bool ChameleonClient::setSlotDataDefault(uint8_t slot, uint16_t tagType) {
  uint16_t st = 0;
  uint8_t p[3] = { slot, (uint8_t)(tagType >> 8), (uint8_t)(tagType & 0xFF) };
  return sendCommand(CMD_SET_SLOT_DATA_DEF, p, 3, nullptr, nullptr, &st);
}

bool ChameleonClient::setSlotEnable(uint8_t slot, uint8_t freq, bool enabled) {
  uint16_t st = 0;
  uint8_t p[3] = { slot, freq, (uint8_t)(enabled ? 1 : 0) };
  return sendCommand(CMD_SET_SLOT_ENABLE, p, 3, nullptr, nullptr, &st);
}

bool ChameleonClient::setSlotNick(uint8_t slot, uint8_t freq, const char* name) {
  uint16_t st = 0;
  uint8_t buf[48];
  buf[0] = slot;
  buf[1] = freq;
  size_t nl = strlen(name);
  if (nl > 32) nl = 32;
  memcpy(buf + 2, name, nl);
  return sendCommand(CMD_SET_SLOT_NICK, buf, 2 + nl, nullptr, nullptr, &st);
}

bool ChameleonClient::getSlotNick(uint8_t slot, uint8_t freq, char* out, uint8_t maxLen) {
  uint16_t st = 0;
  uint8_t p[2] = { slot, freq };
  uint8_t buf[64] = {};
  uint16_t len = 0;
  if (!sendCommand(CMD_GET_SLOT_NICK, p, 2, buf, &len, &st)) return false;
  if (st != 0) { out[0] = 0; return false; }
  if (len >= maxLen) len = maxLen - 1;
  memcpy(out, buf, len);
  out[len] = 0;
  return true;
}

bool ChameleonClient::saveSlotNicks() {
  uint16_t st = 0;
  return sendCommand(CMD_SAVE_SLOT_NICKS, nullptr, 0, nullptr, nullptr, &st);
}

bool ChameleonClient::deleteSlot(uint8_t slot, uint8_t freq) {
  uint16_t st = 0;
  uint8_t p[2] = { slot, freq };
  return sendCommand(CMD_DELETE_SLOT, p, 2, nullptr, nullptr, &st);
}

// ── MF Classic probes / IO ───────────────────────────────────────────────────
bool ChameleonClient::mf1Support() {
  uint16_t st = 0;
  if (!sendCommand(CMD_MF1_SUPPORT, nullptr, 0, nullptr, nullptr, &st, 3000)) return false;
  return st == 0;
}

bool ChameleonClient::mf1NTLevel(uint8_t* level) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_NT_LEVEL, nullptr, 0, buf, &len, &st, 4000)) return false;
  if (st != 0 || len < 1) return false;
  *level = buf[0];
  return true;
}

bool ChameleonClient::mf1CheckKey(uint8_t block, uint8_t keyType, const uint8_t key[6]) {
  uint8_t p[8];
  p[0] = keyType; // 0x60 = keyA, 0x61 = keyB
  p[1] = block;
  memcpy(p + 2, key, 6);
  uint16_t st = 0;
  if (!sendCommand(CMD_MF1_CHECK_KEY, p, 8, nullptr, nullptr, &st, 1500)) return false;
  return st == 0;
}

bool ChameleonClient::mf1ReadBlock(uint8_t block, uint8_t keyType,
                                    const uint8_t key[6], uint8_t out[16]) {
  uint8_t p[8];
  p[0] = keyType;
  p[1] = block;
  memcpy(p + 2, key, 6);
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_READ_BLOCK, p, 8, buf, &len, &st, 1500)) return false;
  if (st != 0 || len < 16) return false;
  memcpy(out, buf, 16);
  return true;
}

bool ChameleonClient::mf1CheckKeysOfBlock(uint8_t block, uint8_t keyType,
                                           const uint8_t* keys, uint8_t keyCount,
                                           uint32_t* hitBitmap) {
  if (keyCount == 0 || keyCount > 32) return false;
  uint8_t p[2 + 32 * 6];
  p[0] = keyType;
  p[1] = block;
  memcpy(p + 2, keys, 6 * keyCount);
  uint8_t rsp[48] = {};
  uint16_t rspLen = 0, st = 0;
  if (!sendCommand(CMD_MF1_CHECK_BLOCK, p, 2 + 6 * keyCount,
                   rsp, &rspLen, &st, 8000)) return false;
  if (st != 0) { *hitBitmap = 0; return false; }
  // Firmware packs hits little-endian: byte i bit b → key index (i*8 + b).
  uint32_t bm = 0;
  for (uint16_t i = 0; i < rspLen && i < 4; i++)
    bm |= ((uint32_t)rsp[i]) << (i * 8);
  *hitBitmap = bm;
  return true;
}

bool ChameleonClient::mf1LoadBlockData(uint8_t slot, uint8_t startBlock,
                                        const uint8_t* data, uint16_t dataLen) {
  uint16_t st = 0;
  // Buffer on heap to keep stack small.
  uint8_t* buf = (uint8_t*)malloc(2 + dataLen);
  if (!buf) return false;
  buf[0] = slot;
  buf[1] = startBlock;
  memcpy(buf + 2, data, dataLen);
  bool ok = sendCommand(CMD_MF1_LOAD_BLOCK, buf, 2 + dataLen,
                        nullptr, nullptr, &st, 3000);
  free(buf);
  return ok && st == 0;
}

bool ChameleonClient::mf1GetBlockData(uint8_t startBlock, uint8_t count, uint8_t* out) {
  if (count == 0) return false;
  uint8_t p[2] = { startBlock, count };
  uint16_t st = 0, rlen = 0;
  uint16_t wantBytes = (uint16_t)count * 16;
  bool ok = sendCommand(CMD_MF1_GET_BLOCK, p, 2,
                        out, &rlen, &st, 3000, wantBytes);
  return ok && st == 0 && rlen >= wantBytes;
}

bool ChameleonClient::hf14ARaw(uint8_t options, uint16_t timeoutMs, uint16_t bitLen,
                                const uint8_t* data, uint16_t dataBytes,
                                uint8_t* respOut, uint16_t* respLen,
                                uint16_t respBufSize) {
  uint16_t payLen = 5 + dataBytes;
  uint8_t* p = (uint8_t*)malloc(payLen);
  if (!p) return false;
  p[0] = options;
  p[1] = (uint8_t)(timeoutMs >> 8);
  p[2] = (uint8_t)(timeoutMs & 0xFF);
  p[3] = (uint8_t)(bitLen >> 8);
  p[4] = (uint8_t)(bitLen & 0xFF);
  if (dataBytes) memcpy(p + 5, data, dataBytes);
  uint16_t st = 0;
  bool ok = sendCommand(CMD_HF14A_RAW, p, payLen, respOut, respLen, &st,
                        timeoutMs + 1500, respBufSize);
  free(p);
  return ok;
}

// ── MFKey32 log ──────────────────────────────────────────────────────────────
bool ChameleonClient::mf1SetDetectEnable(bool on) {
  uint8_t v = on ? 1 : 0;
  uint16_t st = 0;
  return sendCommand(CMD_MF1_DET_ENABLE, &v, 1, nullptr, nullptr, &st);
}

bool ChameleonClient::mf1GetDetectCount(uint32_t* count) {
  uint8_t buf[8] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_DET_COUNT, nullptr, 0, buf, &len, &st)) return false;
  if (len < 4) return false;
  *count = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
  return true;
}

bool ChameleonClient::mf1GetDetectRecord(uint32_t index, uint8_t out[18]) {
  uint8_t p[4] = {
    (uint8_t)(index >> 24), (uint8_t)(index >> 16),
    (uint8_t)(index >>  8), (uint8_t)(index & 0xFF)
  };
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_DET_RESULT, p, 4, buf, &len, &st, 2500)) return false;
  if (st != 0 || len < 18) return false;
  memcpy(out, buf, 18);
  return true;
}

// ── LF scan/clone ────────────────────────────────────────────────────────────
bool ChameleonClient::scanHIDProx(uint8_t payload[13], uint8_t* payloadLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_HID_PROX, nullptr, 0, buf, &len, &st, 3000)) return false;
  if (st != 0 || len == 0) return false;
  uint16_t cp = len > 13 ? 13 : len;
  memcpy(payload, buf, cp);
  *payloadLen = (uint8_t)cp;
  return true;
}

bool ChameleonClient::scanViking(uint8_t uid[4], uint8_t* uidLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_VIKING, nullptr, 0, buf, &len, &st, 3000)) return false;
  if (st != 0 || len == 0) return false;
  uint16_t cp = len > 4 ? 4 : len;
  memcpy(uid, buf, cp);
  *uidLen = (uint8_t)cp;
  return true;
}

static uint8_t _t5577Default[4] = { 0x51, 0x24, 0x36, 0x48 };

bool ChameleonClient::writeEM410XToT5577(const uint8_t uid[5], const uint8_t newKey[4],
                                          const uint8_t* oldKeys, uint8_t oldKeyCount) {
  uint8_t p[5 + 4 + 4 * 8];
  memcpy(p, uid, 5);
  memcpy(p + 5, newKey ? newKey : _t5577Default, 4);
  uint16_t off = 9;
  for (uint8_t i = 0; i < oldKeyCount && off + 4 <= sizeof(p); i++) {
    memcpy(p + off, oldKeys + i * 4, 4);
    off += 4;
  }
  uint16_t st = 0;
  if (!sendCommand(CMD_WRITE_EM410X_T5, p, off, nullptr, nullptr, &st, 8000)) return false;
  return st == 0;
}

bool ChameleonClient::writeHIDProxToT5577(const uint8_t* payload, uint8_t payloadLen,
                                           const uint8_t newKey[4],
                                           const uint8_t* oldKeys, uint8_t oldKeyCount) {
  uint8_t p[32 + 4 + 32];
  if (payloadLen > 32) payloadLen = 32;
  memcpy(p, payload, payloadLen);
  memcpy(p + payloadLen, newKey ? newKey : _t5577Default, 4);
  uint16_t off = payloadLen + 4;
  for (uint8_t i = 0; i < oldKeyCount && off + 4 <= sizeof(p); i++) {
    memcpy(p + off, oldKeys + i * 4, 4);
    off += 4;
  }
  uint16_t st = 0;
  if (!sendCommand(CMD_WRITE_HID_T5, p, off, nullptr, nullptr, &st, 8000)) return false;
  return st == 0;
}

bool ChameleonClient::writeVikingToT5577(const uint8_t uid[4], const uint8_t newKey[4],
                                          const uint8_t* oldKeys, uint8_t oldKeyCount) {
  uint8_t p[4 + 4 + 32];
  memcpy(p, uid, 4);
  memcpy(p + 4, newKey ? newKey : _t5577Default, 4);
  uint16_t off = 8;
  for (uint8_t i = 0; i < oldKeyCount && off + 4 <= sizeof(p); i++) {
    memcpy(p + off, oldKeys + i * 4, 4);
    off += 4;
  }
  uint16_t st = 0;
  if (!sendCommand(CMD_WRITE_VIKING_T5, p, off, nullptr, nullptr, &st, 8000)) return false;
  return st == 0;
}

bool ChameleonClient::setHIDProxSlot(const uint8_t* payload, uint8_t payloadLen) {
  uint16_t st = 0;
  return sendCommand(CMD_SET_HID_PROX_ID, payload, payloadLen, nullptr, nullptr, &st);
}

bool ChameleonClient::setVikingSlot(const uint8_t uid[4], uint8_t uidLen) {
  uint16_t st = 0;
  return sendCommand(CMD_SET_VIKING_ID, uid, uidLen, nullptr, nullptr, &st);
}

bool ChameleonClient::getEM410XSlot(uint8_t uid[5]) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_EM410X_ID, nullptr, 0, buf, &len, &st)) return false;
  if (st != 0 || len < 5) return false;
  memcpy(uid, buf, 5);
  return true;
}

bool ChameleonClient::getHIDProxSlot(uint8_t payload[13], uint8_t* payloadLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_HID_PROX_ID, nullptr, 0, buf, &len, &st)) return false;
  if (st != 0 || len == 0) return false;
  uint16_t cp = len > 13 ? 13 : len;
  memcpy(payload, buf, cp);
  *payloadLen = (uint8_t)cp;
  return true;
}

bool ChameleonClient::getVikingSlot(uint8_t uid[4], uint8_t* uidLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_VIKING_ID, nullptr, 0, buf, &len, &st)) return false;
  if (st != 0 || len == 0) return false;
  uint16_t cp = len > 4 ? 4 : len;
  memcpy(uid, buf, cp);
  *uidLen = (uint8_t)cp;
  return true;
}