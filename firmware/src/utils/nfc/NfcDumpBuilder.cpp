#include "NfcDumpBuilder.h"

#include <cstring>


bool NfcDumpBuilder::buildMifareClassic1K(const uint8_t uid[4],
                                         const uint8_t* ndef, size_t ndefLen,
                                         uint8_t* out, size_t& outLen,
                                         size_t maxLen) {
  if (!uid || !out || maxLen < MIFARE_CLASSIC_1K_SIZE) return false;
  if (ndefLen > 0 && !ndef) return false;

  // Sectors 1..15 provide 15 * 3 * 16 = 720 bytes of NDEF data area.
  // TLV overhead is: type + length + terminator (short) or
  // type + FF + 2-byte length + terminator (extended).
  const size_t tlvOverhead = (ndefLen < 0xFF) ? 3 : 5;
  if (ndefLen + tlvOverhead > MIFARE_CLASSIC_1K_NDEF_CAPACITY) return false;

  memset(out, 0x00, MIFARE_CLASSIC_1K_SIZE);

  // Block 0 — manufacturer block. The first five bytes are the 4-byte UID
  // followed by BCC. The remaining bytes mirror a conventional MFC1K image;
  // the Chameleon Ultra treats this file as a raw 64-block dump.
  out[0] = uid[0];
  out[1] = uid[1];
  out[2] = uid[2];
  out[3] = uid[3];
  out[4] = (uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
  out[5] = 0x08;
  out[6] = 0x04;
  out[7] = 0x00;
  out[8] = 0x62;
  out[9] = 0x63;
  out[10] = 0x64;
  out[11] = 0x65;
  out[12] = 0x66;
  out[13] = 0x67;
  out[14] = 0x68;
  out[15] = 0x69;

  // MAD1, sector 0 blocks 1 and 2.
  // 0x14 is the MAD CRC for this fixed map; 0x01 is the MAD info byte.
  // Every sector 1..15 is assigned NFC Forum NDEF AID 0x03E1.
  out[16] = 0x14;
  out[17] = 0x01;
  for (size_t i = 0; i < 15; ++i) {
    const size_t p = 18 + i * 2;
    out[p] = 0x03;
    out[p + 1] = 0xE1;
  }

  // MAD sector trailer (block 3).
  static const uint8_t madTrailer[16] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
    0x78, 0x77, 0x88, 0xC1,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  memcpy(&out[3 * 16], madTrailer, sizeof(madTrailer));

  // NFC Forum sector trailers for sectors 1..15.
  static const uint8_t ndefTrailer[16] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7,
    0x7F, 0x07, 0x88, 0x40,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  for (uint8_t sector = 1; sector < 16; ++sector) {
    const size_t trailerBlock = sector * 4 + 3;
    memcpy(&out[trailerBlock * 16], ndefTrailer, sizeof(ndefTrailer));
  }

  // Build the NDEF Message TLV into a contiguous temporary buffer, then
  // distribute it over the three data blocks of each NFC Forum sector,
  // skipping every sector trailer.
  uint8_t tlv[MIFARE_CLASSIC_1K_NDEF_CAPACITY] = {};
  size_t t = 0;
  tlv[t++] = 0x03;
  if (ndefLen < 0xFF) {
    tlv[t++] = (uint8_t)ndefLen;
  } else {
    tlv[t++] = 0xFF;
    tlv[t++] = (uint8_t)((ndefLen >> 8) & 0xFF);
    tlv[t++] = (uint8_t)(ndefLen & 0xFF);
  }
  if (ndefLen > 0) {
    memcpy(&tlv[t], ndef, ndefLen);
    t += ndefLen;
  }
  tlv[t++] = 0xFE;

  size_t src = 0;
  for (uint8_t sector = 1; sector < 16 && src < t; ++sector) {
    for (uint8_t blockInSector = 0; blockInSector < 3 && src < t; ++blockInSector) {
      const size_t block = sector * 4 + blockInSector;
      const size_t n = (t - src > 16) ? 16 : (t - src);
      memcpy(&out[block * 16], &tlv[src], n);
      src += n;
    }
  }

  outLen = MIFARE_CLASSIC_1K_SIZE;
  return true;
}


namespace {

static uint8_t _madCrc8(const uint8_t* data, size_t len) {
  // CRC-8/MIFARE-MAD: poly 0x1D, effective MSB-first preset 0xC7.
  // AN10787 documents preset 0xE3; its bit-mirrored representation is 0xC7.
  uint8_t crc = 0xC7;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80)
              ? (uint8_t)((crc << 1) ^ 0x1D)
              : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static size_t _mfc4kNdefOffset(size_t logicalOffset) {
  // NFC sectors 1..15: 15 * 3 data blocks = 720 bytes.
  if (logicalOffset < 720) {
    const size_t sector = 1 + logicalOffset / 48;
    const size_t within = logicalOffset % 48;
    const size_t block = sector * 4 + within / 16;
    return block * 16 + within % 16;
  }

  logicalOffset -= 720;

  // NFC sectors 17..31: another 15 * 3 data blocks = 720 bytes.
  if (logicalOffset < 720) {
    const size_t sector = 17 + logicalOffset / 48;
    const size_t within = logicalOffset % 48;
    const size_t block = sector * 4 + within / 16;
    return block * 16 + within % 16;
  }

  logicalOffset -= 720;

  // NFC sectors 32..39: 8 * 15 data blocks = 1920 bytes.
  const size_t sector = 32 + logicalOffset / 240;
  const size_t within = logicalOffset % 240;
  const size_t block = 128 + (sector - 32) * 16 + within / 16;
  return block * 16 + within % 16;
}

} // namespace


bool NfcDumpBuilder::buildMifareClassic4K(const uint8_t uid[4],
                                          const uint8_t* ndef, size_t ndefLen,
                                          uint8_t* out, size_t& outLen,
                                          size_t maxLen) {
  if (!uid || !out || maxLen < MIFARE_CLASSIC_4K_SIZE) return false;
  if (ndefLen > 0 && !ndef) return false;

  const size_t tlvOverhead = (ndefLen < 0xFF) ? 3 : 5;
  if (ndefLen + tlvOverhead > MIFARE_CLASSIC_4K_NDEF_CAPACITY) return false;

  memset(out, 0x00, MIFARE_CLASSIC_4K_SIZE);

  // Block 0 — manufacturer / anti-collision fields used by UniGeek dumps.
  out[0] = uid[0];
  out[1] = uid[1];
  out[2] = uid[2];
  out[3] = uid[3];
  out[4] = (uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]);
  out[5] = 0x18; // SAK: MIFARE Classic 4K
  out[6] = 0x02; // ATQA wire byte 0
  out[7] = 0x00; // ATQA wire byte 1
  out[8] = 0x62;
  out[9] = 0x63;
  out[10] = 0x64;
  out[11] = 0x65;
  out[12] = 0x66;
  out[13] = 0x67;
  out[14] = 0x68;
  out[15] = 0x69;

  static const uint8_t madTrailerV2[16] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
    0x78, 0x77, 0x88, 0xC2,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };

  static const uint8_t ndefTrailer[16] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7,
    0x7F, 0x07, 0x88, 0x40,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };

  // MAD1 in sector 0: sectors 1..15 all belong to NFC Forum AID 0x03E1.
  // Keep the same CPS pointer/info byte used by the existing 1K builder.
  uint8_t mad1[31] = {};
  mad1[0] = 0x01; // info byte
  for (size_t i = 0; i < 15; ++i) {
    mad1[1 + i * 2] = 0x03;     // application code (LSB)
    mad1[2 + i * 2] = 0xE1;     // function-cluster code (MSB)
  }
  out[16] = _madCrc8(mad1, sizeof(mad1));
  memcpy(&out[17], mad1, sizeof(mad1));
  memcpy(&out[3 * 16], madTrailerV2, sizeof(madTrailerV2));

  // MAD2 in sector 16 (blocks 64..66): sectors 17..39 are NFC Forum sectors.
  uint8_t mad2[47] = {};
  mad2[0] = 0x01; // info byte; same CPS pointer as MAD1
  for (size_t i = 0; i < 23; ++i) {
    mad2[1 + i * 2] = 0x03;
    mad2[2 + i * 2] = 0xE1;
  }
  out[64 * 16] = _madCrc8(mad2, sizeof(mad2));
  memcpy(&out[64 * 16 + 1], mad2, sizeof(mad2));
  memcpy(&out[67 * 16], madTrailerV2, sizeof(madTrailerV2));

  // NFC Forum trailers. Sector 16 is reserved for MAD2.
  for (uint8_t sector = 1; sector < 40; ++sector) {
    if (sector == 16) continue;

    const size_t trailerBlock =
        (sector < 32)
          ? (size_t)sector * 4 + 3
          : 128 + (size_t)(sector - 32) * 16 + 15;

    memcpy(&out[trailerBlock * 16], ndefTrailer, sizeof(ndefTrailer));
  }

  // Write the NDEF Message TLV directly into the logical NFC data area,
  // avoiding a multi-kilobyte temporary buffer on the ESP32 task stack.
  size_t logical = 0;
  auto putByte = [&](uint8_t value) {
    out[_mfc4kNdefOffset(logical++)] = value;
  };

  putByte(0x03);
  if (ndefLen < 0xFF) {
    putByte((uint8_t)ndefLen);
  } else {
    putByte(0xFF);
    putByte((uint8_t)((ndefLen >> 8) & 0xFF));
    putByte((uint8_t)(ndefLen & 0xFF));
  }

  for (size_t i = 0; i < ndefLen; ++i) putByte(ndef[i]);
  putByte(0xFE);

  outLen = MIFARE_CLASSIC_4K_SIZE;
  return true;
}


bool NfcDumpBuilder::buildNtag21x(Ntag21xType type, const uint8_t uid[7],
                                  const uint8_t* ndef, size_t ndefLen,
                                  uint8_t* out, size_t& outLen, size_t maxLen) {
  if (!uid || !out || (ndefLen > 0 && !ndef)) return false;

  size_t imageSize = 0, ndefCapacity = 0;
  uint8_t ccSize = 0;
  uint16_t cfgPage = 0;

  switch (type) {
    case Ntag21xType::NTAG210: imageSize=NTAG210_SIZE; ndefCapacity=NTAG210_NDEF_CAPACITY; ccSize=0x06; cfgPage=16; break;
    case Ntag21xType::NTAG212: imageSize=NTAG212_SIZE; ndefCapacity=NTAG212_NDEF_CAPACITY; ccSize=0x10; cfgPage=37; break;
    case Ntag21xType::NTAG213: imageSize=NTAG213_SIZE; ndefCapacity=NTAG213_NDEF_CAPACITY; ccSize=0x12; cfgPage=41; break;
    case Ntag21xType::NTAG215: imageSize=NTAG215_SIZE; ndefCapacity=NTAG215_NDEF_CAPACITY; ccSize=0x3E; cfgPage=131; break;
    case Ntag21xType::NTAG216: imageSize=NTAG216_SIZE; ndefCapacity=NTAG216_NDEF_CAPACITY; ccSize=0x6D; cfgPage=227; break;
  }

  if (maxLen < imageSize) return false;
  const size_t tlvOverhead = (ndefLen < 0xFF) ? 3 : 5;
  if (ndefLen + tlvOverhead > ndefCapacity) return false;

  memset(out, 0x00, imageSize);

  out[0]=uid[0]; out[1]=uid[1]; out[2]=uid[2];
  out[3]=(uint8_t)(0x88 ^ uid[0] ^ uid[1] ^ uid[2]);
  out[4]=uid[3]; out[5]=uid[4]; out[6]=uid[5]; out[7]=uid[6];
  out[8]=(uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
  out[9]=0x48; out[10]=0x00; out[11]=0x00;

  out[12]=0xE1; out[13]=0x10; out[14]=ccSize; out[15]=0x00;

  size_t pos=16;
  out[pos++]=0x03;
  if (ndefLen < 0xFF) out[pos++]=(uint8_t)ndefLen;
  else {
    out[pos++]=0xFF;
    out[pos++]=(uint8_t)((ndefLen >> 8) & 0xFF);
    out[pos++]=(uint8_t)(ndefLen & 0xFF);
  }
  if (ndefLen) { memcpy(&out[pos], ndef, ndefLen); pos += ndefLen; }
  out[pos++]=0xFE;

  // Unprotected factory-style configuration. Dynamic-lock pages, where
  // present, remain zero-filled.
  size_t off=(size_t)cfgPage*4;
  out[off+0]=0x00; out[off+1]=0x00; out[off+2]=0x00; out[off+3]=0xFF;
  off=(size_t)(cfgPage+1)*4;
  out[off+0]=0x00; out[off+1]=0x05; out[off+2]=0x00; out[off+3]=0x00;

  outLen=imageSize;
  return true;
}

bool NfcDumpBuilder::buildNtag215(const uint8_t uid[7],
                                  const uint8_t* ndef, size_t ndefLen,
                                  uint8_t* out, size_t& outLen, size_t maxLen) {
  return buildNtag21x(Ntag21xType::NTAG215, uid, ndef, ndefLen,
                      out, outLen, maxLen);
}
