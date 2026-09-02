#include "utils/ble/ChameleonClient.h"
#include <Arduino.h>
#include <string.h>

volatile bool ChameleonClient::_notifyReady = false;
uint8_t       ChameleonClient::_notifyBuf[1100] = {};
uint16_t      ChameleonClient::_notifyLen = 0;

// Notification callbacks run in the NimBLE host task while sendCommand() runs
// in its caller's task. Protect the shared single-response buffer and ready
// flag so a new notification cannot overwrite or be cleared while it is being
// inspected.
static portMUX_TYPE s_notifyMux = portMUX_INITIALIZER_UNLOCKED;
static bool           s_waitingForCmd = false;
static uint16_t       s_expectedCmd = 0;

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

  portENTER_CRITICAL(&s_notifyMux);

  // While a command is in flight, ignore notifications for other commands
  // before they can overwrite a matching response already waiting in the
  // single-response buffer.
  if (s_waitingForCmd && n >= 4) {
    const uint16_t incomingCmd = ((uint16_t)d[2] << 8) | d[3];
    if (incomingCmd != s_expectedCmd) {
      portEXIT_CRITICAL(&s_notifyMux);
      return;
    }
  }

  memcpy(_notifyBuf, d, n);
  _notifyLen   = (uint16_t)n;
  _notifyReady = true;
  portEXIT_CRITICAL(&s_notifyMux);
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

  // Handshake frame used to settle MTU / notification timing on the nRF stack.
  portENTER_CRITICAL(&s_notifyMux);
  _notifyReady = false;
  s_expectedCmd = 0x03FB;
  s_waitingForCmd = true;
  portEXIT_CRITICAL(&s_notifyMux);

  _rxChar->writeValue(kChamNoOpFrame, sizeof(kChamNoOpFrame), false);
  uint32_t t0 = millis();
  for (;;) {
    bool ready = false;
    portENTER_CRITICAL(&s_notifyMux);
    ready = _notifyReady;
    portEXIT_CRITICAL(&s_notifyMux);
    if (ready || (millis() - t0) >= 500) break;
    delay(10);
  }

  portENTER_CRITICAL(&s_notifyMux);
  s_waitingForCmd = false;
  _notifyReady = false;
  portEXIT_CRITICAL(&s_notifyMux);

  // Initial ping: getVersion.
  uint8_t frame[10];
  uint16_t frameLen = 0;
  _buildFrame(CMD_GET_VERSION, nullptr, 0, frame, &frameLen);

  portENTER_CRITICAL(&s_notifyMux);
  s_expectedCmd = CMD_GET_VERSION;
  s_waitingForCmd = true;
  portEXIT_CRITICAL(&s_notifyMux);

  _rxChar->writeValue(frame, frameLen, false);
  uint32_t t = millis();
  for (;;) {
    bool ready = false;
    portENTER_CRITICAL(&s_notifyMux);
    ready = _notifyReady;
    portEXIT_CRITICAL(&s_notifyMux);
    if (ready || (millis() - t) >= 1500) break;
    delay(10);
  }

  portENTER_CRITICAL(&s_notifyMux);
  s_waitingForCmd = false;
  _notifyReady = false;
  portEXIT_CRITICAL(&s_notifyMux);

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

  portENTER_CRITICAL(&s_notifyMux);
  _notifyReady = false;
  s_expectedCmd = cmd;
  s_waitingForCmd = true;
  portEXIT_CRITICAL(&s_notifyMux);

  bool wrote = _rxChar->writeValue(frame, frameLen, false);
  free(frame);
  if (!wrote) {
    portENTER_CRITICAL(&s_notifyMux);
    s_waitingForCmd = false;
    portEXIT_CRITICAL(&s_notifyMux);
    return false;
  }

  uint32_t start = millis();

  for (;;) {
    bool ready = false;
    while (!ready) {
      portENTER_CRITICAL(&s_notifyMux);
      ready = _notifyReady;
      portEXIT_CRITICAL(&s_notifyMux);

      if (ready) break;
      if ((millis() - start) >= timeoutMs) {
        portENTER_CRITICAL(&s_notifyMux);
        s_waitingForCmd = false;
        _notifyReady = false;
        portEXIT_CRITICAL(&s_notifyMux);
        return false;
      }
      delay(10);
    }

    // Keep the notification buffer stable while validating and consuming it.
    // The NimBLE callback will wait briefly on the same spinlock if another
    // notification arrives at this exact moment.
    portENTER_CRITICAL(&s_notifyMux);

    if (!_notifyReady) {
      portEXIT_CRITICAL(&s_notifyMux);
      continue;
    }

    if (_notifyLen < 10) {
      _notifyReady = false;
      s_waitingForCmd = false;
      portEXIT_CRITICAL(&s_notifyMux);
      return false;
    }
    if (_lrc(_notifyBuf, 1) != _notifyBuf[1]) {
      _notifyReady = false;
      s_waitingForCmd = false;
      portEXIT_CRITICAL(&s_notifyMux);
      return false;
    }

    uint16_t parsedCmd = ((uint16_t)_notifyBuf[2] << 8) | _notifyBuf[3];
    uint16_t st        = ((uint16_t)_notifyBuf[4] << 8) | _notifyBuf[5];
    uint16_t dl        = ((uint16_t)_notifyBuf[6] << 8) | _notifyBuf[7];

    if (_lrc(_notifyBuf + 2, 6) != _notifyBuf[8] ||
        _notifyLen < (uint16_t)(10 + dl) ||
        _lrc(_notifyBuf, 9 + dl) != _notifyBuf[9 + dl]) {
      _notifyReady = false;
      s_waitingForCmd = false;
      portEXIT_CRITICAL(&s_notifyMux);
      return false;
    }

    // A delayed notification from a previous command must not be accepted as
    // the response to the command currently in flight. Consume it atomically
    // and keep waiting within the original timeout window.
    if (parsedCmd != cmd) {
      _notifyReady = false;
      portEXIT_CRITICAL(&s_notifyMux);
      continue;
    }

    if (respBuf && respBufSize == 0) {
      _notifyReady = false;
      s_waitingForCmd = false;
      portEXIT_CRITICAL(&s_notifyMux);
      return false;
    }

    if (respStatus) *respStatus = st;

    uint16_t copy = dl;
    if (respBuf && copy > 0) {
      if (copy > respBufSize) copy = respBufSize;
      memcpy(respBuf, _notifyBuf + 9, copy);
    }
    if (respLen) *respLen = copy;

    _notifyReady = false;
    s_waitingForCmd = false;
    portEXIT_CRITICAL(&s_notifyMux);
    return true;
  }
}

// ── High-level commands ──────────────────────────────────────────────────────

bool ChameleonClient::getVersion(char* out, uint8_t maxLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_VERSION, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_BATTERY, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len < 3) return false;
  if (mV)  *mV  = ((uint16_t)buf[0] << 8) | buf[1];
  if (pct) *pct = buf[2];
  return true;
}

bool ChameleonClient::getDeviceType(uint8_t* type) {
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_DEV_TYPE, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len < 1) return false;
  *type = buf[0];
  return true;
}

bool ChameleonClient::getChipId(char* out, uint8_t maxLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_CHIP_ID, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_ACT_SLOT, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_MODE, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_SLOT_INFO, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_EN_SLOTS, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_SCAN_14A, nullptr, 0, buf, &len, &st, 3000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_SCAN_EM410X, nullptr, 0, buf, &len, &st, 3000, sizeof(buf))) return false;
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
    case 1104: return "ULight-C";
    case 1105: return "UL-EV1-11";
    case 1106: return "UL-EV1-21";
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
  // Match the slot-loader sequence that is known to work:
  // configure type -> initialise slot memory -> select slot ->
  // configure anti-collision -> enable HF -> emulator mode.
  if (!setSlotTagType(slot, tagType)) return false;
  if (!setSlotDataDefault(slot, tagType)) return false;
  if (!setActiveSlot(slot)) return false;

  // Anti-collision payload:
  //   uidLen | UID | ATQA[0] | ATQA[1] | SAK | ATS length
  //
  // scan14A() already returns ATQA in the order expected by the firmware.
  // The old clone path reversed these bytes and omitted the ATS-length byte.
  uint8_t acoPayload[12] = {};
  acoPayload[0] = uidLen;
  memcpy(acoPayload + 1, uid, uidLen);
  acoPayload[1 + uidLen] = atqa[0];
  acoPayload[2 + uidLen] = atqa[1];
  acoPayload[3 + uidLen] = sak;
  acoPayload[4 + uidLen] = 0; // no ATS for MIFARE Classic

  uint16_t st = 0;
  if (!sendCommand(CMD_MF1_SET_ANTI_COLL,
                   acoPayload, 5 + uidLen,
                   nullptr, nullptr, &st) ||
      (st != 0 && st != 0x68)) {
    return false;
  }

  if (!setSlotEnable(slot, 2, true)) return false; // HF
  return setMode(0); // emulator mode
}

// ── Git version ──────────────────────────────────────────────────────────────
bool ChameleonClient::getGitVersion(char* out, uint8_t maxLen) {
  uint8_t buf[64] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_GIT_VERSION, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len >= maxLen) len = maxLen - 1;
  memcpy(out, buf, len);
  out[len] = 0;
  return true;
}

// ── Device settings ──────────────────────────────────────────────────────────
bool ChameleonClient::getDeviceSettings(DeviceSettings* out) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_DEV_SETTINGS, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_ANIMATION, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(cmd, &buttonIdx, 1, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_GET_SLOT_NICK, p, 2, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_MF1_NT_LEVEL, nullptr, 0, buf, &len, &st, 4000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_MF1_READ_BLOCK, p, 8, buf, &len, &st, 1500, sizeof(buf))) return false;
  if (st != 0 || len < 16) return false;
  memcpy(out, buf, 16);
  return true;
}

bool ChameleonClient::mf1WriteBlock(uint8_t block, uint8_t keyType,
                                     const uint8_t key[6],
                                     const uint8_t data[16]) {
  uint8_t p[24];
  p[0] = keyType;
  p[1] = block;
  memcpy(p + 2, key, 6);
  memcpy(p + 8, data, 16);
  uint16_t st = 0;
  if (!sendCommand(CMD_MF1_WRITE_BLOCK, p, sizeof(p),
                   nullptr, nullptr, &st, 1800)) return false;
  return st == 0 || st == 0x68;
}

bool ChameleonClient::mf1CheckKeysOfSectors(
    const uint8_t mask[10], const uint8_t* keys, uint8_t keyCount,
    uint8_t found[10], uint8_t sectorKeys[40][2][6]) {
  if (!mask || !keys || !found || !sectorKeys || keyCount == 0 || keyCount > 83)
    return false;

  const uint16_t payLen = (uint16_t)(10u + (uint16_t)keyCount * 6u);
  uint8_t* payload = (uint8_t*)malloc(payLen);
  if (!payload) return false;
  memcpy(payload, mask, 10);
  memcpy(payload + 10, keys, (size_t)keyCount * 6u);

  uint8_t* rsp = (uint8_t*)malloc(490);
  if (!rsp) { free(payload); return false; }
  uint16_t len = 0, st = 0;
  bool ok = sendCommand(CMD_MF1_CHECK_SECTORS, payload, payLen,
                        rsp, &len, &st, 30000, 490);
  free(payload);
  if (!ok || (st != 0 && st != 0x68) || len < 490) { free(rsp); return false; }

  memcpy(found, rsp, 10);
  memcpy(sectorKeys, rsp + 10, 480);
  free(rsp);
  return true;
}

bool ChameleonClient::mf1CheckKeysOfBlock(uint8_t block, uint8_t keyType,
                                           const uint8_t* keys, uint8_t keyCount,
                                           uint8_t outKey[6]) {
  if (keyCount == 0 || keyCount > 32) return false;
  // Upstream wire format: [block, keyType, keyCount, key0..keyN-1 * 6B]
  uint8_t p[3 + 32 * 6];
  p[0] = block;
  p[1] = keyType;
  p[2] = keyCount;
  memcpy(p + 3, keys, 6 * keyCount);
  uint8_t rsp[16] = {};
  uint16_t rspLen = 0, st = 0;
  if (!sendCommand(CMD_MF1_CHECK_BLOCK, p, 3 + 6 * keyCount,
                   rsp, &rspLen, &st, 8000, sizeof(rsp))) return false;
  if (st != 0) return false;        // no match in this batch
  // Response: [first byte reserved / index, key0..key5]
  if (rspLen < 7) return false;
  memcpy(outKey, rsp + 1, 6);
  return true;
}

bool ChameleonClient::mf1LoadBlockData(uint8_t slot, uint8_t startBlock,
                                        const uint8_t* data, uint16_t dataLen) {
  // CMD_MF1_LOAD_BLOCK operates on the active emulator slot. Its payload is
  // [startBlock][block data...]; the slot number is not part of the command.
  if (!setActiveSlot(slot)) return false;

  uint16_t st = 0;
  uint8_t* buf = (uint8_t*)malloc(1 + dataLen);
  if (!buf) return false;
  buf[0] = startBlock;
  memcpy(buf + 1, data, dataLen);
  bool ok = sendCommand(CMD_MF1_LOAD_BLOCK, buf, 1 + dataLen,
                        nullptr, nullptr, &st, 3000);
  free(buf);
  return ok && (st == 0 || st == 0x68);
}

bool ChameleonClient::mf1GetBlockData(uint8_t startBlock, uint8_t count, uint8_t* out,
                                       uint16_t* outStatus, uint16_t* outLen) {
  if (count == 0) return false;
  uint8_t p[2] = { startBlock, count };
  uint16_t st = 0, rlen = 0;
  uint16_t wantBytes = (uint16_t)count * 16;
  bool ok = sendCommand(CMD_MF1_GET_BLOCK, p, 2,
                        out, &rlen, &st, 3000, wantBytes);
  if (outStatus) *outStatus = st;
  if (outLen)    *outLen    = rlen;
  // Firmware returns 0x68 (HF_TAG_OK) for successful HF-family reads; 0 is the
  // generic success code. Either means the payload is valid.
  if (!ok || rlen < wantBytes) return false;
  return st == 0 || st == 0x68;
}

bool ChameleonClient::mfuLoadPageData(uint8_t slot, uint8_t firstPage,
                                          const uint8_t* data, uint8_t pageCount) {
  if (!data || pageCount == 0) return false;
  if (!setActiveSlot(slot)) return false;

  const uint16_t dataLen = (uint16_t)pageCount * 4;
  uint8_t* p = (uint8_t*)malloc(2 + dataLen);
  if (!p) return false;
  p[0] = firstPage;
  p[1] = pageCount;
  memcpy(p + 2, data, dataLen);

  uint16_t st = 0;
  bool ok = sendCommand(CMD_MF0_NTAG_WRITE_EMU_PAGE_DATA, p, 2 + dataLen,
                        nullptr, nullptr, &st, 3000);
  free(p);
  return ok && (st == 0 || st == 0x68);
}


bool ChameleonClient::mfuGetPageCount(uint8_t* pageCount) {
  if (!pageCount) return false;
  uint8_t buf[4] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF0_NTAG_GET_PAGE_COUNT, nullptr, 0,
                   buf, &len, &st, 3000, sizeof(buf))) return false;
  if ((st != 0 && st != 0x68) || len < 1) return false;
  *pageCount = buf[0];
  return true;
}

bool ChameleonClient::mfuGetPageData(uint8_t firstPage, uint8_t pageCount,
                                     uint8_t* out,
                                     uint16_t* outStatus, uint16_t* outLen) {
  if (!out || pageCount == 0) return false;
  uint8_t p[2] = { firstPage, pageCount };
  uint16_t st = 0, len = 0;
  const uint16_t want = (uint16_t)pageCount * 4u;
  bool ok = sendCommand(CMD_MF0_NTAG_READ_EMU_PAGE_DATA, p, 2,
                        out, &len, &st, 3000, want);
  if (outStatus) *outStatus = st;
  if (outLen) *outLen = len;
  if (!ok || len < want) return false;
  return st == 0 || st == 0x68;
}

bool ChameleonClient::hf14ARaw(uint8_t options, uint16_t timeoutMs, uint16_t bitLen,
                                const uint8_t* data, uint16_t dataBytes,
                                uint8_t* respOut, uint16_t* respLen,
                                uint16_t respBufSize,
                                uint16_t* outStatus) {
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
  if (outStatus) *outStatus = st;
  free(p);
  return ok;
}


// ── Ultralight / NTAG reader ─────────────────────────────────────────────────

namespace {

// HF14A_RAW option bits, MSB first:
// activate_rf_field | wait_response | append_crc | auto_select | keep_rf_field.
// auto_select performs the ISO14443A selection before each command.
static constexpr uint8_t kMfuRawOptions = 0xF8;

static bool _mfuRaw(ChameleonClient& c, const uint8_t* cmd, uint8_t cmdLen,
                    uint8_t* out, uint16_t* outLen, uint16_t outSize,
                    uint16_t timeoutMs = 500) {
  uint16_t st = 0;
  if (!c.hf14ARaw(kMfuRawOptions, timeoutMs, (uint16_t)cmdLen * 8,
                  cmd, cmdLen, out, outLen, outSize, &st)) {
    return false;
  }
  // HF commands normally report 0x68 (HF_TAG_OK); some firmware revisions
  // use generic success (0). Keep both, as elsewhere in ChameleonClient.
  return st == 0 || st == 0x68;
}

static bool _mfuRead4(ChameleonClient& c, uint8_t page, uint8_t out16[16]) {
  const uint8_t cmd[2] = {0x30, page}; // READ: four pages / 16 data bytes
  uint8_t rsp[24] = {};
  uint16_t len = 0;
  if (!_mfuRaw(c, cmd, sizeof(cmd), rsp, &len, sizeof(rsp))) return false;
  // A successful READ contains 16 data bytes (+ CRC on firmware paths that
  // return it). A one-byte response is a Type-2 NAK.
  if (len < 16) return false;
  memcpy(out16, rsp, 16);
  return true;
}

static bool _mfuGetVersion(ChameleonClient& c, uint8_t out8[8]) {
  const uint8_t cmd = 0x60; // GET_VERSION
  uint8_t rsp[16] = {};
  uint16_t len = 0;
  if (!_mfuRaw(c, &cmd, 1, rsp, &len, sizeof(rsp))) return false;
  if (len < 8) return false;
  memcpy(out8, rsp, 8);
  return true;
}

static bool _mfuUlCAuthProbe(ChameleonClient& c) {
  // Ultralight C AUTHENTICATE part 1. UL-C answers AF + 8-byte encrypted RndB.
  const uint8_t cmd[2] = {0x1A, 0x00};
  uint8_t rsp[16] = {};
  uint16_t len = 0;
  if (!_mfuRaw(c, cmd, sizeof(cmd), rsp, &len, sizeof(rsp))) return false;
  return len >= 9 && rsp[0] == 0xAF;
}

static bool _mfuVersionToInfo(const uint8_t v[8], uint16_t* type, uint16_t* pages) {
  // NXP GET_VERSION layout:
  // vendor | product type | subtype | major | minor | storage size | protocol
  // v[6] is the storage-size code used to distinguish members of a family.
  if (v[1] != 0x04) return false; // NXP

  // NTAG21x: product type 0x04.
  if (v[2] == 0x04) {
    switch (v[6]) {
      case 0x0B: *type = ChameleonClient::MFU_NTAG210; *pages = 20;  return true;
      case 0x0E: *type = ChameleonClient::MFU_NTAG212; *pages = 41;  return true;
      case 0x0F: *type = ChameleonClient::MFU_NTAG213; *pages = 45;  return true;
      case 0x11: *type = ChameleonClient::MFU_NTAG215; *pages = 135; return true;
      case 0x13: *type = ChameleonClient::MFU_NTAG216; *pages = 231; return true;
      default: break;
    }
  }

  // MIFARE Ultralight EV1: product type 0x03.
  if (v[2] == 0x03) {
    switch (v[6]) {
      case 0x0B:
        *type = ChameleonClient::MFU_ULTRALIGHT_EV1_11; *pages = 20; return true;
      case 0x0E:
        *type = ChameleonClient::MFU_ULTRALIGHT_EV1_21; *pages = 41; return true;
      default: break;
    }
  }
  return false;
}

} // namespace

const char* ChameleonClient::mfuTagTypeName(uint16_t type) {
  switch (type) {
    case MFU_NTAG210:          return "NTAG210";
    case MFU_NTAG212:          return "NTAG212";
    case MFU_NTAG213:          return "NTAG213";
    case MFU_NTAG215:          return "NTAG215";
    case MFU_NTAG216:          return "NTAG216";
    case MFU_ULTRALIGHT:       return "Ultralight";
    case MFU_ULTRALIGHT_C:     return "Ultralight C";
    case MFU_ULTRALIGHT_EV1_11:return "Ultralight EV1 11";
    case MFU_ULTRALIGHT_EV1_21:return "Ultralight EV1 21";
    default:                   return "Unknown";
  }
}

bool ChameleonClient::mfuDetect(MfuTagInfo* out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));

  // First establish that an ISO14443A Type-2 candidate is present and retain
  // its anti-collision data for the result screen / dump metadata.
  if (!scan14A(out->uid, &out->uidLen, out->atqa, &out->sak)) return false;
  if (out->sak != 0x00) return false;

  uint8_t version[8] = {};
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_mfuGetVersion(*this, version)) {
      if (_mfuVersionToInfo(version, &out->type, &out->pages)) return true;
      // We received a real GET_VERSION response, but not one we know.
      // Do not misclassify it with legacy memory-size probes.
      return false;
    }
    delay(30);
  }

  // Legacy Ultralight / Ultralight C do not implement GET_VERSION.
  // Probe UL-C explicitly before falling back to the original 16-page UL.
  // The AUTH probe changes UL-C authentication state, but every raw operation
  // auto-selects the card again, so subsequent reads start a fresh activation.
  if (_mfuUlCAuthProbe(*this)) {
    out->type  = MFU_ULTRALIGHT_C;
    out->pages = 48;
    return true;
  }

  uint8_t tmp[16] = {};
  if (_mfuRead4(*this, 0, tmp) && !_mfuRead4(*this, 16, tmp)) {
    out->type  = MFU_ULTRALIGHT;
    out->pages = 16;
    return true;
  }

  return false;
}

bool ChameleonClient::mfuReadDump(const MfuTagInfo& info, uint8_t* out,
                                  uint16_t outSize, uint16_t* bytesRead,
                                  MfuProgressCallback progress) {
  if (bytesRead) *bytesRead = 0;
  const uint32_t total = (uint32_t)info.pages * 4u;
  if (!out || outSize < total || info.pages < 4) return false;

  uint16_t page = 0;
  uint16_t done = 0;

  while (page < info.pages) {
    uint8_t readPage;
    uint8_t skipPages = 0;
    uint8_t pagesToCopy = 4;

    const uint16_t remainingPages = info.pages - page;
    if (remainingPages >= 4) {
      readPage = (uint8_t)page;
    } else {
      // READ always returns four pages. For a tag whose page count is not a
      // multiple of four (e.g. NTAG215 = 135 pages), start four pages from
      // the physical end and copy only the still-missing tail. This avoids
      // asking the tag for page 135, which does not exist.
      readPage = (uint8_t)(info.pages - 4);
      skipPages = (uint8_t)(page - readPage);
      pagesToCopy = (uint8_t)remainingPages;
    }

    uint8_t chunk[16] = {};
    if (!_mfuRead4(*this, readPage, chunk)) {
      // Ultralight C pages 44..47 contain the 3DES key and are intentionally
      // not readable. Preserve a complete 48-page image with zeroes there.
      if (info.type == MFU_ULTRALIGHT_C && readPage >= 44) {
        memset(chunk, 0, sizeof(chunk));
      } else {
        if (bytesRead) *bytesRead = done;
        return false;
      }
    }

    const uint16_t copyBytes = (uint16_t)pagesToCopy * 4u;
    memcpy(out + done, chunk + ((uint16_t)skipPages * 4u), copyBytes);
    done += copyBytes;
    page += pagesToCopy;
    if (progress) progress(page, info.pages);
  }

  if (bytesRead) *bytesRead = done;
  return done == total;
}

bool ChameleonClient::mfuWriteNtag215User(const uint8_t* dump, uint16_t dumpLen,
                                           MfuProgressCallback progress,
                                           const MfuTagInfo* expectedTarget) {
  static constexpr uint16_t kNtag215Bytes = 135u * 4u;
  static constexpr uint8_t kFirstUserPage = 4;
  static constexpr uint8_t kLastUserPage  = 129;
  static constexpr uint16_t kUserPages =
      (uint16_t)kLastUserPage - kFirstUserPage + 1u;

  if (!dump || dumpLen != kNtag215Bytes) return false;

  MfuTagInfo target = {};
  if (!mfuDetect(&target) || target.type != MFU_NTAG215 || target.pages != 135)
    return false;
  if (expectedTarget) {
    if (target.uidLen != expectedTarget->uidLen ||
        memcmp(target.uid, expectedTarget->uid, target.uidLen) != 0)
      return false;
  }

  // Standard Type-2 WRITE (A2) writes exactly one 4-byte page. Do not touch
  // pages 0..3 (UID/manufacturer + CC), page 130 (dynamic locks), or pages
  // 131..134 (CFG0/CFG1/PWD/PACK). This first writer intentionally clones
  // user content without changing identity, locks or security configuration.
  uint16_t done = 0;
  for (uint16_t page = kFirstUserPage; page <= kLastUserPage; ++page) {
    uint8_t cmd[6] = {0xA2, (uint8_t)page, 0, 0, 0, 0};
    memcpy(cmd + 2, dump + page * 4u, 4);

    uint8_t rsp[4] = {};
    uint16_t len = 0;
    if (!_mfuRaw(*this, cmd, sizeof(cmd), rsp, &len, sizeof(rsp), 700))
      return false;
    // Type-2 ACK is 0xA (4 bits). HF14A_RAW exposes it in the low nibble.
    if (len < 1 || (rsp[0] & 0x0F) != 0x0A) return false;

    ++done;
    if (progress) progress(done, kUserPages);
  }
  return done == kUserPages;
}


// ── Firmware nested-attack helpers ───────────────────────────────────────────

bool ChameleonClient::mf1NTDistance(uint8_t keyType, uint8_t block,
                                     const uint8_t key[6],
                                     uint32_t* uidOut, uint32_t* distOut) {
  uint8_t p[8] = { keyType, block,
                   key[0], key[1], key[2], key[3], key[4], key[5] };
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_NT_DISTANCE, p, 8, buf, &len, &st, 4000, sizeof(buf))) return false;
  if (st != 0 || len < 8) return false;
  if (uidOut)  *uidOut  = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                        | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
  if (distOut) *distOut = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                        | ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];
  return true;
}

bool ChameleonClient::mf1NestedAcquire(uint8_t keyType, uint8_t block,
                                        const uint8_t key[6],
                                        uint8_t targetKeyType, uint8_t targetBlock,
                                        NestedSample* out, int maxOut, int* count) {
  uint8_t p[10] = { keyType, block,
                    key[0], key[1], key[2], key[3], key[4], key[5],
                    targetKeyType, targetBlock };
  // Firmware can return many 9-byte records; size buffer generously.
  uint8_t buf[256] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_NESTED_ACQ, p, 10, buf, &len, &st,
                   30000, sizeof(buf))) return false;
  if (st != 0) return false;
  int n = 0;
  for (uint16_t i = 0; i + 9 <= len && n < maxOut; i += 9) {
    out[n].nt    = ((uint32_t)buf[i+0] << 24) | ((uint32_t)buf[i+1] << 16)
                 | ((uint32_t)buf[i+2] <<  8) |  (uint32_t)buf[i+3];
    out[n].ntEnc = ((uint32_t)buf[i+4] << 24) | ((uint32_t)buf[i+5] << 16)
                 | ((uint32_t)buf[i+6] <<  8) |  (uint32_t)buf[i+7];
    out[n].par   = buf[i+8];
    n++;
  }
  if (count) *count = n;
  return n > 0;
}

bool ChameleonClient::mf1StaticNestedAcquire(uint8_t keyType, uint8_t block,
                                              const uint8_t key[6],
                                              uint8_t targetKeyType,
                                              uint8_t targetBlock,
                                              uint32_t* uidOut,
                                              NestedSample* out, int maxOut,
                                              int* count) {
  uint8_t p[10] = { keyType, block,
                    key[0], key[1], key[2], key[3], key[4], key[5],
                    targetKeyType, targetBlock };
  // Static format: {uid[4]} {nt[4], ntEnc[4]}*  (no parity).
  uint8_t buf[256] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_MF1_STATIC_NESTED_ACQ, p, 10, buf, &len, &st,
                   30000, sizeof(buf))) return false;
  if (st != 0 || len < 4) return false;
  if (uidOut) *uidOut = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                      | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
  int n = 0;
  for (uint16_t i = 4; i + 8 <= len && n < maxOut; i += 8) {
    out[n].nt    = ((uint32_t)buf[i+0] << 24) | ((uint32_t)buf[i+1] << 16)
                 | ((uint32_t)buf[i+2] <<  8) |  (uint32_t)buf[i+3];
    out[n].ntEnc = ((uint32_t)buf[i+4] << 24) | ((uint32_t)buf[i+5] << 16)
                 | ((uint32_t)buf[i+6] <<  8) |  (uint32_t)buf[i+7];
    out[n].par   = 0;
    n++;
  }
  if (count) *count = n;
  return n > 0;
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
  if (!sendCommand(CMD_MF1_DET_COUNT, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
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
  if (!sendCommand(CMD_MF1_DET_RESULT, p, 4, buf, &len, &st, 2500, sizeof(buf))) return false;
  if (st != 0 || len < 18) return false;
  memcpy(out, buf, 18);
  return true;
}

// ── LF scan/clone ────────────────────────────────────────────────────────────
bool ChameleonClient::scanHIDProx(uint8_t payload[13], uint8_t* payloadLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_HID_PROX, nullptr, 0, buf, &len, &st, 3000, sizeof(buf))) return false;
  if (st != 0 || len == 0) return false;
  uint16_t cp = len > 13 ? 13 : len;
  memcpy(payload, buf, cp);
  *payloadLen = (uint8_t)cp;
  return true;
}

bool ChameleonClient::scanViking(uint8_t uid[4], uint8_t* uidLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_SCAN_VIKING, nullptr, 0, buf, &len, &st, 3000, sizeof(buf))) return false;
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

// Upstream GUI ignores the status byte on LF getters and just reads data length.
// Mirror that — some firmware revisions return status != 0 but still deliver the
// stored UID payload.
bool ChameleonClient::getEM410XSlot(uint8_t uid[5]) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_EM410X_ID, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len < 5) return false;
  memcpy(uid, buf, 5);
  return true;
}

bool ChameleonClient::getHIDProxSlot(uint8_t payload[13], uint8_t* payloadLen) {
  uint8_t buf[32] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_HID_PROX_ID, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len == 0) return false;
  uint16_t cp = len > 13 ? 13 : len;
  memcpy(payload, buf, cp);
  *payloadLen = (uint8_t)cp;
  return true;
}

bool ChameleonClient::getVikingSlot(uint8_t uid[4], uint8_t* uidLen) {
  uint8_t buf[16] = {};
  uint16_t len = 0, st = 0;
  if (!sendCommand(CMD_GET_VIKING_ID, nullptr, 0, buf, &len, &st, 2000, sizeof(buf))) return false;
  if (len == 0) return false;
  uint16_t cp = len > 4 ? 4 : len;
  memcpy(uid, buf, cp);
  *uidLen = (uint8_t)cp;
  return true;
}
