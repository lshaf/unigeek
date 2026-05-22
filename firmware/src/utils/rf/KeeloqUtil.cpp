#include "KeeloqUtil.h"
#include "KeeloqKeystore.h"

static constexpr uint32_t KEELOQ_NLF = 0x3A5C742E;

#define bitAt(x, n) (((x) >> (n)) & 1)
#define g5(x, a, b, c, d, e) \
    (bitAt(x, a) + bitAt(x, b) * 2 + bitAt(x, c) * 4 + bitAt(x, d) * 8 + bitAt(x, e) * 16)

uint32_t KeeloqUtil::encrypt(uint32_t data, uint64_t key) {
  uint32_t x = data;
  for (uint32_t r = 0; r < 528; r++) {
    x = (x >> 1) ^ ((bitAt(x, 0) ^ bitAt(x, 16) ^ (uint32_t)bitAt(key, r & 63) ^
                     bitAt(KEELOQ_NLF, g5(x, 1, 9, 20, 26, 31))) << 31);
  }
  return x;
}

uint32_t KeeloqUtil::decrypt(uint32_t data, uint64_t key) {
  uint32_t x = data;
  for (uint32_t r = 0; r < 528; r++) {
    x = (x << 1) ^ bitAt(x, 31) ^ bitAt(x, 15) ^
        (uint32_t)bitAt(key, (15 - r) & 63) ^
        bitAt(KEELOQ_NLF, g5(x, 0, 8, 19, 25, 30));
  }
  return x;
}

uint64_t KeeloqUtil::normalLearning(uint32_t data, uint64_t key) {
  data &= 0x0FFFFFFF;
  data |= 0x20000000;
  uint32_t k1 = decrypt(data, key);
  data &= 0x0FFFFFFF;
  data |= 0x60000000;
  uint32_t k2 = decrypt(data, key);
  return ((uint64_t)k2 << 32) | k1;
}

uint64_t KeeloqUtil::reverseBits(uint64_t num, uint8_t bits) {
  uint64_t res = 0;
  for (uint8_t i = 0; i < bits; ++i) {
    res <<= 1;
    res |= bitAt(num, i);
  }
  return res;
}

void KeeloqUtil::unpack(uint64_t decoded, uint32_t& fix, uint32_t& encrypted,
                        uint8_t& btn, uint32_t& serial) {
  uint64_t yek = reverseBits(decoded, 64);
  fix       = (uint32_t)(yek >> 32);
  encrypted = (uint32_t)(yek & 0xFFFFFFFF);
  btn       = (uint8_t)(fix >> 28);
  serial    = fix & 0x0FFFFFFFu;
}

// Mirrors Bruce's RfCodes::keeloq_check_decrypt — the structure check applied
// to a candidate plaintext after decrypting with a manufacturer key. Match
// requires the top 4 bits to equal the captured button, and the middle 8 bits
// to equal either the low byte of the serial or zero (which some chips emit).
static bool _checkDecrypt(uint32_t decrypted, uint8_t btn, uint32_t serial,
                           uint16_t& cnt_out) {
  uint16_t end_serial = serial & 0xFF;
  uint8_t  mid_byte   = (uint8_t)((decrypted >> 16) & 0xFF);
  if ((decrypted >> 28) == btn && (mid_byte == end_serial || mid_byte == 0)) {
    cnt_out = (uint16_t)(decrypted & 0xFFFF);
    return true;
  }
  return false;
}

// Manufacturer-specific hop bit layout. Bruce's keeloq_step switch.
// Default: (btn << 28) | ((serial & 0x3FF) << 16) | cnt
static uint32_t _buildHop(const String& mf_name, uint8_t btn, uint32_t serial, uint16_t cnt) {
  if (mf_name == "Aprimatic") {
    uint32_t apri_serial = serial;
    uint8_t  apr1 = 0;
    for (uint16_t i = 1; i != 0x400; i <<= 1) {
      if (apri_serial & i) apr1++;
    }
    apri_serial &= 0x3FF;
    if ((apr1 % 2) == 0) apri_serial |= 0xC00;
    return ((uint32_t)btn << 28) | ((apri_serial & 0xFFF) << 16) | cnt;
  }
  if (mf_name == "DTM_Neo"   || mf_name == "FAAC_RC,XT"     || mf_name == "Mutanco_Mutancode" ||
      mf_name == "Came_Space"|| mf_name == "Genius_Bravo"   || mf_name == "GSN"               ||
      mf_name == "Rosh"      || mf_name == "Rossi"          || mf_name == "Peccinin"          ||
      mf_name == "Steelmate" || mf_name == "Cardin_S449") {
    return ((uint32_t)btn << 28) | ((serial & 0xFFF) << 16) | cnt;
  }
  if (mf_name == "NICE_Smilo" || mf_name == "NICE_MHOUSE" || mf_name == "JCM_Tech") {
    return ((uint32_t)btn << 28) | ((serial & 0xFF) << 16) | cnt;
  }
  if (mf_name == "Merlin")    return ((uint32_t)btn << 28) | ((uint32_t)0x000 << 16) | cnt;
  if (mf_name == "Centurion") return ((uint32_t)btn << 28) | ((uint32_t)0x1CE << 16) | cnt;
  if (mf_name == "Monarch")   return ((uint32_t)btn << 28) | ((uint32_t)0x100 << 16) | cnt;
  if (mf_name == "Dea_Mio") {
    uint8_t  first_disc_num = (serial >> 8) & 0xF;
    uint8_t  result_disc    = 0xC + (first_disc_num % 4);
    uint32_t dea_serial     = (serial & 0xFF) | ((uint32_t)result_disc << 8);
    return ((uint32_t)btn << 28) | ((dea_serial & 0xFFF) << 16) | cnt;
  }
  return ((uint32_t)btn << 28) | ((serial & 0x3FF) << 16) | cnt;
}

bool KeeloqUtil::step(CC1101Util::Signal& sig, uint16_t increment) {
  if (sig.mf_name.length() == 0) return false;

  auto& store = KeeloqKeystore::instance();
  if (store.count() == 0) return false;

  const KeeloqKey* match = nullptr;
  for (size_t i = 0; i < store.count(); i++) {
    if (store.at(i).mf_name == sig.mf_name) { match = &store.at(i); break; }
  }
  if (!match) return false;

  sig.cnt = (uint16_t)(sig.cnt + increment);
  sig.hop = _buildHop(sig.mf_name, sig.btn, sig.serial, sig.cnt);

  if (match->type == LEARNING_SIMPLE) {
    sig.encrypted = encrypt(sig.hop, match->key);
  } else if (match->type == LEARNING_NORMAL) {
    uint64_t man = normalLearning(sig.hop, match->key);
    sig.encrypted = encrypt(sig.hop, man);
  } else {
    return false;
  }

  uint64_t enc_rev = reverseBits(sig.encrypted, 32);
  uint64_t fix_rev = reverseBits(sig.fix, 32);
  sig.key = (enc_rev << 32) | (fix_rev & 0xFFFFFFFF);

  return true;
}

bool KeeloqUtil::identify(CC1101Util::Signal& sig) {
  auto& store = KeeloqKeystore::instance();
  if (store.count() == 0) return false;

  for (size_t i = 0; i < store.count(); i++) {
    const KeeloqKey& k = store.at(i);
    uint32_t decrypted = 0;

    if (k.type == LEARNING_SIMPLE) {
      decrypted = decrypt(sig.encrypted, k.key);
    } else if (k.type == LEARNING_NORMAL) {
      uint64_t man = normalLearning(sig.fix, k.key);
      decrypted = decrypt(sig.encrypted, man);
    } else {
      continue;
    }

    uint16_t cnt = 0;
    if (_checkDecrypt(decrypted, sig.btn, sig.serial, cnt)) {
      sig.mf_name = k.mf_name;
      sig.hop     = decrypted;
      sig.cnt     = cnt;
      return true;
    }
  }
  return false;
}
