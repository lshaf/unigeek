#include "ST25R3916Backend.h"

#if defined(DEVICE_HAS_ST25R3916)

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <esp_random.h>

#include "utils/crypto/crapto1.h"

// MIFARE Classic emulation trace. Set to 0 to compile the prints out.
#ifndef MFC_EMU_TRACE
#define MFC_EMU_TRACE 1
#endif
#if MFC_EMU_TRACE
#define MFCT(...) Serial.printf(__VA_ARGS__)
#else
#define MFCT(...) ((void)0)
#endif

namespace {
uint32_t bytesToU32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void setPackedBit(uint8_t* buf, size_t bit, uint8_t value) {
  if (value) buf[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
}

uint8_t getPackedBit(const uint8_t* buf, size_t bit) {
  return (uint8_t)((buf[bit >> 3] >> (bit & 7U)) & 1U);
}

uint16_t mfcPackBits(const uint8_t* data, const uint8_t* parity, size_t count, uint8_t* out) {
  uint16_t bit = 0;
  for (size_t i = 0; i < count; ++i) {
    for (uint8_t b = 0; b < 8; ++b, ++bit)
      if (data[i] & (1U << b)) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
    if (parity[i] & 1U) out[bit >> 3] |= (uint8_t)(1U << (bit & 7U));
    ++bit;
  }
  return bit;
}

uint8_t mfcUnpackBits(const uint8_t* in, uint16_t nbits, uint8_t* out, uint8_t maxBytes) {
  uint8_t n = 0;
  uint16_t bit = 0;
  while ((bit + 8U) <= nbits && n < maxBytes) {
    uint8_t value = 0;
    for (uint8_t b = 0; b < 8; ++b, ++bit)
      if (in[bit >> 3] & (1U << (bit & 7U))) value |= (uint8_t)(1U << b);
    out[n++] = value;
    if (bit < nbits) ++bit;
  }
  return n;
}

bool mfcRxOk(ReturnCode rc) {
  return rc == ST_ERR_NONE ||
         (rc >= ST_ERR_INCOMPLETE_BYTE && rc <= ST_ERR_INCOMPLETE_BYTE_07);
}
}

ST25R3916Backend::~ST25R3916Backend() {
  end();
}

bool ST25R3916Backend::beginI2C(TwoWire* wire, uint8_t address) {
  end();
  _transport = Transport::I2C;
  if (!wire) return false;

  wire->beginTransmission(address);
  _info.busDetected = (wire->endTransmission() == 0);
  if (!_info.busDetected) return false;

  _hw = new RfalRfST25R3916Class(wire, -1);
  return _finishBegin();
}

bool ST25R3916Backend::beginSPI(SPIClass* spi, int csPin, int irqPin, uint32_t spiHz) {
  end();
  _transport = Transport::SPI;
  if (!spi || csPin < 0 || irqPin < 0) return false;

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
  _info.busDetected = true;

  _hw = new RfalRfST25R3916Class(spi, csPin, irqPin, spiHz);
  return _finishBegin();
}

bool ST25R3916Backend::_finishBegin() {
  if (!_hw) return false;

  _nfc = new RfalNfcClass(_hw);
  if (!_nfc) return false;

  _info.initCode = _nfc->rfalNfcInitialize();
  _info.initialized = (_info.initCode == ST_ERR_NONE);
  if (!_info.initialized) return false;

  _hw->st25r3916OscOn();
  if (_hw->st25r3916ReadRegister(ST25R3916_REG_IC_IDENTITY, &_info.chipId) != ST_ERR_NONE) {
    _info.initialized = false;
    return false;
  }
  _info.is3916 = _hw->st25r3916ChipIsST25R3916();
  _info.is3916B = _hw->st25r3916ChipIsST25R3916B();
  return true;
}

bool ST25R3916Backend::scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs,
                            bool keepActive) {
  result = ScanResult{};
  _activeTag = ScanResult{};
  _active = false;
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _lastScanCode = 0xFFFF;
  if (!_nfc || !_hw || !_info.initialized || techMask == 0) return false;

  rfalNfcDiscoverParam params;
  memset(&params, 0, sizeof(params));
  params.compMode = RFAL_COMPLIANCE_MODE_NFC;
  params.devLimit = 1;
  params.nfcfBR = RFAL_BR_212;
  params.ap2pBR = RFAL_BR_424;
  params.techs2Find = techMask;
  params.GBLen = RFAL_NFCDEP_GB_MAX_LEN;
  params.totalDuration = (uint16_t)((timeoutMs > 0xFFFFU) ? 0xFFFFU : timeoutMs);
  params.wakeupEnabled = false;
  params.wakeupConfigDefault = true;

  _lastScanCode = _nfc->rfalNfcDiscover(&params);
  if (_lastScanCode != ST_ERR_NONE) return false;

  _hw->st25r3916OscOn();
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < timeoutMs) {
    _nfc->rfalNfcWorker();
    const auto state = _nfc->rfalNfcGetState();
    if (state == RFAL_NFC_STATE_ACTIVATED) {
      rfalNfcDevice* dev = nullptr;
      _lastScanCode = _nfc->rfalNfcGetActiveDevice(&dev);
      if (_lastScanCode != ST_ERR_NONE || !dev) break;

      switch (dev->type) {
        case RFAL_NFC_LISTEN_TYPE_NFCA: result.technology = Technology::NFC_A; break;
        case RFAL_NFC_LISTEN_TYPE_NFCB: result.technology = Technology::NFC_B; break;
        case RFAL_NFC_LISTEN_TYPE_NFCF: result.technology = Technology::NFC_F; break;
        case RFAL_NFC_LISTEN_TYPE_NFCV: result.technology = Technology::NFC_V; break;
        default: result.technology = Technology::UNKNOWN; break;
      }

      result.nfcidLen = (dev->nfcidLen < sizeof(result.nfcid)) ? dev->nfcidLen : sizeof(result.nfcid);
      if (dev->nfcid && result.nfcidLen) memcpy(result.nfcid, dev->nfcid, result.nfcidLen);
      result.isoDep = (dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP);
      if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
        result.sak = dev->dev.nfca.selRes.sak;
        result.atqa[0] = dev->dev.nfca.sensRes.anticollisionInfo;
        result.atqa[1] = dev->dev.nfca.sensRes.platformInfo;
      }

      _lastScanCode = ST_ERR_NONE;
      if (keepActive) {
        _activeTag = result;
        _active = true;
      } else {
        deactivate();
      }
      return true;
    }

    if (state == RFAL_NFC_STATE_IDLE) {
      const uint32_t elapsed = (uint32_t)(millis() - started);
      if (elapsed >= timeoutMs) break;
      const uint32_t remaining = timeoutMs - elapsed;
      params.totalDuration =
          (uint16_t)((remaining > 0xFFFFU) ? 0xFFFFU : remaining);
      _lastScanCode = _nfc->rfalNfcDiscover(&params);
      if (_lastScanCode != ST_ERR_NONE) break;
    }
    delay(5);
  }

  deactivate();
  return false;
}

void ST25R3916Backend::deactivate() {
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _active = false;
  _activeTag = ScanResult{};
  if (!_nfc) return;

  _nfc->rfalNfcDeactivate(false);
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 80U) {
    _nfc->rfalNfcWorker();
    if (_nfc->rfalNfcGetState() == RFAL_NFC_STATE_IDLE) break;
    delay(1);
  }
}

uint8_t ST25R3916Backend::_oddParity(uint8_t value) {
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return (uint8_t)((~value) & 1U);
}

uint16_t ST25R3916Backend::_crcA(const uint8_t* data, size_t len) {
  uint16_t crc = 0x6363U;
  while (len--) {
    uint8_t d = (uint8_t)(*data++ ^ (crc & 0x00FFU));
    d ^= (uint8_t)(d << 4);
    crc = (uint16_t)((crc >> 8) ^ ((uint16_t)d << 8) ^ ((uint16_t)d << 3) ^ ((uint16_t)d >> 4));
  }
  return crc;
}

uint64_t ST25R3916Backend::_key48(const uint8_t key[6]) {
  uint64_t value = 0;
  for (uint8_t i = 0; i < 6; ++i) value = (value << 8) | key[i];
  return value;
}

uint32_t ST25R3916Backend::_uid32(const ScanResult& tag) {
  if (tag.nfcidLen < 4) return 0;
  const uint8_t* p = &tag.nfcid[tag.nfcidLen - 4];
  return bytesToU32(p);
}

bool ST25R3916Backend::_transceiveBytes(const uint8_t* tx, size_t txLen,
                                         uint8_t* rx, size_t rxMaxLen,
                                         size_t& rxLen, uint32_t timeoutMs) {
  rxLen = 0;
  if (!_hw || !tx || !txLen || !rx || !rxMaxLen) return false;

  uint16_t receivedBits = 0;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = const_cast<uint8_t*>(tx);
  ctx.txBufLen = (uint16_t)(txLen * 8U);
  ctx.rxBuf = rx;
  ctx.rxBufLen = (uint16_t)(rxMaxLen * 8U);
  ctx.rxRcvdLen = &receivedBits;
  ctx.flags = RFAL_TXRX_FLAGS_DEFAULT;
  ctx.fwt = rfalConvMsTo1fc(timeoutMs);

  ReturnCode rc = _hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return false;
  do {
    _hw->rfalWorker();
    rc = _hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY);
  if (rc != ST_ERR_NONE || receivedBits == 0 || (receivedBits & 7U)) return false;

  rxLen = receivedBits / 8U;
  if (rxLen > rxMaxLen) rxLen = rxMaxLen;
  return true;
}

bool ST25R3916Backend::_transceivePacked(const uint8_t* txPacked, size_t txBits,
                                            uint8_t* rxPacked, size_t rxMaxBits,
                                            size_t& rxBits, uint32_t timeoutMs,
                                            bool autoTxParity) {
  rxBits = 0;
  if (!_hw || !txPacked || !txBits || !rxPacked || !rxMaxBits) return false;

  uint16_t receivedBits = 0;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = const_cast<uint8_t*>(txPacked);
  ctx.txBufLen = (uint16_t)txBits;
  ctx.rxBuf = rxPacked;
  ctx.rxBufLen = (uint16_t)rxMaxBits;
  ctx.rxRcvdLen = &receivedBits;
  ctx.flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
              RFAL_TXRX_FLAGS_CRC_RX_KEEP |
              RFAL_TXRX_FLAGS_PAR_RX_KEEP |
              (autoTxParity ? RFAL_TXRX_FLAGS_PAR_TX_AUTO : RFAL_TXRX_FLAGS_PAR_TX_NONE) |
              RFAL_TXRX_FLAGS_AGC_ON;
  ctx.fwt = rfalConvMsTo1fc(timeoutMs);

  ReturnCode rc = _hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return false;
  do {
    _hw->rfalWorker();
    rc = _hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY);

  const uint16_t err = (uint16_t)(rc & 0x00FFU);
  const bool dataBearingStatus =
      (rc == ST_ERR_NONE) || err == 21U /* ERR_CRC */ ||
      (err >= 41U && err <= 47U) /* ERR_INCOMPLETE_BYTE_01..07 */;
  if (!dataBearingStatus || receivedBits == 0) return false;
  rxBits = receivedBits;
  return true;
}

bool ST25R3916Backend::_transceiveRaw9(const uint8_t* txData, const uint8_t* txParity,
                                       size_t txLen, uint8_t* rxData, uint8_t* rxParity,
                                       size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  rxLen = 0;
  if (!txData || !txParity || !txLen || !rxData || !rxParity || !rxMaxLen) return false;
  if (txLen > 24 || rxMaxLen > 24) return false;

  uint8_t txPacked[27] = {};
  uint8_t rxPacked[27] = {};
  const size_t txBits = txLen * 9U;
  const size_t rxMaxBits = rxMaxLen * 9U;

  for (size_t i = 0; i < txLen; ++i) {
    const size_t base = i * 9U;
    for (uint8_t bit = 0; bit < 8; ++bit) setPackedBit(txPacked, base + bit, (txData[i] >> bit) & 1U);
    setPackedBit(txPacked, base + 8U, txParity[i] & 1U);
  }

  size_t rxBits = 0;
  if (!_transceivePacked(txPacked, txBits, rxPacked, rxMaxBits, rxBits, timeoutMs) || rxBits < 9U) {
    return false;
  }

  rxLen = rxBits / 9U;
  if (rxLen > rxMaxLen) rxLen = rxMaxLen;
  for (size_t i = 0; i < rxLen; ++i) {
    const size_t base = i * 9U;
    uint8_t value = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) value |= (uint8_t)(getPackedBit(rxPacked, base + bit) << bit);
    rxData[i] = value;
    rxParity[i] = getPackedBit(rxPacked, base + 8U);
  }
  return true;
}

static ReturnCode mifareTransceiveRaw(RfalRfST25R3916Class* hw, uint8_t* txBuf, uint16_t txBits,
                                      uint8_t* rxBuf, uint16_t rxCapBytes, uint16_t* rxBits,
                                      uint32_t fwt, uint32_t flags) {
  if (!hw || !txBuf || !txBits || !rxBuf || !rxCapBytes || !rxBits) return ST_ERR_PARAM;
  rfalTransceiveContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.txBuf = txBuf;
  ctx.txBufLen = txBits;
  ctx.rxBuf = rxBuf;
  ctx.rxBufLen = (uint16_t)rfalConvBytesToBits(rxCapBytes);
  ctx.rxRcvdLen = rxBits;
  ctx.flags = flags;
  ctx.fwt = fwt;
  *rxBits = 0;
  ReturnCode rc = hw->rfalStartTransceive(&ctx);
  if (rc != ST_ERR_NONE) return rc;
  const uint32_t started = millis();
  do {
    hw->rfalWorker();
    rc = hw->rfalGetTransceiveStatus();
  } while (rc == ST_ERR_BUSY && millis() - started < 60U);
  return rc;
}

bool ST25R3916Backend::mifareClassicAuthenticate(uint8_t block, const uint8_t key[6], bool keyB) {
  if (!_active || _activeTag.technology != Technology::NFC_A || !_hw || !key) return false;

  const uint32_t encFlags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                            RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                            RFAL_TXRX_FLAGS_NFCIP1_OFF |
                            RFAL_TXRX_FLAGS_AGC_ON |
                            RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                            RFAL_TXRX_FLAGS_PAR_TX_NONE;

  uint8_t cmd[2] = {(uint8_t)(keyB ? 0x61 : 0x60), block};
  uint32_t nt = 0;

  if (!_crypto) {
    const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, cmd, 2);
    uint8_t tx[4] = {cmd[0], cmd[1], (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8)};
    uint8_t rx[8] = {};
    uint16_t rxBits = 0;
    const uint32_t firstFlags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                                RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                                RFAL_TXRX_FLAGS_NFCIP1_OFF |
                                RFAL_TXRX_FLAGS_AGC_ON |
                                RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                                RFAL_TXRX_FLAGS_PAR_TX_AUTO;
    ReturnCode rc = mifareTransceiveRaw(_hw, tx, rfalConvBytesToBits(sizeof(tx)), rx, sizeof(rx),
                                        &rxBits, rfalConvMsTo1fc(20), firstFlags);
    if (!mfcRxOk(rc) || rxBits < 32U) return false;

    uint8_t ntBytes[4] = {};
    if (mfcUnpackBits(rx, rxBits, ntBytes, sizeof(ntBytes)) < 4U) return false;
    nt = bytesToU32(ntBytes);
  } else {
    const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, cmd, 2);
    uint8_t plain[4] = {cmd[0], cmd[1], (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8)};
    uint8_t enc[4] = {}, parity[4] = {};
    for (uint8_t i = 0; i < 4; ++i) {
      enc[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ plain[i]);
      parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
    }
    uint8_t packed[8] = {}, rx[8] = {};
    const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), packed);
    uint16_t rxBits = 0;
    ReturnCode rc = mifareTransceiveRaw(_hw, packed, txBits, rx, sizeof(rx), &rxBits,
                                        rfalConvMsTo1fc(20), encFlags);
    if (!mfcRxOk(rc) || rxBits < 32U) {
      crypto1_destroy(_crypto); _crypto = nullptr;
      return false;
    }

    uint8_t encNt[4] = {};
    if (mfcUnpackBits(rx, rxBits, encNt, sizeof(encNt)) < 4U) {
      crypto1_destroy(_crypto); _crypto = nullptr;
      return false;
    }
    for (uint8_t i = 0; i < 4; ++i)
      nt = (nt << 8) | (uint8_t)(encNt[i] ^ crypto1_byte(_crypto, 0, 0));

    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }

  _crypto = crypto1_create(_key48(key));
  if (!_crypto) return false;
  crypto1_word(_crypto, nt ^ _uid32(_activeTag), 0);

  const uint32_t nr = esp_random();
  uint8_t nrPlain[4] = {(uint8_t)(nr >> 24), (uint8_t)(nr >> 16),
                        (uint8_t)(nr >> 8), (uint8_t)nr};
  uint8_t enc[8] = {}, parity[8] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    enc[i] = (uint8_t)(crypto1_byte(_crypto, nrPlain[i], 0) ^ nrPlain[i]);
    parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(nrPlain[i]));
  }
  const uint32_t ar = prng_successor(nt, 64);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t value = (uint8_t)(ar >> (24U - 8U * i));
    enc[4 + i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ value);
    parity[4 + i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(value));
  }

  uint8_t packed[16] = {}, rx[8] = {};
  const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), packed);
  uint16_t rxBits = 0;
  ReturnCode rc = mifareTransceiveRaw(_hw, packed, txBits, rx, sizeof(rx), &rxBits,
                                      rfalConvMsTo1fc(20), encFlags);
  if (!mfcRxOk(rc) || rxBits < 32U) { crypto1_destroy(_crypto); _crypto = nullptr; return false; }

  uint8_t atEnc[4] = {};
  if (mfcUnpackBits(rx, rxBits, atEnc, sizeof(atEnc)) < 4U) {
    crypto1_destroy(_crypto); _crypto = nullptr; return false;
  }
  uint32_t at = 0;
  for (uint8_t i = 0; i < 4; ++i)
    at = (at << 8) | (uint8_t)(atEnc[i] ^ crypto1_byte(_crypto, 0, 0));
  if (at != prng_successor(nt, 96)) {
    crypto1_destroy(_crypto); _crypto = nullptr; return false;
  }
  return true;
}

bool ST25R3916Backend::mifareClassicReadBlock(uint8_t block, uint8_t data[16]) {
  if (!_active || !_crypto || !data) return false;
  uint8_t plain[4] = {0x30, block, 0, 0};
  const uint16_t crc = _hw->rfalCrcCalculateCcitt(0x6363, plain, 2);
  plain[2] = (uint8_t)(crc & 0xFFU); plain[3] = (uint8_t)(crc >> 8);
  uint8_t enc[4] = {}, parity[4] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    enc[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ plain[i]);
    parity[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
  }
  uint8_t tx[8] = {}, rx[32] = {};
  const uint16_t txBits = mfcPackBits(enc, parity, sizeof(enc), tx);
  uint16_t rxBits = 0;
  const uint32_t flags = RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
                         RFAL_TXRX_FLAGS_CRC_RX_KEEP |
                         RFAL_TXRX_FLAGS_NFCIP1_OFF |
                         RFAL_TXRX_FLAGS_AGC_ON |
                         RFAL_TXRX_FLAGS_PAR_RX_KEEP |
                         RFAL_TXRX_FLAGS_PAR_TX_NONE;
  ReturnCode rc = mifareTransceiveRaw(_hw, tx, txBits, rx, sizeof(rx), &rxBits,
                                        rfalConvMsTo1fc(20), flags);
  if (!mfcRxOk(rc) || rxBits < 144U) return false;
  uint8_t decoded[18] = {};
  if (mfcUnpackBits(rx, rxBits, decoded, sizeof(decoded)) < 18U) return false;
  for (uint8_t i = 0; i < 18; ++i) decoded[i] ^= crypto1_byte(_crypto, 0, 0);
  const uint16_t expected = _hw->rfalCrcCalculateCcitt(0x6363, decoded, 16);
  const uint16_t got = (uint16_t)decoded[16] | ((uint16_t)decoded[17] << 8);
  if (expected != got) return false;
  memcpy(data, decoded, 16);
  return true;
}

bool ST25R3916Backend::mifareClassicWriteBlock(uint8_t block, const uint8_t data[16]) {
  if (!_active || !_crypto || !data) return false;

  auto sendEncryptedFrame = [&](const uint8_t* plain, size_t len) -> bool {
    uint8_t enc[18] = {};
    uint8_t par[18] = {};
    uint8_t txPacked[21] = {};
    uint8_t rxPacked[2] = {};
    for (size_t i = 0; i < len; ++i) {
      enc[i] = crypto1_byte(_crypto, 0, 0) ^ plain[i];
      par[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(plain[i]));
      const size_t base = i * 9U;
      for (uint8_t bit = 0; bit < 8; ++bit) setPackedBit(txPacked, base + bit, (enc[i] >> bit) & 1U);
      setPackedBit(txPacked, base + 8U, par[i]);
    }

    size_t rxBits = 0;
    if (!_transceivePacked(txPacked, len * 9U, rxPacked, 4U, rxBits) || rxBits != 4U) return false;
    uint8_t ack = 0;
    for (uint8_t bit = 0; bit < 4; ++bit) {
      const uint8_t encryptedBit = getPackedBit(rxPacked, bit);
      const uint8_t plainBit = (uint8_t)(encryptedBit ^ crypto1_bit(_crypto, 0, 0));
      ack |= (uint8_t)(plainBit << bit);
    }
    return ack == 0x0AU;
  };

  uint8_t cmd[4] = {0xA0, block, 0, 0};
  const uint16_t cmdCrc = _crcA(cmd, 2);
  cmd[2] = (uint8_t)(cmdCrc & 0xFFU);
  cmd[3] = (uint8_t)(cmdCrc >> 8);
  if (!sendEncryptedFrame(cmd, sizeof(cmd))) return false;

  uint8_t payload[18] = {};
  memcpy(payload, data, 16);
  const uint16_t dataCrc = _crcA(payload, 16);
  payload[16] = (uint8_t)(dataCrc & 0xFFU);
  payload[17] = (uint8_t)(dataCrc >> 8);
  return sendEncryptedFrame(payload, sizeof(payload));
}

bool ST25R3916Backend::type2Transceive(const uint8_t* tx, size_t txLen,
                                          uint8_t* rx, size_t rxMaxLen,
                                          size_t& rxLen, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) {
    rxLen = 0;
    return false;
  }
  return _transceiveBytes(tx, txLen, rx, rxMaxLen, rxLen, timeoutMs);
}

bool ST25R3916Backend::nfcATransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) { rxLen = 0; return false; }
  return _transceiveBytes(tx, txLen, rx, rxMaxLen, rxLen, timeoutMs);
}

bool ST25R3916Backend::nfcATransceiveBits(const uint8_t* tx, size_t txBits, uint8_t* rx,
                                            size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs) {
  if (!_active || _activeTag.technology != Technology::NFC_A) { rxBits = 0; return false; }
  const bool autoTxParity = (txBits >= 8U) && ((txBits & 7U) == 0U);
  return _transceivePacked(tx, txBits, rx, rxMaxBits, rxBits, timeoutMs, autoTxParity);
}

bool ST25R3916Backend::activeRfTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                                            size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  if (!_active || !_hw) { rxLen = 0; return false; }
  return _transceiveBytes(tx, txLen, rx, rxMaxLen, rxLen, timeoutMs);
}

bool ST25R3916Backend::isoDepTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                                         size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs) {
  rxLen = 0;
  if (!_active || !_nfc || !_activeTag.isoDep || !tx || !txLen || !rx || !rxMaxLen) return false;
  if (txLen > 0xFFFFU) return false;

  uint8_t* rxData = nullptr;
  uint16_t* received = nullptr;
  ReturnCode rc = _nfc->rfalNfcDataExchangeStart(const_cast<uint8_t*>(tx), (uint16_t)txLen,
                                                  &rxData, &received, 0);
  if (rc != ST_ERR_NONE) return false;

  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < timeoutMs) {
    _nfc->rfalNfcWorker();
    rc = _nfc->rfalNfcDataExchangeGetStatus();
    if (rc == ST_ERR_BUSY) { delay(1); continue; }
    if (rc != ST_ERR_NONE && rc != ST_ERR_AGAIN) return false;
    if (!rxData || !received) return false;
    const size_t chunk = (size_t)*received;
    if (chunk > rxMaxLen - rxLen) return false;
    if (chunk) { memcpy(rx + rxLen, rxData, chunk); rxLen += chunk; }
    if (rc == ST_ERR_NONE) return true;
    delay(1);
  }
  return false;
}

bool ST25R3916Backend::type2ReadPages(uint8_t startPage, uint8_t data[16]) {
  if (!data) return false;
  const uint8_t cmd[2] = {0x30, startPage};
  size_t rxLen = 0;
  memset(data, 0, 16);
  return type2Transceive(cmd, sizeof(cmd), data, 16, rxLen, 20) && rxLen == 16;
}

bool ST25R3916Backend::type2WritePage(uint8_t page, const uint8_t data[4]) {
  _lastType2WriteAckNibble = 0xFF;
  _lastType2WriteBits = 0;
  _lastType2WriteCode = 0xFFFF;
  if (!_active || _activeTag.technology != Technology::NFC_A || !_nfc || !_hw || !data) return false;

  uint8_t frame[8] = {0xA2, page, data[0], data[1], data[2], data[3], 0, 0};
  const uint16_t crc = _crcA(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFFU);
  frame[7] = (uint8_t)(crc >> 8);

  uint8_t rxPacked[2] = {};
  size_t rxBits = 0;
  const bool gotShortReply = _transceivePacked(
      frame, sizeof(frame) * 8U, rxPacked, 4U, rxBits, 12, true) && rxBits == 4U;
  _lastType2WriteBits = (uint16_t)rxBits;
  if (gotShortReply) {
    _lastType2WriteAckNibble = (uint8_t)(rxPacked[0] & 0x0FU);
    if (_lastType2WriteAckNibble != 0x0AU) return false;
    delay(10);
    return true;
  }

  const ReturnCode rc = _nfc->rfalT2TPollerWrite(page, const_cast<uint8_t*>(data));
  _lastType2WriteCode = (uint16_t)rc;
  if (rc == ST_ERR_NONE) {
    delay(10);
    return true;
  }
  return false;
}

bool ST25R3916Backend::type2WritePageRfalFirst(uint8_t page, const uint8_t data[4]) {
  _lastType2WriteAckNibble = 0xFF;
  _lastType2WriteBits = 0;
  _lastType2WriteCode = 0xFFFF;
  if (!_active || _activeTag.technology != Technology::NFC_A || !_nfc || !_hw || !data) return false;

  const ReturnCode rc = _nfc->rfalT2TPollerWrite(page, const_cast<uint8_t*>(data));
  _lastType2WriteCode = (uint16_t)rc;
  if (rc == ST_ERR_NONE) {
    delay(10);
    return true;
  }

  return type2WritePage(page, data);
}

bool ST25R3916Backend::type2PwdAuth(const uint8_t pwd[4], uint8_t pack[2]) {
  _lastPwdAuthCode = 0xFFFF;
  _lastPwdAuthBits = 0;
  if (!_active || _activeTag.technology != Technology::NFC_A || !_hw || !pwd) return false;

  const uint8_t tx[5] = {0x1B, pwd[0], pwd[1], pwd[2], pwd[3]};
  uint8_t rx[4] = {};
  uint16_t receivedLen = 0;

  ReturnCode rc = _hw->rfalTransceiveBlockingTxRx(
      const_cast<uint8_t*>(tx), sizeof(tx), rx, sizeof(rx), &receivedLen,
      RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(20));

  _lastPwdAuthCode = (uint16_t)rc;
  _lastPwdAuthBits = (uint16_t)(receivedLen * 8U);

  if (rc == ST_ERR_NONE && receivedLen == 2U) {
    if (pack) {
      pack[0] = rx[0];
      pack[1] = rx[1];
    }
    return true;
  }

  size_t rxLen = 0;
  memset(rx, 0, sizeof(rx));
  if (_transceiveBytes(tx, sizeof(tx), rx, sizeof(rx), rxLen, 20) && rxLen == 2U) {
    _lastPwdAuthCode = ST_ERR_NONE;
    _lastPwdAuthBits = 16U;
    if (pack) {
      pack[0] = rx[0];
      pack[1] = rx[1];
    }
    return true;
  }
  return false;
}

bool ST25R3916Backend::_startNfcaListen(const ScanResult& identity) {
  if (!_hw || !_nfc || !_info.initialized ||
      (identity.nfcidLen != 4 && identity.nfcidLen != 7)) return false;

  deactivate();
  if (_hw->rfalSetMode(RFAL_MODE_LISTEN_NFCA, RFAL_BR_106, RFAL_BR_106) != ST_ERR_NONE) return false;
  _hw->st25r3916OscOn();
  _hw->st25r3916WriteRegister(
      ST25R3916_REG_OP_CONTROL,
      ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
          ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
  _hw->st25r3916WriteRegister(
      ST25R3916_REG_MODE, ST25R3916_REG_MODE_targ_targ | ST25R3916_REG_MODE_om_targ_nfca);
  _hw->st25r3916WriteRegister(
      ST25R3916_REG_PASSIVE_TARGET,
      ST25R3916_REG_PASSIVE_TARGET_fdel_2 | ST25R3916_REG_PASSIVE_TARGET_fdel_0 |
          ST25R3916_REG_PASSIVE_TARGET_d_ac_ap2p | ST25R3916_REG_PASSIVE_TARGET_d_212_424_1r);
  _hw->st25r3916WriteRegister(ST25R3916_REG_MASK_RX_TIMER, 0x02);
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_STOP);

  const uint32_t interrupts = ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE |
      ST25R3916_IRQ_MASK_RXS | ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR |
      ST25R3916_IRQ_MASK_CRC | ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 |
      ST25R3916_IRQ_MASK_NRE | ST25R3916_IRQ_MASK_EON | ST25R3916_IRQ_MASK_EOF |
      ST25R3916_IRQ_MASK_WU_A_X | ST25R3916_IRQ_MASK_WU_A;
  _hw->st25r3916ClearInterrupts();
  _hw->st25r3916DisableInterrupts(ST25R3916_IRQ_MASK_ALL);
  _hw->st25r3916EnableInterrupts(interrupts);

  _hw->st25r3916ChangeRegisterBits(
      ST25R3916_REG_AUX, ST25R3916_REG_AUX_nfc_id_mask,
      identity.nfcidLen == 4 ? ST25R3916_REG_AUX_nfc_id_4bytes : ST25R3916_REG_AUX_nfc_id_7bytes);

  uint8_t pt[ST25R3916_PTM_A_LEN] = {};
  memcpy(pt, identity.nfcid, identity.nfcidLen);
  pt[10] = identity.atqa[0];
  pt[11] = identity.atqa[1];
  pt[12] = identity.nfcidLen == 4 ? (uint8_t)(identity.sak & ~0x04U) : 0x04;
  pt[13] = (uint8_t)(identity.sak & ~0x04U);
  pt[14] = (uint8_t)(identity.sak & ~0x04U);
  if (_hw->st25r3916WritePTMem(pt, sizeof(pt)) != ST_ERR_NONE) return false;

  _hw->st25r3916ClrRegisterBits(
      ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SENSE);
  return true;
}

bool ST25R3916Backend::_listenRespond(const uint8_t* data, uint16_t len, bool withCrc) {
  if (!_hw || !data || !len) return false;
  _hw->st25r3916ChangeRegisterBits(
      ST25R3916_REG_ISO14443A_NFC, ST25R3916_REG_ISO14443A_NFC_no_tx_par,
      ST25R3916_REG_ISO14443A_NFC_no_tx_par_off);
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
  _hw->st25r3916WriteFifo(data, len);
  _hw->st25r3916SetNumTxBits((uint16_t)(len * 8U));
  _hw->st25r3916ExecuteCommand(withCrc ? ST25R3916_CMD_TRANSMIT_WITH_CRC
                                      : ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
  return (_hw->st25r3916WaitForInterruptsTimed(ST25R3916_IRQ_MASK_TXE, 20) &
          ST25R3916_IRQ_MASK_TXE) != 0U;
}

bool ST25R3916Backend::startType2Emulation(const ScanResult& identity, uint8_t* dump, size_t dumpLen) {
  if (!dump || dumpLen < 16 || (dumpLen & 3U)) return false;
  stopEmulation();
  _emuIdentity = identity;
  _emuIdentity.sak = 0x00;
  if (_emuIdentity.atqa[0] == 0 && _emuIdentity.atqa[1] == 0) {
    _emuIdentity.atqa[0] = 0x44;  // anticollisionInfo (first byte on air)
    _emuIdentity.atqa[1] = 0x00;  // platformInfo
  }
  _emuDump = dump;
  _emuDumpLen = dumpLen;
  _emuMfc = false;
  _emulating = _startNfcaListen(_emuIdentity);
  return _emulating;
}

bool ST25R3916Backend::startMfcUidEmulation(const ScanResult& identity) {
  if (identity.nfcidLen < 4) return false;
  stopEmulation();
  _emuIdentity = identity;
  if (_emuIdentity.sak != 0x09 && _emuIdentity.sak != 0x08 && _emuIdentity.sak != 0x18)
    _emuIdentity.sak = 0x08;
  if (_emuIdentity.atqa[0] == 0 && _emuIdentity.atqa[1] == 0) _emuIdentity.atqa[0] = 0x04;
  _emuDump = nullptr;
  _emuDumpLen = 0;
  _emuMfc = true;
  _emuMfcDump = false;
  _emulating = _startNfcaListen(_emuIdentity);
  return _emulating;
}

bool ST25R3916Backend::startMfcDumpEmulation(const ScanResult& identity, uint8_t* dump, size_t dumpLen) {
  // 1K Crypto1 path only for now (4K sectoring differs above sector 32).
  if (!dump || dumpLen < 1024) return false;
  stopEmulation();
  _emuIdentity = identity;
  if (_emuIdentity.sak != 0x08 && _emuIdentity.sak != 0x18 && _emuIdentity.sak != 0x09)
    _emuIdentity.sak = 0x08;
  if (_emuIdentity.atqa[0] == 0 && _emuIdentity.atqa[1] == 0) _emuIdentity.atqa[0] = 0x04;
  _emuDump = dump;
  _emuDumpLen = dumpLen;
  _emuMfc = true;
  _emuMfcDump = true;

  // Keys from the 16 sector trailers (block s*4+3): KeyA = bytes 0..5, KeyB = 10..15.
  _mfcSectorCount = 16;
  for (uint8_t s = 0; s < _mfcSectorCount; ++s) {
    const uint8_t* tr = _emuDump + (size_t)(s * 4 + 3) * 16U;
    memcpy(_mfcKeyA[s], tr, 6);      _mfcHaveKeyA[s] = true;
    memcpy(_mfcKeyB[s], tr + 10, 6); _mfcHaveKeyB[s] = true;
  }
  _emuNonceCount = 0;
  if (_crypto) { crypto1_destroy(_crypto); _crypto = nullptr; }
  _emulating = _startNfcaListen(_emuIdentity);
  return _emulating;
}

const uint8_t* ST25R3916Backend::_mfcSectorKey(uint8_t block, uint8_t keytype) const {
  const uint8_t sector = block >> 2;  // 1K: 4 blocks/sector
  if (sector >= _mfcSectorCount) return nullptr;
  if (keytype == 0x60) return _mfcHaveKeyA[sector] ? _mfcKeyA[sector] : nullptr;
  return _mfcHaveKeyB[sector] ? _mfcKeyB[sector] : nullptr;
}

bool ST25R3916Backend::_mfcTxEnc(const uint8_t* data, const uint8_t* parity, uint8_t len) {
  uint8_t packed[27] = {};
  const uint16_t bits = mfcPackBits(data, parity, len, packed);  // 9 bits per byte
  _hw->st25r3916SetRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                ST25R3916_REG_ISO14443A_NFC_no_tx_par);
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
  _hw->st25r3916WriteFifo(packed, (uint16_t)((bits + 7U) / 8U));
  _hw->st25r3916SetNumTxBits(bits);
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
  return (_hw->st25r3916WaitForInterruptsTimed(ST25R3916_IRQ_MASK_TXE, 20) &
          ST25R3916_IRQ_MASK_TXE) != 0U;
}

bool ST25R3916Backend::_mfcRxRaw(uint8_t* out, uint8_t wantBytes, uint32_t timeoutMs) {
  const uint32_t irq = _hw->st25r3916WaitForInterruptsTimed(ST25R3916_IRQ_MASK_RXE, timeoutMs);
  if (!(irq & ST25R3916_IRQ_MASK_RXE)) return false;
  const uint16_t n = _hw->st25r3916GetNumFIFOBytes();
  if (!n) return false;
  uint8_t raw[40] = {};
  const uint16_t rd = (n > sizeof(raw)) ? (uint16_t)sizeof(raw) : n;
  _hw->st25r3916ReadFifo(raw, rd);
  const uint8_t got = mfcUnpackBits(raw, (uint16_t)(rd * 8U), out, wantBytes);
  return got >= wantBytes;
}

void ST25R3916Backend::_mfcHandleAuth(const uint8_t* frame, uint16_t len) {
  if (len < 2) return;
  const uint8_t keytype = frame[0];             // 0x60 / 0x61
  const uint8_t block   = frame[1];
  const uint32_t nt  = MFC_STATIC_NT;
  const uint32_t uid = _uid32(_emuIdentity);
  MFCT("[MFE] AUTH kt=%02X blk=%u uid=%08lX\n", keytype, block, (unsigned long)uid);

  // 1) Answer nt immediately: plaintext, hardware (standard) parity, no CRC.
  //    _listenRespond clears no_tx_par, so the hardware appends the parity bit.
  uint8_t ntb[4] = {(uint8_t)(nt >> 24), (uint8_t)(nt >> 16),
                    (uint8_t)(nt >> 8),  (uint8_t)nt};
  if (!_listenRespond(ntb, 4, /*withCrc=*/false)) { MFCT("[MFE] nt TX fail\n"); return; }

  // 2) Capture the reader's {nr,ar} (encrypted) under no_rx_par, blocking.
  _hw->st25r3916SetRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                ST25R3916_REG_ISO14443A_NFC_no_rx_par);
  uint8_t na[8] = {};
  if (!_mfcRxRaw(na, 8, 40)) {                   // nr(4) + ar(4)
    MFCT("[MFE] no {nr}{ar} after nt (reader gave up -> FDT?)\n");
    _hw->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                  ST25R3916_REG_ISO14443A_NFC_no_rx_par);
    return;                                      // AUTH seen, {nr,ar} incomplete
  }
  const uint32_t nr = bytesToU32(na);
  const uint32_t ar = bytesToU32(na + 4);
  MFCT("[MFE] got nr=%08lX ar=%08lX\n", (unsigned long)nr, (unsigned long)ar);

  // 3) Record the tuple for mfkey32 (dedup identical repeats).
  if (_emuNonceCount < MFC_MAX_NONCES) {
    bool dup = false;
    for (uint8_t i = 0; i < _emuNonceCount; ++i)
      if (_emuNonces[i].nr == nr && _emuNonces[i].ar == ar &&
          _emuNonces[i].block == block && _emuNonces[i].keytype == keytype) { dup = true; break; }
    if (!dup) {
      MfcNonce& e = _emuNonces[_emuNonceCount++];
      e.block = block; e.keytype = keytype; e.nt = nt; e.nr = nr; e.ar = ar; e.uid = uid;
    }
  }

  // 4) Complete the handshake only if the sector key is known.
  const uint8_t* key = _mfcSectorKey(block, keytype);
  if (key) {
    MFCT("[MFE] key sec=%u %s=%02X%02X%02X%02X%02X%02X\n", block >> 2,
         keytype == 0x60 ? "A" : "B", key[0], key[1], key[2], key[3], key[4], key[5]);
  } else {
    MFCT("[MFE] key sec=%u %s ABSENT -> capture-only\n", block >> 2,
         keytype == 0x60 ? "A" : "B");
  }
  if (!key) {
    _hw->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                  ST25R3916_REG_ISO14443A_NFC_no_rx_par);
    return;                                      // capture-only (mode 1)
  }

  if (_crypto) { crypto1_destroy(_crypto); _crypto = nullptr; }
  _crypto = crypto1_create(_key48(key));
  if (!_crypto) {
    _hw->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                  ST25R3916_REG_ISO14443A_NFC_no_rx_par);
    return;
  }

  // Same convention as the proven reader path (mifareClassicAuthenticate):
  //   absorb uid^nt, then absorb the reader nonce. Card side differs from the
  //   reader in one point only: we receive nr ENCRYPTED, so we feed the
  //   ciphertext with is_encrypted=1 and the LFSR absorbs the plaintext nr.
  crypto1_word(_crypto, uid ^ nt, 0);
  for (uint8_t i = 0; i < 4; ++i) crypto1_byte(_crypto, na[i], 1);

  // Verify ar == suc(nt,64), decrypting the reader's 4 ar bytes MSB-first
  // (same decomposition the reader expects: (ar >> (24-8i)) & 0xFF).
  const uint32_t arExp = prng_successor(nt, 64);
  bool ok = true;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t ks   = crypto1_byte(_crypto, 0, 0);
    const uint8_t got  = (uint8_t)(ks ^ na[4 + i]);
    const uint8_t want = (uint8_t)(arExp >> (24U - 8U * i));
    if (got != want) ok = false;
  }
  if (!ok) {
    MFCT("[MFE] ar MISMATCH (wrong key or desync) exp=%08lX\n", (unsigned long)arExp);
    crypto1_destroy(_crypto); _crypto = nullptr;
    _hw->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                  ST25R3916_REG_ISO14443A_NFC_no_rx_par);
    return;
  }
  MFCT("[MFE] ar OK -> sending at, entering session\n");

  // Answer at = suc(nt,96), encrypted (encrypted parity), no CRC, no_tx_par.
  const uint32_t at = prng_successor(nt, 96);
  uint8_t atb[4] = {}, atp[4] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t b = (uint8_t)(at >> (24U - 8U * i));
    atb[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ b);
    atp[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(b));
  }
  _mfcTxEnc(atb, atp, 4);

  // 5) Serve the encrypted session synchronously (no return to the worker).
  _mfcServeSession();

  if (_crypto) { crypto1_destroy(_crypto); _crypto = nullptr; }
  _hw->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                ST25R3916_REG_ISO14443A_NFC_no_rx_par);
}

void ST25R3916Backend::_mfcServeSession() {
  const uint32_t deadline = millis() + 500U;     // hard cap on a reader session
  while (millis() < deadline) {
    const uint32_t irq = _hw->st25r3916WaitForInterruptsTimed(
        ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_EOF, 80);
    if (irq & ST25R3916_IRQ_MASK_EOF) { MFCT("[MFE] sess EOF (field off)\n"); break; }
    if (!(irq & ST25R3916_IRQ_MASK_RXE)) { MFCT("[MFE] sess idle timeout\n"); break; }

    const uint16_t n = _hw->st25r3916GetNumFIFOBytes();
    if (!n || n > 40) break;
    uint8_t raw[40] = {};
    _hw->st25r3916ReadFifo(raw, n);
    uint8_t enc[8] = {};
    if (mfcUnpackBits(raw, (uint16_t)(n * 8U), enc, 8) < 2) continue;

    const uint8_t c0 = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ enc[0]);
    const uint8_t c1 = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ enc[1]);
    crypto1_byte(_crypto, 0, 0);                 // consume the 2 CRC-byte
    crypto1_byte(_crypto, 0, 0);                 // keystream to stay in sync
    MFCT("[MFE] sess cmd=%02X arg=%02X\n", c0, c1);

    if (c0 == 0x30) {                            // READ block c1
      const uint8_t blk = c1 & 0x3F;
      uint8_t resp[18] = {};
      memcpy(resp, _emuDump + (size_t)blk * 16U, 16);
      const uint16_t crc = _crcA(resp, 16);
      resp[16] = (uint8_t)(crc & 0xFF); resp[17] = (uint8_t)(crc >> 8);
      uint8_t eb[18] = {}, ep[18] = {};
      for (uint8_t i = 0; i < 18; ++i) {
        eb[i] = (uint8_t)(crypto1_byte(_crypto, 0, 0) ^ resp[i]);
        ep[i] = (uint8_t)(filter(_crypto->odd) ^ _oddParity(resp[i]));
      }
      _mfcTxEnc(eb, ep, 18);
    } else if (c0 == 0xA0) {                     // WRITE block c1: 4-bit ACK then data
      uint8_t ks = 0;
      for (uint8_t i = 0; i < 4; ++i) ks |= (uint8_t)((crypto1_bit(_crypto, 0, 0) & 1U) << i);
      uint8_t ackEnc = (uint8_t)((ks ^ 0x0A) & 0x0F);
      _hw->st25r3916SetRegisterBits(ST25R3916_REG_ISO14443A_NFC,
                                    ST25R3916_REG_ISO14443A_NFC_no_tx_par);
      _hw->st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
      _hw->st25r3916WriteFifo(&ackEnc, 1);
      _hw->st25r3916SetNumTxBits(4);
      _hw->st25r3916ExecuteCommand(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
      _hw->st25r3916WaitForInterruptsTimed(ST25R3916_IRQ_MASK_TXE, 20);
      uint8_t wn[18] = {};
      if (_mfcRxRaw(wn, 16, 40)) {
        const uint8_t blk = c1 & 0x3F;
        for (uint8_t i = 0; i < 16; ++i) {
          const uint8_t k = crypto1_byte(_crypto, 0, 0);
          _emuDump[(size_t)blk * 16U + i] = (uint8_t)(k ^ wn[i]);
        }
        crypto1_byte(_crypto, 0, 0);             // consume data-CRC keystream
        crypto1_byte(_crypto, 0, 0);
      }
    } else {                                     // HALT (0x50), nested AUTH, or other
      MFCT("[MFE] sess END cmd=%02X%s\n", c0,
           (c0 == 0x60 || c0 == 0x61) ? " = NESTED AUTH (not handled yet)" : "");
      break;
    }
  }
}

bool ST25R3916Backend::emulationWorker() {
  if (!_emulating || !_hw) return false;

  const uint32_t irqs = _hw->st25r3916WaitForInterruptsTimed(
      ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X |
          ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_EOF, 3);
  if (!irqs) return true;

  if (irqs & (ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X)) {
    _hw->st25r3916SetRegisterBits(
        ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
  }

  if (irqs & ST25R3916_IRQ_MASK_RXE) {
    const uint16_t n = _hw->st25r3916GetNumFIFOBytes();
    uint8_t frame[64] = {};
    if (n && n <= sizeof(frame)) {
      _hw->st25r3916ReadFifo(frame, n);
    if (_emuMfcDump) {
      // Full MIFARE Classic emulation: AUTH drives the synchronous handshake;
      // READ/WRITE are served inside _mfcHandleAuth, never back here.
      if (n >= 2 && (frame[0] == 0x60 || frame[0] == 0x61)) _mfcHandleAuth(frame, n);
      else if (frame[0] == 0x50) _hw->st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SLEEP);
    } else if (!_emuMfc) {
      const size_t pages = _emuDumpLen / 4U;
      const uint8_t cmd = frame[0];
      if (cmd == 0x30 && n >= 2) {
        uint8_t resp[16] = {};
        for (uint8_t i = 0; i < 4; ++i) {
          const size_t p = pages ? ((size_t)frame[1] + i) % pages : 0;
          memcpy(resp + i * 4U, _emuDump + p * 4U, 4);
        }
        _listenRespond(resp, sizeof(resp));
      } else if (cmd == 0x3A && n >= 3 && frame[2] >= frame[1]) {
        const uint16_t count = (uint16_t)frame[2] - frame[1] + 1U;
        if (count <= 64) {
          uint8_t resp[256] = {}; uint16_t out = 0;
          for (uint16_t p = frame[1]; p <= frame[2]; ++p) {
            const size_t pp = pages ? p % pages : 0;
            memcpy(resp + out, _emuDump + pp * 4U, 4); out += 4;
          }
          _listenRespond(resp, out);
        }
      } else if (cmd == 0x60) {
        const uint8_t storage = pages <= 45 ? 0x0F : (pages <= 135 ? 0x11 : 0x13);
        const uint8_t ver[8] = {0x00,0x04,0x04,0x02,0x01,0x00,storage,0x03};
        _listenRespond(ver, sizeof(ver));
      } else if (cmd == 0x1B && n >= 5) {
        const uint8_t pack[2] = {0x00, 0x00};
        _listenRespond(pack, sizeof(pack));
      } else if (cmd == 0x3C) {
        const uint8_t sig[32] = {};
        _listenRespond(sig, sizeof(sig));
      } else if (cmd == 0xA2 && n >= 6) {
        const size_t p = frame[1];
        if (p < pages) memcpy(_emuDump + p * 4U, frame + 2, 4);
        uint8_t ack = 0x0A;
        _hw->st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
        _hw->st25r3916WriteFifo(&ack, 1);
        _hw->st25r3916SetNumTxBits(4);
        _hw->st25r3916ExecuteCommand(ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
      } else if (cmd == 0x50) {
        _hw->st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SLEEP);
      }
    }  // end mode dispatch (_emuMfcDump / NTAG)
    }
  }

  if (irqs & ST25R3916_IRQ_MASK_EOF) {
    _hw->st25r3916ClrRegisterBits(
        ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a);
    _hw->st25r3916ExecuteCommand(ST25R3916_CMD_GOTO_SENSE);
  }
  return true;
}

bool ST25R3916Backend::startReaderScan() {
  if (!_hw || !_nfc || !_info.initialized) return false;
  stopReaderScan();
  stopEmulation();
  ScanResult identity{};
  identity.technology = Technology::NFC_A;
  identity.nfcidLen = 4;
  identity.nfcid[0] = 0x12; identity.nfcid[1] = 0x34;
  identity.nfcid[2] = 0x56; identity.nfcid[3] = 0x78;
  identity.atqa[0] = 0x04; identity.atqa[1] = 0x00;
  identity.sak = 0x08;
  if (!_startNfcaListen(identity)) return false;
  _readerScanning = true;
  return true;
}

bool ST25R3916Backend::readerScanDetected() {
  if (!_readerScanning || !_hw) return false;
  const uint32_t irqs = _hw->st25r3916WaitForInterruptsTimed(
      ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X, 3);
  return (irqs & (ST25R3916_IRQ_MASK_WU_A | ST25R3916_IRQ_MASK_WU_A_X)) != 0U;
}

void ST25R3916Backend::stopReaderScan() {
  const bool wasScanning = _readerScanning;
  _readerScanning = false;
  if (!_hw || !wasScanning) return;
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_STOP);
  _hw->st25r3916DisableInterrupts(ST25R3916_IRQ_MASK_ALL);
  _hw->st25r3916ClearInterrupts();
  _hw->rfalSetMode(RFAL_MODE_POLL_NFCA, RFAL_BR_106, RFAL_BR_106);
  _hw->rfalFieldOff();
}

void ST25R3916Backend::stopEmulation() {
  const bool wasEmulating = _emulating;
  _emulating = false;
  _emuMfc = false;
  _emuMfcDump = false;
  _emuDump = nullptr;
  _emuDumpLen = 0;
  if (_crypto) { crypto1_destroy(_crypto); _crypto = nullptr; }
  if (!_hw || !wasEmulating) return;
  _hw->st25r3916ExecuteCommand(ST25R3916_CMD_STOP);
  _hw->st25r3916DisableInterrupts(ST25R3916_IRQ_MASK_ALL);
  _hw->st25r3916ClearInterrupts();
  _hw->rfalSetMode(RFAL_MODE_POLL_NFCA, RFAL_BR_106, RFAL_BR_106);
  _hw->rfalFieldOff();
}

void ST25R3916Backend::end() {
  stopReaderScan();
  stopEmulation();
  if (_crypto) {
    crypto1_destroy(_crypto);
    _crypto = nullptr;
  }
  _active = false;
  _activeTag = ScanResult{};

  if (_hw && _info.initialized) {
    _hw->rfalFieldOff();
    _hw->rfalDeinitialize();
  }

  delete _nfc;
  _nfc = nullptr;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
  delete _hw;
#pragma GCC diagnostic pop
  _hw = nullptr;

  _info = Info{};
}

#endif
