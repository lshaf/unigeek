#include "ui/components/TagPrompt.h"
#include "ST25R3916Screen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "core/ConfigManager.h"
#include "ui/views/ProgressView.h"
#include "ui/views/DictionaryKeyProgressView.h"
#include "ui/views/DictionaryDiscoverySummary.h"
#include "ui/views/LogView.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"
#include "ui/components/TagPrompt.h"
#include "utils/nfc/NdefParser.h"
#include "utils/nfc/HfDumpParser.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/HfDumpBuilder.h"
#include "utils/nfc/MagicCard.h"
#include "utils/nfc/MfcKeyStore.h"
#include "utils/IdentityFile.h"
#include "screens/utility/NfcMemoryViewScreen.h"
#include <mbedtls/md.h>

#if defined(DEVICE_HAS_ST25R3916)
#include "utils/nfc/NFCUtility.h"
#include "utils/nfc/ST25R3916Backend.h"
#include "utils/nfc/ST25R3916Experimental.h"
#endif

namespace {
String st25BaseName(const String& path) { int slash=path.lastIndexOf('/'); return slash>=0 ? path.substring(slash+1) : path; }
constexpr uint8_t kSt25I2cAddr = 0x50;
const char* inferNfcAType(uint8_t sak, const uint8_t atqa[2]) {
  if (sak == 0x09) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) {
    if (atqa[0] == 0x03) return "MIFARE DESFire";
    return "ISO14443-4";
  }
  if (sak == 0x00) {
    return "Ultralight / NTAG";
  }
  return "ISO14443A";
}

bool isMifareClassic(uint8_t sak) {
  return sak == 0x09 || sak == 0x08 || sak == 0x18;
}

void mfcDimensions(uint8_t sak, size_t& sectors, size_t& blocks) {
  sectors = 0;
  blocks = 0;
  if (sak == 0x09) { sectors = 5; blocks = 20; }
  else if (sak == 0x08) { sectors = 16; blocks = 64; }
  else if (sak == 0x18) { sectors = 40; blocks = 256; }
}

size_t sectorFirstBlock(size_t sector) {
  return sector < 32 ? sector * 4U : 128U + (sector - 32U) * 16U;
}

size_t sectorBlockCount(size_t sector) {
  return sector < 32 ? 4U : 16U;
}

#if defined(DEVICE_HAS_ST25R3916)
bool sameTag(const ST25R3916Backend::ScanResult& a,
             const ST25R3916Backend::ScanResult& b) {
  return a.nfcidLen == b.nfcidLen && a.nfcidLen > 0 &&
         memcmp(a.nfcid, b.nfcid, a.nfcidLen) == 0;
}
#endif
}

#if defined(DEVICE_HAS_ST25R3916)
static bool st25Begin(ST25R3916Backend& dev, ST25R3916Screen::Interface interface) {
  if (interface == ST25R3916Screen::Interface::I2C)
    return dev.beginI2C(Uni.ExI2C, kSt25I2cAddr);
  return dev.beginSPI(Uni.Spi, ST25R3916_CS_PIN, ST25R3916_IRQ_PIN, ST25R3916_SPI_HZ);
}

static const char* st25InterfaceName(ST25R3916Screen::Interface interface) {
  return interface == ST25R3916Screen::Interface::I2C ? "I2C" : "SPI";
}

enum class St25WaitResult : uint8_t { FOUND, TIMEOUT, CANCELLED };

static St25WaitResult st25WaitForTag(ST25R3916Backend& dev,
                                      uint16_t techMask,
                                      ST25R3916Backend::ScanResult& tag,
                                      uint32_t timeoutMs,
                                      bool keepActive = true) {
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      dev.deactivate();
      return St25WaitResult::CANCELLED;
    }

    const uint32_t elapsed = millis() - started;
    const uint32_t remaining = elapsed < timeoutMs ? timeoutMs - elapsed : 0;
    const uint32_t slice = remaining < 200U ? remaining : 200U;
    if (slice && dev.scan(techMask, tag, slice, keepActive))
      return St25WaitResult::FOUND;
    delay(50);
  }
  dev.deactivate();
  return St25WaitResult::TIMEOUT;
}

enum class St25Type4bScanResult : uint8_t {
  FOUND,
  NO_TAG,
  WRONG_TECH,
  NO_ISODEP,
  CANCELLED,
};

static St25Type4bScanResult st25ScanType4b(ST25R3916Backend& dev,
                                           ST25R3916Backend::ScanResult& tag,
                                           bool requireIsoDep) {
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_B, tag, 1500, true);
  if (waitResult == St25WaitResult::CANCELLED)
    return St25Type4bScanResult::CANCELLED;
  if (waitResult == St25WaitResult::FOUND) {
    if (requireIsoDep && !tag.isoDep) {
      dev.deactivate();
      return St25Type4bScanResult::NO_ISODEP;
    }
    return St25Type4bScanResult::FOUND;
  }

  ST25R3916Backend::ScanResult other;
  const uint16_t otherTechs =
      ST25R3916Backend::TECH_A | ST25R3916Backend::TECH_F | ST25R3916Backend::TECH_V;
  const auto otherResult = st25WaitForTag(dev, otherTechs, other, 250, false);
  if (otherResult == St25WaitResult::CANCELLED)
    return St25Type4bScanResult::CANCELLED;
  if (otherResult == St25WaitResult::FOUND)
    return St25Type4bScanResult::WRONG_TECH;
  return St25Type4bScanResult::NO_TAG;
}

static void st25ShowType4bScanError(St25Type4bScanResult result) {
  if (result == St25Type4bScanResult::CANCELLED) return;
  if (result == St25Type4bScanResult::WRONG_TECH) {
    ShowStatusAction::show("Tag not supported", 1200);
  } else if (result == St25Type4bScanResult::NO_ISODEP) {
    ShowStatusAction::show("ISO-DEP not supported", 1600);
  } else {
    ShowStatusAction::show("Tag not detected", 1200);
  }
}

static uint16_t st25MfuConfig0(const String& type) {
  if (type == "NTAG210" || type == "Ultralight EV1 11") return 16;
  if (type == "NTAG212" || type == "Ultralight EV1 21") return 37;
  if (type == "NTAG213") return 41;
  if (type == "NTAG215") return 131;
  if (type == "NTAG216") return 227;
  return 0xFFFF;
}

static uint16_t st25MfuDynamicLock(const String& type) {
  if (type == "NTAG212" || type == "Ultralight EV1 21") return 36;
  if (type == "NTAG213") return 40;
  if (type == "NTAG215") return 130;
  if (type == "NTAG216") return 226;
  return 0xFFFF;
}

static bool st25PwdFromText(const String& input, uint8_t pwd[4]) {
  String text = input;
  text.trim();
  if (!text.length()) return false;

  String raw = text;
  bool explicitRaw = false;
  if (raw.startsWith("0x") || raw.startsWith("0X")) {
    raw = raw.substring(2);
    explicitRaw = true;
  }
  if (raw.indexOf(':') >= 0 || raw.indexOf('-') >= 0 || raw.indexOf(' ') >= 0) {
    explicitRaw = true;
    raw.replace(":", "");
    raw.replace("-", "");
    raw.replace(" ", "");
  }
  if (explicitRaw) {
    if (raw.length() != 8) return false;
    for (uint8_t i = 0; i < 4; ++i) {
      char byteText[3] = {raw[(unsigned)i * 2U], raw[(unsigned)i * 2U + 1U], 0};
      char* end = nullptr;
      unsigned long v = strtoul(byteText, &end, 16);
      if (!end || *end) return false;
      pwd[i] = (uint8_t)v;
    }
    return true;
  }

  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (!md) return false;
  uint8_t digest[16] = {};
  if (mbedtls_md(md, reinterpret_cast<const unsigned char*>(text.c_str()), text.length(), digest) != 0) return false;
  memcpy(pwd, digest, 4);
  return true;
}

static bool st25PromptPwd(uint8_t pwd[4], const char* title = "Password / 0xXXXXXXXX") {
  String text = InputTextAction::popup(title, "", InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled()) return false;
  if (!st25PwdFromText(text, pwd)) {
    if (Screen.current()) Screen.current()->render();
    ShowStatusAction::show("Invalid password");
    return false;
  }
  return true;
}

static bool st25PwdAuth(ST25R3916Backend& dev, const uint8_t pwd[4]) {
  return dev.type2PwdAuth(pwd);
}

static bool st25ReadPage(ST25R3916Backend& dev, uint16_t page, uint8_t out[4]) {
  uint8_t buf[16] = {};
  if (!dev.type2ReadPages((uint8_t)page, buf)) return false;
  memcpy(out, buf, 4); return true;
}

static bool st25ReadPageTailSafe(ST25R3916Backend& dev, uint16_t page,
                                 uint16_t totalPages, uint8_t out[4]) {
  if (!out || !totalPages || page >= totalPages || totalPages > 256) return false;
  uint16_t start = page;
  uint8_t skip = 0;
  if (page + 3U >= totalPages) {
    start = totalPages >= 4U ? totalPages - 4U : 0U;
    skip = (uint8_t)(page - start);
  }
  uint8_t buf[16] = {};
  if (!dev.type2ReadPages((uint8_t)start, buf)) return false;
  memcpy(out, buf + (size_t)skip * 4U, 4);
  return true;
}

static bool st25WritePageVerified(ST25R3916Backend& dev, uint16_t page,
                                  const uint8_t data[4], uint16_t totalPages = 0) {
  if (dev.type2WritePage((uint8_t)page, data)) return true;
  uint8_t verify[4] = {};
  const bool readOk = totalPages
      ? st25ReadPageTailSafe(dev, page, totalPages, verify)
      : st25ReadPage(dev, page, verify);
  return readOk && memcmp(verify, data, 4) == 0;
}

static bool st25EnsureMfuAuth(ST25R3916Backend& dev, const String& type, uint16_t pages,
                              uint16_t firstPage, uint16_t lastPage, bool forRead) {
  const uint16_t cfg = st25MfuConfig0(type);
  if (cfg == 0xFFFF || cfg + 1 >= pages) return true;
  uint8_t c0[4] = {}, c1[4] = {};
  bool readable = st25ReadPageTailSafe(dev, cfg, pages, c0) &&
                  st25ReadPageTailSafe(dev, cfg + 1, pages, c1);
  bool need = !readable;
  if (readable) {
    uint8_t auth0 = c0[3];
    need = auth0 != 0xFF && auth0 < pages && lastPage >= auth0 && (!forRead || (c1[0] & 0x80));
  }
  if (!need) return true;
  uint8_t pwd[4] = {};
  if (!st25PromptPwd(pwd)) return false;

  dev.deactivate();
  ST25R3916Backend::ScanResult current;
  if (!dev.scan(ST25R3916Backend::TECH_A, current, 600, true)) {
    ShowStatusAction::show("Authentication failed", 1400);
    return false;
  }
  if (!st25PwdAuth(dev, pwd)) {
    ShowStatusAction::show("Authentication failed", 1800);
    return false;
  }
  return true;
}

static bool st25ParseKey(const String& line, uint8_t out[6]) {
  String s = line; s.trim(); if (!s.length() || s.startsWith("#")) return false; s.replace(":", ""); s.replace(" ", "");
  if (s.length() != 12) return false;
  for (int i=0;i<6;++i) { char b[3]={s[i*2],s[i*2+1],0}; char* e=nullptr; unsigned long v=strtoul(b,&e,16); if(!e||*e) return false; out[i]=(uint8_t)v; }
  return true;
}

static bool st25ParseHex(String text, uint8_t* out, size_t outMax, size_t& outLen) {
  outLen = 0; text.trim(); text.replace(":", ""); text.replace(" ", ""); text.replace("-", "");
  if (!text.length() || (text.length() & 1U) || text.length() / 2U > outMax) return false;
  for (size_t i = 0; i < text.length(); i += 2) {
    char b[3] = {text[(unsigned)i], text[(unsigned)i + 1], 0}; char* end = nullptr;
    unsigned long v = strtoul(b, &end, 16); if (!end || *end) { outLen = 0; return false; }
    out[outLen++] = (uint8_t)v;
  }
  return true;
}

static String st25Hex(const uint8_t* data, size_t len, bool spaced = true) {
  String out; if (!data) return out;
  for (size_t i = 0; i < len; ++i) { char h[4]; snprintf(h, sizeof(h), spaced && i ? " %02X" : "%02X", data[i]); out += h; }
  return out;
}

static const char* st25MfuSensitivePageLabel(const String& type, uint16_t page) {
  const uint16_t dyn = st25MfuDynamicLock(type);
  const uint16_t cfg = st25MfuConfig0(type);
  if (type == "Ultralight C") {
    if (page >= 40 && page <= 43) return "Security/config page";
    if (page >= 44 && page <= 47) return "3DES key page";
    return nullptr;
  }
  if (page == dyn) return "Dynamic lock page";
  if (cfg != 0xFFFF) {
    if (page == cfg || page == cfg + 1) return "Configuration page";
    if (page == cfg + 2) return "Password page";
    if (page == cfg + 3) return "PACK page";
  }
  return nullptr;
}

static bool st25ConfirmMfuSensitiveWrite(const String& type, uint16_t page) {
  const char* label = st25MfuSensitivePageLabel(type, page);
  if (!label) return true;
  static const InputSelectAction::Option opts[] = {{"Write anyway", "write"}};
  String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}

static bool st25Type2DefaultCc(const String& type, uint8_t cc[4]) {
  uint8_t size = 0;
  if (type == "NTAG210" || type == "Ultralight EV1 11" || type == "Ultralight") size = 0x06;
  else if (type == "NTAG212" || type == "Ultralight EV1 21") size = 0x10;
  else if (type == "NTAG213" || type == "Ultralight C") size = 0x12;
  else if (type == "NTAG215") size = 0x3F;
  else if (type == "NTAG216") size = 0x6F;
  else return false;
  cc[0] = 0xE1; cc[1] = 0x10; cc[2] = size; cc[3] = 0x00;
  return true;
}

static bool st25Type2CcIsValid(const uint8_t cc[4]) {
  return cc && cc[0] == 0xE1 && (cc[1] & 0xF0) == 0x10 && cc[2] != 0;
}

static bool st25Type2CcCanProgramSafely(const uint8_t current[4], const uint8_t desired[4]) {
  for (uint8_t i = 0; i < 4; ++i)
    if ((current[i] & (uint8_t)~desired[i]) != 0) return false;
  return true;
}

struct St25Type2TlvInfo {
  bool found = false;
  bool nonEmpty = false;
  size_t tlvOffset = 0;
  size_t valueOffset = 0;
  size_t length = 0;
  size_t insertOffset = 0;
};

static St25Type2TlvInfo st25FindNdefTlv(const uint8_t* area, size_t len, bool preferNonEmpty = true) {
  St25Type2TlvInfo emptyCandidate;
  size_t pos = 0;
  size_t insert = 0;

  while (pos < len) {
    const size_t tlvOffset = pos;
    const uint8_t type = area[pos++];
    if (type == 0x00) { insert = pos; continue; }
    if (type == 0xFE) { insert = tlvOffset; break; }
    if (pos >= len) break;

    size_t valueLen = area[pos++];
    if (valueLen == 0xFF) {
      if (pos + 1 >= len) break;
      valueLen = ((size_t)area[pos] << 8) | area[pos + 1];
      pos += 2;
    }
    if (pos + valueLen > len) break;

    if (type == 0x03) {
      St25Type2TlvInfo cur;
      cur.found = true;
      cur.nonEmpty = valueLen != 0;
      cur.tlvOffset = tlvOffset;
      cur.valueOffset = pos;
      cur.length = valueLen;
      cur.insertOffset = tlvOffset;
      if (!preferNonEmpty || cur.nonEmpty) return cur;
      if (!emptyCandidate.found) emptyCandidate = cur;
    }

    pos += valueLen;
    insert = pos;
  }

  if (emptyCandidate.found) return emptyCandidate;
  St25Type2TlvInfo none;
  none.insertOffset = min(insert, len);
  return none;
}

static bool st25ReadType2Area(ST25R3916Backend& dev, size_t capacity,
                              uint8_t* area, size_t& got) {
  got = 0;
  if (!area || !capacity) return false;
  for (size_t off = 0; off < capacity; off += 16) {
    uint8_t data[16] = {};
    if (!dev.type2ReadPages((uint8_t)(4 + off / 4), data)) return false;
    const size_t take = min((size_t)16, capacity - off);
    memcpy(area + off, data, take);
    got += take;
  }
  return true;
}

static bool st25WriteType2Range(ST25R3916Backend& dev, const uint8_t* area,
                                size_t capacity, size_t firstByte,
                                size_t lastByteExclusive) {
  if (!area || firstByte >= lastByteExclusive || lastByteExclusive > capacity) return false;
  const size_t firstPage = firstByte / 4;
  const size_t lastPage = (lastByteExclusive - 1) / 4;
  for (size_t p = firstPage; p <= lastPage; ++p) {
    if (!dev.type2WritePage((uint8_t)(4 + p), area + p * 4)) return false;
  }
  return true;
}

static bool st25BuildMfuLockMasks(const String& type, uint16_t first, uint16_t last,
                                  uint8_t staticMask[2], uint8_t dynamicMask[3]) {
  staticMask[0] = staticMask[1] = 0;
  dynamicMask[0] = dynamicMask[1] = dynamicMask[2] = 0;
  if (first > last || first < 4) return false;
  for (uint16_t page = first; page <= last && page <= 15; ++page) {
    if (page <= 7) staticMask[0] |= (uint8_t)(1u << page);
    else staticMask[1] |= (uint8_t)(1u << (page - 8));
  }
  if (last <= 15) return true;
  auto setGroup = [&](uint16_t start, uint16_t end, uint8_t byte, uint8_t bit) {
    if (last >= start && first <= end) dynamicMask[byte] |= (uint8_t)(1u << bit);
  };
  if (type == "NTAG212" || type == "Ultralight EV1 21") {
    for (uint8_t i = 0; i < 10; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (type == "NTAG213") {
    for (uint8_t i = 0; i < 12; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (type == "NTAG215") {
    for (uint8_t i = 0; i < 7; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    setGroup(128, 129, 0, 7);
    return true;
  }
  if (type == "NTAG216") {
    for (uint8_t i = 0; i < 8; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    for (uint8_t i = 0; i < 5; ++i) setGroup(144 + i * 16, 159 + i * 16, 1, i);
    setGroup(224, 225, 1, 5);
    return true;
  }
  if (type == "NTAG210" || type == "Ultralight EV1 11" || type == "Ultralight") return last <= 15;
  return false;
}
#endif

void ST25R3916Screen::onInit() {
  render();
  ProgressView::init();
  ProgressView::progress("Starting ST25R3916...", 30);
  ProgressView::finish();
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend probe;
  const bool present = st25Begin(probe, _interface);
  probe.end();
  if (!present) {
    ShowStatusAction::show("ST25R3916 not detected", 1600);
    Screen.goBack();
    return;
  }
#endif
  _showMenu();
}

void ST25R3916Screen::onUpdate() {
  if (_mfcRecoveryNext) {
    _mfcRecoveryNext = false;
    static const char* const kRecoveryDicts[] = {
      MfcKeyStore::kBuiltinDefaultId, MfcKeyStore::kDiscoveredDictionary, MfcKeyStore::kBuiltinExtendedId,
    };
    if (_mfcRecoverChainIndex < 3)
      _runMfcDictionaryAttack(kRecoveryDicts[_mfcRecoverChainIndex]);
    return;
  }

  if (_mfcReadAfterDict) {
    _mfcKnownKeysOnlyRead = true;
    _readMfcTag();
    return;
  }

  if (_state == STATE_EMULATING) {
#if defined(DEVICE_HAS_ST25R3916)
    if (_emuDev) _emuDev->emulationWorker();
#endif
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) _stopEmulation();
    return;
  }

  if (_state == STATE_MAGIC_DETECT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK ||
          (dir == INavigation::DIR_PRESS && _magicUiPhase == MAGIC_RESULT))
        _showMfcTagMenu();
    }
    return;
  }

  if (_state == STATE_MENU || _state == STATE_FAMILIES_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU || _state == STATE_MFC_ADVANCED_MENU ||
      _state == STATE_MFC_ATTACKS_MENU || _state == STATE_MFC_KEYS_MENU ||
      _state == STATE_MFC_DICT_SELECT || _state == STATE_MFC_DICT_ATTACK_SELECT ||
      _state == STATE_MFU_MENU || _state == STATE_MFU_TAG_MENU || _state == STATE_MFU_ADVANCED_MENU ||
      _state == STATE_MFU_DUMP_SELECT || _state == STATE_MFU_NDEF_MENU ||
      _state == STATE_MFU_NDEF_WRITE_MENU || _state == STATE_MFU_NDEF_FILE_SELECT ||
      _state == STATE_MFC_NDEF_MENU ||
      _state == STATE_MFC_NDEF_WRITE_MENU || _state == STATE_MFC_NDEF_FILE_SELECT ||
      _state == STATE_MFC_DUMP_SELECT || _state == STATE_MFC_UID_SOURCE_FORM || _state == STATE_MFC_UID_FILE_SELECT || _state == STATE_MFC_UID_DUMP_SELECT ||
      _state == STATE_EXP_MENU || _state == STATE_EXP_TAG_MENU || _state == STATE_EXP_ADVANCED_MENU ||
      _state == STATE_EXP_SUB1_MENU || _state == STATE_EXP_SUB2_MENU || _state == STATE_EXP_NDEF_MENU ||
      _state == STATE_EXP_NDEF_WRITE_MENU || _state == STATE_EXP_NDEF_FILE_SELECT) {
    ListScreen::onUpdate();
    return;
  }

  if (_state == STATE_DETAILS && _scanFamily != 0xFF &&
      Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 700) {
    Uni.Nav->suppressCurrentPress();
    _familyFromScan = true;
    _openFamily(_scanFamily);
    return;
  }
  if (!Uni.Nav->wasPressed()) return;

  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    onBack();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_DETAILS) {
    if (Uni.Nav->pressDuration() < 700) _scan(_lastTechMask);
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_DETAILS) {
    _genericReadActive = false;
    _showMfuDumpActions();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_WRITE_PREVIEW) {
    _writeMfuDumpToTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_EMULATE_PREVIEW) {
    _emulateMfuTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_WRITE_PREVIEW) {
    _writeMfcDumpToTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_UID_WRITE_PREVIEW) {
    _writeMfcUidToTag();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_DETAILS) {
    _genericReadActive = false;
    _showMfcDumpActions();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFU_NDEF_DETAILS && _ndefWritePreview) {
    bool fromFile=_ndefWritePreviewFromFile; if (_writeMfuNdef(_ndefBuf,_ndefLen)) _showMfuNdefMenu(); else _showNdefWritePreview(_ndefBuf,_ndefLen,fromFile); return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_MFC_NDEF_DETAILS && _ndefWritePreview) {
    const bool fromFile = _ndefWritePreviewFromFile;
    if (_writeMfcNdef(_ndefBuf, _ndefLen)) _showMfcNdefMenu();
    else _showNdefWritePreview(_ndefBuf, _ndefLen, fromFile);
    return;
  }
  if (dir == INavigation::DIR_PRESS && _state == STATE_EXP_NDEF_DETAILS && _ndefWritePreview) {
    const bool fromFile = _ndefWritePreviewFromFile;
    if (_experimentalWriteNdef(_ndefBuf, _ndefLen, false)) _showExperimentalNdefMenu();
    else _showNdefWritePreview(_ndefBuf, _ndefLen, fromFile);
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX || _state == STATE_MFC_MEMORY) {
    _handleMfcDumpNav(dir);
    return;
  }
  if (_state == STATE_MFU_DUMP_HEX || _state == STATE_MFU_MEMORY) {
    _handleMfuDumpNav(dir);
    return;
  }
  _scrollView.onNav(dir);
}

void ST25R3916Screen::onRender() {
  if (_state == STATE_MAGIC_DETECT) {
    if (_magicUiPhase == MAGIC_WAITING) {
      TagPrompt::show("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
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
  if (_state == STATE_EMULATING) {
    auto& lcd = Uni.Lcd;
    const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextDatum(MC_DATUM); lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("Emulating tag...", bx + bw / 2, by + bh / 2);
    return;
  }
  if (_state == STATE_MENU || _state == STATE_FAMILIES_MENU || _state == STATE_MFC_MENU || _state == STATE_MFC_TAG_MENU || _state == STATE_MFC_ADVANCED_MENU ||
      _state == STATE_MFC_ATTACKS_MENU || _state == STATE_MFC_KEYS_MENU ||
      _state == STATE_MFC_DICT_SELECT || _state == STATE_MFC_DICT_ATTACK_SELECT ||
      _state == STATE_MFU_MENU || _state == STATE_MFU_TAG_MENU || _state == STATE_MFU_ADVANCED_MENU ||
      _state == STATE_MFU_DUMP_SELECT || _state == STATE_MFU_NDEF_MENU ||
      _state == STATE_MFU_NDEF_WRITE_MENU || _state == STATE_MFU_NDEF_FILE_SELECT ||
      _state == STATE_MFC_NDEF_MENU ||
      _state == STATE_MFC_NDEF_WRITE_MENU || _state == STATE_MFC_NDEF_FILE_SELECT ||
      _state == STATE_MFC_DUMP_SELECT || _state == STATE_MFC_UID_SOURCE_FORM || _state == STATE_MFC_UID_FILE_SELECT || _state == STATE_MFC_UID_DUMP_SELECT ||
      _state == STATE_EXP_MENU || _state == STATE_EXP_TAG_MENU || _state == STATE_EXP_ADVANCED_MENU ||
      _state == STATE_EXP_SUB1_MENU || _state == STATE_EXP_SUB2_MENU || _state == STATE_EXP_NDEF_MENU ||
      _state == STATE_EXP_NDEF_WRITE_MENU || _state == STATE_EXP_NDEF_FILE_SELECT) {
    ListScreen::onRender();
    return;
  }
  if (_state == STATE_SCANNING || _state == STATE_SCAN_READER || _state == STATE_MFC_READING || _state == STATE_MFU_READING || _state == STATE_MFC_NDEF_READING ||
      _state == STATE_MFC_NDEF_WRITING || _state == STATE_MFC_WRITING || _state == STATE_MFC_UID_WRITING || _state == STATE_MFC_ERASING ||
      _state == STATE_MFU_WRITING || _state == STATE_MFU_ERASING || _state == STATE_EXP_WORKING) {
    _renderTagPrompt();
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX || _state == STATE_MFC_MEMORY) {
    _renderMfcDump();
    return;
  }
  if (_state == STATE_MFU_DUMP_HEX || _state == STATE_MFU_MEMORY) {
    _renderMfuDump();
    return;
  }
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void ST25R3916Screen::onBack() {
  if (_genericReadActive) {
    if (_state == STATE_MFC_DETAILS || _state == STATE_MFU_DETAILS ||
        _state == STATE_EXP_DETAILS || _state == STATE_EXP_RESULT ||
        _state == STATE_MFC_READING || _state == STATE_MFU_READING ||
        _state == STATE_EXP_WORKING) {
      _genericReadActive = false;
      _showMenu();
      return;
    }
  }
  if (_state == STATE_EMULATING) { _stopEmulation(); return; }
  if (_state == STATE_SCANNING ||
      _state == STATE_SCAN_READER_RESULT || _state == STATE_DETAILS) {
    _showMenu();
    return;
  }
  if (_state == STATE_EXP_DETAILS || _state == STATE_EXP_WORKING) { _showExperimentalTagMenu(); return; }
  if (_state == STATE_EXP_RESULT) { if (_expResultReturn == STATE_EXP_SUB1_MENU) _showExperimentalSub1Menu(); else if (_expResultReturn == STATE_EXP_SUB2_MENU) _showExperimentalSub2Menu(); else if (_expResultReturn == STATE_EXP_ADVANCED_MENU) _showExperimentalAdvancedMenu(); else _showExperimentalTagMenu(); return; }
  if (_state == STATE_EXP_NDEF_DETAILS) {
    if (_ndefWritePreview) { const bool fromFile=_ndefWritePreviewFromFile; _ndefWritePreview=false; _ndefWritePreviewFromFile=false; if(fromFile)_openNdefFilePicker(); else _showExperimentalNdefWriteMenu(); }
    else _showExperimentalNdefMenu();
    return;
  }
  if (_state == STATE_EXP_NDEF_WRITE_MENU || _state == STATE_EXP_NDEF_FILE_SELECT) { _showExperimentalNdefMenu(); return; }
  if (_state == STATE_EXP_NDEF_MENU) { _showExperimentalMenu(_expFamily); return; }
  if (_state == STATE_EXP_ADVANCED_MENU || _state == STATE_EXP_SUB1_MENU || _state == STATE_EXP_SUB2_MENU) { _showExperimentalTagMenu(); return; }
  if (_state == STATE_EXP_TAG_MENU) { _showExperimentalMenu(_expFamily); return; }
  if (_state == STATE_EXP_MENU) { if (_familyFromScan) _showMenu(); else _showFamiliesMenu(); return; }
  if (_state == STATE_MFU_DUMP_HEX) {
    _state = STATE_MFU_DETAILS;
    render();
    return;
  }
  if (_state == STATE_MFU_DETAILS || _state == STATE_MFU_READING || _state == STATE_MFU_WRITE_PREVIEW ||
      _state == STATE_MFU_EMULATE_PREVIEW || _state == STATE_MFU_WRITING || _state == STATE_MFU_ERASING || _state == STATE_MFU_DUMP_SELECT) {
    _showMfuTagMenu();
    return;
  }
  if (_state == STATE_MFU_TAG_MENU) {
    _showMfuMenu();
    return;
  }
  if (_state == STATE_MFU_MENU) {
    if (_familyFromScan) _showMenu(); else _showFamiliesMenu();
    return;
  }
  if (_state == STATE_MFC_DUMP_HEX) {
    _state = STATE_MFC_DETAILS;
    render();
    return;
  }
  if (_state == STATE_MFC_MEMORY) {
    _showMfcAdvancedMenu();
    return;
  }
  if (_state == STATE_MFC_UID_FILE_SELECT || _state == STATE_MFC_UID_DUMP_SELECT || _state == STATE_MFC_UID_WRITE_PREVIEW) { _rebuildMfcUidWriteForm(1); return; }
  if (_state == STATE_MFC_WRITE_PREVIEW || _state == STATE_MFC_DUMP_SELECT || _state == STATE_MFC_UID_SOURCE_FORM ||
      _state == STATE_MFC_UID_WRITING || _state == STATE_MFC_WRITING || _state == STATE_MFC_ERASING) {
    _showMfcTagMenu();
    return;
  }
  if (_state == STATE_MFU_NDEF_DETAILS) { if(_ndefWritePreview){bool fromFile=_ndefWritePreviewFromFile;_ndefWritePreview=false;if(fromFile)_openNdefFilePicker();else _showMfuNdefWriteMenu();}else _showMfuNdefMenu(); return; }
  if (_state == STATE_MFU_NDEF_READING || _state == STATE_MFU_NDEF_WRITING) { _showMfuNdefMenu(); return; }
  if (_state == STATE_MFU_NDEF_WRITE_MENU || _state == STATE_MFU_NDEF_FILE_SELECT) { _showMfuNdefMenu(); return; }
  if (_state == STATE_MFU_NDEF_MENU) { _showMfuMenu(); return; }
  if (_state == STATE_MFC_NDEF_DETAILS) {
    if (_ndefWritePreview) {
      const bool fromFile = _ndefWritePreviewFromFile;
      _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
      if (fromFile) _openNdefFilePicker(); else _showMfcNdefWriteMenu();
    } else _showMfcNdefMenu();
    return;
  }
  if (_state == STATE_MFC_NDEF_READING || _state == STATE_MFC_NDEF_WRITING) { _showMfcNdefMenu(); return; }
  if (_state == STATE_MFC_NDEF_WRITE_MENU || _state == STATE_MFC_NDEF_FILE_SELECT) { _showMfcNdefMenu(); return; }
  if (_state == STATE_MFC_NDEF_MENU) {
    _showMfcMenu();
    return;
  }
  if (_state == STATE_MFC_DETAILS || _state == STATE_MFC_READING) {
    _showMfcTagMenu();
    return;
  }
  if (_state == STATE_MFC_TAG_MENU) {
    _showMfcMenu();
    return;
  }
  if (_state == STATE_MFC_ADVANCED_MENU) { _showMfcTagMenu(); return; }
  if (_state == STATE_MFC_ATTACKS_MENU || _state == STATE_MFC_KEYS_MENU) { _showMfcMenu(); return; }
  if (_state == STATE_MFC_DICT_SELECT || _state == STATE_MFC_DICT_VIEW) { _openMfcDictionaries(false); return; }
  if (_state == STATE_MFC_KEYS_VIEW) { _showMfcKeysMenu(); return; }
  if (_state == STATE_MFC_DICT_ATTACK_SELECT) {
    if (_resumeMfcReadAfterDict) { _resumeMfcReadAfterDict = false; _showMfcTagMenu(); }
    else _showMfcAttacksMenu();
    return;
  }
  if (_state == STATE_MFU_MEMORY) { _showMfuAdvancedMenu(); return; }
  if (_state == STATE_MFU_ADVANCED_MENU) { _showMfuTagMenu(); return; }
  if (_state == STATE_MAGIC_DETECT) { _showMfcTagMenu(); return; }
  if (_state == STATE_DEVICE_INFO) { _showMenu(); return; }
  if (_state == STATE_FAMILIES_MENU) { _showMenu(); return; }
  if (_state == STATE_MFC_MENU) {
    if (_familyFromScan) _showMenu(); else _showFamiliesMenu();
    return;
  }
  Screen.goBack();
}

void ST25R3916Screen::onItemSelected(uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  if (_state == STATE_EXP_MENU) {
    _selExp = index;
    if (index == 0) _showExperimentalTagMenu();
    else if (index == 1) _showExperimentalNdefMenu();
    return;
  }
  if (_state == STATE_EXP_TAG_MENU) {
    _selExpTag = index;
    if (_expFamily == EXP_DESFIRE) {
      if (index == 0) _experimentalReadTag(); else if (index == 1) _showExperimentalSub1Menu(); else if (index == 2) _showExperimentalSub2Menu(); else if (index == 3) _showExperimentalAdvancedMenu();
    } else if (_expFamily == EXP_NFCV) {
      if (index == 0) _experimentalReadTag(); else if (index == 1) _experimentalNfcvAction(10); else if (index == 2) _experimentalNfcvAction(11); else if (index == 3) _showExperimentalAdvancedMenu();
    } else if (_expFamily == EXP_FELICA) {
      if (index == 0) _experimentalReadTag(); else if (index == 1) _showExperimentalSub1Menu(); else if (index == 2) _showExperimentalSub2Menu(); else if (index == 3) _showExperimentalAdvancedMenu();
    } else if (_expFamily == EXP_TYPE4B || _expFamily == EXP_TYPE4A) {
      if (index == 0) _experimentalReadTag(); else if (index == 1) _showExperimentalAdvancedMenu();
    }
    return;
  }
  if (_state == STATE_EXP_ADVANCED_MENU) {
    _selExpAdvanced = index;
    if (_expFamily == EXP_DESFIRE) _experimentalDesfireAction(3, index);
    else if (_expFamily == EXP_NFCV) _experimentalNfcvAction(index);
    else if (_expFamily == EXP_FELICA) _experimentalFelicaAction(3, index);
    else if (_expFamily == EXP_TYPE4B) _experimentalType4bAction(index);
    else if (_expFamily == EXP_TYPE4A && index == 0) _experimentalSendApdu(false);
    return;
  }
  if (_state == STATE_EXP_SUB1_MENU) { _selExpSub1=index; if(_expFamily==EXP_DESFIRE)_experimentalDesfireAction(1,index); else if(_expFamily==EXP_FELICA)_experimentalFelicaAction(1,index); return; }
  if (_state == STATE_EXP_SUB2_MENU) { _selExpSub2=index; if(_expFamily==EXP_DESFIRE)_experimentalDesfireAction(2,index); else if(_expFamily==EXP_FELICA)_experimentalFelicaAction(2,index); return; }
  if (_state == STATE_EXP_NDEF_MENU) {
    _selExpNdef=index;
    if (_expFamily == EXP_TYPE4A) { if (index == 0) _experimentalReadNdef(); return; }
    if(index==0)_experimentalReadNdef(); else if(index==1)_showExperimentalNdefWriteMenu(); else if(index==2)_experimentalEraseNdef(true); else if(index==3)_experimentalEraseNdef(false);
    return;
  }
  if (_state == STATE_EXP_NDEF_WRITE_MENU) {
    _selExpNdefWrite = index;
    if(index<4)_writeNdefBuilt(index); else if(index==4)_writeNdefVcard(); else if(index==5)_openNdefFilePicker();
    return;
  }
  if (_state == STATE_EXP_NDEF_FILE_SELECT) { _openNdefFile(index); return; }
  if (_state == STATE_MFU_MENU) {
    _selMfu = index;
    if (index == 0) _showMfuTagMenu();
    else if (index == 1) _showMfuNdefMenu();
    return;
  }
  if (_state == STATE_MFU_NDEF_MENU) {
    _selMfuNdef = index;
    if (index == 0) _readMfuNdef();
    else if (index == 1) _showMfuNdefWriteMenu();
    else if (index == 2) _formatMfuNdef();
    else if (index == 3) _eraseMfuNdef();
    return;
  }
  if (_state == STATE_MFU_NDEF_WRITE_MENU) {
    _selMfuNdefWrite = index;
    if (index < 4) _writeNdefBuilt(index);
    else if (index == 4) _writeNdefVcard();
    else if (index == 5) _openNdefFilePicker();
    return;
  }
  if (_state == STATE_MFU_NDEF_FILE_SELECT) { _openNdefFile(index); return; }
  if (_state == STATE_MFU_TAG_MENU) {
    _selMfuTag = index;
    if (index == 0) _readMfuTag();
    else if (index == 1) _openMfuDumpPicker(false);
    else if (index == 2) _openMfuDumpPicker(true);
    else if (index == 3) _eraseMfuTag();
    else if (index == 4) _showMfuAdvancedMenu();
    return;
  }
  if (_state == STATE_MFU_DUMP_SELECT) {
    _openMfuDumpFile(index);
    return;
  }
  if (_state == STATE_MFC_MENU) {
    _selMfc = index;
    if (index == 0) _showMfcTagMenu();
    else if (index == 1) _showMfcNdefMenu();
    else if (index == 2) _showMfcAttacksMenu();
    else if (index == 3) _showMfcKeysMenu();
    return;
  }
  if (_state == STATE_MFC_ATTACKS_MENU) {
    _selMfcAttacks = index;
    if (index == 0) { _resumeMfcReadAfterDict = false; _openMfcDictionaries(true); }
    return;
  }
  if (_state == STATE_MFC_KEYS_MENU) {
    _selMfcKeys = index;
    if (index == 0) _showMfcKnownKeys();
    else if (index == 1) _openMfcDictionaries(false);
    return;
  }
  if (_state == STATE_MFC_DICT_SELECT) { _openMfcDictionary(index, false); return; }
  if (_state == STATE_MFC_DICT_ATTACK_SELECT) { _openMfcDictionary(index, true); return; }
  if (_state == STATE_MFU_ADVANCED_MENU) {
    _selMfuAdvanced = index;
    if (index == 0) _readMfuMemory();
    else if (index == 1) _editMfuMemory();
    else if (index == 2) _setMfuPassword();
    else if (index == 3) _removeMfuPassword();
    else if (index == 4) _lockMfuTag();
    return;
  }
  if (_state == STATE_MFC_ADVANCED_MENU) {
    _selMfcAdvanced = index;
    if (index == 0) _readMfcMemory();
    else if (index == 1) _editMfcMemory();
    else if (index == 2) _lockMfcUidGen3();
    return;
  }
  if (_state == STATE_MFC_NDEF_MENU) {
    _selMfcNdef = index;
    if (index == 0) _readMfcNdef();
    else if (index == 1) _showMfcNdefWriteMenu();
    else if (index == 2) _formatMfc1kNdef();
    else if (index == 3) _eraseMfcNdef();
    return;
  }
  if (_state == STATE_MFC_NDEF_WRITE_MENU) {
    _selMfcNdefWrite = index;
    if (index < 4) _writeNdefBuilt(index);
    else if (index == 4) _writeNdefVcard();
    else if (index == 5) _openNdefFilePicker();
    return;
  }
  if (_state == STATE_MFC_NDEF_FILE_SELECT) { _openNdefFile(index); return; }
  if (_state == STATE_MFC_TAG_MENU) {
    _selMfcTag = index;
    if (index == 0) _detectMagic();
    else if (index == 1) _readMfcTag();
    else if (index == 2) _chooseMfcUidSource();
    else if (index == 3) _openMfcDumpPicker();
    else if (index == 4) _eraseMfcTag();
    else if (index == 5) _emulateMfcTag();
    else if (index == 6) _showMfcAdvancedMenu();
    return;
  }
  if (_state == STATE_MFC_DUMP_SELECT) { _openMfcDumpFile(index); return; }
  if (_state == STATE_MFC_UID_SOURCE_FORM) {
    if (index == 0) {
      static const InputSelectAction::Option sources[] = {{"Manual","manual"},{"UID File","uid"},{"Dump","dump"}};
      const char* current = _uidWriteSourceMode == UID_WRITE_MANUAL ? "manual" : _uidWriteSourceMode == UID_WRITE_FILE ? "uid" : "dump";
      const char* r = InputSelectAction::popup("UID", sources, 3, current);
      if (r) {
        _uidWriteSourceMode = strcmp(r,"manual")==0 ? UID_WRITE_MANUAL : strcmp(r,"uid")==0 ? UID_WRITE_FILE : UID_WRITE_DUMP;
        _uidWriteLen = 0;
        _uidWriteFilePath = ""; _uidWriteDumpPath = "";
        _rebuildMfcUidWriteForm(0);
      } else render();
    } else if (index == 1) {
      if (_uidWriteSourceMode == UID_WRITE_MANUAL) _editMfcUidManual();
      else if (_uidWriteSourceMode == UID_WRITE_FILE) _openMfcUidPicker();
      else _openMfcUidDumpPicker();
    } else if (index == 2) _startMfcUidWriteFromForm();
    return;
  }
  if (_state == STATE_MFC_UID_FILE_SELECT) { _openMfcUidFile(index); return; }
  if (_state == STATE_MFC_UID_DUMP_SELECT) { _openMfcUidDumpFile(index); return; }

  if (_state == STATE_FAMILIES_MENU) {
    _selFamilies = index;
    _familyFromScan = false;
    _openFamily(index);
    return;
  }
  _selMain = index;
  switch (index) {
    case 0: _scan(ST25R3916Backend::TECH_ALL); break;
    case 1: _genericReadTag(); break;
    case 2: _scanReader(); break;
    case 3: _familyFromScan = false; _showFamiliesMenu(); break;
    case 4: _showDeviceInfo(); break;
  }
#else
  (void)index;
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_scanReader() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_SCAN_READER;
  render();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    _showStatusAndReturn("ST25R3916 not detected", STATE_MENU, 1200);
    return;
  }
  if (!dev.startReaderScan()) {
    dev.end();
    _showStatusAndReturn("Reader scan unavailable", STATE_MENU, 1200);
    return;
  }
  bool detected = false, cancelled = false;
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 15000U) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      cancelled = true; break;
    }
    if (dev.readerScanDetected()) { detected = true; break; }
    delay(10);
  }
  dev.stopReaderScan();
  dev.end();
  if (cancelled) { _showMenu(); return; }
  if (!detected) {
    _showStatusAndReturn("Reader not detected", STATE_MENU, 1200);
    return;
  }
  _rowCount = 0;
  _rowLabels[_rowCount]="Technology"; _rowValues[_rowCount]="ISO 14443-A";
  _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; ++_rowCount;
  _rowLabels[_rowCount]="Likely tag"; _rowValues[_rowCount]="Unknown";
  _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; ++_rowCount;
  _rowLabels[_rowCount]="Confidence"; _rowValues[_rowCount]="Low";
  _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; ++_rowCount;
  _rowLabels[_rowCount]="Reader action"; _rowValues[_rowCount]="NFC-A activation";
  _rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]}; ++_rowCount;
  _scrollView.setRows(_rows,_rowCount);
  _state = STATE_SCAN_READER_RESULT;
  render();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_showStatusAndReturn(const char* message, State target, int32_t durationMs) {
  if (_genericReadActive) {
    _genericReadActive = false;
    target = STATE_MENU;
  }
  auto showTarget = [this, target]() {
    switch (target) {
      case STATE_MENU: _showMenu(); break;
      case STATE_MFC_MENU: _showMfcMenu(); break;
      case STATE_MFC_TAG_MENU: _showMfcTagMenu(); break;
      case STATE_MFC_ADVANCED_MENU: _showMfcAdvancedMenu(); break;
      case STATE_MFC_NDEF_MENU: _showMfcNdefMenu(); break;
      case STATE_MFC_ATTACKS_MENU: _showMfcAttacksMenu(); break;
      case STATE_MFC_KEYS_MENU: _showMfcKeysMenu(); break;
      case STATE_MFU_MENU: _showMfuMenu(); break;
      case STATE_MFU_TAG_MENU: _showMfuTagMenu(); break;
      case STATE_MFU_ADVANCED_MENU: _showMfuAdvancedMenu(); break;
      case STATE_MFU_NDEF_MENU: _showMfuNdefMenu(); break;
      case STATE_EXP_TAG_MENU: _showExperimentalTagMenu(); break;
      case STATE_EXP_ADVANCED_MENU: _showExperimentalAdvancedMenu(); break;
      case STATE_EXP_SUB1_MENU: _showExperimentalSub1Menu(); break;
      case STATE_EXP_SUB2_MENU: _showExperimentalSub2Menu(); break;
      case STATE_EXP_NDEF_MENU: _showExperimentalNdefMenu(); break;
      default: _showMenu(); break;
    }
  };

  showTarget();
  ShowStatusAction::show(message, durationMs);
  showTarget();
}

void ST25R3916Screen::_showMenu() {
  _genericReadActive = false;
  _state = STATE_MENU;
  setItems(_items, 5, _selMain);
}

void ST25R3916Screen::_showFamiliesMenu() {
  _state = STATE_FAMILIES_MENU;
  setItems(_familyItems, 7, _selFamilies);
  render();
}

void ST25R3916Screen::_openFamily(uint8_t index) {
  switch (index) {
    case 0: _showMfcMenu(); break;
    case 1: _showMfuMenu(); break;
    case 2: _showExperimentalMenu(EXP_TYPE4A); break;
    case 3: _showExperimentalMenu(EXP_TYPE4B); break;
    case 4: _showExperimentalMenu(EXP_DESFIRE); break;
    case 5: _showExperimentalMenu(EXP_FELICA); break;
    case 6: _showExperimentalMenu(EXP_NFCV); break;
  }
}

void ST25R3916Screen::_showMfcMenu() {
  _state = STATE_MFC_MENU;
  setItems(_mfcItems, 4, _selMfc);
  render();
}

void ST25R3916Screen::_showMfcTagMenu() {
  if (_genericReadActive) { _genericReadActive = false; _showMenu(); return; }
  _state = STATE_MFC_TAG_MENU;
  setItems(_mfcTagItems, 7, _selMfcTag);
  render();
}

void ST25R3916Screen::_showMfcAdvancedMenu() {
  _state = STATE_MFC_ADVANCED_MENU;
  setItems(_mfcAdvancedItems, 3, _selMfcAdvanced);
  render();
}

void ST25R3916Screen::_showMfcNdefMenu() {
  _ndefMfuTarget = false;
  _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFC_NDEF_MENU;
  setItems(_mfcNdefItems, 4, _selMfcNdef);
  render();
}

void ST25R3916Screen::_showMfcNdefWriteMenu() {
  _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFC_NDEF_WRITE_MENU;
  setItems(_mfcNdefWriteItems, 6, _selMfcNdefWrite);
  render();
}

void ST25R3916Screen::_showMfcAttacksMenu() {
  _dictPickDir = _dictPath;
  _state = STATE_MFC_ATTACKS_MENU;
  setItems(_mfcAttackItems, 1, _selMfcAttacks);
  render();
}

void ST25R3916Screen::_showMfcKeysMenu() {
  _dictPickDir = _dictPath;
  _state = STATE_MFC_KEYS_MENU;
  setItems(_mfcKeysItems, 2, _selMfcKeys);
  render();
}

void ST25R3916Screen::_showMfuMenu() {
  _state = STATE_MFU_MENU;
  setItems(_mfuItems, 2, _selMfu);
  render();
}

void ST25R3916Screen::_showMfuTagMenu() {
  if (_genericReadActive) { _genericReadActive = false; _showMenu(); return; }
  _state = STATE_MFU_TAG_MENU;
  setItems(_mfuTagItems, 5, _selMfuTag);
  render();
}

void ST25R3916Screen::_showMfuAdvancedMenu() {
  _state = STATE_MFU_ADVANCED_MENU;
  setItems(_mfuAdvancedItems, 5, _selMfuAdvanced);
  render();
}

void ST25R3916Screen::_showMfuNdefMenu() {
  _ndefMfuTarget = true; _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFU_NDEF_MENU; setItems(_mfuNdefItems, 4, _selMfuNdef); render();
}

void ST25R3916Screen::_showMfuNdefWriteMenu() {
  _ndefMfuTarget = true; _ndefWritePreview = false; _ndefWritePreviewFromFile = false;
  _state = STATE_MFU_NDEF_WRITE_MENU; setItems(_mfcNdefWriteItems, 6, _selMfuNdefWrite); render();
}

void ST25R3916Screen::_renderTagPrompt() {
  StatusBar::refresh();
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  TagPrompt::show(_state == STATE_SCAN_READER ? "Waiting for reader..." : "Waiting for tag...", bx, by, bw, bh);
}

bool ST25R3916Screen::_matchesGenericReadTag(const uint8_t* uid, uint8_t uidLen) const {
  return !_genericReadActive ||
         (uidLen == _genericReadUidLen && uidLen > 0 &&
          uidLen <= sizeof(_genericReadUid) &&
          memcmp(uid, _genericReadUid, uidLen) == 0);
}

void ST25R3916Screen::_genericReadTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _genericReadActive = false;
  _state = STATE_SCANNING;
  render();
  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    _showStatusAndReturn("ST25R3916 not detected", STATE_MENU, 1600);
    return;
  }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_ALL, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT)
      ShowStatusAction::show("Tag not detected", 1200);
    _showMenu();
    return;
  }
  uint8_t family = 0xFF;
  switch (tag.technology) {
    case ST25R3916Backend::Technology::NFC_A:
      if (isMifareClassic(tag.sak)) family = 0;
      else if (tag.sak == 0x00) family = 1;
      else if (tag.sak == 0x20 && tag.atqa[0] == 0x03) family = 4;
      else if (tag.isoDep) family = 2;
      break;
    case ST25R3916Backend::Technology::NFC_B:
      if (tag.isoDep) family = 3;
      break;
    case ST25R3916Backend::Technology::NFC_F: family = 5; break;
    case ST25R3916Backend::Technology::NFC_V: family = 6; break;
    default: break;
  }
  dev.deactivate();
  if (family == 0xFF) {
    _showStatusAndReturn("Tag not supported", STATE_MENU, 1600);
    return;
  }
  if (tag.nfcidLen == 0 || tag.nfcidLen > sizeof(_genericReadUid)) {
    _showStatusAndReturn("Tag ID unavailable", STATE_MENU, 1600);
    return;
  }
  _genericReadUidLen = tag.nfcidLen;
  memcpy(_genericReadUid, tag.nfcid, _genericReadUidLen);
  _genericReadActive = true;
  _familyFromScan = false;
  switch (family) {
    case 0: _readMfcTag(); break;
    case 2: _expFamily = EXP_TYPE4A; _experimentalReadTag(); break;
    case 1: _readMfuTag(); break;
    case 3: _expFamily = EXP_TYPE4B; _experimentalReadTag(); break;
    case 4: _expFamily = EXP_DESFIRE; _experimentalReadTag(); break;
    case 5: _expFamily = EXP_FELICA; _experimentalReadTag(); break;
    case 6: _expFamily = EXP_NFCV; _experimentalReadTag(); break;
  }
#endif
}

void ST25R3916Screen::_scan(uint16_t techMask) {
#if defined(DEVICE_HAS_ST25R3916)
  _lastTechMask = techMask;
  _scanFamily = 0xFF;
  _state = STATE_SCANNING;
  render();

  ST25R3916Backend dev;
  const char* bus = st25InterfaceName(_interface);
  bool ready = st25Begin(dev, _interface);

  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMenu();
    return;
  }

  ST25R3916Backend::ScanResult result;
  const auto waitResult = st25WaitForTag(dev, techMask, result, 1800, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT)
      ShowStatusAction::show("Tag not detected", 1200);
    _showMenu();
    return;
  }

  _scanFamily = 0xFF;
  String tech = "Unknown";
  const char* protocol = "Unknown";
  switch (result.technology) {
    case ST25R3916Backend::Technology::NFC_A:
      tech = inferNfcAType(result.sak, result.atqa);
      if (isMifareClassic(result.sak)) _scanFamily = 0;
      else if (result.sak == 0x00) _scanFamily = 1;
      else if (result.sak == 0x20 && result.atqa[0] == 0x03) _scanFamily = 4;
      else if (result.isoDep) _scanFamily = 2;
      protocol = "ISO14443A";
      if (result.sak == 0x00) {
        String mfuType;
        uint16_t mfuPages = 0;
        if (_detectMfuType(dev, mfuType, mfuPages)) tech = mfuType;
        else tech = "Ultralight / NTAG";
      }
      break;
    case ST25R3916Backend::Technology::NFC_B:
      tech = "NFC-B";
      if (result.isoDep) _scanFamily = 3;
      protocol = "ISO14443B";
      break;
    case ST25R3916Backend::Technology::NFC_F:
      tech = "NFC-F / FeliCa";
      _scanFamily = 5;
      protocol = "NFC-F";
      break;
    case ST25R3916Backend::Technology::NFC_V:
      tech = "NFC-V / ISO15693";
      _scanFamily = 6;
      protocol = "ISO15693";
      break;
    default:
      break;
  }

  String id;
  for (uint8_t i = 0; i < result.nfcidLen; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", result.nfcid[i]);
    id += h;
  }
  if (!id.length()) id = "--";

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };

  addRow("Type", tech);
  addRow(result.technology == ST25R3916Backend::Technology::NFC_A ? "UID" : "NFCID", id);

  if (result.technology == ST25R3916Backend::Technology::NFC_A) {
    char atqa[8];
    snprintf(atqa, sizeof(atqa), "%02X:%02X", result.atqa[0], result.atqa[1]);
    addRow("ATQA", atqa);

    char sak[4];
    snprintf(sak, sizeof(sak), "%02X", result.sak);
    addRow("SAK", sak);
  }

  addRow(result.technology == ST25R3916Backend::Technology::NFC_A ? "UID Len" : "NFCID Len",
         String(result.nfcidLen) + " bytes");
  addRow("Protocol", protocol);
  if (result.isoDep) addRow("ISO-DEP", "Yes");
  addRow("Reader", bus ? bus : "--");
  if (_scanFamily != 0xFF) {
    static const char* const familyNames[] = {
      "MIFARE Classic", "Ultralight / NTAG", "Type 4A", "Type 4B",
      "DESFire", "FeliCa", "ICODE / ST25V"
    };
    addRow("[Press]", "Scan again");
    addRow("[Hold]", String(familyNames[_scanFamily]) + " Menu");
  } else addRow("[Press]", "Scan again");

  dev.deactivate();
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_readMfcTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _mfcReadAfterDict = false;
  const bool knownKeysOnly = _mfcKnownKeysOnlyRead;
  _mfcKnownKeysOnlyRead = false;
  _state = STATE_MFC_READING;
  render();

  ST25R3916Backend dev;
  const char* bus = st25InterfaceName(_interface);
  bool ready = st25Begin(dev, _interface);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfcTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 3000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1200);
    _showMfcTagMenu();
    return;
  }
  if (!_matchesGenericReadTag(tag.nfcid, tag.nfcidLen)) {
    dev.deactivate();
    _showStatusAndReturn("Different tag detected", STATE_MENU, 1600);
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Tag not supported");
    _showMfcTagMenu();
    return;
  }
  if (knownKeysOnly && (_mfcUidLen != tag.nfcidLen || memcmp(_mfcUid, tag.nfcid, _mfcUidLen) != 0)) {
    dev.deactivate();
    ShowStatusAction::show("Different tag detected", 1400);
    _showMfcTagMenu();
    return;
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (!sectors || !blocks) {
    ShowStatusAction::show("Tag not supported");
    _showMfcTagMenu();
    return;
  }

  const size_t dumpLen = blocks * 16U;
  if (dumpLen > sizeof(_mfcDump)) {
    ShowStatusAction::show("Dump too large");
    _showMfcTagMenu();
    return;
  }
  memset(_mfcDump, 0, sizeof(_mfcDump));
  _mfcDumpFromCompleteRead = false;
  _mfcDumpLen = dumpLen;
  _mfcDumpBlocks = (uint16_t)blocks;
  _mfcDumpOffset = 0;
  memset(_mfcUid, 0, sizeof(_mfcUid));
  _mfcUidLen = min((uint8_t)sizeof(_mfcUid), tag.nfcidLen);
  memcpy(_mfcUid, tag.nfcid, _mfcUidLen);
  _mfcSak = tag.sak;
  _mfcAtqa[0] = tag.atqa[0];
  _mfcAtqa[1] = tag.atqa[1];

  const auto defaults = NFCUtility::getDefaultKeys();
  bool blockSeen[256] = {};
  uint8_t sectorKnownKey[40][6] = {};
  bool sectorKnownKeyB[40] = {};
  bool sectorKnownKeyValid[40] = {};
  uint8_t savedKeyA[40][6] = {};
  uint8_t savedKeyB[40][6] = {};
  bool savedKeyAValid[40] = {};
  bool savedKeyBValid[40] = {};
  uint8_t rollingKey[2][6] = {};
  bool rollingKeyValid[2] = {};
  size_t sectorsWithKey = 0;
  size_t blocksRead = 0;

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    String uidFile;
    for (uint8_t i = 0; i < tag.nfcidLen; ++i) {
      char h[3]; snprintf(h, sizeof(h), "%02X", tag.nfcid[i]); uidFile += h;
    }
    String persisted = Uni.Storage->readFile((String("/unigeek/nfc/keys/") + uidFile + ".txt").c_str());
    int pos = 0;
    while (pos < (int)persisted.length()) {
      int nl = persisted.indexOf('\n', pos); if (nl < 0) nl = persisted.length();
      String line = persisted.substring(pos, nl); line.trim();
      int sec = -1; char kt = 0; char hex[13] = {};
      if (sscanf(line.c_str(), "S%d %c %12s", &sec, &kt, hex) == 3 && sec >= 0 && sec < (int)sectors) {
        uint8_t parsed[6] = {};
        if (st25ParseKey(String(hex), parsed)) {
          if (kt == 'A' || kt == 'a') { memcpy(savedKeyA[sec], parsed, 6); savedKeyAValid[sec] = true; }
          else if (kt == 'B' || kt == 'b') { memcpy(savedKeyB[sec], parsed, 6); savedKeyBValid[sec] = true; }
        }
      }
      pos = nl + 1;
    }
  }

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    char msg[42];
    snprintf(msg, sizeof(msg), "Authenticating sectors (%u/%u)...",
             (unsigned)(sector + 1), (unsigned)sectors);
    ProgressView::progress(msg, (int)(sector * 100U / sectors));

    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);
    bool sectorHasKey = false;
    size_t sectorBlocksRead = 0;

    for (uint8_t keyType = 0; keyType < 2 && sectorBlocksRead < count; ++keyType) {
      auto tryKey = [&](const uint8_t key[6]) -> bool {
        const bool wasActive = dev.hasActiveTag();
        if (!wasActive && !reactivate()) return false;
        if (!dev.mifareClassicAuthenticate(trailer, key, keyType == 1)) {
          dev.deactivate();
          if (!wasActive || !reactivate() ||
              !dev.mifareClassicAuthenticate(trailer, key, keyType == 1)) {
            dev.deactivate();
            return false;
          }
        }

        sectorHasKey = true;
        if (!sectorKnownKeyValid[sector]) {
          memcpy(sectorKnownKey[sector], key, 6);
          sectorKnownKeyB[sector] = (keyType == 1);
          sectorKnownKeyValid[sector] = true;
        }
        memcpy(rollingKey[keyType], key, 6);
        rollingKeyValid[keyType] = true;

        for (size_t offset = 0; offset < count; ++offset) {
          const size_t block = first + offset;
          if (blockSeen[block]) continue;
          uint8_t data[16];
          if (!dev.mifareClassicReadBlock((uint8_t)block, data)) break;
          memcpy(_mfcDump + block * 16U, data, 16);
          blockSeen[block] = true;
          ++sectorBlocksRead;
          ++blocksRead;
        }
        if (sectorBlocksRead != count) dev.deactivate();
        return sectorBlocksRead == count;
      };

      const uint8_t* saved = keyType == 0 ? savedKeyA[sector] : savedKeyB[sector];
      const bool savedValid = keyType == 0 ? savedKeyAValid[sector] : savedKeyBValid[sector];
      if (savedValid && tryKey(saved)) break;
      if (knownKeysOnly) continue;

      if (rollingKeyValid[keyType] &&
          (!savedValid || memcmp(rollingKey[keyType], saved, 6) != 0) &&
          tryKey(rollingKey[keyType])) break;

      for (const auto& candidate : defaults) {
        const auto& key = candidate.value();
        static const uint8_t quickKeys[5][6] = {
          {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0x00,0x00,0x00,0x00,0x00,0x00},
          {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
          {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}
        };
        bool isQuick=false; for (const auto& qk : quickKeys) if (memcmp(key.data(),qk,6)==0) { isQuick=true; break; }
        if (!isQuick) continue;
        if (savedValid && memcmp(key.data(), saved, 6) == 0) continue;
        if (rollingKeyValid[keyType] && memcmp(key.data(), rollingKey[keyType], 6) == 0) continue;
        if (tryKey(key.data())) break;
      }
    }
    if (sectorHasKey) ++sectorsWithKey;

    if (sectorKnownKeyValid[sector] && blockSeen[trailer]) {
      uint8_t* trailerData = _mfcDump + (size_t)trailer * 16U;
      if (sectorKnownKeyB[sector]) memcpy(trailerData + 10, sectorKnownKey[sector], 6);
      else memcpy(trailerData, sectorKnownKey[sector], 6);
    }
  }
  ProgressView::finish();
  dev.deactivate();
  _mfcDumpFromCompleteRead = (blocksRead == blocks);

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    String uidFile, persisted;
    for (uint8_t i = 0; i < tag.nfcidLen; ++i) {
      char h[3]; snprintf(h, sizeof(h), "%02X", tag.nfcid[i]); uidFile += h;
    }
    for (size_t sector = 0; sector < sectors; ++sector) {
      if (!sectorKnownKeyValid[sector]) continue;
      char hex[13] = {};
      for (uint8_t i = 0; i < 6; ++i) snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", sectorKnownKey[sector][i]);
      persisted += "S" + String((unsigned)sector) + " " + (sectorKnownKeyB[sector] ? "B " : "A ") + String(hex) + "\n";
    }
    if (persisted.length()) {
      Uni.Storage->makeDir("/unigeek/nfc/keys");
      Uni.Storage->writeFile((String("/unigeek/nfc/keys/") + uidFile + ".txt").c_str(), persisted.c_str());
      MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, persisted);
    }
  }

  String uid;
  for (uint8_t i = 0; i < tag.nfcidLen; ++i) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", tag.nfcid[i]);
    uid += h;
  }

  MagicCardType magic = _detectMagicType(dev);

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };

  addRow("Type", inferNfcAType(tag.sak, tag.atqa));
  addRow("UID", uid);
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X:%02X", tag.atqa[0], tag.atqa[1]);
  addRow("ATQA", atqa);
  char sak[4];
  snprintf(sak, sizeof(sak), "%02X", tag.sak);
  addRow("SAK", sak);
  addRow("Magic", magic == MagicCardType::GEN1A ? "Gen1A" :
                  magic == MagicCardType::GEN3 ? "Gen3" : "None");
  addRow("Dump", String((unsigned)_mfcDumpLen) + " bytes");
  addRow("Sectors", String((unsigned)sectors));
  addRow("Blocks", String((unsigned)blocksRead) + "/" + String((unsigned)blocks));
  addRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)sectors) + " sectors");
  addRow("Status", blocksRead == blocks ? "Complete" : "Partial");

  uint8_t* ndef = nullptr; size_t ndefLen = 0; NdefParser::Result parsed;
  if (HfDumpParser::extractNdef(_mfcDump, _mfcDumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        addRow("NDEF", "Text");
        if (parsed.language.length()) addRow("Language", parsed.language);
        _addWrappedRow("Text", parsed.text); break;
      case NdefParser::RECORD_URL:
        addRow("NDEF", "URL"); _addWrappedRow("URL", parsed.uri); break;
      case NdefParser::RECORD_PHONE:
        addRow("NDEF", "Phone"); _addWrappedRow("Phone", parsed.phone); break;
      case NdefParser::RECORD_EMAIL:
        addRow("NDEF", "Email"); _addWrappedRow("Email", parsed.email); break;
      case NdefParser::RECORD_VCARD:
        addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) _addWrappedRow("Company", parsed.company);
        if (parsed.address.length()) _addWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) _addWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) _addWrappedRow("Email", parsed.email);
        if (parsed.website.length()) _addWrappedRow("Website", parsed.website);
        break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else addRow("NDEF", "Not found");
  delete[] ndef;

  addRow("Reader", bus ? bus : "--");
  addRow("[Press]", "Actions");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_showMfcDumpActions() {
  if (!_mfcDumpFromCompleteRead) {
    static const InputSelectAction::Option missingOpts[] = {
      {"Recover Keys", "dict"},
      {"Partial Read", "partial"},
    };
    const char* choice = InputSelectAction::popup("Missing sector keys", missingOpts, 2, nullptr);
    render();
    if (!choice) return;
    if (strcmp(choice, "dict") == 0) { _startMfcRecoverKeys(); return; }
  }
  if (!_mfcDumpLen || !_mfcDumpBlocks) {
    ShowStatusAction::show("No dump available", 1600);
    render();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"Read Memory", "view"},
    {"Save UID", "uid"},
    {"Save Dump", "save"},
    {"Write UID to Tag", "writeuid"},
    {"Write Dump to Tag", "write"},
    {"Emulate UID", "emulate"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 6, nullptr);
  if (!r) { render(); return; }

  render();
  if (strcmp(r, "view") == 0) {
    const char* type = _mfcDumpBlocks == 20 ? "MF Classic Mini" :
                       _mfcDumpBlocks == 256 ? "MF Classic 4K" : "MF Classic 1K";
    Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::MIFARE_CLASSIC, type,
                                        _mfcUid, _mfcUidLen, _mfcDump, _mfcDumpLen));
  } else if (strcmp(r, "uid") == 0) {
    _saveMfcUid();
  } else if (strcmp(r, "save") == 0) {
    _saveMfcDump();
  } else if (strcmp(r, "writeuid") == 0) {
    if (_mfcUidLen == 4 || _mfcUidLen == 7) _showMfcUidWritePreview(_mfcUid, _mfcUidLen, "Read Tag");
    else ShowStatusAction::show("UID not supported", 1600);
  } else if (strcmp(r, "write") == 0) {
    _showMfcWritePreview(_mfcDump, _mfcDumpLen, false);
  } else if (strcmp(r, "emulate") == 0) {
    _emulateMfcTag();
  }
}

void ST25R3916Screen::_emulateMfcTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (!_mfcDumpLen || !_mfcDumpBlocks) {
    ShowStatusAction::show("Read or load a tag first", 1500);
    _showMfcTagMenu();
    return;
  }
  _stopEmulation();
  _emuDev = new ST25R3916Backend();
  if (!_emuDev) { ShowStatusAction::show("Out of memory"); _showMfcTagMenu(); return; }
  bool ready = st25Begin(*_emuDev, _interface);
  if (!ready) { delete _emuDev; _emuDev = nullptr; ShowStatusAction::show("ST25R3916 not detected"); _showMfcTagMenu(); return; }

  ST25R3916Backend::ScanResult id;
  id.technology = ST25R3916Backend::Technology::NFC_A;
  if (_mfcUidLen >= 4) {
    id.nfcidLen = _mfcUidLen > 7 ? 7 : _mfcUidLen;
    memcpy(id.nfcid, _mfcUid, id.nfcidLen);
  } else {
    id.nfcidLen = 4;
    memcpy(id.nfcid, _mfcDump, 4);
  }
  id.sak = _mfcSak ? _mfcSak : (_mfcDumpLen == 4096 ? 0x18 : (_mfcDumpLen == 320 ? 0x09 : 0x08));
  id.atqa[0] = _mfcAtqa[0]; id.atqa[1] = _mfcAtqa[1];
  if (id.atqa[0] == 0 && id.atqa[1] == 0) id.atqa[0] = 0x04;

  if (!_emuDev->startMfcUidEmulation(id)) {
    delete _emuDev; _emuDev = nullptr;
    ShowStatusAction::show("Failed", 1600); _showMfcTagMenu(); return;
  }
  _emuReturnMfc = true;
  _state = STATE_EMULATING;
  render();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_emulateMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (!_mfuDumpLen || !_mfuPages) {
    ShowStatusAction::show("Read or load a tag first", 1500);
    _showMfuTagMenu();
    return;
  }
  _stopEmulation();
  _emuDev = new ST25R3916Backend();
  if (!_emuDev) { ShowStatusAction::show("Out of memory"); _showMfuTagMenu(); return; }
  bool ready = st25Begin(*_emuDev, _interface);
  if (!ready) { delete _emuDev; _emuDev = nullptr; ShowStatusAction::show("ST25R3916 not detected"); _showMfuTagMenu(); return; }

  ST25R3916Backend::ScanResult id;
  id.technology = ST25R3916Backend::Technology::NFC_A;
  if (_mfuUidLen == 4 || _mfuUidLen == 7) {
    id.nfcidLen = _mfuUidLen; memcpy(id.nfcid, _mfuUid, id.nfcidLen);
  } else {
    const HfDumpParser::Info info = HfDumpParser::inspect(_mfuDump, _mfuDumpLen);
    if (!info.uidValid || info.uidLen == 0 || info.uidLen > sizeof(id.nfcid)) {
      delete _emuDev; _emuDev = nullptr; ShowStatusAction::show("UID unavailable"); _showMfuTagMenu(); return;
    }
    id.nfcidLen = info.uidLen;
    memcpy(id.nfcid, info.uid, info.uidLen);
  }
  id.sak = 0x00;
  id.atqa[0] = _mfuAtqa[0]; id.atqa[1] = _mfuAtqa[1];
  // ATQA order matches RFAL sensRes: [0]=anticollisionInfo, [1]=platformInfo.
  // NTAG21x answers 0x0044, i.e. {0x44, 0x00}.
  if (id.atqa[0] == 0 && id.atqa[1] == 0) { id.atqa[0] = 0x44; id.atqa[1] = 0x00; }
  if (!_emuDev->startType2Emulation(id, _mfuDump, _mfuDumpLen)) {
    delete _emuDev; _emuDev = nullptr;
    ShowStatusAction::show("Failed", 1600); _showMfuTagMenu(); return;
  }
  _emuReturnMfc = false;
  _state = STATE_EMULATING;
  render();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_stopEmulation() {
#if defined(DEVICE_HAS_ST25R3916)
  const bool wasMfc = _emuReturnMfc;
  if (_emuDev) {
    _emuDev->stopEmulation();
    delete _emuDev;
    _emuDev = nullptr;
  }
  if (_state == STATE_EMULATING) {
    if (wasMfc) _showMfcTagMenu(); else _showMfuTagMenu();
  }
#endif
}

void ST25R3916Screen::_chooseMfcUidSource() {
  _uidWriteSourceMode = UID_WRITE_MANUAL;
  _uidWriteLen = 0;
  _uidWriteFilePath = "";
  _uidWriteDumpPath = "";
  _rebuildMfcUidWriteForm(0);
}

void ST25R3916Screen::_rebuildMfcUidWriteForm(uint8_t selected) {
  _state = STATE_MFC_UID_SOURCE_FORM;
  _rowCount = 0;
  auto add = [&](const char* label, const String& value) {
    if (_rowCount >= 3) return;
    _rowLabels[_rowCount] = label; _rowValues[_rowCount] = value;
    _uidWriteItems[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount].c_str()};
    ++_rowCount;
  };
  const char* source = _uidWriteSourceMode == UID_WRITE_MANUAL ? "Manual" :
                       _uidWriteSourceMode == UID_WRITE_FILE ? "UID File" : "Dump";
  add("UID", source);
  if (_uidWriteSourceMode == UID_WRITE_MANUAL) {
    String value = "-";
    if (_uidWriteLen) { value = ""; for(uint8_t i=0;i<_uidWriteLen;++i){char b[4];snprintf(b,sizeof(b),"%s%02X",i?":":"",_uidWriteValue[i]);value+=b;} }
    add("Value", value);
  } else if (_uidWriteSourceMode == UID_WRITE_FILE) {
    add("UID File", _uidWriteFilePath.length() ? st25BaseName(_uidWriteFilePath) : "-");
  } else {
    add("Dump File", _uidWriteDumpPath.length() ? st25BaseName(_uidWriteDumpPath) : "-");
  }
  add("Write", "");
  setItems(_uidWriteItems, _rowCount, min<uint8_t>(selected, _rowCount - 1));
  render();
}

void ST25R3916Screen::_editMfcUidManual() {
  String initial;
  for(uint8_t i=0;i<_uidWriteLen;++i){char b[4];snprintf(b,sizeof(b),"%s%02X",i?":":"",_uidWriteValue[i]);initial+=b;}
  String hex = InputTextAction::popup("UID (8 or 14 hex)", initial, InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _rebuildMfcUidWriteForm(1); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length()!=8 && hex.length()!=14) { _rebuildMfcUidWriteForm(1); ShowStatusAction::show("UID must be 4 or 7 bytes",1600); return; }
  uint8_t uid[7]={}; const uint8_t len=(uint8_t)(hex.length()/2u);
  for(uint8_t i=0;i<len;++i){char b[3]={hex[i*2],hex[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(b,&e,16);if(!e||*e){_rebuildMfcUidWriteForm(1);ShowStatusAction::show("Bad hex",1200);return;}uid[i]=(uint8_t)v;}
  memcpy(_uidWriteValue,uid,len); _uidWriteLen=len; _rebuildMfcUidWriteForm(1);
}

void ST25R3916Screen::_startMfcUidWriteFromForm() {
  if (_uidWriteLen != 4 && _uidWriteLen != 7) {
    ShowStatusAction::show(_uidWriteSourceMode == UID_WRITE_MANUAL ? "Enter UID" : "Select source file", 1600);
    render(); return;
  }
  _showMfcUidWritePreview(_uidWriteValue, _uidWriteLen,
      _uidWriteSourceMode == UID_WRITE_MANUAL ? "Manual" : _uidWriteSourceMode == UID_WRITE_FILE ? "UID File" : "Dump");
}

void ST25R3916Screen::_openMfcUidPicker() {
  _state=STATE_MFC_UID_FILE_SELECT; _browser.root="/unigeek/nfc/uids";
  if(!_uidPickDir.startsWith(_browser.root)) _uidPickDir=_browser.root;
  uint8_t n=_browser.load(this,_uidPickDir,BrowseFileView::Mode(".uid"));
  if(n==0&&_uidPickDir==_browser.root){_rebuildMfcUidWriteForm(1);ShowStatusAction::show("No saved UIDs",1600);return;} setItems(_browser.items(),n);
}
void ST25R3916Screen::_openMfcUidDumpPicker() {
  _state=STATE_MFC_UID_DUMP_SELECT; _browser.root="/unigeek/nfc/dumps";
  if(!_uidDumpPickDir.startsWith(_browser.root)) _uidDumpPickDir=_browser.root;
  uint8_t n=_browser.load(this,_uidDumpPickDir,BrowseFileView::Mode(".bin"));
  if(n==0&&_uidDumpPickDir==_browser.root){_rebuildMfcUidWriteForm(1);ShowStatusAction::show("No saved dumps",1600);return;} setItems(_browser.items(),n);
}
void ST25R3916Screen::_openMfcUidFile(uint8_t index) {
  if(index>=_browser.count())return; const auto&e=_browser.entry(index); if(e.isDir){_uidPickDir=e.path;_openMfcUidPicker();return;}
  uint8_t uid[7]={};size_t n=0;if(!IdentityFile::loadNfcUid(e.path,uid,sizeof(uid),n)||(n!=4&&n!=7)){ShowStatusAction::show("Invalid UID file",1600);return;}memcpy(_uidWriteValue,uid,n);_uidWriteLen=(uint8_t)n;_uidWriteFilePath=e.path;_rebuildMfcUidWriteForm(1);
}
void ST25R3916Screen::_openMfcUidDumpFile(uint8_t index) {
  if(index>=_browser.count())return;const auto&e=_browser.entry(index);if(e.isDir){_uidDumpPickDir=e.path;_openMfcUidDumpPicker();return;}
  if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable",1600);return;}fs::File f=Uni.Storage->open(e.path.c_str(),"r");if(!f){ShowStatusAction::show("Failed to open file",1600);return;}size_t n=f.size();if(n==0||n>kMfcMaxDumpLen){f.close();ShowStatusAction::show("Invalid dump",1600);return;}uint8_t*d=new uint8_t[n];if(!d){f.close();ShowStatusAction::show("Out of memory",1600);return;}size_t got=f.read(d,n);f.close();if(got!=n){delete[]d;ShowStatusAction::show("Invalid dump",1600);return;}auto info=HfDumpParser::inspect(d,n);delete[]d;if(!HfDumpParser::isMifareClassic(info.type)||!info.uidValid||(info.uidLen!=4&&info.uidLen!=7)){ShowStatusAction::show("Dump UID not supported",1600);return;}memcpy(_uidWriteValue,info.uid,info.uidLen);_uidWriteLen=info.uidLen;_uidWriteDumpPath=e.path;_rebuildMfcUidWriteForm(1);
}
void ST25R3916Screen::_showMfcUidWritePreview(const uint8_t* uid,uint8_t uidLen,const char* source) {
  if(!uid||(uidLen!=4&&uidLen!=7)){ShowStatusAction::show("UID not supported",1600);_showMfcTagMenu();return;}memcpy(_uidWriteValue,uid,uidLen);_uidWriteLen=uidLen;
  _rowCount=0;auto add=[&](const char*l,const String&v){if(_rowCount>=kMaxRows)return;_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;};
  String t;for(uint8_t i=0;i<uidLen;++i){char b[4];snprintf(b,sizeof(b),"%s%02X",i?":":"",uid[i]);t+=b;}add("Source",source?source:"-");add("UID",t);add("Target","Magic Gen1A/Gen3");add("[Press]","Write to Tag");_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_MFC_UID_WRITE_PREVIEW;render();
}
bool ST25R3916Screen::_writeMfcUidToTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if(_uidWriteLen!=4&&_uidWriteLen!=7)return false;_state=STATE_MFC_UID_WRITING;render();ST25R3916Backend dev;if(!st25Begin(dev,_interface)){ShowStatusAction::show("ST25R3916 not detected",1600);_showMfcTagMenu();return false;}
  ST25R3916Backend::ScanResult presented;bool found=false;uint32_t start=millis();while(millis()-start<5000){Uni.update();if(Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){dev.deactivate();_showMfcTagMenu();return false;}if(dev.scan(ST25R3916Backend::TECH_A,presented,200,true)){found=true;break;}delay(50);}if(!found){ShowStatusAction::show("Tag not detected",1600);_showMfcTagMenu();return false;}dev.deactivate();
  MagicCardType magic=_detectMagicType(dev);if(magic!=MagicCardType::GEN1A&&magic!=MagicCardType::GEN3){ShowStatusAction::show("Tag not Gen1A/Gen3",1800);_showMfcTagMenu();return false;}if(magic==MagicCardType::GEN1A&&_uidWriteLen!=4){ShowStatusAction::show("Gen1A UID must be 4 bytes",1600);_showMfcTagMenu();return false;}
  uint8_t block0[16]={};if(magic==MagicCardType::GEN1A){ST25R3916Backend::ScanResult tag;if(!dev.scan(ST25R3916Backend::TECH_A,tag,1000,true)){ShowStatusAction::show("Tag not detected",1600);_showMfcTagMenu();return false;}const uint8_t halt[2]={0x50,0};uint8_t tmp[4]={};size_t tmpLen=0;(void)dev.nfcATransceive(halt,2,tmp,4,tmpLen,80);uint8_t ack[2]={};uint8_t wake=0x40;size_t bits=0;bool ok=dev.nfcATransceiveBits(&wake,7,ack,4,bits,250)&&bits>=4&&(ack[0]&0x0F)==0x0A;uint8_t unlock=0x43;if(ok){bits=0;ok=dev.nfcATransceiveBits(&unlock,8,ack,4,bits,250)&&bits>=4&&(ack[0]&0x0F)==0x0A;}if(ok){const uint8_t rd[2]={0x30,0};size_t len=0;ok=dev.nfcATransceive(rd,2,block0,16,len,500)&&len>=16;}dev.deactivate();if(!ok){ShowStatusAction::show("Failed",1600);_showMfcTagMenu();return false;}}
  bool ok=_writeMagicUid(dev,magic,_uidWriteValue,_uidWriteLen,magic==MagicCardType::GEN1A?block0:nullptr);ShowStatusAction::show(ok?"UID written":"Failed",1600);_showMfcTagMenu();return ok;
#else
  return false;
#endif
}

void ST25R3916Screen::_openMfcDumpPicker() {
  _state = STATE_MFC_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = "/unigeek/nfc/dumps";
  _browser.root = "/unigeek/nfc/dumps";
  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(".bin", 320, 1024, 4096));
  if (n == 0 && _dumpPickDir == _browser.root) {
    ShowStatusAction::show("No compatible Classic .bin", 1600);
    _showMfcTagMenu();
    return;
  }
  setItems(_browser.items(), n);
}

void ST25R3916Screen::_openMfcDumpFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _dumpPickDir = e.path;
    _openMfcDumpPicker();
    return;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    _showMfcTagMenu();
    return;
  }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Failed to open file"); _showMfcTagMenu(); return; }
  const size_t len = f.size();
  if (len != 320 && len != 1024 && len != 4096) {
    f.close(); ShowStatusAction::show("Dump size not supported", 1600); _showMfcTagMenu(); return;
  }
  const size_t got = f.read(_mfcDump, len);
  f.close();
  if (got != len) { ShowStatusAction::show("Failed to read dump"); _showMfcTagMenu(); return; }
  _mfcDumpLen = len;
  _mfcDumpBlocks = (uint16_t)(len / 16U);
  _mfcDumpFromCompleteRead = false;
  _showMfcWritePreview(_mfcDump, len, true);
}

void ST25R3916Screen::_showMfcWritePreview(const uint8_t* dump, size_t len, bool fromFile) {
  const HfDumpParser::Info info = HfDumpParser::inspect(dump, len);
  if (!dump || !HfDumpParser::isMifareClassic(info.type) ||
      info.type == HfDumpParser::TYPE_MIFARE_CLASSIC_2K) {
    ShowStatusAction::show("Invalid dump", 1600);
    _showMfcTagMenu();
    return;
  }
  if (dump != _mfcDump) memcpy(_mfcDump, dump, len);
  _mfcDumpLen = len;
  _mfcDumpBlocks = (uint16_t)(len / 16U);
  _writePreviewFromFile = fromFile;
  memset(_writeSourceUid, 0, sizeof(_writeSourceUid));
  if (info.uidLen >= sizeof(_writeSourceUid))
    memcpy(_writeSourceUid, info.uid, sizeof(_writeSourceUid));
  _writeSourceUidKnown = info.uidLen == sizeof(_writeSourceUid) && info.uidValid;

  _rowCount = 0;
  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    _rowCount++;
  };
  addRow("Source", fromFile ? "File" : "Read Tag");
  addRow("Type", info.type == HfDumpParser::TYPE_MIFARE_CLASSIC_MINI ? "MF Classic Mini" :
                 (info.type == HfDumpParser::TYPE_MIFARE_CLASSIC_4K ? "MF Classic 4K" : "MF Classic 1K"));
  String uid = "Unknown";
  if (_writeSourceUidKnown) {
    uid = "";
    for (uint8_t i = 0; i < 4; ++i) {
      char h[4];
      snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", _writeSourceUid[i]);
      uid += h;
    }
  }
  addRow("UID", uid);
  addRow("UID Action", "Preserved");
  addRow("Blocks", String((unsigned)(len / 16U)));
  addRow("Dump", String((unsigned)len) + " bytes");
  addRow("[Press]", "Write Dump to Tag");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFC_WRITE_PREVIEW;
  render();
}

bool ST25R3916Screen::_writeMfcDumpToTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (_mfcDumpLen != 320 && _mfcDumpLen != 1024 && _mfcDumpLen != 4096) {
    ShowStatusAction::show("Invalid dump");
    _showMfcTagMenu();
    return false;
  }

  _state = STATE_MFC_WRITING;
  render();
  ST25R3916Backend dev;
  bool ready = st25Begin(dev, _interface);
  if (!ready) { ShowStatusAction::show("ST25R3916 not detected"); _showMfcTagMenu(); return false; }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfcTagMenu(); return false;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Tag not supported"); _showMfcTagMenu(); return false;
  }
  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (blocks * 16U != _mfcDumpLen || sectors > 40) {
    ShowStatusAction::show("Tag size mismatch"); _showMfcTagMenu(); return false;
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t keysA[40][6] = {}, keysB[40][6] = {};
  bool foundA[40] = {}, foundB[40] = {};

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (uint8_t keyType = 0; keyType < 2; ++keyType) {
      char msg[42];
      snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...",
               (unsigned)(sector * 2U + keyType + 1U), (unsigned)(sectors * 2U));
      ProgressView::progress(msg, (int)((sector * 2U + keyType) * 100U / (sectors * 2U)));
      for (const auto& candidate : defaults) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        const auto& key = candidate.value();
        if (dev.mifareClassicAuthenticate(trailer, key.data(), keyType == 1)) {
          memcpy(keyType ? keysB[sector] : keysA[sector], key.data(), 6);
          (keyType ? foundB[sector] : foundA[sector]) = true;
          dev.deactivate();
          break;
        }
        dev.deactivate();
      }
    }
    if (!foundA[sector] && !foundB[sector]) {
      ProgressView::finish();
      render();
      ShowStatusAction::show("Key not available", 1600);
      _showMfcTagMenu();
      return false;
    }
  }
  ProgressView::finish();

  size_t written = 0;
  const size_t totalWritable = blocks > 0 ? blocks - 1U : 0;
  ProgressView::init();

  for (size_t sector = 0; sector < sectors; ++sector) {
    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);
    int8_t activeKey = -1;

    auto auth = [&](uint8_t which) -> bool {
      if ((which == 0 && !foundA[sector]) || (which == 1 && !foundB[sector])) return false;
      if (!dev.hasActiveTag() && !reactivate()) return false;
      const uint8_t* key = which ? keysB[sector] : keysA[sector];
      if (!dev.mifareClassicAuthenticate(trailer, key, which == 1)) {
        dev.deactivate();
        return false;
      }
      activeKey = (int8_t)which;
      return true;
    };

    bool authenticated = foundA[sector] && auth(0);
    if (!authenticated) {
      dev.deactivate();
      activeKey = -1;
      authenticated = foundB[sector] && auth(1);
    }
    if (!authenticated) {
      ProgressView::finish();
      render();
      ShowStatusAction::show("Failed", 1600);
      _showMfcTagMenu();
      return false;
    }

    for (size_t off = 0; off < count; ++off) {
      const size_t block = first + off;
      if (block == 0) continue;
      char msg[42];
      snprintf(msg, sizeof(msg), "Writing blocks (%u/%u)...",
               (unsigned)(written + 1U), (unsigned)totalWritable);
      ProgressView::progress(msg, totalWritable ? (int)(written * 100U / totalWritable) : 0);

      const uint8_t* desired = _mfcDump + block * 16U;
      bool ok = dev.mifareClassicWriteBlock((uint8_t)block, desired);
      if (!ok) {
        const uint8_t other = activeKey == 0 ? 1 : 0;
        dev.deactivate();
        activeKey = -1;
        if (auth(other)) ok = dev.mifareClassicWriteBlock((uint8_t)block, desired);
      }

      if (!ok) {
        dev.deactivate();
        if (block != trailer && reactivate()) {
          const uint8_t verifyWhich = foundA[sector] ? 0 : 1;
          const uint8_t* verifyKey = verifyWhich ? keysB[sector] : keysA[sector];
          if (dev.mifareClassicAuthenticate(trailer, verifyKey, verifyWhich == 1)) {
            uint8_t verify[16] = {};
            ok = dev.mifareClassicReadBlock((uint8_t)block, verify) && memcmp(verify, desired, 16) == 0;
          }
        } else if (block == trailer && reactivate()) {
          if (dev.mifareClassicAuthenticate(trailer, desired, false)) ok = true;
          else {
            dev.deactivate();
            if (reactivate() && dev.mifareClassicAuthenticate(trailer, desired + 10, true)) ok = true;
          }
        }
      }
      if (!ok) {
        dev.deactivate();
        ProgressView::finish();
        render();
        char err[32]; snprintf(err, sizeof(err), "Failed to write block %u", (unsigned)block);
        ShowStatusAction::show(err, 1600);
        _showMfcTagMenu();
        return false;
      }
      ++written;
    }
    dev.deactivate();
  }

  ProgressView::finish();
  render();
  ShowStatusAction::show("Written", 1500);
  _showMfcTagMenu();
  return true;
#else
  return false;
#endif
}

void ST25R3916Screen::_eraseMfcTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_ERASING;
  render();

  ST25R3916Backend dev;
  bool ready = st25Begin(dev, _interface);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfcTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1200);
    _showMfcTagMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Tag not supported");
    _showMfcTagMenu();
    return;
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (!sectors || !blocks || sectors > 40) {
    ShowStatusAction::show("Tag not supported");
    _showMfcTagMenu();
    return;
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t keysA[40][6] = {}, keysB[40][6] = {};
  bool foundA[40] = {}, foundB[40] = {};

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  ProgressView::init();
  bool keysOk = true;
  for (size_t sector = 0; sector < sectors; ++sector) {
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (uint8_t keyType = 0; keyType < 2; ++keyType) {
      char msg[40];
      snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...",
               (unsigned)(sector * 2U + keyType + 1U), (unsigned)(sectors * 2U));
      ProgressView::progress(msg, (int)((sector * 2U + keyType) * 100U / (sectors * 2U)));
      for (const auto& candidate : defaults) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        const auto& key = candidate.value();
        if (dev.mifareClassicAuthenticate(trailer, key.data(), keyType == 1)) {
          memcpy(keyType ? keysB[sector] : keysA[sector], key.data(), 6);
          (keyType ? foundB[sector] : foundA[sector]) = true;
          dev.deactivate();
          break;
        }
        dev.deactivate();
      }
    }
    if (!foundA[sector] && !foundB[sector]) { keysOk = false; break; }
  }
  ProgressView::finish();

  if (!keysOk) {
    render();
    ShowStatusAction::show("Key not available", 1700);
    _showMfcTagMenu();
    return;
  }

  uint8_t zero[16] = {};
  const size_t totalDataBlocks = blocks - sectors - 1U;
  size_t erased = 0;

  ProgressView::init();
  for (size_t sector = 0; sector < sectors; ++sector) {
    const size_t first = sectorFirstBlock(sector);
    const size_t count = sectorBlockCount(sector);
    const uint8_t trailer = (uint8_t)(first + count - 1U);
    int8_t activeKey = -1;

    auto auth = [&](uint8_t which) -> bool {
      if ((which == 0 && !foundA[sector]) || (which == 1 && !foundB[sector])) return false;
      if (!dev.hasActiveTag() && !reactivate()) return false;
      const uint8_t* key = which ? keysB[sector] : keysA[sector];
      if (!dev.mifareClassicAuthenticate(trailer, key, which == 1)) {
        dev.deactivate();
        return false;
      }
      activeKey = (int8_t)which;
      return true;
    };

    bool authenticated = foundA[sector] && auth(0);
    if (!authenticated) {
      dev.deactivate();
      activeKey = -1;
      authenticated = foundB[sector] && auth(1);
    }
    if (!authenticated) {
      ProgressView::finish();
      render();
      ShowStatusAction::show("Failed", 1600);
      _showMfcTagMenu();
      return;
    }

    for (size_t off = 0; off + 1U < count; ++off) {
      const size_t block = first + off;
      if (block == 0) continue;

      char msg[40];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...",
               (unsigned)(erased + 1U), (unsigned)totalDataBlocks);
      ProgressView::progress(msg, totalDataBlocks ? (int)(erased * 100U / totalDataBlocks) : 0);

      bool ok = dev.mifareClassicWriteBlock((uint8_t)block, zero);
      if (!ok) {
        const uint8_t other = activeKey == 0 ? 1 : 0;
        dev.deactivate();
        activeKey = -1;
        if (auth(other)) ok = dev.mifareClassicWriteBlock((uint8_t)block, zero);
      }
      if (!ok) {
        dev.deactivate();
        ProgressView::finish();
        render();
        char err[32]; snprintf(err, sizeof(err), "Failed to erase block %u", (unsigned)block);
        ShowStatusAction::show(err, 1600);
        _showMfcTagMenu();
        return;
      }
      ++erased;
    }
    dev.deactivate();
  }
  ProgressView::finish();

  _mfcDumpLen = 0;
  _mfcDumpBlocks = 0;
  _mfcDumpFromCompleteRead = false;
  _mfcUidLen = 0;
  _mfcSak = 0;
  _mfcAtqa[0] = _mfcAtqa[1] = 0;

  render();
  ShowStatusAction::show("Tag erased", 1600);
  _showMfcTagMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_saveMfcUid() {
  if (!_mfcUidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1200);
    render();
    return;
  }

  const char* typeName = (_mfcSak == 0x09) ? "MF-Mini"
                       : (_mfcSak == 0x18) ? "MF-4K"
                                           : "MF-1K";
  String suggested = String(typeName) + "_";
  char h[3];
  for (uint8_t i = 0; i < _mfcUidLen; ++i) {
    snprintf(h, sizeof(h), "%02X", _mfcUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save UID", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".uid")) name.remove(name.length() - 4);
  const String filename = name + ".uid";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/uids");
  const bool ok = IdentityFile::saveNfcUid(
      String("/unigeek/nfc/uids/") + filename, _mfcUid, _mfcUidLen);
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

void ST25R3916Screen::_saveMfcDump() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1600);
    render();
    return;
  }
  if (!_mfcDumpLen || !_mfcUidLen) {
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  const char* typeName = (_mfcSak == 0x09) ? "MF-Mini"
                       : (_mfcSak == 0x18) ? "MF-4K"
                                           : "MF-1K";
  String suggested = String(typeName) + "_";
  for (uint8_t i = 0; i < _mfcUidLen; ++i) {
    char h[3];
    snprintf(h, sizeof(h), "%02X", _mfcUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save Dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  const String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_mfcDump, _mfcDumpLen) == _mfcDumpLen;
    f.close();
  }

  render();
  if (ok) {
    const String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
  } else {
    ShowStatusAction::show("Failed", 1600);
  }
  render();
}

void ST25R3916Screen::_renderMfcDump() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  static constexpr int kRowH = 14;
  static constexpr int kScrollW = 3;
  const int fullyVisible = max(1, bh / kRowH);
  const int visible = fullyVisible + ((bh % kRowH >= 5) ? 1 : 0);
  const uint16_t totalRows = _mfcDumpBlocks * 2U;
  const int textW = bw - kScrollW - 4;

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  for (int i = 0; i < visible; ++i) {
    const uint16_t row = _mfcDumpOffset + (uint16_t)i;
    if (row >= totalRows) break;
    const uint16_t block = row / 2U;
    const uint8_t half = row & 1U;
    const size_t off = (size_t)block * 16U + half * 8U;

    char label[16];
    snprintf(label, sizeof(label), "B%u %s", (unsigned)block, half ? "8-F" : "0-7");
    char value[24];
    size_t valuePos = 0;
    for (uint8_t b = 0; b < 8; ++b) {
      valuePos += snprintf(value + valuePos, sizeof(value) - valuePos,
                           "%02X%s", _mfcDump[off + b], b == 7 ? "" : " ");
    }

    const int rowY = by + i * kRowH;
    const int rowH = min(kRowH, by + bh - rowY);
    if (rowH <= 0) break;
    Sprite sp(&lcd);
    sp.createSprite(bw - kScrollW, rowH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_DARKGREY);
    sp.drawString(label, 2, 3);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(value, textW, 3);
    sp.pushSprite(bx, rowY);
    sp.deleteSprite();
  }

  const int sbX = bx + bw - kScrollW;
  lcd.fillRect(sbX, by, kScrollW, bh, 0x2104);
  if (totalRows > (uint16_t)fullyVisible) {
    const int sbH = max(4, bh * fullyVisible / (int)totalRows);
    const uint16_t maxOffset = totalRows - fullyVisible;
    const int sbY = (bh - sbH) * _mfcDumpOffset / max(1, (int)maxOffset);
    lcd.fillRect(sbX, by + sbY, kScrollW, sbH, Config.getThemeColor());
  }
}

void ST25R3916Screen::_handleMfcDumpNav(INavigation::Direction dir) {
  const uint16_t totalRows = _mfcDumpBlocks * 2U;
  const int visible = max(1, bodyH() / 14);
  const uint16_t maxOffset = totalRows > (uint16_t)visible ? totalRows - visible : 0;

  if (dir == INavigation::DIR_UP && _mfcDumpOffset > 0) {
    --_mfcDumpOffset;
  } else if (dir == INavigation::DIR_DOWN && _mfcDumpOffset < maxOffset) {
    ++_mfcDumpOffset;
  } else if (dir == INavigation::DIR_LEFT && _mfcDumpOffset > 0) {
    _mfcDumpOffset = _mfcDumpOffset > (uint16_t)visible ? _mfcDumpOffset - visible : 0;
  } else if (dir == INavigation::DIR_RIGHT && _mfcDumpOffset < maxOffset) {
    _mfcDumpOffset = (uint16_t)min((int)maxOffset, (int)_mfcDumpOffset + visible);
  } else {
    return;
  }
  _renderMfcDump();
}

void ST25R3916Screen::_addWrappedRow(const String& label, const String& value) {
  static constexpr size_t kChunk = 24;
  if (!value.length()) return;
  size_t pos = 0;
  bool first = true;
  while (pos < value.length() && _rowCount < kMaxRows) {
    size_t take = min(kChunk, value.length() - pos);
    _rowLabels[_rowCount] = first ? label : "";
    _rowValues[_rowCount] = value.substring(pos, pos + take);
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
    pos += take;
    first = false;
  }
}

void ST25R3916Screen::_showNdefDetails(const uint8_t* uid, uint8_t uidLen,
                                       const uint8_t* ndef, size_t ndefLen) {
  _rowCount = 0;
  _hasNdef = false;
  _ndefLen = 0;

  auto addRow = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
  };

  if (uid && uidLen) {
    String id;
    for (uint8_t i = 0; i < uidLen; ++i) {
      char h[4];
      snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", uid[i]);
      id += h;
    }
    addRow("UID", id);
  }

  if (!ndef) {
    addRow("NDEF", "Not found");
  } else {
    if (ndefLen <= sizeof(_ndefBuf)) {
      memcpy(_ndefBuf, ndef, ndefLen);
      _ndefLen = ndefLen;
      _hasNdef = true;
    }
    addRow("NDEF Size", String((unsigned)ndefLen) + " bytes");
    if (_ndefCapacity) {
      addRow("Capacity", String((unsigned)_ndefCapacity) + " bytes");
      addRow("Free", String((unsigned)(_ndefCapacity > ndefLen ? _ndefCapacity - ndefLen : 0)) + " bytes");
    }

    if (ndefLen == 0) {
      addRow("NDEF", "Empty");
    } else {
      NdefParser::Result parsed;
      if (!NdefParser::parse(ndef, ndefLen, parsed)) {
        addRow("NDEF", "Invalid record");
      } else {
        switch (parsed.kind) {
          case NdefParser::RECORD_TEXT:
            addRow("Record", "Text");
            if (parsed.language.length()) addRow("Language", parsed.language);
            _addWrappedRow("Text", parsed.text);
            break;
          case NdefParser::RECORD_URL:
            addRow("Record", "URL");
            _addWrappedRow("URL", parsed.uri);
            break;
          case NdefParser::RECORD_PHONE:
            addRow("Record", "Phone");
            _addWrappedRow("Phone", parsed.phone);
            break;
          case NdefParser::RECORD_EMAIL:
            addRow("Record", "Email");
            _addWrappedRow("Email", parsed.email);
            break;
          case NdefParser::RECORD_VCARD:
            addRow("Record", "vCard");
            if (parsed.contact.length()) _addWrappedRow("Contact", parsed.contact);
            if (parsed.company.length()) _addWrappedRow("Company", parsed.company);
            if (parsed.address.length()) _addWrappedRow("Address", parsed.address);
            if (parsed.phone.length()) _addWrappedRow("Phone", parsed.phone);
            if (parsed.email.length()) _addWrappedRow("Email", parsed.email);
            if (parsed.website.length()) _addWrappedRow("Website", parsed.website);
            break;
          default:
            addRow("Record", "Unsupported");
            break;
        }
      }
    }
  }

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = _ndefExperimentalTarget ? STATE_EXP_NDEF_DETAILS : (_ndefMfuTarget ? STATE_MFU_NDEF_DETAILS : STATE_MFC_NDEF_DETAILS);
  render();
}

void ST25R3916Screen::_showNdefWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile) {
  if (!ndef || !ndefLen || ndefLen > kMaxNdefBytes) { ShowStatusAction::show("Invalid NDEF"); _returnToNdefWriteMenu(); return; }
  memcpy(_ndefBuf, ndef, ndefLen); _ndefLen = ndefLen; _hasNdef = true;
  _ndefWritePreview = true; _ndefWritePreviewFromFile = fromFile; _ndefCapacity = 0;
  _showNdefDetails(nullptr, 0, _ndefBuf, _ndefLen);
  if (_rowCount < kMaxRows) {
    _rowLabels[_rowCount] = "[Press]"; _rowValues[_rowCount] = "Write to Tag";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
    _scrollView.setRows(_rows, _rowCount); render();
  }
}

void ST25R3916Screen::_writeNdefBuilt(uint8_t kind) {
  const char* label = kind == 0 ? "Text" : kind == 1 ? "URL" : kind == 2 ? "Phone" : "Email";
  String initial = kind == 1 ? "https://" : "";
  String value = InputTextAction::popup(label, initial, kind == 2 ? InputTextAction::INPUT_PHONE : InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled() || !value.length()) { _returnToNdefWriteMenu(); return; }
  uint8_t b[kMaxNdefBytes] = {}; size_t n = 0;
  bool ok = kind == 0 ? NdefBuilder::buildText(value, b, n, sizeof(b)) :
            kind == 1 ? NdefBuilder::buildUrl(value, b, n, sizeof(b)) :
            kind == 2 ? NdefBuilder::buildPhone(value, b, n, sizeof(b)) :
                        NdefBuilder::buildEmail(value, b, n, sizeof(b));
  if (!ok) { _returnToNdefWriteMenu(); ShowStatusAction::show("NDEF too large"); return; }
  _showNdefWritePreview(b, n, false);
}

void ST25R3916Screen::_writeNdefVcard() {
  String contact = InputTextAction::popup("Contact name", ""); if (InputTextAction::wasCancelled() || !contact.length()) { _returnToNdefWriteMenu(); return; }
  String company = InputTextAction::popup("Company", ""); if (InputTextAction::wasCancelled()) { _returnToNdefWriteMenu(); return; }
  String address = InputTextAction::popup("Address", ""); if (InputTextAction::wasCancelled()) { _returnToNdefWriteMenu(); return; }
  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE); if (InputTextAction::wasCancelled()) { _returnToNdefWriteMenu(); return; }
  String email = InputTextAction::popup("Mail", ""); if (InputTextAction::wasCancelled()) { _returnToNdefWriteMenu(); return; }
  String website = InputTextAction::popup("Website", "https://"); if (InputTextAction::wasCancelled()) { _returnToNdefWriteMenu(); return; }
  uint8_t b[kMaxNdefBytes] = {}; size_t n = 0;
  if (!NdefBuilder::buildVcard(contact, company, address, phone, email, website, b, n, sizeof(b))) {
    _returnToNdefWriteMenu(); ShowStatusAction::show("vCard too large"); return;
  }
  _showNdefWritePreview(b, n, false);
}

void ST25R3916Screen::_openNdefFilePicker() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable"); _returnToNdefWriteMenu(); return; }
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/nfc"); Uni.Storage->makeDir("/unigeek/nfc/ndefs");
  if (!_ndefPickDir.startsWith("/unigeek/nfc/ndefs")) _ndefPickDir = "/unigeek/nfc/ndefs";
  _browser.root = "/unigeek/nfc/ndefs"; _state = _ndefExperimentalTarget ? STATE_EXP_NDEF_FILE_SELECT : (_ndefMfuTarget ? STATE_MFU_NDEF_FILE_SELECT : STATE_MFC_NDEF_FILE_SELECT);
  uint8_t n = _browser.load(this, _ndefPickDir, BrowseFileView::Mode(".ndef", 1, kMaxNdefBytes));
  if (!n && _ndefPickDir == _browser.root) { ShowStatusAction::show("No NDEF files"); _returnToNdefWriteMenu(); return; }
  setItems(_browser.items(), n); render();
}

void ST25R3916Screen::_openNdefFile(uint8_t index) {
  if (index >= _browser.count()) return; const auto& e = _browser.entry(index);
  if (e.isDir) { _ndefPickDir = e.path; _openNdefFilePicker(); return; }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r"); if (!f || f.size() == 0 || f.size() > kMaxNdefBytes) { if (f) f.close(); ShowStatusAction::show("Invalid NDEF file"); _openNdefFilePicker(); return; }
  size_t n = f.size(); uint8_t b[kMaxNdefBytes] = {}; bool ok = f.read(b, n) == (int)n; f.close();
  if (!ok) { ShowStatusAction::show("Failed to read NDEF"); _openNdefFilePicker(); return; }
  _showNdefWritePreview(b, n, true);
}

bool ST25R3916Screen::_writeMfcNdef(const uint8_t* ndef, size_t ndefLen) {
#if defined(DEVICE_HAS_ST25R3916)
  if (!ndef || !ndefLen || ndefLen > kMaxNdefBytes) { ShowStatusAction::show("NDEF too large"); return false; }
  _state = STATE_MFC_NDEF_WRITING; render();
  ST25R3916Backend dev; bool ready = st25Begin(dev, _interface);
  if (!ready) { ShowStatusAction::show("ST25R3916 not detected"); return false; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) { if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected"); return false; }
  if (!isMifareClassic(tag.sak)) { ShowStatusAction::show("Tag not supported"); return false; }
  size_t totalSectors=0,totalBlocks=0; mfcDimensions(tag.sak,totalSectors,totalBlocks);
  auto reactivate=[&](){ dev.deactivate(); ST25R3916Backend::ScanResult cur; return dev.scan(ST25R3916Backend::TECH_A,cur,1200,true)&&sameTag(tag,cur); };
  static const uint8_t madKeys[][6]={{0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},{0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}};
  auto readMad=[&](uint8_t block,uint8_t out[16]){ uint8_t sec=block<128?block/4:(uint8_t)(32+(block-128)/16); uint8_t trailer=(uint8_t)(sectorFirstBlock(sec)+sectorBlockCount(sec)-1); for(auto& key:madKeys) for(uint8_t kt=0;kt<2;++kt){ if(!dev.hasActiveTag()&&!reactivate()) continue; if(dev.mifareClassicAuthenticate(trailer,key,kt==1)&&dev.mifareClassicReadBlock(block,out)){dev.deactivate();return true;} dev.deactivate(); } return false; };
  uint8_t sectors[39]={}; size_t sectorCount=0; auto add=[&](uint8_t sec,uint8_t a,uint8_t c){ if(sec<totalSectors&&sectorCount<sizeof(sectors)&&((a==0x03&&c==0xE1)||(a==0xE1&&c==0x03))) sectors[sectorCount++]=sec; };
  uint8_t b1[16]={},b2[16]={}; if(!readMad(1,b1)||!readMad(2,b2)){ShowStatusAction::show("Not NDEF formatted");return false;}
  for(uint8_t sec=1;sec<=7;++sec){size_t o=2+(sec-1)*2;add(sec,b1[o],b1[o+1]);} for(uint8_t sec=8;sec<=15;++sec){size_t o=(sec-8)*2;add(sec,b2[o],b2[o+1]);}
  if(totalSectors>16){uint8_t m0[16]={},m1[16]={},m2[16]={}; if(readMad(64,m0)&&readMad(65,m1)&&readMad(66,m2)){for(uint8_t sec=17;sec<=23;++sec){size_t o=2+(sec-17)*2;add(sec,m0[o],m0[o+1]);}for(uint8_t sec=24;sec<=31;++sec){size_t o=(sec-24)*2;add(sec,m1[o],m1[o+1]);}for(uint8_t sec=32;sec<=39;++sec){size_t o=(sec-32)*2;add(sec,m2[o],m2[o+1]);}}}
  if(!sectorCount){ShowStatusAction::show("Not NDEF formatted");return false;}
  size_t capacity=0; for(size_t i=0;i<sectorCount;++i) capacity += sectors[i]<32?48:240;
  size_t payloadLen=ndefLen+3; if(payloadLen>capacity){ShowStatusAction::show("NDEF does not fit");return false;}
  uint8_t* payload=new uint8_t[payloadLen]; if(!payload){ShowStatusAction::show("Out of memory");return false;} payload[0]=0x03;payload[1]=(uint8_t)ndefLen;memcpy(payload+2,ndef,ndefLen);payload[2+ndefLen]=0xFE;
  static const uint8_t keyA[6]={0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}; size_t off=0; bool success=true; uint8_t firstFinal[16]={},firstStaged[16]={}; uint8_t firstBlockNo=0; bool haveFirst=false;
  ProgressView::init();
  size_t totalData=0; for(size_t i=0;i<sectorCount;++i) totalData+=sectors[i]<32?3:15; size_t done=0;
  for(size_t si=0;si<sectorCount&&off<payloadLen&&success;++si){ uint8_t sec=sectors[si]; size_t first=sectorFirstBlock(sec); uint8_t dataBlocks=sec<32?3:15; uint8_t trailer=(uint8_t)(first+sectorBlockCount(sec)-1);
    for(uint8_t bi=0;bi<dataBlocks&&off<payloadLen&&success;++bi){ uint8_t blockNo=(uint8_t)(first+bi); char msg[40]; snprintf(msg,sizeof(msg),"Writing blocks (%u/%u)...",(unsigned)(done+1),(unsigned)totalData);ProgressView::progress(msg,totalData?(int)(done*100/totalData):0);
      if(!dev.hasActiveTag()&&!reactivate()){success=false;break;} if(!dev.mifareClassicAuthenticate(trailer,keyA,false)){dev.deactivate();success=false;break;} uint8_t block[16]={}; if(!dev.mifareClassicReadBlock(blockNo,block)){dev.deactivate();success=false;break;} dev.deactivate();
      size_t take=min((size_t)16,payloadLen-off); memcpy(block,payload+off,take); if(!haveFirst){memcpy(firstFinal,block,16);memcpy(firstStaged,block,16);firstStaged[1]=0x00;firstBlockNo=blockNo;haveFirst=true;memcpy(block,firstStaged,16);} if(!dev.hasActiveTag()&&!reactivate()){success=false;break;} if(!dev.mifareClassicAuthenticate(trailer,keyA,false)||!dev.mifareClassicWriteBlock(blockNo,block)){dev.deactivate();success=false;break;} dev.deactivate(); off+=take;++done;
    }
  }
  if(success&&haveFirst){uint8_t sec=firstBlockNo<128?firstBlockNo/4:(uint8_t)(32+(firstBlockNo-128)/16);uint8_t trailer=(uint8_t)(sectorFirstBlock(sec)+sectorBlockCount(sec)-1);if(!dev.hasActiveTag()&&!reactivate())success=false;else if(!dev.mifareClassicAuthenticate(trailer,keyA,false)||!dev.mifareClassicWriteBlock(firstBlockNo,firstFinal))success=false;dev.deactivate();}
  ProgressView::finish(); delete[] payload;
  render();
  ShowStatusAction::show(success?"NDEF written":"Failed", 1600); return success;
#else
  return false;
#endif
}

void ST25R3916Screen::_eraseMfcNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_WRITING;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  bool ready = st25Begin(dev, _interface);
  if (!ready) { ShowStatusAction::show("ST25R3916 not detected"); _showMfcNdefMenu(); return; }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfcNdefMenu(); return;
  }
  if (!isMifareClassic(tag.sak)) {
    dev.deactivate(); ShowStatusAction::show("Tag not supported"); _showMfcNdefMenu(); return;
  }

  size_t totalSectors = 0, totalBlocks = 0;
  mfcDimensions(tag.sak, totalSectors, totalBlocks);
  uint8_t uid[10] = {};
  const uint8_t uidLen = min((uint8_t)sizeof(uid), tag.nfcidLen);
  memcpy(uid, tag.nfcid, uidLen);

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           current.nfcidLen == uidLen && memcmp(current.nfcid, uid, uidLen) == 0;
  };

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };
  auto readMadBlock = [&](uint8_t block, uint8_t out[16]) -> bool {
    const uint8_t sector = block < 128 ? (uint8_t)(block / 4U)
                                       : (uint8_t)(32U + (block - 128U) / 16U);
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (const auto& key : madKeys) {
      for (uint8_t keyType = 0; keyType < 2; ++keyType) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        if (dev.mifareClassicAuthenticate(trailer, key, keyType == 1) &&
            dev.mifareClassicReadBlock(block, out)) {
          dev.deactivate();
          return true;
        }
        dev.deactivate();
      }
    }
    return false;
  };

  uint8_t ndefSectors[39] = {};
  size_t ndefSectorCount = 0;
  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= totalSectors || ndefSectorCount >= sizeof(ndefSectors)) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03)) {
      ndefSectors[ndefSectorCount++] = sector;
    }
  };

  uint8_t b1[16] = {}, b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) {
    ShowStatusAction::show("Not NDEF formatted"); _showMfcNdefMenu(); return;
  }
  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2U + (size_t)(sec - 1U) * 2U;
    addIfNdef(sec, b1[off], b1[off + 1U]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8U) * 2U;
    addIfNdef(sec, b2[off], b2[off + 1U]);
  }
  if (totalSectors > 16) {
    uint8_t m0[16] = {}, m1[16] = {}, m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2U + (size_t)(sec - 17U) * 2U;
        addIfNdef(sec, m0[off], m0[off + 1U]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24U) * 2U;
        addIfNdef(sec, m1[off], m1[off + 1U]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32U) * 2U;
        addIfNdef(sec, m2[off], m2[off + 1U]);
      }
    }
  }
  if (!ndefSectorCount) {
    ShowStatusAction::show("Not NDEF formatted"); _showMfcNdefMenu(); return;
  }

  static const uint8_t nfcKey[6] = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};
  const uint8_t sector = ndefSectors[0];
  const uint8_t block = (uint8_t)sectorFirstBlock(sector);
  const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
  uint8_t data[16] = {};

  if (!dev.hasActiveTag() && !reactivate()) {
    ShowStatusAction::show("NDEF sector locked"); _showMfcNdefMenu(); return;
  }
  if (!dev.mifareClassicAuthenticate(trailer, nfcKey, false) ||
      !dev.mifareClassicReadBlock(block, data)) {
    dev.deactivate(); ShowStatusAction::show("NDEF sector locked"); _showMfcNdefMenu(); return;
  }
  dev.deactivate();

  data[0] = 0x03;
  data[1] = 0x00;
  data[2] = 0xFE;

  if (!reactivate() || !dev.mifareClassicAuthenticate(trailer, nfcKey, false) ||
      !dev.mifareClassicWriteBlock(block, data)) {
    dev.deactivate(); ShowStatusAction::show("Failed"); _showMfcNdefMenu(); return;
  }
  dev.deactivate();
  _hasNdef = false;
  _ndefLen = 0;
  ShowStatusAction::show("NDEF erased", 1600);
  _showMfcNdefMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

bool ST25R3916Screen::_formatMfc1kNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_WRITING;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfcNdefMenu();
    return false;
  }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfcNdefMenu();
    return false;
  }
  if (tag.sak != 0x08) {
    dev.deactivate();
    ShowStatusAction::show("Format supports Classic 1K");
    _showMfcNdefMenu();
    return false;
  }

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t sectorKey[3][2][6] = {};
  bool sectorKeyValid[3][2] = {};
  uint8_t rollingKey[2][6] = {};
  bool rollingValid[2] = {};

  ProgressView::init();
  for (uint8_t sec = 0; sec < 3; ++sec) {
    const uint8_t trailer = (uint8_t)(sec * 4U + 3U);
    for (uint8_t kt = 0; kt < 2; ++kt) {
      char msg[40];
      snprintf(msg, sizeof(msg), "Checking keys (%u/6)...", (unsigned)(sec * 2U + kt + 1U));
      ProgressView::progress(msg, (int)((sec * 2U + kt) * 25U / 2U));

      auto tryKey = [&](const uint8_t key[6]) -> bool {
        if (!dev.hasActiveTag() && !reactivate()) return false;
        if (!dev.mifareClassicAuthenticate(trailer, key, kt == 1)) {
          dev.deactivate();
          return false;
        }
        memcpy(sectorKey[sec][kt], key, 6);
        sectorKeyValid[sec][kt] = true;
        memcpy(rollingKey[kt], key, 6);
        rollingValid[kt] = true;
        dev.deactivate();
        return true;
      };

      if (rollingValid[kt] && tryKey(rollingKey[kt])) continue;
      for (const auto& candidate : defaults) {
        const auto& key = candidate.value();
        if (rollingValid[kt] && memcmp(key.data(), rollingKey[kt], 6) == 0) continue;
        if (tryKey(key.data())) break;
      }
    }
    if (!sectorKeyValid[sec][0] && !sectorKeyValid[sec][1]) {
      ProgressView::finish();
      render();
      ShowStatusAction::show("Format: unknown sector key");
      _showMfcNdefMenu();
      return false;
    }
  }
  ProgressView::finish();

  uint8_t madPayload[31] = {};
  madPayload[0] = 0x01; madPayload[1] = 0x03; madPayload[2] = 0xE1;
  madPayload[3] = 0x03; madPayload[4] = 0xE1;
  auto crc8 = [](const uint8_t* d, size_t n) {
    uint8_t c = 0xC7;
    for (size_t i = 0; i < n; ++i) {
      c ^= d[i];
      for (uint8_t b = 0; b < 8; ++b)
        c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x1D) : (uint8_t)(c << 1);
    }
    return c;
  };
  uint8_t mad1[16] = {}, mad2[16] = {};
  mad1[0] = crc8(madPayload, 31);
  memcpy(mad1 + 1, madPayload, 15);
  memcpy(mad2, madPayload + 15, 16);
  static const uint8_t madTrailer[16] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x78,0x77,0x88,0xC1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  static const uint8_t nfcTrailer[16] = {
    0xD3,0xF7,0xD3,0xF7,0xD3,0xF7,0x7F,0x07,0x88,0x40,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  uint8_t zero[16] = {};
  uint8_t empty[16] = {0x03,0x00,0xFE};

  struct FormatBlock { uint8_t block; const uint8_t* data; };
  const FormatBlock sectorBlocks[3][4] = {
    {{1, mad1}, {2, mad2}, {3, madTrailer}, {0, nullptr}},
    {{4, empty}, {5, zero}, {6, zero}, {7, nfcTrailer}},
    {{8, zero}, {9, zero}, {10, zero}, {11, nfcTrailer}},
  };
  const uint8_t sectorCounts[3] = {3, 4, 4};
  uint8_t completed = 0;
  static constexpr uint8_t total = 11;

  auto writeSector = [&](uint8_t sec, uint8_t kt) -> bool {
    if (!sectorKeyValid[sec][kt]) return false;
    if (!reactivate()) return false;
    const uint8_t trailer = (uint8_t)(sec * 4U + 3U);
    if (!dev.mifareClassicAuthenticate(trailer, sectorKey[sec][kt], kt == 1)) {
      dev.deactivate();
      return false;
    }
    for (uint8_t i = 0; i < sectorCounts[sec]; ++i) {
      const auto& item = sectorBlocks[sec][i];
      char msg[40];
      snprintf(msg, sizeof(msg), "Formatting blocks (%u/%u)...",
               (unsigned)(completed + 1U), (unsigned)total);
      ProgressView::progress(msg, (int)((uint16_t)completed * 100U / total));
      if (!dev.mifareClassicWriteBlock(item.block, item.data)) {
        dev.deactivate();
        return false;
      }
      ++completed;
    }
    dev.deactivate();
    return true;
  };

  ProgressView::init();
  bool ok = true;
  for (uint8_t sec = 0; sec < 3 && ok; ++sec) {
    const uint8_t before = completed;
    ok = writeSector(sec, 1);
    if (!ok) {
      completed = before;
      ok = writeSector(sec, 0);
    }
  }

  if (ok) ProgressView::progress("Format complete", 100);
  ProgressView::finish();
  render();
  ShowStatusAction::show(ok ? "NDEF formatted" : "Failed", 1600);
  _showMfcNdefMenu();
  return ok;
#else
  ShowStatusAction::show("ST25R3916 not supported");
  return false;
#endif
}

void ST25R3916Screen::_showMfuDumpActions() {
  if (!_mfuDumpLen || !_mfuPages) {
    ShowStatusAction::show("No dump available", 1600);
    render();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"Read Memory", "view"},
    {"Save UID", "uid"},
    {"Save Dump", "save"},
    {"Write to Tag", "write"},
    {"Emulate Tag", "emulate"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 5, nullptr);
  if (!r) { render(); return; }

  render();
  if (strcmp(r, "view") == 0) {
    Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::TYPE2, _mfuType,
                                        _mfuUid, _mfuUidLen, _mfuDump, _mfuDumpLen));
  } else if (strcmp(r, "uid") == 0) {
    _saveMfuUid();
  } else if (strcmp(r, "save") == 0) {
    _saveMfuDump();
  } else if (strcmp(r, "write") == 0) {
    if (_mfuType != "NTAG215" || _mfuPages != 135 || _mfuDumpLen != 540) {
      ShowStatusAction::show("Write supports NTAG215", 1400);
      render();
      return;
    }
    _showMfuWritePreview(false);
  } else if (strcmp(r, "emulate") == 0) {
    _emulateMfuTag();
  }
}

void ST25R3916Screen::_openMfuDumpPicker(bool forEmulation) {
  _mfuDumpPickerForEmulation = forEmulation;
  _state = STATE_MFU_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = "/unigeek/nfc/dumps";
  _browser.root = "/unigeek/nfc/dumps";
  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (n == 0 && _dumpPickDir == _browser.root) {
    ShowStatusAction::show("No NTAG215 .bin", 1500);
    _showMfuTagMenu();
    return;
  }
  setItems(_browser.items(), n);
}

void ST25R3916Screen::_openMfuDumpFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) { _dumpPickDir = e.path; _openMfuDumpPicker(_mfuDumpPickerForEmulation); return; }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable"); _showMfuTagMenu(); return;
  }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Failed to open file"); _showMfuTagMenu(); return; }
  if (f.size() != 540) { f.close(); ShowStatusAction::show("Invalid NTAG215 dump"); _showMfuTagMenu(); return; }
  const size_t got = f.read(_mfuDump, 540);
  f.close();
  if (got != 540) { ShowStatusAction::show("Failed to read dump"); _showMfuTagMenu(); return; }
  _mfuDumpLen = 540; _mfuPages = 135; _mfuType = "NTAG215";
  _mfuUidLen = 0;
  memset(_mfuUid, 0, sizeof(_mfuUid));
  // .bin dumps carry no ATQA; use the NTAG215 default in RFAL byte order.
  _mfuAtqa[0] = 0x44; _mfuAtqa[1] = 0x00;
  _mfuSak = 0;

  if (_mfuDumpPickerForEmulation) {
    const HfDumpParser::Info info = HfDumpParser::inspect(_mfuDump, _mfuDumpLen);
    if (info.type != HfDumpParser::TYPE_NTAG215 || !info.uidValid || info.uidLen == 0) {
      ShowStatusAction::show("Invalid NTAG215 dump", 1500);
      _showMfuTagMenu();
      return;
    }
    _showMfuEmulatePreview();
    return;
  }
  _showMfuWritePreview(true);
}

void ST25R3916Screen::_showMfuWritePreview(bool fromFile) {
  _mfuWritePreviewFromFile = fromFile;
  const HfDumpParser::Info info = HfDumpParser::inspect(_mfuDump, _mfuDumpLen);
  if (info.type != HfDumpParser::TYPE_NTAG215 || _mfuPages != 135) {
    ShowStatusAction::show("Write supports NTAG215", 1400); _showMfuTagMenu(); return;
  }
  _rowCount = 0;
  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label; _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
  };
  addRow("Source", fromFile ? "File" : "Read Tag");
  addRow("Type", "NTAG215");
  String uid;
  for (uint8_t i = 0; i < info.uidLen; ++i) { char h[4]; snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", info.uid[i]); uid += h; }
  addRow("UID", uid);
  addRow("UID Action", "Preserved");
  addRow("Pages", "135");
  addRow("Dump", "540 bytes");
  uint8_t* ndef = nullptr; size_t ndefLen = 0; NdefParser::Result parsed;
  if (HfDumpParser::extractNdef(_mfuDump, _mfuDumpLen, &ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT: addRow("NDEF", "Text"); break;
      case NdefParser::RECORD_URL: addRow("NDEF", "URL"); break;
      case NdefParser::RECORD_PHONE: addRow("NDEF", "Phone"); break;
      case NdefParser::RECORD_EMAIL: addRow("NDEF", "Email"); break;
      case NdefParser::RECORD_VCARD: addRow("NDEF", "vCard"); break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else addRow("NDEF", "Not found");
  delete[] ndef;
  addRow("[Press]", "Write to Tag");
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFU_WRITE_PREVIEW; render();
}

void ST25R3916Screen::_showMfuEmulatePreview() {
  const HfDumpParser::Info info = HfDumpParser::inspect(_mfuDump, _mfuDumpLen);
  if (info.type != HfDumpParser::TYPE_NTAG215 || !info.uidValid || info.uidLen == 0 ||
      _mfuPages != 135 || _mfuDumpLen != 540) {
    ShowStatusAction::show("Invalid NTAG215 dump", 1500);
    _showMfuTagMenu();
    return;
  }
  _rowCount = 0;
  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label; _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
  };
  addRow("Source", "File");
  addRow("Type", "NTAG215");
  String uid;
  for (uint8_t i = 0; i < info.uidLen; ++i) { char h[4]; snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", info.uid[i]); uid += h; }
  addRow("UID", uid);
  addRow("Pages", "135");
  addRow("Dump", "540 bytes");
  uint8_t* ndef = nullptr; size_t ndefLen = 0; NdefParser::Result parsed;
  if (HfDumpParser::extractNdef(_mfuDump, _mfuDumpLen, &ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT: addRow("NDEF", "Text"); break;
      case NdefParser::RECORD_URL: addRow("NDEF", "URL"); break;
      case NdefParser::RECORD_PHONE: addRow("NDEF", "Phone"); break;
      case NdefParser::RECORD_EMAIL: addRow("NDEF", "Email"); break;
      case NdefParser::RECORD_VCARD: addRow("NDEF", "vCard"); break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else addRow("NDEF", "Not found");
  delete[] ndef;
  addRow("[Press]", "Emulate Tag");
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFU_EMULATE_PREVIEW; render();
}

bool ST25R3916Screen::_writeMfuDumpToTag() {
#if defined(DEVICE_HAS_ST25R3916)
  if (_mfuDumpLen != 540 || _mfuPages != 135 || _mfuType != "NTAG215") {
    ShowStatusAction::show("Invalid NTAG215 dump"); _showMfuTagMenu(); return false;
  }
  _state = STATE_MFU_WRITING; render();
  ST25R3916Backend dev; bool ready = st25Begin(dev, _interface);
  if (!ready) { ShowStatusAction::show("ST25R3916 not detected"); _showMfuTagMenu(); return false; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) { if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected"); _showMfuTagMenu(); return false; }
  if (tag.sak != 0x00) { dev.deactivate(); ShowStatusAction::show("Tag not supported", 1600); _showMfuTagMenu(); return false; }
  String type; uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || type != "NTAG215" || pages != 135) {
    dev.deactivate(); ShowStatusAction::show("Tag not supported", 1600); _showMfuTagMenu(); return false;
  }
  if (!st25EnsureMfuAuth(dev, type, pages, 4, 129, false)) { dev.deactivate(); _showMfuWritePreview(_mfuWritePreviewFromFile); return false; }
  render(); _renderTagPrompt();
  static constexpr uint16_t firstPage = 4, lastPage = 129, total = 126;
  ProgressView::init();
  for (uint16_t page = firstPage; page <= lastPage; ++page) {
    char msg[36]; const uint16_t done = page - firstPage;
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...", (unsigned)(done + 1), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100U / total));
    if (!st25WritePageVerified(dev, page, &_mfuDump[page * 4U])) {
      ProgressView::finish(); dev.deactivate();
      render();
      ShowStatusAction::show("Failed", 1600);
      _showMfuWritePreview(_mfuWritePreviewFromFile);
      return false;
    }
  }
  ProgressView::progress("Written", 100); ProgressView::finish(); dev.deactivate();
  render();
  ShowStatusAction::show("Written", 1600); _showMfuTagMenu(); return true;
#else
  ShowStatusAction::show("ST25R3916 not supported"); return false;
#endif
}

void ST25R3916Screen::_eraseMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFU_ERASING;
  render();

  ST25R3916Backend dev;
  bool ready = st25Begin(dev, _interface);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfuTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfuTagMenu();
    return;
  }
  if (tag.sak != 0x00) {
    dev.deactivate();
    ShowStatusAction::show("Tag not supported", 1600);
    _showMfuTagMenu();
    return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || type != "NTAG215" || pages != 135 || tag.nfcidLen != 7) {
    dev.deactivate();
    ShowStatusAction::show("Tag not supported", 1600);
    _showMfuTagMenu();
    return;
  }
  if (!st25EnsureMfuAuth(dev, type, pages, 4, 129, false)) {
    dev.deactivate();
    _showMfuTagMenu();
    return;
  }
  render(); _renderTagPrompt();

  uint8_t image[HfDumpBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!HfDumpBuilder::buildNtag215(tag.nfcid, nullptr, 0, image, imageLen, sizeof(image)) ||
      imageLen != HfDumpBuilder::NTAG215_SIZE) {
    dev.deactivate();
    ShowStatusAction::show("Cannot build empty tag", 1500);
    _showMfuTagMenu();
    return;
  }

  static constexpr uint16_t firstPage = 4;
  static constexpr uint16_t lastPage = 129;
  static constexpr uint16_t total = 126;
  ProgressView::init();
  ProgressView::progress("Erasing pages (0/126)...", 0);

  for (uint16_t page = firstPage; page <= lastPage; ++page) {
    const uint16_t done = page - firstPage;
    char msg[40];
    snprintf(msg, sizeof(msg), "Erasing pages (%u/%u)...",
             (unsigned)(done + 1), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100U / total));
    if (!dev.type2WritePage((uint8_t)page, &image[page * 4U])) {
      ProgressView::finish();
      dev.deactivate();
      render();
      char err[36];
      snprintf(err, sizeof(err), "Failed to erase page %u", (unsigned)page);
      ShowStatusAction::show(err, 1600);
      _showMfuTagMenu();
      return;
    }
  }

  ProgressView::progress("Erase complete", 100);
  ProgressView::finish();
  dev.deactivate();

  _mfuDumpLen = 0;
  _mfuPages = 0;
  _mfuUidLen = 0;
  _mfuType = "";
  render();
  ShowStatusAction::show("Tag erased", 1600);
  _showMfuTagMenu();
#else
  ShowStatusAction::show("ST25R3916 not supported");
#endif
}

void ST25R3916Screen::_saveMfuUid() {
  if (!_mfuUidLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1200);
    render();
    return;
  }

  String safeType = _mfuType;
  safeType.replace(" / ", "-");
  safeType.replace(" ", "-");
  safeType.replace("/", "-");
  String suggested = safeType + "_";
  char h[3];
  for (uint8_t i = 0; i < _mfuUidLen; ++i) {
    snprintf(h, sizeof(h), "%02X", _mfuUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save UID", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".uid")) name.remove(name.length() - 4);
  const String filename = name + ".uid";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/uids");
  const bool ok = IdentityFile::saveNfcUid(
      String("/unigeek/nfc/uids/") + filename, _mfuUid, _mfuUidLen);
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

void ST25R3916Screen::_saveMfuDump() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1600);
    render();
    return;
  }
  if (!_mfuDumpLen || !_mfuUidLen) {
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  String safeType = _mfuType;
  safeType.replace(" / ", "-");
  safeType.replace(" ", "-");
  safeType.replace("/", "-");

  String suggested = safeType + "_";
  for (uint8_t i = 0; i < _mfuUidLen; ++i) {
    char h[3];
    snprintf(h, sizeof(h), "%02X", _mfuUid[i]);
    suggested += h;
  }

  String name = InputTextAction::popup("Save Dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }
  render();

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  const String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_mfuDump, _mfuDumpLen) == _mfuDumpLen;
    f.close();
  }

  render();
  if (ok) {
    const String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1600);
  } else {
    ShowStatusAction::show("Failed", 1600);
  }
  render();
}

void ST25R3916Screen::_renderMfuDump() {
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  static constexpr int kRowH = 14;
  static constexpr int kScrollW = 3;
  const int fullyVisible = max(1, bh / kRowH);
  const int visible = fullyVisible + ((bh % kRowH >= 5) ? 1 : 0);
  const uint16_t totalRows = _mfuPages;
  const int textW = bw - kScrollW - 4;

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  for (int i = 0; i < visible; ++i) {
    const uint16_t row = _mfuDumpOffset + (uint16_t)i;
    if (row >= totalRows) break;
    const uint16_t page = row;
    const size_t off = (size_t)page * 4U;

    char label[16];
    snprintf(label, sizeof(label), "P%03u", (unsigned)page);
    char value[9];
    snprintf(value, sizeof(value), "%02X%02X%02X%02X",
             _mfuDump[off], _mfuDump[off + 1U], _mfuDump[off + 2U], _mfuDump[off + 3U]);

    const int rowY = by + i * kRowH;
    const int rowH = min(kRowH, by + bh - rowY);
    if (rowH <= 0) break;
    Sprite sp(&lcd);
    sp.createSprite(bw - kScrollW, rowH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString(label, 0, 2);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(value, min(42, textW / 3), 2);
    sp.pushSprite(bx, rowY);
    sp.deleteSprite();
  }

  if (totalRows > (uint16_t)fullyVisible) {
    const int trackH = bh;
    const int thumbH = max(6, (trackH * fullyVisible) / (int)totalRows);
    const int maxOff = max(1, (int)totalRows - fullyVisible);
    const int thumbY = by + ((trackH - thumbH) * min((int)_mfuDumpOffset, maxOff)) / maxOff;
    lcd.fillRect(bx + bw - kScrollW, by, kScrollW, bh, TFT_DARKGREY);
    lcd.fillRect(bx + bw - kScrollW, thumbY, kScrollW, thumbH, TFT_WHITE);
  }
}

void ST25R3916Screen::_handleMfuDumpNav(INavigation::Direction dir) {
  static constexpr int kRowH = 14;
  const uint16_t totalRows = _mfuPages;
  const uint16_t visible = (uint16_t)max(1, bodyH() / kRowH);
  const uint16_t maxOffset = totalRows > visible ? totalRows - visible : 0;

  if (dir == INavigation::DIR_UP) {
    if (_mfuDumpOffset > 0) --_mfuDumpOffset;
  } else if (dir == INavigation::DIR_DOWN) {
    if (_mfuDumpOffset < maxOffset) ++_mfuDumpOffset;
  } else if (dir == INavigation::DIR_LEFT) {
    _mfuDumpOffset = _mfuDumpOffset > visible ? _mfuDumpOffset - visible : 0;
  } else if (dir == INavigation::DIR_RIGHT) {
    _mfuDumpOffset = min((uint16_t)(_mfuDumpOffset + visible), maxOffset);
  }
  render();
}

void ST25R3916Screen::_readMfcNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFC_NDEF_READING;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;
  render();

  ST25R3916Backend dev;
  bool ready = st25Begin(dev, _interface);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfcNdefMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfcNdefMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    ShowStatusAction::show("Tag not supported");
    _showMfcNdefMenu();
    return;
  }

  size_t totalSectors = 0, totalBlocks = 0;
  mfcDimensions(tag.sak, totalSectors, totalBlocks);
  if (!totalSectors) {
    ShowStatusAction::show("Tag not supported");
    _showMfcNdefMenu();
    return;
  }

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };

  auto readMadBlock = [&](uint8_t block, uint8_t out[16]) -> bool {
    const uint8_t sector = block < 128 ? (uint8_t)(block / 4U)
                                       : (uint8_t)(32U + (block - 128U) / 16U);
    const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
    for (const auto& key : madKeys) {
      for (uint8_t keyType = 0; keyType < 2; ++keyType) {
        if (!dev.hasActiveTag() && !reactivate()) continue;
        if (dev.mifareClassicAuthenticate(trailer, key, keyType == 1) &&
            dev.mifareClassicReadBlock(block, out)) {
          dev.deactivate();
          return true;
        }
        dev.deactivate();
      }
    }
    return false;
  };

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= totalSectors || sectorCount >= sizeof(sectors)) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03)) {
      sectors[sectorCount++] = sector;
    }
  };

  uint8_t b1[16] = {}, b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _showMfcNdefMenu();
    return;
  }
  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2U + (size_t)(sec - 1U) * 2U;
    addIfNdef(sec, b1[off], b1[off + 1U]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8U) * 2U;
    addIfNdef(sec, b2[off], b2[off + 1U]);
  }

  if (totalSectors > 16) {
    uint8_t m0[16] = {}, m1[16] = {}, m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2U + (size_t)(sec - 17U) * 2U;
        addIfNdef(sec, m0[off], m0[off + 1U]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24U) * 2U;
        addIfNdef(sec, m1[off], m1[off + 1U]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32U) * 2U;
        addIfNdef(sec, m2[off], m2[off + 1U]);
      }
    }
  }

  if (!sectorCount) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _showMfcNdefMenu();
    return;
  }

  _ndefCapacity = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    _ndefCapacity += sectors[i] < 32 ? 48U : 240U;

  uint8_t* area = new uint8_t[_ndefCapacity];
  if (!area) {
    ShowStatusAction::show("Out of memory");
    _showMfcNdefMenu();
    return;
  }
  memset(area, 0, _ndefCapacity);

  static const uint8_t nfcKeyA[6] = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};
  size_t out = 0;
  size_t totalDataBlocks = 0;
  for (size_t i = 0; i < sectorCount; ++i) totalDataBlocks += sectors[i] < 32 ? 3U : 15U;
  size_t done = 0;

  ProgressView::init();
  for (size_t si = 0; si < sectorCount; ++si) {
    const uint8_t sector = sectors[si];
    const size_t first = sectorFirstBlock(sector);
    const uint8_t dataBlocks = sector < 32 ? 3 : 15;
    const uint8_t trailer = (uint8_t)(first + sectorBlockCount(sector) - 1U);

    if (!dev.hasActiveTag() && !reactivate()) {
      ProgressView::finish(); delete[] area;
      render();
      ShowStatusAction::show("Failed to read NDEF"); _showMfcNdefMenu(); return;
    }
    if (!dev.mifareClassicAuthenticate(trailer, nfcKeyA, false)) {
      dev.deactivate();
      ProgressView::finish(); delete[] area;
      render();
      ShowStatusAction::show("Failed to read NDEF"); _showMfcNdefMenu(); return;
    }

    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      char msg[40];
      snprintf(msg, sizeof(msg), "Reading blocks (%u/%u)...",
               (unsigned)(done + 1U), (unsigned)totalDataBlocks);
      ProgressView::progress(msg, totalDataBlocks ? (int)(done * 100U / totalDataBlocks) : 0);
      if (!dev.mifareClassicReadBlock((uint8_t)(first + bi), area + out)) {
        dev.deactivate();
        ProgressView::finish(); delete[] area;
        render();
        ShowStatusAction::show("Failed to read NDEF"); _showMfcNdefMenu(); return;
      }
      out += 16;
      ++done;
    }
    dev.deactivate();
  }
  ProgressView::finish();

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;
  while (pos < out) {
    const uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE || pos >= out) break;
    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= out) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1U];
      pos += 2;
    }
    if (pos + len > out) break;
    if (tlv == 0x03) { ndef = area + pos; ndefLen = len; break; }
    pos += len;
  }

  _showNdefDetails(tag.nfcid, tag.nfcidLen, ndef, ndefLen);
  delete[] area;
#else
  ShowStatusAction::show("ST25R3916 not supported");
  _showMfcNdefMenu();
#endif
}

void ST25R3916Screen::_readMfuNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _ndefMfuTarget = true;
  _state = STATE_MFU_NDEF_READING;
  _ndefCapacity = 0;
  _hasNdef = false;
  _ndefLen = 0;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { ShowStatusAction::show("ST25R3916 not detected"); _showMfuNdefMenu(); return; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected");
    _showMfuNdefMenu(); return;
  }
  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages)) {
    dev.deactivate(); ShowStatusAction::show("Tag not supported"); _showMfuNdefMenu(); return;
  }
  if (!st25EnsureMfuAuth(dev, type, pages, 3, pages - 1, true)) {
    dev.deactivate(); _showMfuNdefMenu(); return;
  }
  _state = STATE_MFU_NDEF_READING;
  render();

  uint8_t cc[4] = {};
  if (!st25ReadPage(dev, 3, cc) || !st25Type2CcIsValid(cc)) {
    dev.deactivate(); ShowStatusAction::show("Not NDEF formatted"); _showMfuNdefMenu(); return;
  }
  _ndefCapacity = (size_t)cc[2] * 8u;
  const size_t physicalCapacity = pages > 4 ? ((size_t)pages - 4u) * 4u : 0u;
  if (!_ndefCapacity || !physicalCapacity) {
    dev.deactivate(); ShowStatusAction::show("Invalid NDEF capacity"); _showMfuNdefMenu(); return;
  }
  if (_ndefCapacity > physicalCapacity) _ndefCapacity = physicalCapacity;
  if (_ndefCapacity > kMfuMaxDumpLen) {
    dev.deactivate(); ShowStatusAction::show("NDEF area too large"); _showMfuNdefMenu(); return;
  }

  uint8_t area[kMfuMaxDumpLen] = {};
  size_t got = 0;
  ProgressView::init();
  bool ok = true;
  for (size_t off = 0; off < _ndefCapacity; off += 16) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading NDEF (%u/%u)...",
             (unsigned)min(off + 16, _ndefCapacity), (unsigned)_ndefCapacity);
    ProgressView::progress(msg, (int)(off * 100 / _ndefCapacity));
    uint8_t data[16] = {};
    if (!dev.type2ReadPages((uint8_t)(4 + off / 4), data)) { ok = false; break; }
    const size_t take = min((size_t)16, _ndefCapacity - off);
    memcpy(area + got, data, take);
    got += take;
  }
  ProgressView::finish();
  dev.deactivate();
  if (!ok) { _state = STATE_MFU_NDEF_READING; render(); ShowStatusAction::show("Failed", 1600); _showMfuNdefMenu(); return; }

  const St25Type2TlvInfo tlv = st25FindNdefTlv(area, got);
  const uint8_t* ndef = tlv.found ? area + tlv.valueOffset : nullptr;
  _showNdefDetails(tag.nfcid, tag.nfcidLen, ndef, tlv.found ? tlv.length : 0);
#else
  ShowStatusAction::show("ST25R3916 not enabled");
#endif
}

bool ST25R3916Screen::_writeMfuNdef(const uint8_t* ndef, size_t ndefLen) {
#if defined(DEVICE_HAS_ST25R3916)
  if (!ndef || !ndefLen || ndefLen > kMaxNdefBytes) { ShowStatusAction::show("NDEF too large"); return false; }
  _state = STATE_MFU_NDEF_WRITING;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { ShowStatusAction::show("ST25R3916 not detected"); return false; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) { if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected"); return false; }
  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages)) { dev.deactivate(); ShowStatusAction::show("Tag not supported"); return false; }
  if (!st25EnsureMfuAuth(dev, type, pages, 3, pages - 1, false)) { dev.deactivate(); return false; }
  _state = STATE_MFU_NDEF_WRITING;
  render();

  uint8_t cc[4] = {};
  if (!st25ReadPage(dev, 3, cc) || !st25Type2CcIsValid(cc)) {
    dev.deactivate(); ShowStatusAction::show("Not NDEF formatted"); return false;
  }
  size_t capacity = (size_t)cc[2] * 8u;
  const size_t physicalCapacity = pages > 4 ? ((size_t)pages - 4u) * 4u : 0u;
  if (!capacity || !physicalCapacity) { dev.deactivate(); ShowStatusAction::show("Invalid NDEF capacity"); return false; }
  if (capacity > physicalCapacity) capacity = physicalCapacity;
  if (capacity > kMfuMaxDumpLen) { dev.deactivate(); ShowStatusAction::show("NDEF area too large"); return false; }

  uint8_t area[kMfuMaxDumpLen] = {};
  size_t got = 0;
  if (!st25ReadType2Area(dev, capacity, area, got)) {
    dev.deactivate(); ShowStatusAction::show("Failed to read NDEF"); return false;
  }
  const St25Type2TlvInfo old = st25FindNdefTlv(area, got, false);
  const size_t start = old.found ? old.tlvOffset : old.insertOffset;
  const size_t lenBytes = ndefLen < 0xFFu ? 1u : 3u;
  const size_t needed = 1u + lenBytes + ndefLen + 1u;
  if (start + needed > capacity) { dev.deactivate(); ShowStatusAction::show("NDEF does not fit"); return false; }

  size_t pos = start;
  area[pos++] = 0x03;
  if (ndefLen < 0xFFu) {
    area[pos++] = (uint8_t)ndefLen;
  } else {
    area[pos++] = 0xFF;
    area[pos++] = (uint8_t)(ndefLen >> 8);
    area[pos++] = (uint8_t)ndefLen;
  }
  memcpy(area + pos, ndef, ndefLen);
  pos += ndefLen;
  area[pos++] = 0xFE;

  ProgressView::init();
  const size_t firstPage = start / 4;
  const size_t lastPage = (pos - 1) / 4;
  bool ok = true;
  for (size_t p = firstPage; p <= lastPage; ++p) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...",
             (unsigned)(p - firstPage + 1), (unsigned)(lastPage - firstPage + 1));
    ProgressView::progress(msg, (int)((p - firstPage) * 100 / (lastPage - firstPage + 1)));
    if (!dev.type2WritePage((uint8_t)(4 + p), area + p * 4)) { ok = false; break; }
  }
  ProgressView::finish();
  dev.deactivate();
  _state = STATE_MFU_NDEF_WRITING;
  render();
  ShowStatusAction::show(ok ? "NDEF written" : "Failed", 1600);
  return ok;
#else
  return false;
#endif
}

void ST25R3916Screen::_eraseMfuNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _ndefMfuTarget = true;
  _state = STATE_MFU_NDEF_WRITING;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { ShowStatusAction::show("ST25R3916 not detected"); _showMfuNdefMenu(); return; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) { if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected"); _showMfuNdefMenu(); return; }
  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages)) { dev.deactivate(); ShowStatusAction::show("Tag not supported"); _showMfuNdefMenu(); return; }
  if (!st25EnsureMfuAuth(dev, type, pages, 3, pages - 1, false)) { dev.deactivate(); _showMfuNdefMenu(); return; }
  _state = STATE_MFU_NDEF_WRITING;
  render();

  uint8_t cc[4] = {};
  if (!st25ReadPage(dev, 3, cc) || !st25Type2CcIsValid(cc)) {
    dev.deactivate(); ShowStatusAction::show("Not NDEF formatted"); _showMfuNdefMenu(); return;
  }
  size_t capacity = (size_t)cc[2] * 8u;
  const size_t physicalCapacity = pages > 4 ? ((size_t)pages - 4u) * 4u : 0u;
  if (capacity > physicalCapacity) capacity = physicalCapacity;
  if (!capacity || capacity > kMfuMaxDumpLen) { dev.deactivate(); ShowStatusAction::show("Invalid NDEF capacity"); _showMfuNdefMenu(); return; }

  uint8_t area[kMfuMaxDumpLen] = {};
  size_t got = 0;
  if (!st25ReadType2Area(dev, capacity, area, got)) {
    dev.deactivate(); ShowStatusAction::show("Failed to read NDEF"); _showMfuNdefMenu(); return;
  }
  const St25Type2TlvInfo tlv = st25FindNdefTlv(area, got, false);
  const size_t start = tlv.found ? tlv.tlvOffset : tlv.insertOffset;
  if (start + 3 > capacity) { dev.deactivate(); ShowStatusAction::show("Failed"); _showMfuNdefMenu(); return; }
  area[start] = 0x03;
  area[start + 1] = 0x00;
  area[start + 2] = 0xFE;
  const bool ok = st25WriteType2Range(dev, area, capacity, start, start + 3);
  dev.deactivate();
  _hasNdef = false;
  _ndefLen = 0;
  _state = STATE_MFU_NDEF_WRITING;
  render();
  ShowStatusAction::show(ok ? "NDEF erased" : "Failed", 1600);
  _showMfuNdefMenu();
#endif
}

bool ST25R3916Screen::_detectMfuType(ST25R3916Backend& dev, String& type, uint16_t& pages) {
#if defined(DEVICE_HAS_ST25R3916)
  type = "Ultralight / NTAG";
  pages = 0;

  const uint8_t getVersion = 0x60;
  uint8_t version[16] = {};
  size_t versionLen = 0;
  if (dev.type2Transceive(&getVersion, 1, version, sizeof(version), versionLen, 20) &&
      versionLen >= 8 && version[0] == 0x00 && version[1] == 0x04) {
    if (version[2] == 0x04) {
      switch (version[6]) {
        case 0x0B: type = "NTAG210"; pages = 20; return true;
        case 0x0E: type = "NTAG212"; pages = 41; return true;
        case 0x0F: type = "NTAG213"; pages = 45; return true;
        case 0x11: type = "NTAG215"; pages = 135; return true;
        case 0x13: type = "NTAG216"; pages = 231; return true;
        default: return false;
      }
    }
    if (version[2] == 0x03) {
      switch (version[6]) {
        case 0x0B: type = "Ultralight EV1 11"; pages = 20; return true;
        case 0x0E: type = "Ultralight EV1 21"; pages = 41; return true;
        default: return false;
      }
    }
    return false;
  }

  uint8_t data[16] = {};
  if (!dev.type2ReadPages(0, data)) return false;
  if (!dev.type2ReadPages(16, data)) {
    type = "Ultralight";
    pages = 16;
    return true;
  }

  return false;
#else
  (void)dev; (void)type; (void)pages;
  return false;
#endif
}

void ST25R3916Screen::_readMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_MFU_READING;
  render();

  ST25R3916Backend dev;
  const char* bus = st25InterfaceName(_interface);
  bool ready = st25Begin(dev, _interface);
  if (!ready) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfuTagMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 3000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1200);
    _showMfuTagMenu();
    return;
  }
  if (!_matchesGenericReadTag(tag.nfcid, tag.nfcidLen)) {
    dev.deactivate();
    _showStatusAndReturn("Different tag detected", STATE_MENU, 1600);
    return;
  }
  if (tag.sak != 0x00) {
    ShowStatusAction::show("Tag not supported");
    _showMfuTagMenu();
    return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || pages == 0 || pages * 4U > kMfuMaxDumpLen) {
    ShowStatusAction::show("Tag not supported", 1400);
    _showMfuTagMenu();
    return;
  }
  if (!st25EnsureMfuAuth(dev, type, pages, 0, pages - 1, true)) {
    dev.deactivate(); _showMfuTagMenu(); return;
  }
  render(); _renderTagPrompt();

  _mfuDumpLen = 0;
  _mfuPages = pages;
  _mfuType = type;
  _mfuUidLen = tag.nfcidLen;
  memcpy(_mfuUid, tag.nfcid, tag.nfcidLen);
  memcpy(_mfuAtqa, tag.atqa, sizeof(_mfuAtqa));
  _mfuSak = tag.sak;

  ProgressView::init();
  bool complete = true;
  uint16_t page = 0;
  while (page < pages) {
    const uint16_t remaining = pages - page;
    const uint16_t readPage = remaining >= 4U ? page : (pages - 4U);
    const uint16_t sourceOffset = page - readPage;
    const uint16_t copyPages = remaining >= 4U ? 4U : remaining;

    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(page + copyPages), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100U / pages));

    uint8_t fourPages[16] = {};
    if (!dev.type2ReadPages((uint8_t)readPage, fourPages)) {
      complete = false;
      break;
    }
    memcpy(&_mfuDump[page * 4U], &fourPages[sourceOffset * 4U], copyPages * 4U);
    _mfuDumpLen += copyPages * 4U;
    page += copyPages;
  }
  ProgressView::finish();
  dev.deactivate();

  if (!complete || _mfuDumpLen != (size_t)pages * 4U) {
    _mfuDumpLen = 0;
    render();
    ShowStatusAction::show("Failed", 1200);
    _showMfuTagMenu();
    return;
  }

  _rowCount = 0;
  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
  };

  String uid;
  for (uint8_t i = 0; i < _mfuUidLen; ++i) {
    char h[4];
    snprintf(h, sizeof(h), "%s%02X", i ? ":" : "", _mfuUid[i]);
    uid += h;
  }
  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%02X:%02X", _mfuAtqa[0], _mfuAtqa[1]);
  char sak[4];
  snprintf(sak, sizeof(sak), "%02X", _mfuSak);

  addRow("Type", _mfuType);
  addRow("UID", uid);
  addRow("ATQA", atqa);
  addRow("SAK", sak);
  addRow("Pages", String(_mfuPages));
  addRow("Dump", String(_mfuDumpLen) + " bytes");

  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (HfDumpParser::extractNdef(_mfuDump, _mfuDumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        addRow("NDEF", "Text");
        if (parsed.language.length()) addRow("Language", parsed.language);
        _addWrappedRow("Text", parsed.encoding == "UTF-16" ? String("(UTF-16 raw)") : parsed.text);
        break;
      case NdefParser::RECORD_URL:
        addRow("NDEF", "URL"); _addWrappedRow("URL", parsed.uri); break;
      case NdefParser::RECORD_PHONE:
        addRow("NDEF", "Phone"); _addWrappedRow("Phone", parsed.phone); break;
      case NdefParser::RECORD_EMAIL:
        addRow("NDEF", "Email"); _addWrappedRow("Email", parsed.email); break;
      case NdefParser::RECORD_VCARD:
        addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) _addWrappedRow("Company", parsed.company);
        if (parsed.address.length()) _addWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) _addWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) _addWrappedRow("Email", parsed.email);
        if (parsed.website.length()) _addWrappedRow("Website", parsed.website);
        break;
      default: addRow("NDEF", "Unsupported"); break;
    }
  } else {
    addRow("NDEF", "Not found");
  }
  delete[] ndef;
  addRow("Reader", bus ? bus : "--");
  addRow("[Press]", "Actions");

  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_MFU_DETAILS;
  render();
#endif
}

void ST25R3916Screen::_readMfuMemory() {
#if defined(DEVICE_HAS_ST25R3916)
  _advancedOperationTitle = "Read Memory";
  _state = STATE_MFU_MEMORY; render(); _renderTagPrompt();
  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { ShowStatusAction::show("ST25R3916 not detected"); _showMfuAdvancedMenu(); return; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) { if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected"); _showMfuAdvancedMenu(); return; }
  String type; uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages) || pages == 0 || (size_t)pages * 4U > kMfuMaxDumpLen) {
    dev.deactivate(); ShowStatusAction::show("Tag not supported"); _showMfuAdvancedMenu(); return;
  }
  if (!st25EnsureMfuAuth(dev, type, pages, 0, pages - 1, true)) { dev.deactivate(); _showMfuAdvancedMenu(); return; }
  render(); _renderTagPrompt();

  _mfuPages = pages; _mfuDumpLen = 0; _mfuDumpOffset = 0; _mfuType = type;
  ProgressView::init();
  bool complete = true;
  uint16_t page = 0;
  while (page < pages) {
    const uint16_t remaining = pages - page;
    const uint16_t readPage = remaining >= 4U ? page : (pages - 4U);
    const uint16_t sourceOffset = page - readPage;
    const uint16_t copyPages = remaining >= 4U ? 4U : remaining;
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...", (unsigned)(page + copyPages), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100U / pages));
    uint8_t fourPages[16] = {};
    if (!dev.type2ReadPages((uint8_t)readPage, fourPages)) { complete = false; break; }
    memcpy(&_mfuDump[page * 4U], &fourPages[sourceOffset * 4U], copyPages * 4U);
    _mfuDumpLen += copyPages * 4U;
    page += copyPages;
  }
  ProgressView::finish(); dev.deactivate();
  if (!complete || _mfuDumpLen != (size_t)pages * 4U) {
    _mfuDumpLen = 0; render(); ShowStatusAction::show("Failed"); _showMfuAdvancedMenu(); return;
  }
  _mfuUidLen = tag.nfcidLen;
  memcpy(_mfuUid, tag.nfcid, tag.nfcidLen);
  Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::TYPE2, _mfuType,
                                      _mfuUid, _mfuUidLen, _mfuDump, _mfuDumpLen));
#endif
}

void ST25R3916Screen::_editMfuMemory() {
#if defined(DEVICE_HAS_ST25R3916)
  _advancedOperationTitle = "Edit Memory";
  _state=STATE_MFU_MEMORY; render(); _renderTagPrompt(); ST25R3916Backend dev; if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected",STATE_MFU_ADVANCED_MENU,1600);return;} ST25R3916Backend::ScanResult tag; const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_A,tag,5000,true); if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showMfuAdvancedMenu();return;} String type;uint16_t pages=0;if(!_detectMfuType(dev,type,pages)||pages<=4){dev.deactivate();_showStatusAndReturn("Tag not supported",STATE_MFU_ADVANCED_MENU,1600);return;}
  int pg=InputNumberAction::popup((String("Page (4..")+String(pages-1)+")").c_str(),4,pages-1,4); if(InputNumberAction::wasCancelled()){dev.deactivate();_showMfuAdvancedMenu();return;} if(!st25ConfirmMfuSensitiveWrite(type,(uint16_t)pg)){dev.deactivate();_showMfuAdvancedMenu();return;} String hex=InputTextAction::popup("Page data (8 hex)","",InputTextAction::INPUT_HEX); if(InputTextAction::wasCancelled()){dev.deactivate();_showMfuAdvancedMenu();return;} hex.replace(" ","");hex.replace(":",""); if(hex.length()!=8){dev.deactivate();_state=STATE_MFU_MEMORY;render();_showStatusAndReturn("Need 8 hex chars",STATE_MFU_ADVANCED_MENU,1600);return;} uint8_t d[4]={};for(int i=0;i<4;++i){char b[3]={hex[i*2],hex[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(b,&e,16);if(!e||*e){dev.deactivate();_state=STATE_MFU_MEMORY;render();_showStatusAndReturn("Bad hex",STATE_MFU_ADVANCED_MENU,1200);return;}d[i]=(uint8_t)v;}
  if(!st25EnsureMfuAuth(dev,type,pages,pg,pg,false)){dev.deactivate();_showMfuAdvancedMenu();return;} _state=STATE_MFU_MEMORY;render(); bool ok=dev.type2WritePage((uint8_t)pg,d);dev.deactivate();_showStatusAndReturn(ok?"Page written":"Failed",STATE_MFU_ADVANCED_MENU,1600);
#endif
}

void ST25R3916Screen::_setMfuPassword() {
#if defined(DEVICE_HAS_ST25R3916)
  _advancedOperationTitle = "Set Password";
  _state = STATE_MFU_MEMORY;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { _showStatusAndReturn("ST25R3916 not detected", STATE_MFU_ADVANCED_MENU, 1600); return; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1600);
    _showMfuAdvancedMenu(); return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages)) {
    dev.deactivate(); _showStatusAndReturn("Tag not supported", STATE_MFU_ADVANCED_MENU, 1600); return;
  }
  const uint16_t cfg = st25MfuConfig0(type);
  if (cfg == 0xFFFF || cfg + 3 >= pages) {
    dev.deactivate(); _showStatusAndReturn("Password not supported", STATE_MFU_ADVANCED_MENU, 1600); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  const bool protectionReadable = st25ReadPageTailSafe(dev, cfg, pages, c0) &&
                                  st25ReadPageTailSafe(dev, cfg + 1, pages, c1);
  const bool wasProtected = !protectionReadable || (c0[3] != 0xFF && c0[3] < pages);
  if (wasProtected && !st25EnsureMfuAuth(dev, type, pages, cfg, cfg + 3, false)) {
    dev.deactivate(); _showMfuAdvancedMenu(); return;
  }
  if (!st25ReadPageTailSafe(dev, cfg, pages, c0) ||
      !st25ReadPageTailSafe(dev, cfg + 1, pages, c1)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1600); return;
  }

  uint8_t newPwd[4] = {};
  if (!st25PromptPwd(newPwd, "New Password")) {
    dev.deactivate(); _showMfuAdvancedMenu(); return;
  }
  static const InputSelectAction::Option modes[] = {{"Write Only", "w"}, {"Read & Write", "rw"}};
  const char* mode = InputSelectAction::popup("Protection", modes, 2, nullptr);
  if (!mode) { dev.deactivate(); _showMfuAdvancedMenu(); return; }

  const uint8_t desiredAccess = (uint8_t)((c1[0] & ~0x87u) | (strcmp(mode, "rw") == 0 ? 0x80u : 0u));
  const bool configLocked = (c1[0] & 0x40u) != 0;
  if (configLocked && (c0[3] != 4 || (c1[0] & 0x87u) != (desiredAccess & 0x87u))) {
    dev.deactivate(); _showStatusAndReturn("Configuration locked", STATE_MFU_ADVANCED_MENU, 1600); return;
  }

  if (!configLocked) {
    const uint8_t oldAccess = c1[0];
    c1[0] = desiredAccess;
    c0[3] = 4;
    if (c1[0] != oldAccess && !st25WritePageVerified(dev, cfg + 1, c1, pages)) {
      dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
    }
  }

  dev.deactivate();
  delay(8);
  ST25R3916Backend::ScanResult writeTarget;
  if (!dev.scan(ST25R3916Backend::TECH_A, writeTarget, 1200, true) || !sameTag(tag, writeTarget)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  if (!dev.type2WritePage((uint8_t)(cfg + 2), newPwd)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  dev.deactivate();
  delay(8);
  ST25R3916Backend::ScanResult current;
  if (!dev.scan(ST25R3916Backend::TECH_A, current, 1200, true)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (!sameTag(tag, current)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  uint8_t returnedPack[2] = {};
  if (!dev.type2PwdAuth(newPwd, returnedPack)) {
    dev.deactivate(); _showStatusAndReturn("Authentication failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (!configLocked && !st25WritePageVerified(dev, cfg, c0, pages)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  dev.deactivate();
  _showStatusAndReturn("Password set", STATE_MFU_ADVANCED_MENU, 1600);
#endif
}

void ST25R3916Screen::_removeMfuPassword() {
#if defined(DEVICE_HAS_ST25R3916)
  _advancedOperationTitle = "Remove Password";
  _state = STATE_MFU_MEMORY;
  render();
  _renderTagPrompt();

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) { _showStatusAndReturn("ST25R3916 not detected", STATE_MFU_ADVANCED_MENU, 1600); return; }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1600);
    _showMfuAdvancedMenu(); return;
  }

  String type;
  uint16_t pages = 0;
  if (!_detectMfuType(dev, type, pages)) {
    dev.deactivate(); _showStatusAndReturn("Tag not supported", STATE_MFU_ADVANCED_MENU, 1600); return;
  }
  const uint16_t cfg = st25MfuConfig0(type);
  if (cfg == 0xFFFF || cfg + 3 >= pages) {
    dev.deactivate(); _showStatusAndReturn("Password not supported", STATE_MFU_ADVANCED_MENU, 1600); return;
  }

  uint8_t auth0 = 0xFF;
  uint8_t probe0[4] = {}, probe1[4] = {};
  const bool readable = st25ReadPageTailSafe(dev, cfg, pages, probe0) &&
                        st25ReadPageTailSafe(dev, cfg + 1, pages, probe1);
  if (readable) auth0 = probe0[3];
  const bool protectedNow = !readable || (auth0 != 0xFF && auth0 < pages);
  if (protectedNow && !st25EnsureMfuAuth(dev, type, pages, cfg, cfg + 3, false)) {
    dev.deactivate(); _showMfuAdvancedMenu(); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  if (!st25ReadPageTailSafe(dev, cfg, pages, c0) ||
      !st25ReadPageTailSafe(dev, cfg + 1, pages, c1)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1600); return;
  }
  if (c1[0] & 0x40u) {
    dev.deactivate(); _showStatusAndReturn("Configuration locked", STATE_MFU_ADVANCED_MENU, 1600); return;
  }

  c1[0] &= (uint8_t)~0x87u;
  c0[3] = 0xFF;
  const uint8_t defaultPwd[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t defaultPackPage[4] = {0x00, 0x00, 0x00, 0x00};
  const uint8_t expectedPack[2] = {0x00, 0x00};

  if (!st25WritePageVerified(dev, cfg + 1, c1, pages)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (!st25WritePageVerified(dev, cfg, c0, pages)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  if (!dev.type2WritePage((uint8_t)(cfg + 3), defaultPackPage)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (!dev.type2WritePage((uint8_t)(cfg + 2), defaultPwd)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  dev.deactivate();
  delay(8);
  ST25R3916Backend::ScanResult current;
  if (!dev.scan(ST25R3916Backend::TECH_A, current, 1200, true) || !sameTag(tag, current)) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  uint8_t v0[4] = {}, v1[4] = {}, returnedPack[2] = {};
  if (!st25ReadPageTailSafe(dev, cfg, pages, v0) ||
      !st25ReadPageTailSafe(dev, cfg + 1, pages, v1) ||
      v0[3] != 0xFF || (v1[0] & 0x87u) != 0) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (!dev.type2PwdAuth(defaultPwd, returnedPack)) {
    dev.deactivate(); _showStatusAndReturn("Authentication failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }
  if (memcmp(returnedPack, expectedPack, sizeof(expectedPack)) != 0) {
    dev.deactivate(); _showStatusAndReturn("Failed", STATE_MFU_ADVANCED_MENU, 1800); return;
  }

  dev.deactivate();
  _showStatusAndReturn("Password removed", STATE_MFU_ADVANCED_MENU, 1600);
#endif
}

void ST25R3916Screen::_lockMfuTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _advancedOperationTitle = "Lock Tag";
  _state=STATE_MFU_MEMORY;render();_renderTagPrompt();ST25R3916Backend dev;
  if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected",STATE_MFU_ADVANCED_MENU,1600);return;}
  ST25R3916Backend::ScanResult tag;const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_A,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showMfuAdvancedMenu();return;}
  String type;uint16_t pages=0;if(!_detectMfuType(dev,type,pages)||type=="Ultralight C"||type=="Ultralight / NTAG"){dev.deactivate();_showStatusAndReturn("Lock not supported",STATE_MFU_ADVANCED_MENU,1600);return;}
  uint16_t lastUser=st25MfuDynamicLock(type);if(lastUser!=0xFFFF)--lastUser;else lastUser=15;
  if(lastUser<4){dev.deactivate();_showStatusAndReturn("No lockable memory",STATE_MFU_ADVANCED_MENU,1600);return;}
  static const InputSelectAction::Option warn[]={{"Lock permanently","lock"}};
  if(!InputSelectAction::popup("Make tag read-only?",warn,1,nullptr)){dev.deactivate();_showMfuAdvancedMenu();return;}
  _state=STATE_MFU_MEMORY;render();
  uint8_t sm[2]={},dm[3]={};if(!st25BuildMfuLockMasks(type,4,lastUser,sm,dm)){dev.deactivate();_showStatusAndReturn("Lock not supported",STATE_MFU_ADVANCED_MENU,1600);return;}
  uint16_t dyn=st25MfuDynamicLock(type);bool needsDyn=dyn!=0xFFFF&&(dm[0]||dm[1]||dm[2]);uint16_t authPage=needsDyn?dyn:2;
  if(!st25EnsureMfuAuth(dev,type,pages,authPage,authPage,false)){dev.deactivate();_showMfuAdvancedMenu();return;}
  _state=STATE_MFU_MEMORY;render();
  bool ok=true;
  if(sm[0]||sm[1]){uint8_t p2[4]={0,0,sm[0],sm[1]};ok=dev.type2WritePage(2,p2);}
  if(ok&&needsDyn){uint8_t d[4]={};if(!st25ReadPage(dev,dyn,d))ok=false;else{d[0]|=dm[0];d[1]|=dm[1];d[2]|=dm[2];ok=dev.type2WritePage((uint8_t)dyn,d);}}
  dev.deactivate();_showStatusAndReturn(ok?"Tag locked":"Failed",STATE_MFU_ADVANCED_MENU,1600);
#endif
}

void ST25R3916Screen::_formatMfuNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_MFU_NDEF_WRITING;render();_renderTagPrompt();ST25R3916Backend dev;
  if(!st25Begin(dev, _interface)){ShowStatusAction::show("ST25R3916 not detected");_showMfuNdefMenu();return;}
  ST25R3916Backend::ScanResult tag;const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_A,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected");_showMfuNdefMenu();return;}
  String type;uint16_t pages=0;if(!_detectMfuType(dev,type,pages)){dev.deactivate();ShowStatusAction::show("Tag not supported");_showMfuNdefMenu();return;}
  if(!st25EnsureMfuAuth(dev,type,pages,3,4,false)){dev.deactivate();_showMfuNdefMenu();return;}
  _state=STATE_MFU_NDEF_WRITING;render();
  uint8_t current[4]={},desired[4]={};
  if(!st25ReadPage(dev,3,current)||!st25Type2DefaultCc(type,desired)){dev.deactivate();ShowStatusAction::show("Format not supported");_showMfuNdefMenu();return;}
  bool ok=true;
  const bool hadValidCc=st25Type2CcIsValid(current);
  if(!hadValidCc){
    if(!st25Type2CcCanProgramSafely(current,desired)){dev.deactivate();ShowStatusAction::show("CC cannot be safely formatted");_showMfuNdefMenu();return;}
    ok=dev.type2WritePage(3,desired);
  }
  if(ok&&hadValidCc){
    size_t capacity=(size_t)current[2]*8u;
    const size_t physical=pages>4?((size_t)pages-4u)*4u:0u;
    if(capacity>physical)capacity=physical;
    if(!capacity||capacity>kMfuMaxDumpLen)ok=false;
    else{
      uint8_t area[kMfuMaxDumpLen]={};size_t got=0;
      if(!st25ReadType2Area(dev,capacity,area,got))ok=false;
      else{const St25Type2TlvInfo tlv=st25FindNdefTlv(area,got,false);const size_t start=tlv.found?tlv.tlvOffset:tlv.insertOffset;if(start+3>capacity)ok=false;else{area[start]=0x03;area[start+1]=0x00;area[start+2]=0xFE;ok=st25WriteType2Range(dev,area,capacity,start,start+3);}}
    }
  }else if(ok){
    const uint8_t empty[4]={0x03,0x00,0xFE,0x00};
    ok=dev.type2WritePage(4,empty);
  }
  dev.deactivate();_state=STATE_MFU_NDEF_WRITING;render();ShowStatusAction::show(ok?"NDEF formatted":"Failed",1600);_showMfuNdefMenu();
#endif
}

void ST25R3916Screen::_showMfcKnownKeys() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  if(!st25Begin(dev, _interface)){ShowStatusAction::show("ST25R3916 not detected");return;}
  ST25R3916Backend::ScanResult tag;
  if(!dev.scan(ST25R3916Backend::TECH_A,tag,5000,true)||!isMifareClassic(tag.sak)){dev.deactivate();ShowStatusAction::show("Tag not supported");return;}
  dev.deactivate();
  size_t sectors=0,blocks=0;mfcDimensions(tag.sak,sectors,blocks);
  String uidDisplay,uidFile;
  for(uint8_t i=0;i<tag.nfcidLen;++i){char h[4];snprintf(h,sizeof(h),"%s%02X",i?":":"",tag.nfcid[i]);uidDisplay+=h;char hh[3];snprintf(hh,sizeof(hh),"%02X",tag.nfcid[i]);uidFile+=hh;}
  String savedA[40],savedB[40];
  if(Uni.Storage&&Uni.Storage->isAvailable()){
    String content=Uni.Storage->readFile((String("/unigeek/nfc/keys/")+uidFile+".txt").c_str());
    int pos=0;while(pos<(int)content.length()){int nl=content.indexOf('\n',pos);if(nl<0)nl=content.length();String line=content.substring(pos,nl);line.trim();int sec=-1;char kt=0;char hex[13]={};if(sscanf(line.c_str(),"S%d %c %12s",&sec,&kt,hex)==3&&sec>=0&&sec<(int)sectors){String k(hex);k.toUpperCase();if(kt=='A'||kt=='a')savedA[sec]=k;else if(kt=='B'||kt=='b')savedB[sec]=k;}pos=nl+1;}
  }
  _rowCount=0;auto add=[&](const String&l,const String&v){if(_rowCount>=kMaxRows)return;_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;};
  add("UID",uidDisplay);char sak[4];snprintf(sak,sizeof(sak),"%02X",tag.sak);add("SAK",sak);
  size_t known=0;for(size_t sec=0;sec<sectors;++sec){if(savedA[sec].length())++known;if(savedB[sec].length())++known;}add("Keys",String((unsigned)known)+" / "+String((unsigned)(sectors*2U)));
  for(size_t sec=0;sec<sectors;++sec){add("S"+String((int)sec)+" A",savedA[sec].length()?savedA[sec]:"---");add("S"+String((int)sec)+" B",savedB[sec].length()?savedB[sec]:"---");}
  _state=STATE_MFC_KEYS_VIEW;_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();
#endif
}

void ST25R3916Screen::_openMfcDictionaries(bool attackMode) {
  if(!_dictPickDir.length())_dictPickDir=_dictPath; _browser.root=_dictPath;
  uint8_t n=_browser.load(this,_dictPickDir,".txt",nullptr,BrowseFileView::NAME,nullptr);
  _state=attackMode?STATE_MFC_DICT_ATTACK_SELECT:STATE_MFC_DICT_SELECT;
  if(_dictPickDir==_dictPath){uint8_t out=0;_dictItems[out++]={"Default"};_dictItems[out++]={"Discovered"};_dictItems[out++]={"Extended"};for(uint8_t i=0;i<n;++i)if(_browser.entry(i).path!=MfcKeyStore::kDiscoveredDictionary)_dictItems[out++]=_browser.items()[i];setItems(_dictItems,out);}
  else setItems(_browser.items(),n); render();
}

void ST25R3916Screen::_openMfcDictionary(uint8_t index,bool attackMode) {
  int fi=-1;
  if(_dictPickDir==_dictPath){
    int discovered=-1;for(uint8_t i=0;i<_browser.count();++i)if(_browser.entry(i).path==MfcKeyStore::kDiscoveredDictionary){discovered=i;break;}
    const uint8_t extendedIndex=2;
    if(index==0 || index==extendedIndex){
      String id=index==0?MfcKeyStore::kBuiltinDefaultId:MfcKeyStore::kBuiltinExtendedId;
      if(attackMode){_runMfcDictionaryAttack(id);return;}
      const uint8_t (*keys)[6]=nullptr;size_t count=0;const char* label=nullptr;MfcKeyStore::builtinDictionary(id,&keys,&count,&label);_rowCount=0;
      for(size_t i=0;i<count&&_rowCount<kMaxRows;++i){_rowLabels[_rowCount]=String(_rowCount+1);_rowValues[_rowCount]=MfcKeyStore::keyString(keys[i]);_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}
      _dictViewTitle=label;_state=STATE_MFC_DICT_VIEW;_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();return;
    }
    if(index==1){
      if(attackMode){_runMfcDictionaryAttack(MfcKeyStore::kDiscoveredDictionary);return;}
      if(discovered<0){ShowStatusAction::show("No keys in file");return;}
      fi=discovered;
    } else{uint8_t wanted=index-3;for(uint8_t i=0,seen=0;i<_browser.count();++i){if((int)i==discovered)continue;if(seen++==wanted){fi=i;break;}}}
  } else fi=index;
  if(fi<0 || fi>=_browser.count())return;const auto&e=_browser.entry((uint8_t)fi);if(e.isDir){_dictPickDir=e.path;_openMfcDictionaries(attackMode);return;}if(attackMode){_runMfcDictionaryAttack(e.path);return;}if(!Uni.Storage||!Uni.Storage->isAvailable()){ShowStatusAction::show("Storage unavailable");return;}String content=Uni.Storage->readFile(e.path.c_str());_rowCount=0;int pos=0;while(pos<(int)content.length()&&_rowCount<kMaxRows){int nl=content.indexOf('\n',pos);if(nl<0)nl=content.length();String line=content.substring(pos,nl);line.trim();if(line.length()&&!line.startsWith("#")){_rowLabels[_rowCount]=String(_rowCount+1);_rowValues[_rowCount]=line;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}pos=nl+1;}if(!_rowCount){ShowStatusAction::show("No keys in file");return;}_dictViewTitle=e.label;_state=STATE_MFC_DICT_VIEW;_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);render();
}

void ST25R3916Screen::_startMfcRecoverKeys() {
  _resumeMfcReadAfterDict = true;
  _mfcRecoverChainActive = true;
  _mfcRecoverChainIndex = 0;
  _mfcRecoverChainNewKeys = 0;
  _mfcRecoverContinueMissing = false;
  _mfcRecoverStopRequested = false;
  _mfcRecoverTagUidLen = 0;
  _mfcRecoverTagSak = 0;
  _mfcRecoverySummary.reset();
  _mfcRecoveryNext = true;
}

void ST25R3916Screen::_runMfcDictionaryAttack(const String& path) {
#if defined(DEVICE_HAS_ST25R3916)
  const size_t sourceKeyCount = MfcKeyStore::dictionaryKeyCount(Uni.Storage, path);

  if (!sourceKeyCount && _mfcRecoverChainActive && _mfcRecoverChainIndex == 1) {
    ++_mfcRecoverChainIndex;
    _mfcRecoveryNext = true;
    return;
  }

  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    if (_mfcRecoverChainActive) {
      _mfcRecoverChainActive = false;
      _mfcRecoverTagUidLen = 0;
      _resumeMfcReadAfterDict = false;
      _mfcRecoveryNext = false;
      _mfcReadAfterDict = false;
    }
    ShowStatusAction::show("ST25R3916 not detected");
    return;
  }
  ST25R3916Backend::ScanResult tag;
  if (_mfcRecoverChainActive && _mfcRecoverTagUidLen && _mfcRecoverChainIndex > 0) {
    tag.sak = _mfcRecoverTagSak;
    tag.nfcidLen = _mfcRecoverTagUidLen;
    memcpy(tag.nfcid, _mfcRecoverTagUid, _mfcRecoverTagUidLen);
  } else {
    if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true) || !isMifareClassic(tag.sak)) {
      dev.deactivate();
      if (_mfcRecoverChainActive) {
        _mfcRecoverChainActive = false;
        _resumeMfcReadAfterDict = false;
        _mfcRecoveryNext = false;
        _mfcReadAfterDict = false;
        _mfcRecoverTagUidLen = 0;
      }
      ShowStatusAction::show("Tag not supported");
      return;
    }
    if (_mfcRecoverChainActive) {
      _mfcRecoverTagUidLen = tag.nfcidLen;
      _mfcRecoverTagSak = tag.sak;
      memcpy(_mfcRecoverTagUid, tag.nfcid, tag.nfcidLen);
    }
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  String uidFile;
  for (uint8_t i = 0; i < tag.nfcidLen; ++i) {
    char h[3]; snprintf(h, sizeof(h), "%02X", tag.nfcid[i]); uidFile += h;
  }

  String savedA[40], savedB[40];
  if (!_mfcRecoverChainActive) _mfcRecoverySummary.reset();
  String keyPath = String("/unigeek/nfc/keys/") + uidFile + ".txt";
  String persisted = Uni.Storage->readFile(keyPath.c_str());
  int pos = 0;
  while (pos < (int)persisted.length()) {
    int nl = persisted.indexOf('\n', pos);
    if (nl < 0) nl = persisted.length();
    String line = persisted.substring(pos, nl); line.trim();
    int sec = -1; char kt = 0; char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sec, &kt, hex) == 3 &&
        sec >= 0 && sec < (int)sectors) {
      String key(hex); key.toUpperCase();
      if (kt == 'A' || kt == 'a') savedA[sec] = key;
      else if (kt == 'B' || kt == 'b') savedB[sec] = key;
    }
    pos = nl + 1;
  }

  int recovered = 0;
  auto formatSummary = [&](char* out, size_t capacity) {
    unsigned covered = 0;
    for (size_t sec = 0; sec < sectors; ++sec)
      if (savedA[sec].length() || savedB[sec].length()) ++covered;
    unsigned found = 0;
    for (size_t sec = 0; sec < sectors; ++sec) { if (savedA[sec].length()) ++found; if (savedB[sec].length()) ++found; }
    _mfcRecoverySummary.format(out, capacity, (unsigned)_mfcRecoverySummary.count, (unsigned)_mfcRecoverySummary.count, covered, (unsigned)sectors);
  };

  if (!sourceKeyCount) {
    if (_mfcRecoverChainActive) {
      ++_mfcRecoverChainIndex;
      if (_mfcRecoverChainIndex < 3) _mfcRecoveryNext = true;
      else {
        _mfcRecoverChainActive = false;
        _mfcRecoverTagUidLen = 0;
        _resumeMfcReadAfterDict = false;
        char finalMsg[80];
        formatSummary(finalMsg, sizeof(finalMsg));
        ShowStatusAction::show(finalMsg, 1400);
        _mfcReadAfterDict = true;
      }
      return;
    }
    ShowStatusAction::show("No valid keys");
    return;
  }

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
  if (!_mfcRecoverChainActive) {
    if (path == MfcKeyStore::kBuiltinDefaultId) snprintf(liveStatus, sizeof(liveStatus), "Default");
    else if (path == MfcKeyStore::kDiscoveredDictionary) snprintf(liveStatus, sizeof(liveStatus), "Discovered");
    else if (path == MfcKeyStore::kBuiltinExtendedId) snprintf(liveStatus, sizeof(liveStatus), "Extended");
    else { String label=path; int slash=label.lastIndexOf('/'); if(slash>=0) label=label.substring(slash+1); snprintf(liveStatus,sizeof(liveStatus),"%s",label.c_str()); }
  }
  if (_mfcRecoverChainActive) {
    static const char* const kStageNames[] = {"Default", "Discovered", "Extended"};
    Header header;
    header.render("Dictionary Attack");
    StatusBar::refresh();
    snprintf(liveStatus, sizeof(liveStatus), "%s", kStageNames[_mfcRecoverChainIndex]);
  }
  ui.status = liveStatus; ui.stage=_mfcRecoverChainActive ? (uint8_t)(_mfcRecoverChainIndex + 1U) : 0;
  actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);

  if (!_mfcRecoverChainActive) {
    for (size_t sec = 0; sec < sectors; ++sec) {
      const uint8_t trailer = (uint8_t)(sectorFirstBlock(sec) + sectorBlockCount(sec) - 1U);
      for (uint8_t kt = 0; kt < 2; ++kt) {
        String& slot = kt ? savedB[sec] : savedA[sec];
        if (!slot.length()) continue;
        uint8_t key[6];
        if (!MfcKeyStore::parseDictionaryKey(slot, key)) { slot = ""; continue; }
        Uni.update();
        if (Uni.Nav && Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
          dev.deactivate(); ShowStatusAction::show("Cancelled",1000); _openMfcDictionaries(true); return;
        }
        if (!dev.hasActiveTag()) { ST25R3916Backend::ScanResult current; if (!dev.scan(ST25R3916Backend::TECH_A,current,500,true) || !sameTag(tag,current)) { slot=""; continue; } }
        if (!dev.mifareClassicAuthenticate(trailer,key,kt==1)) slot="";
        dev.deactivate();
      }
    }
  }

  if (!_mfcRecoverChainActive) {
    ui.pct=0;
    actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);
  }

  bool objectiveMet = false;
  bool accessPromptShown = false;
  DictionaryDiscoverySummary discoveredKeys;
  const size_t totalSlots=sectors*2U;
  bool cancelled=false;
  auto authenticate=[&](size_t sec,uint8_t kt,const uint8_t key[6])->bool{
    const uint8_t trailer=(uint8_t)(sectorFirstBlock(sec)+sectorBlockCount(sec)-1U);
    if(!dev.hasActiveTag()){ST25R3916Backend::ScanResult current;if(!dev.scan(ST25R3916Backend::TECH_A,current,500,true)||!sameTag(tag,current))return false;}
    const bool ok=dev.mifareClassicAuthenticate(trailer,key,kt==1); dev.deactivate(); return ok;
  };
  auto storeKey=[&](String& slot,const uint8_t key[6]){char hex[13];snprintf(hex,sizeof(hex),"%02X%02X%02X%02X%02X%02X",key[0],key[1],key[2],key[3],key[4],key[5]);slot=hex;++recovered;_mfcRecoverySummary.add(key);};
  auto propagate=[&](const uint8_t key[6],size_t originSec,uint8_t originKt){
    size_t pending=0,done=0;
    for(size_t s=0;s<sectors;++s)for(uint8_t kt=0;kt<2;++kt)
      if(!(s==originSec&&kt==originKt)&&!(kt?savedB[s].length():savedA[s].length()))++pending;

    char keyHex[13];
    snprintf(keyHex,sizeof(keyHex),"%02X%02X%02X%02X%02X%02X",
             key[0],key[1],key[2],key[3],key[4],key[5]);
    char line[60];
    snprintf(line,sizeof(line),"S%02u %c %s recovered",(unsigned)originSec,
             originKt?'B':'A',keyHex);
    actionLog.addLine(line,TFT_GREEN);
    ui.pct=0; ui.attempt="";
    actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);

    for(size_t s=0;s<sectors&&!cancelled;++s)for(uint8_t kt=0;kt<2;++kt){
      if(s==originSec&&kt==originKt)continue;
      String& slot=kt?savedB[s]:savedA[s];
      if(slot.length())continue;
      Uni.update();
      if(Uni.Nav&&Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){
        cancelled=true;break;
      }
      ++done;
      ui.pct=pending?(int)(done*100U/pending):100;
      snprintf(line,sizeof(line),"S%02u %c %s",(unsigned)s,kt?'B':'A',keyHex);
      const bool matched=authenticate(s,kt,key);
      if(matched){
        storeKey(slot,key);
        actionLog.addLine(line,TFT_GREEN);
      }
      ui.attempt="";
      actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);
    }
    if(!cancelled){
      ui.pct=100;
      actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);
    }
  };
  DictionaryKeyProgressView::begin(liveStatus, sourceKeyCount, _mfcRecoverChainActive ? (uint8_t)(_mfcRecoverChainIndex + 1U) : 0);
  for(size_t sec=0;sec<sectors&&!cancelled&&!objectiveMet;++sec)for(uint8_t kt=0;kt<2&&!cancelled&&!objectiveMet;++kt){String& slot=kt?savedB[sec]:savedA[sec];if(slot.length())continue;
    MfcKeyStore::forEachDictionaryKey(Uni.Storage,path,[&](const uint8_t key[6],size_t keyIndex,size_t){Uni.update();if(Uni.Nav&&Uni.Nav->wasPressed()&&Uni.Nav->readDirection()==INavigation::DIR_BACK){cancelled=true;return false;}
      ui.keyIndex=keyIndex+1U; ui.keyTotal=sourceKeyCount;
      DictionaryKeyProgressView::draw(liveStatus,(uint8_t)sec,kt?'B':'A',key,keyIndex+1U,sourceKeyCount,sec*2U+kt+1U,totalSlots,false,true);
      if(!authenticate(sec,kt,key))return true;
      storeKey(slot,key);
      ui.keyIndex=keyIndex+1U; ui.keyTotal=sourceKeyCount;
      DictionaryKeyProgressView::draw(liveStatus,(uint8_t)sec,kt?'B':'A',key,keyIndex+1U,sourceKeyCount,sec*2U+kt+1U,totalSlots,true,true);
      delay(2000);
      propagate(key,sec,kt);
      if(cancelled)return false;
      discoveredKeys.note(key,kt?'B':'A');
      bool allAccessible=true, allKeysKnown=true;
      for(size_t x=0;x<sectors;++x){
        if(!savedA[x].length()&&!savedB[x].length())allAccessible=false;
        if(!savedA[x].length()||!savedB[x].length())allKeysKnown=false;
      }
      const bool canStop=allAccessible && !allKeysKnown;
      const bool finished=allKeysKnown;
      discoveredKeys.render(actionLog,sectors,[&](const String& hex)->size_t{
        size_t n=0;
        for(size_t x=0;x<sectors;++x)
          if(savedA[x].equalsIgnoreCase(hex)||savedB[x].equalsIgnoreCase(hex))++n;
        return n;
      },canStop,finished);
      ui.attempt="";
      actionLog.draw(Uni.Lcd,bodyX(),bodyY(),bodyW(),bodyH(),statusCb,&ui);
      {
        if(canStop)accessPromptShown=true;
        Uni.update();delay(80);Uni.update();
        while(true){
          Uni.update();
          if(Uni.Nav&&Uni.Nav->wasPressed()){
            const auto direction=Uni.Nav->readDirection();
            if(direction==INavigation::DIR_PRESS){
              if(_mfcRecoverChainActive)_mfcRecoverContinueMissing=true;
              break;
            }
            if(canStop&&direction==INavigation::DIR_BACK){
              if(_mfcRecoverChainActive)_mfcRecoverStopRequested=true;
              objectiveMet=true;
              break;
            }
          }
          delay(15);
        }
        if(objectiveMet)return false;
      }
      objectiveMet=finished;
      if(!objectiveMet){
        actionLog.clear();
        ui.attempt="";
        ui.pct=0;
        DictionaryKeyProgressView::begin(liveStatus, sourceKeyCount, _mfcRecoverChainActive ? (uint8_t)(_mfcRecoverChainIndex + 1U) : 0);
      }
      return false;});
  }
  if(cancelled){dev.deactivate();if(_mfcRecoverChainActive){_mfcRecoverChainActive=false;_mfcRecoverTagUidLen=0;_resumeMfcReadAfterDict=false;_mfcRecoveryNext=false;_mfcReadAfterDict=false;ShowStatusAction::show("Cancelled",1000);_showMfcTagMenu();return;}ShowStatusAction::show("Cancelled",1000);_openMfcDictionaries(true);return;}
  if(!_mfcRecoverChainActive)for(size_t sec=0;sec<sectors;++sec)for(uint8_t kt=0;kt<2;++kt){const String& slot=kt?savedB[sec]:savedA[sec];if(!slot.length()){char nf[32];snprintf(nf,sizeof(nf),"S%u %c: not found",(unsigned)sec,kt?'B':'A');actionLog.addLine(nf,TFT_RED);}}
  dev.deactivate();

  if (recovered > 0) {
    Uni.Storage->makeDir("/unigeek/nfc/keys");
    String out;
    for (size_t sec = 0; sec < sectors; ++sec) {
      if (savedA[sec].length()) out += String("S") + (sec < 10 ? "0" : "") + String((unsigned)sec) + " A " + savedA[sec] + "\n";
      if (savedB[sec].length()) out += String("S") + (sec < 10 ? "0" : "") + String((unsigned)sec) + " B " + savedB[sec] + "\n";
    }
    Uni.Storage->writeFile(keyPath.c_str(), out.c_str());
    MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, out);
  }

  ui.pct=100;
  char msg[80];
  formatSummary(msg, sizeof(msg));

  if (_mfcRecoverChainActive) {
    _mfcRecoverChainNewKeys += recovered;
    bool covered = true;
    bool allKeys = true;
    for (size_t sec = 0; sec < sectors; ++sec) {
      if (!savedA[sec].length() && !savedB[sec].length()) covered = false;
      if (!savedA[sec].length() || !savedB[sec].length()) allKeys = false;
    }
    ++_mfcRecoverChainIndex;
    if ((!covered || (_mfcRecoverContinueMissing && !allKeys && !_mfcRecoverStopRequested)) && _mfcRecoverChainIndex < 3) {
      Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      _mfcRecoveryNext = true;
    } else {
      _mfcRecoverChainActive = false;
      _mfcRecoverTagUidLen = 0;
      _resumeMfcReadAfterDict = false;
      char finalMsg[80];
      formatSummary(finalMsg, sizeof(finalMsg));
      ShowStatusAction::show(finalMsg, 1400);
      _mfcReadAfterDict = true;
    }
  } else if (_resumeMfcReadAfterDict) {
    _resumeMfcReadAfterDict = false;
    _mfcReadAfterDict = true;
  } else {
    ShowStatusAction::show(msg, 1400);
    _openMfcDictionaries(true);
  }
#endif
}

MagicCardType ST25R3916Screen::_detectMagicType(ST25R3916Backend& dev, void (*progress)(uint8_t)) {
  auto report = [&](uint8_t percent) { if (progress) progress(percent); };
  report(10);
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend::ScanResult tag;
  report(20);
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 900, true) || !isMifareClassic(tag.sak)) {
    dev.deactivate();
    return MagicCardType::NONE;
  }

  {
    const uint8_t cmd[2] = {0x30, 0x00};
    uint8_t rx[20] = {};
    size_t len = 0;
    if (dev.nfcATransceive(cmd, sizeof(cmd), rx, sizeof(rx), len, 120) && len >= 16) {
      report(100);
      dev.deactivate();
      return MagicCardType::GEN3;
    }
  }

  report(45);
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    report((uint8_t)(55 + attempt * 20));
    dev.deactivate();
    delay(10);
    if (!dev.scan(ST25R3916Backend::TECH_A, tag, 800, true)) continue;

    const uint8_t halt[2] = {0x50, 0x00};
    uint8_t ignored[4] = {};
    size_t ignoredLen = 0;
    (void)dev.nfcATransceive(halt, sizeof(halt), ignored, sizeof(ignored), ignoredLen, 80);
    delay(2);

    uint8_t wake = 0x40;
    uint8_t ack[2] = {};
    size_t bits = 0;
    const bool wakeOk = dev.nfcATransceiveBits(&wake, 7, ack, 4, bits, 250) &&
                        bits >= 4 && (ack[0] & 0x0F) == 0x0A;
    report((uint8_t)(65 + attempt * 20));
    if (!wakeOk) continue;

    uint8_t unlock = 0x43;
    memset(ack, 0, sizeof(ack));
    bits = 0;
    const bool unlockOk = dev.nfcATransceiveBits(&unlock, 8, ack, 4, bits, 250) &&
                          bits >= 4 && (ack[0] & 0x0F) == 0x0A;
    if (unlockOk) {
      report(100);
      dev.deactivate();
      return MagicCardType::GEN1A;
    }
  }
  report(100);
  dev.deactivate();
#endif
  return MagicCardType::NONE;
}

void ST25R3916Screen::_detectMagic() {
  _state = STATE_MAGIC_DETECT;
  _magicUiPhase = MAGIC_WAITING;
  _magicResult = "";
  render();
  _runDetectMagic();
}

void ST25R3916Screen::_runDetectMagic() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    _magicResult = "Tag not detected";
    _magicUiPhase = MAGIC_RESULT;
    render();
    return;
  }
  ST25R3916Backend::ScanResult tag;
  const auto waitResult = st25WaitForTag(dev, ST25R3916Backend::TECH_A, tag, 5000, true);
  if (waitResult != St25WaitResult::FOUND) {
    if (waitResult == St25WaitResult::CANCELLED) {
      _showMfcTagMenu();
      return;
    }
    _magicResult = "Tag not detected";
    _magicUiPhase = MAGIC_RESULT;
    render();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    _magicResult = "Magic not detected";
    _magicUiPhase = MAGIC_RESULT;
    render();
    return;
  }

  _magicUiPhase = MAGIC_SCANNING;
  ProgressView::init();
  ProgressView::progress("Scanning...", 0);
  const MagicCardType mt = _detectMagicType(dev, [](uint8_t p) { ProgressView::progress("Scanning...", p); });
  ProgressView::finish();
  _magicResult = mt == MagicCardType::GEN1A ? "Gen1A" :
                 mt == MagicCardType::GEN3 ? "Gen3" : "Magic not detected";
  _magicUiPhase = MAGIC_RESULT;
  render();
#else
  _magicResult = "Tag not detected";
  _magicUiPhase = MAGIC_RESULT;
  render();
#endif
}

static uint16_t st25CrcA(const uint8_t* data, size_t len) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < len; ++i) {
    uint8_t d = data[i] ^ (uint8_t)(crc & 0x00FF);
    d ^= (uint8_t)(d << 4);
    crc = (uint16_t)((crc >> 8) ^ ((uint16_t)d << 8) ^ ((uint16_t)d << 3) ^ ((uint16_t)d >> 4));
  }
  return crc;
}

bool ST25R3916Screen::_writeMagicUid(ST25R3916Backend& dev, MagicCardType type,
                                      const uint8_t* uid, uint8_t uidLen,
                                      const uint8_t block0[16]) {
#if defined(DEVICE_HAS_ST25R3916)
  if (!uid || (uidLen != 4 && uidLen != 7)) return false;
  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 900, true)) return false;

  if (type == MagicCardType::GEN3) {
    uint8_t cmd[13] = {0x90, 0xFB, 0xCC, 0xCC, uidLen};
    memcpy(cmd + 5, uid, uidLen);
    cmd[5 + uidLen] = 0x00;
    uint8_t rx[8] = {};
    size_t rxLen = 0;
    const bool ok = dev.nfcATransceive(cmd, 6U + uidLen, rx, sizeof(rx), rxLen, 500);
    dev.deactivate();
    if (!ok) return false;
  } else if (type == MagicCardType::GEN1A) {
    if (uidLen != 4 || !block0) { dev.deactivate(); return false; }
    const uint8_t halt[2] = {0x50, 0x00};
    uint8_t scratch[4] = {}; size_t scratchLen = 0;
    (void)dev.nfcATransceive(halt, sizeof(halt), scratch, sizeof(scratch), scratchLen, 100);
    uint8_t ack[2] = {}; size_t bits = 0; uint8_t wake = 0x40;
    if (!dev.nfcATransceiveBits(&wake, 7, ack, 4, bits, 250) || bits < 4 || (ack[0] & 0x0F) != 0x0A) { dev.deactivate(); return false; }
    uint8_t unlock = 0x43; memset(ack,0,sizeof(ack)); bits=0;
    if (!dev.nfcATransceiveBits(&unlock, 8, ack, 4, bits, 250) || bits < 4 || (ack[0] & 0x0F) != 0x0A) { dev.deactivate(); return false; }

    uint8_t writeCmd[4] = {0xA0, 0x00, 0, 0};
    uint16_t crc = st25CrcA(writeCmd, 2); writeCmd[2]=(uint8_t)crc; writeCmd[3]=(uint8_t)(crc>>8);
    memset(ack,0,sizeof(ack)); bits=0;
    if (!dev.nfcATransceiveBits(writeCmd, 32, ack, 4, bits, 400) || bits < 4 || (ack[0]&0x0F)!=0x0A) { dev.deactivate(); return false; }

    uint8_t data[18] = {}; memcpy(data, block0, 16); memcpy(data, uid, 4);
    data[4] = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
    crc = st25CrcA(data, 16); data[16]=(uint8_t)crc; data[17]=(uint8_t)(crc>>8);
    memset(ack,0,sizeof(ack)); bits=0;
    if (!dev.nfcATransceiveBits(data, 144, ack, 4, bits, 700) || bits < 4 || (ack[0]&0x0F)!=0x0A) { dev.deactivate(); return false; }
    dev.deactivate();
  } else { dev.deactivate(); return false; }

  for (uint8_t attempt=0; attempt<3; ++attempt) {
    ST25R3916Backend::ScanResult verify;
    if (dev.scan(ST25R3916Backend::TECH_A, verify, 500, false) &&
        verify.nfcidLen == uidLen && memcmp(verify.nfcid, uid, uidLen)==0) return true;
    delay(30);
  }
#endif
  return false;
}

void ST25R3916Screen::_readMfcMemory() {
#if defined(DEVICE_HAS_ST25R3916)
  _readMfcTag();
  if (_state == STATE_MFC_DETAILS && _mfcDumpLen && _mfcDumpBlocks) {
    const char* type = _mfcDumpBlocks == 20 ? "MF Classic Mini" :
                       _mfcDumpBlocks == 256 ? "MF Classic 4K" : "MF Classic 1K";
    Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::MIFARE_CLASSIC, type,
                                        _mfcUid, _mfcUidLen, _mfcDump, _mfcDumpLen));
    return;
  }
#endif
  _showMfcAdvancedMenu();
}

void ST25R3916Screen::_editMfcMemory() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  if (!st25Begin(dev, _interface)) {
    ShowStatusAction::show("ST25R3916 not detected");
    _showMfcAdvancedMenu();
    return;
  }

  ST25R3916Backend::ScanResult tag;
  if (!dev.scan(ST25R3916Backend::TECH_A, tag, 5000, true)) {
    ShowStatusAction::show("Tag not detected");
    _showMfcAdvancedMenu();
    return;
  }
  if (!isMifareClassic(tag.sak)) {
    dev.deactivate();
    ShowStatusAction::show("Tag not supported");
    _showMfcAdvancedMenu();
    return;
  }

  size_t sectors = 0, blocks = 0;
  mfcDimensions(tag.sak, sectors, blocks);
  if (!blocks || blocks > 256) {
    dev.deactivate();
    ShowStatusAction::show("Tag not supported");
    _showMfcAdvancedMenu();
    return;
  }

  const int block = InputNumberAction::popup(
      (String("Block (1..") + String((unsigned)(blocks - 1)) + ")").c_str(),
      1, (int)blocks - 1, 1);
  if (InputNumberAction::wasCancelled()) {
    dev.deactivate();
    _showMfcAdvancedMenu();
    return;
  }

  String hex = InputTextAction::popup("Block data (32 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) {
    dev.deactivate();
    _showMfcAdvancedMenu();
    return;
  }
  hex.replace(" ", "");
  hex.replace(":", "");
  if (hex.length() != 32) {
    dev.deactivate();
    _showMfcAdvancedMenu();
    ShowStatusAction::show("Need 32 hex chars");
    return;
  }

  uint8_t data[16] = {};
  for (uint8_t i = 0; i < 16; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    unsigned long value = strtoul(b, &end, 16);
    if (!end || *end) {
      dev.deactivate();
      _showMfcAdvancedMenu();
      ShowStatusAction::show("Bad hex");
      return;
    }
    data[i] = (uint8_t)value;
  }

  const uint8_t sector = block < 128 ? (uint8_t)(block / 4) : (uint8_t)(32 + (block - 128) / 16);
  const uint8_t trailer = (uint8_t)(sectorFirstBlock(sector) + sectorBlockCount(sector) - 1U);
  const auto defaults = NFCUtility::getDefaultKeys();

  auto reactivate = [&]() -> bool {
    dev.deactivate();
    ST25R3916Backend::ScanResult current;
    return dev.scan(ST25R3916Backend::TECH_A, current, 350, true) &&
           isMifareClassic(current.sak) && sameTag(tag, current);
  };

  bool ok = false;
  for (uint8_t keyType = 0; keyType < 2 && !ok; ++keyType) {
    for (const auto& candidate : defaults) {
      if (!dev.hasActiveTag() && !reactivate()) continue;
      if (!dev.mifareClassicAuthenticate(trailer, candidate.value().data(), keyType == 1)) {
        if (!reactivate() ||
            !dev.mifareClassicAuthenticate(trailer, candidate.value().data(), keyType == 1)) {
          dev.deactivate();
          continue;
        }
      }
      ok = dev.mifareClassicWriteBlock((uint8_t)block, data);
      dev.deactivate();
      if (ok) break;
    }
  }

  _showMfcAdvancedMenu();
  ShowStatusAction::show(ok ? "Block written" : "Key not available", 1600);
#else
  _showMfcAdvancedMenu();
#endif
}

void ST25R3916Screen::_lockMfcUidGen3() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev; if(!st25Begin(dev, _interface)){ShowStatusAction::show("ST25R3916 not detected");_showMfcAdvancedMenu();return;}
  if(_detectMagicType(dev)!=MagicCardType::GEN3){ShowStatusAction::show("Tag is not Gen3");_showMfcAdvancedMenu();return;}
  static const InputSelectAction::Option opts[]={{"Lock UID permanently","lock"}};
  const char* choice=InputSelectAction::popup("Permanent UID lock",opts,1,nullptr); if(!choice||strcmp(choice,"lock")!=0){_showMfcAdvancedMenu();return;}
  ST25R3916Backend::ScanResult tag; if(!dev.scan(ST25R3916Backend::TECH_A,tag,1000,true)){ShowStatusAction::show("Tag lost");_showMfcAdvancedMenu();return;}
  const uint8_t cmd[]={0x90,0xFD,0x11,0x11,0x00}; uint8_t rx[8]={}; size_t len=0; const bool ok=dev.nfcATransceive(cmd,sizeof(cmd),rx,sizeof(rx),len,500); dev.deactivate();
  ShowStatusAction::show(ok?"Gen3 UID locked":"Failed",1600);
#endif
  _showMfcAdvancedMenu();
}

const char* ST25R3916Screen::_expFamilyTitle() const {
  switch (_expFamily) {
    case EXP_DESFIRE: return "DESFire (experimental)";
    case EXP_NFCV: return "ICODE / ST25V (experimental)";
    case EXP_FELICA: return "FeliCa (experimental)";
    case EXP_TYPE4B: return "Type 4B (experimental)";
    case EXP_TYPE4A: return "Type 4A (experimental)";
    default: return "Experimental";
  }
}

const char* ST25R3916Screen::_expSub1Title() const {
  if (_expFamily == EXP_DESFIRE) return "Applications";
  if (_expFamily == EXP_FELICA) return "Systems";
  return "Details";
}

const char* ST25R3916Screen::_expSub2Title() const {
  if (_expFamily == EXP_DESFIRE) return "Files";
  if (_expFamily == EXP_FELICA) return "Services";
  return "Details";
}

void ST25R3916Screen::_showExperimentalMenu(ExperimentalFamily family) {
  _expFamily = family;
  _ndefExperimentalTarget = false;
  _state = STATE_EXP_MENU;
  setItems(_expRootItems, 2, _selExp);
  render();
}

void ST25R3916Screen::_showExperimentalTagMenu() {
  if (_genericReadActive) { _genericReadActive = false; _showMenu(); return; }
  _ndefExperimentalTarget = false;
  _state = STATE_EXP_TAG_MENU;
  if (_expFamily == EXP_DESFIRE) setItems(_expDesfireTagItems, 4, _selExpTag);
  else if (_expFamily == EXP_NFCV) setItems(_expNfcvTagItems, 4, _selExpTag);
  else if (_expFamily == EXP_FELICA) setItems(_expFelicaTagItems, 4, _selExpTag);
  else if (_expFamily == EXP_TYPE4B) setItems(_expType4bTagItems, 2, _selExpTag);
  else if (_expFamily == EXP_TYPE4A) setItems(_expType4aTagItems, 2, _selExpTag);
  render();
}

void ST25R3916Screen::_showExperimentalAdvancedMenu() {
  _state = STATE_EXP_ADVANCED_MENU;
  if (_expFamily == EXP_DESFIRE) setItems(_expDesfireAdvancedItems, 2, _selExpAdvanced);
  else if (_expFamily == EXP_NFCV) setItems(_expNfcvAdvancedItems, 5, _selExpAdvanced);
  else if (_expFamily == EXP_FELICA) setItems(_expFelicaAdvancedItems, 3, _selExpAdvanced);
  else if (_expFamily == EXP_TYPE4B) setItems(_expType4bAdvancedItems, 2, _selExpAdvanced);
  else if (_expFamily == EXP_TYPE4A) setItems(_expType4aAdvancedItems, 1, _selExpAdvanced);
  render();
}

void ST25R3916Screen::_showExperimentalSub1Menu() {
  _state = STATE_EXP_SUB1_MENU;
  if (_expFamily == EXP_DESFIRE) setItems(_expDesfireAppItems, 3, _selExpSub1);
  else if (_expFamily == EXP_FELICA) setItems(_expFelicaSystemItems, 1, _selExpSub1);
  render();
}

void ST25R3916Screen::_showExperimentalSub2Menu() {
  _state = STATE_EXP_SUB2_MENU;
  if (_expFamily == EXP_DESFIRE) setItems(_expDesfireFileItems, 4, _selExpSub2);
  else if (_expFamily == EXP_FELICA) setItems(_expFelicaServiceItems, 3, _selExpSub2);
  render();
}

void ST25R3916Screen::_showExperimentalNdefMenu() {
  _ndefExperimentalTarget = true;
  _ndefMfuTarget = false;
  _ndefWritePreview = false;
  _ndefWritePreviewFromFile = false;
  _state = STATE_EXP_NDEF_MENU;
  if (_expFamily == EXP_TYPE4A) setItems(_expType4aNdefItems, 1, 0);
  else setItems(_expNdefItems, 4, _selExpNdef);
  render();
}

void ST25R3916Screen::_showExperimentalNdefWriteMenu() {
  _ndefExperimentalTarget = true;
  _ndefMfuTarget = false;
  _ndefWritePreview = false;
  _ndefWritePreviewFromFile = false;
  _state = STATE_EXP_NDEF_WRITE_MENU;
  setItems(_mfcNdefWriteItems, 6, _selExpNdefWrite);
  render();
}

void ST25R3916Screen::_returnToNdefWriteMenu() {
  if (_ndefExperimentalTarget) _showExperimentalNdefWriteMenu();
  else if (_ndefMfuTarget) _showMfuNdefWriteMenu();
  else _showMfcNdefWriteMenu();
}

void ST25R3916Screen::_showExperimentalHex(const char* title, const uint8_t* data, size_t len) {
  _rowCount = 0;
  _advancedOperationTitle = title ? title : "Data";
  for (size_t off = 0; off < len && _rowCount < kMaxRows; off += 16) {
    String v;
    const size_t take = min((size_t)16, len - off);
    for (size_t i = 0; i < take; ++i) {
      char h[4]; snprintf(h, sizeof(h), "%s%02X", i ? " " : "", data[off+i]); v += h;
    }
    char lbl[12]; snprintf(lbl, sizeof(lbl), "%04X", (unsigned)off);
    _rowLabels[_rowCount] = lbl; _rowValues[_rowCount] = v;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
  }
  if (!len && _rowCount < kMaxRows) {
    _rowLabels[_rowCount] = "Data"; _rowValues[_rowCount] = "Empty";
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]}; ++_rowCount;
  }
  _scrollView.resetScroll(); _scrollView.setRows(_rows, _rowCount);
  _state = STATE_EXP_RESULT; render();
}

void ST25R3916Screen::_experimentalReadTag() {
#if defined(DEVICE_HAS_ST25R3916)
  _state = STATE_EXP_WORKING; _advancedOperationTitle = "Read Tag"; render();
  ST25R3916Backend dev; if (!st25Begin(dev, _interface)) { _showStatusAndReturn("ST25R3916 not detected", STATE_EXP_TAG_MENU, 1600); return; }
  uint16_t mask = _expFamily == EXP_NFCV ? ST25R3916Backend::TECH_V : _expFamily == EXP_FELICA ? ST25R3916Backend::TECH_F : ST25R3916Backend::TECH_A;
  ST25R3916Backend::ScanResult tag;
  if (_expFamily == EXP_TYPE4B) {
    const auto scanResult = st25ScanType4b(dev, tag, true);
    if (scanResult != St25Type4bScanResult::FOUND) {
      st25ShowType4bScanError(scanResult);
      _showExperimentalTagMenu();
      return;
    }
  } else {
    const auto waitResult = st25WaitForTag(dev, mask, tag, 5000, true);
    if (waitResult != St25WaitResult::FOUND) {
      if (waitResult == St25WaitResult::TIMEOUT) ShowStatusAction::show("Tag not detected", 1600);
      _showExperimentalTagMenu(); return;
    }
  }
  if (!_matchesGenericReadTag(tag.nfcid, tag.nfcidLen)) {
    dev.deactivate();
    _showStatusAndReturn("Different tag detected", STATE_MENU, 1600);
    return;
  }
  if ((_expFamily == EXP_DESFIRE && (tag.technology != ST25R3916Backend::Technology::NFC_A || !tag.isoDep)) ||
      (_expFamily == EXP_TYPE4A && (tag.technology != ST25R3916Backend::Technology::NFC_A || !tag.isoDep)) ||
      (_expFamily == EXP_NFCV && tag.technology != ST25R3916Backend::Technology::NFC_V) ||
      (_expFamily == EXP_FELICA && tag.technology != ST25R3916Backend::Technology::NFC_F)) {
    dev.deactivate(); _showStatusAndReturn("Tag not supported", STATE_EXP_TAG_MENU, 1600); return;
  }
  if (_expFamily == EXP_DESFIRE && !ST25R3916Experimental::desfireProbe(dev)) {
    dev.deactivate(); _showStatusAndReturn("Tag not supported", STATE_EXP_TAG_MENU, 1600); return;
  }
  _rowCount = 0;
  auto add=[&](const char* l,const String& v){ if(_rowCount>=kMaxRows)return; _rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount; };
  String id; for(uint8_t i=0;i<tag.nfcidLen;++i){char h[4];snprintf(h,sizeof(h),"%s%02X",i?":":"",tag.nfcid[i]);id+=h;} if(!id.length())id="--";
  if (_expFamily == EXP_TYPE4A) {
    add("Type", "Type 4A"); add("UID", id);
    char a[8]; snprintf(a,sizeof(a),"%02X:%02X",tag.atqa[0],tag.atqa[1]); add("ATQA",a);
    char sk[6]; snprintf(sk,sizeof(sk),"0x%02X",tag.sak); add("SAK",sk); add("ISO-DEP",tag.isoDep?"Yes":"No");
  } else if (_expFamily == EXP_DESFIRE) {
    add("Type", "DESFire / Type 4A"); add("UID", id);
    char a[8]; snprintf(a,sizeof(a),"%02X:%02X",tag.atqa[0],tag.atqa[1]); add("ATQA",a);
    char sk[6]; snprintf(sk,sizeof(sk),"0x%02X",tag.sak); add("SAK",sk); add("ISO-DEP",tag.isoDep?"Yes":"No");
    uint8_t v[96]={};size_t vn=0;uint8_t st=0xFF;
    if(ST25R3916Experimental::desfireExchange(dev,0x60,nullptr,0,v,sizeof(v),vn,st)){ add("GetVersion",String((unsigned)vn)+" bytes"); }
    uint8_t apps[192]={};size_t an=0;
    if(ST25R3916Experimental::desfireExchange(dev,0x6A,nullptr,0,apps,sizeof(apps),an,st)) add("Applications",String((unsigned)(an/3)));
  } else if (_expFamily == EXP_NFCV) {
    add("Type", "ICODE / ST25V"); add("UID", id);
    ST25R3916Experimental::TypeVInfo info;
    if(ST25R3916Experimental::typeVGetSystemInfo(dev,tag,info)){ if(info.hasDsfid){char h[6];snprintf(h,sizeof(h),"0x%02X",info.dsfid);add("DSFID",h);} if(info.hasAfi){char h[6];snprintf(h,sizeof(h),"0x%02X",info.afi);add("AFI",h);} add("Blocks",String(info.blocks));add("Block Size",String(info.blockSize)+" bytes");add("Memory",String((unsigned)((size_t)info.blocks*info.blockSize))+" bytes"); }
  } else if (_expFamily == EXP_FELICA) {
    add("Type", "FeliCa / NFC-F"); add("IDm", id);
    uint16_t sys[16]={};size_t count=0;if(ST25R3916Experimental::felicaRequestSystemCodes(dev,tag,sys,16,count)){add("Systems",String((unsigned)count));if(count){char h[8];snprintf(h,sizeof(h),"0x%04X",sys[0]);add("System Code",h);}}
  } else {
    add("Type", "Type 4B"); add("PUPI / NFCID", id); add("ISO-DEP",tag.isoDep?"Yes":"No"); add("Protocol", "ISO14443B");
  }
  dev.deactivate(); _scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_DETAILS;render();
#endif
}

void ST25R3916Screen::_experimentalDesfireAction(uint8_t group, uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_EXP_WORKING; _advancedOperationTitle = group==1?"Applications":group==2?"Files":"Advanced"; render(); _renderTagPrompt();
  ST25R3916Backend dev; if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_TAG_MENU, 1600); return;}
  ST25R3916Backend::ScanResult tag;const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_A,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalTagMenu();return;}if(tag.technology!=ST25R3916Backend::Technology::NFC_A||!tag.isoDep){_showStatusAndReturn("Tag not supported",STATE_EXP_TAG_MENU,1600);return;}
  if(!ST25R3916Experimental::desfireProbe(dev)){dev.deactivate();_showStatusAndReturn("Tag not supported", STATE_EXP_TAG_MENU, 1600); return;}
  auto selectAid=[&](){if(!_expDesfireAidSelected)return true;uint8_t out[8]={};size_t n=0;uint8_t st=0;return ST25R3916Experimental::desfireExchange(dev,0x5A,_expDesfireAid,3,out,sizeof(out),n,st);};
  auto showData=[&](const char* title,const uint8_t* d,size_t n){_expResultReturn=(group==1?STATE_EXP_SUB1_MENU:group==2?STATE_EXP_SUB2_MENU:STATE_EXP_ADVANCED_MENU);dev.deactivate();_showExperimentalHex(title,d,n);};
  static uint8_t out[512]; memset(out,0,sizeof(out));size_t n=0;uint8_t st=0xFF;
  if(group==1 && index==0){
    if(!ST25R3916Experimental::desfireExchange(dev,0x6A,nullptr,0,out,sizeof(out),n,st)){_showStatusAndReturn("Failed", STATE_EXP_SUB1_MENU, 1600); return;}
    _rowCount=0;for(size_t i=0;i+2<n&&_rowCount<kMaxRows;i+=3){char l[12];snprintf(l,sizeof(l),"AID %u",(unsigned)(i/3));char v[12];snprintf(v,sizeof(v),"%02X%02X%02X",out[i],out[i+1],out[i+2]);_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}dev.deactivate();_expResultReturn=STATE_EXP_SUB1_MENU;_advancedOperationTitle="Applications";_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_RESULT;render();return;
  }
  if(group==1 && index==1){
    String h=InputTextAction::popup("Application AID (6 hex)",_expDesfireAidSelected?st25Hex(_expDesfireAid,3).c_str():"",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalSub1Menu();return;}uint8_t aid[3];size_t al=0;if(!st25ParseHex(h,aid,sizeof(aid),al)||al!=3){dev.deactivate();_showStatusAndReturn("Need 6 hex chars", STATE_EXP_SUB1_MENU, 1600); return;}if(!ST25R3916Experimental::desfireExchange(dev,0x5A,aid,3,out,sizeof(out),n,st)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB1_MENU, 1600); return;}memcpy(_expDesfireAid,aid,3);_expDesfireAidSelected=true;dev.deactivate();_showStatusAndReturn("Application selected", STATE_EXP_SUB1_MENU, 1600); return;
  }
  if(group==1 && index==2){
    if(!selectAid()){dev.deactivate();_showStatusAndReturn("Failed to select application", STATE_EXP_SUB1_MENU, 1600); return;}bool ok=ST25R3916Experimental::desfireExchange(dev,0x45,nullptr,0,out,sizeof(out),n,st);if(!ok){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB1_MENU, 1600); return;}showData("Application Details",out,n);return;
  }
  if(group==2){
    if(!_expDesfireAidSelected){dev.deactivate();_showStatusAndReturn("Select application first", STATE_EXP_SUB2_MENU, 1600);return;}
    if(!selectAid()){dev.deactivate();_showStatusAndReturn("Selected application unavailable", STATE_EXP_SUB2_MENU, 1600);return;}
    if(index==0){if(!ST25R3916Experimental::desfireExchange(dev,0x6F,nullptr,0,out,sizeof(out),n,st)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB2_MENU, 1600); return;}showData("File List",out,n);return;}
    int fileNo=InputNumberAction::popup("File number",0,31,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}
    if(index==3){uint8_t f=(uint8_t)fileNo;if(!ST25R3916Experimental::desfireExchange(dev,0xF5,&f,1,out,sizeof(out),n,st)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB2_MENU, 1600); return;}showData("File Details",out,n);return;}
    if(index==1){int off=InputNumberAction::popup("Offset",0,0xFFFF,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}int len=InputNumberAction::popup("Length",1,256,16);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}uint8_t p[7]={(uint8_t)fileNo,(uint8_t)off,(uint8_t)(off>>8),(uint8_t)(off>>16),(uint8_t)len,(uint8_t)(len>>8),(uint8_t)(len>>16)};if(!ST25R3916Experimental::desfireExchange(dev,0xBD,p,7,out,sizeof(out),n,st)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB2_MENU, 1600); return;}showData("File Data",out,n);return;}
    String h=InputTextAction::popup("File data (hex)","",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}uint8_t data[128];size_t dl=0;if(!st25ParseHex(h,data,sizeof(data),dl)||!dl){dev.deactivate();_showStatusAndReturn("Invalid hex data", STATE_EXP_SUB2_MENU, 1600); return;}int off=InputNumberAction::popup("Offset",0,0xFFFF,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}uint8_t p[7+128]={(uint8_t)fileNo,(uint8_t)off,(uint8_t)(off>>8),(uint8_t)(off>>16),(uint8_t)dl,(uint8_t)(dl>>8),(uint8_t)(dl>>16)};memcpy(p+7,data,dl);bool ok=ST25R3916Experimental::desfireExchange(dev,0x3D,p,7+dl,out,sizeof(out),n,st);dev.deactivate();_showStatusAndReturn(ok?"File written":"Failed", STATE_EXP_SUB2_MENU, 1600); return;
  }
  if(group==3 && index==0){
    int keyNo=InputNumberAction::popup("AES key number",0,13,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}String h=InputTextAction::popup("AES-128 key (32 hex)","00000000000000000000000000000000",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t key[16];size_t kl=0;if(!st25ParseHex(h,key,sizeof(key),kl)||kl!=16){dev.deactivate();_showStatusAndReturn("Need 32 hex chars", STATE_EXP_ADVANCED_MENU, 1600); return;}if(!selectAid()){dev.deactivate();_showStatusAndReturn("Failed to select application", STATE_EXP_ADVANCED_MENU, 1600); return;}bool ok=ST25R3916Experimental::desfireAuthenticateAes(dev,(uint8_t)keyNo,key);dev.deactivate();_showStatusAndReturn(ok?"Authenticated":"Authentication failed", STATE_EXP_ADVANCED_MENU, 1600); return;
  }
  dev.deactivate(); _experimentalSendApdu(true);
#endif
}

void ST25R3916Screen::_experimentalNfcvAction(uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_EXP_WORKING;_advancedOperationTitle=index==0?"Read Memory":index==1?"Edit Memory":index==2?"Security Status":index==3?"Password":index==4?"Lock Block":index==10?"Write to Tag":"Erase Tag";render();_renderTagPrompt();
  ST25R3916Backend dev;if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_TAG_MENU, 1600); return;}ST25R3916Backend::ScanResult tag;const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_V,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalTagMenu();return;}if(tag.technology!=ST25R3916Backend::Technology::NFC_V){_showStatusAndReturn("Tag not detected",STATE_EXP_TAG_MENU,1600);return;}ST25R3916Experimental::TypeVInfo info;if(!ST25R3916Experimental::typeVGetSystemInfo(dev,tag,info)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_TAG_MENU, 1600); return;}
  if(index==0){_expResultReturn=STATE_EXP_ADVANCED_MENU;_rowCount=0;for(uint16_t b=0;b<info.blocks&&_rowCount<kMaxRows;++b){uint8_t d[32]={};size_t n=0;if(!ST25R3916Experimental::typeVReadBlock(dev,tag,b,d,sizeof(d),n)){dev.deactivate();char err[40];snprintf(err,sizeof(err),"Failed to read block %u",(unsigned)b);_showStatusAndReturn(err, STATE_EXP_ADVANCED_MENU, 1800);return;}String v;for(size_t i=0;i<n;++i){char h[4];snprintf(h,sizeof(h),"%s%02X",i?" ":"",d[i]);v+=h;}char l[12];snprintf(l,sizeof(l),"Block %u",b);_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}dev.deactivate();_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_RESULT;render();return;}
  if(index==1){int b=InputNumberAction::popup("Block",0,info.blocks-1,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t old[32]={};size_t n=0;if(!ST25R3916Experimental::typeVReadBlock(dev,tag,b,old,sizeof(old),n)){dev.deactivate();char err[40];snprintf(err,sizeof(err),"Failed to read block %u",(unsigned)b);_showStatusAndReturn(err, STATE_EXP_ADVANCED_MENU, 1800); return;}String h=InputTextAction::popup("Block data (hex)",st25Hex(old,n).c_str(),InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t d[32];size_t dl=0;if(!st25ParseHex(h,d,sizeof(d),dl)||dl!=n){dev.deactivate();_showStatusAndReturn((String("Need ")+String((unsigned)n*2)+" hex chars").c_str(), STATE_EXP_ADVANCED_MENU, 1600); return;}bool ok=ST25R3916Experimental::typeVWriteBlock(dev,tag,b,d,dl);dev.deactivate();_showStatusAndReturn(ok?"Block written":"Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}
  if(index==2){int b=InputNumberAction::popup("Block",0,(int)info.blocks-1,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t status=0;if(!ST25R3916Experimental::typeVSecurityStatus(dev,tag,b,status)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}dev.deactivate();char m[24];snprintf(m,sizeof(m),"Security: 0x%02X",status);_showStatusAndReturn(m, STATE_EXP_ADVANCED_MENU, 1800); return;}
  if(index==3){
    const uint8_t mfg = tag.nfcidLen >= 8 ? tag.nfcid[6] : 0x00;
    if(mfg==0x04){
      String h=InputTextAction::popup("ICODE password (8 hex)","00000000",InputTextAction::INPUT_HEX);
      if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}
      uint8_t pwd[4];size_t pl=0;
      if(!st25ParseHex(h,pwd,sizeof(pwd),pl)||pl!=4){dev.deactivate();_showStatusAndReturn("Need 8 hex chars", STATE_EXP_ADVANCED_MENU, 1600); return;}
      int pid=InputNumberAction::popup("Password ID",1,31,1);
      if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}
      bool ok=ST25R3916Experimental::typeVPresentIcodePassword(dev,tag,(uint8_t)pid,pwd);
      dev.deactivate();_showStatusAndReturn(ok?"Authenticated":"Authentication failed", STATE_EXP_ADVANCED_MENU, 1600); return;
    }
    if(mfg==0x02){
      String h=InputTextAction::popup("ST25V password (8/16 hex)","00000000",InputTextAction::INPUT_HEX);
      if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}
      uint8_t pwd[8];size_t pl=0;
      if(!st25ParseHex(h,pwd,sizeof(pwd),pl)||(pl!=4&&pl!=8)){dev.deactivate();_showStatusAndReturn("Need 8 or 16 hex chars", STATE_EXP_ADVANCED_MENU, 1600); return;}
      int pid=InputNumberAction::popup("Password ID",0,3,1);
      if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}
      bool ok=ST25R3916Experimental::typeVPresentStPassword(dev,tag,(uint8_t)pid,pwd,pl);
      dev.deactivate();_showStatusAndReturn(ok?"Authenticated":"Authentication failed", STATE_EXP_ADVANCED_MENU, 1600); return;
    }
    dev.deactivate();_showStatusAndReturn("Password not supported", STATE_EXP_ADVANCED_MENU, 1600); return;
  }
  if(index==4){int b=InputNumberAction::popup("Block to lock",0,info.blocks-1,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}static const InputSelectAction::Option o[]={{"Lock permanently","lock"}};const char*c=InputSelectAction::popup("Permanent operation",o,1,nullptr);if(!c){dev.deactivate();_showExperimentalAdvancedMenu();return;}bool ok=ST25R3916Experimental::typeVLockBlock(dev,tag,b);dev.deactivate();_showStatusAndReturn(ok?"Block locked":"Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}
  if(index==10){int start=InputNumberAction::popup("Start block",0,info.blocks-1,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalTagMenu();return;}String h=InputTextAction::popup("Data (hex, block aligned)","",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalTagMenu();return;}uint8_t d[256];size_t dl=0;if(!st25ParseHex(h,d,sizeof(d),dl)||!dl||dl%info.blockSize){dev.deactivate();_showStatusAndReturn("Data must align to blocks", STATE_EXP_TAG_MENU, 1600); return;}bool ok=true;for(size_t off=0;off<dl&&ok;off+=info.blockSize){uint16_t b=(uint16_t)(start+off/info.blockSize);if(b>=info.blocks){ok=false;break;}ok=ST25R3916Experimental::typeVWriteBlock(dev,tag,b,d+off,info.blockSize);}dev.deactivate();_showStatusAndReturn(ok?"Written":"Failed", STATE_EXP_TAG_MENU, 1600); return;}
  static const InputSelectAction::Option o[]={{"Erase user data","erase"}};const char*c=InputSelectAction::popup("Preserve CC/control TLVs?",o,1,nullptr);if(!c){dev.deactivate();_showExperimentalTagMenu();return;}size_t erasedCapacity=0;bool ok=ST25R3916Experimental::typeVEraseTagSafe(dev,tag,erasedCapacity);dev.deactivate();_showStatusAndReturn(ok?"Tag data erased":"Safe erase not supported", STATE_EXP_TAG_MENU, 1600);
#endif
}

void ST25R3916Screen::_experimentalFelicaAction(uint8_t group, uint8_t index) {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_EXP_WORKING;_advancedOperationTitle=group==1?"Systems":group==2?"Services":"Advanced";render();_renderTagPrompt();ST25R3916Backend dev;if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_TAG_MENU, 1600); return;}ST25R3916Backend::ScanResult tag;const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_F,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalTagMenu();return;}if(tag.technology!=ST25R3916Backend::Technology::NFC_F){_showStatusAndReturn("Tag not detected",STATE_EXP_TAG_MENU,1600);return;}
  if(group==1){_expResultReturn=STATE_EXP_SUB1_MENU;uint16_t sys[32]={};size_t count=0;if(!ST25R3916Experimental::felicaRequestSystemCodes(dev,tag,sys,32,count)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB1_MENU, 1600);return;}_rowCount=0;for(size_t i=0;i<count&&_rowCount<kMaxRows;++i){char l[16];snprintf(l,sizeof(l),"System %u",(unsigned)i);char v[10];snprintf(v,sizeof(v),"0x%04X",sys[i]);_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}dev.deactivate();_advancedOperationTitle="Systems";_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_RESULT;render();return;}
  if(group==2&&index==0){_expResultReturn=STATE_EXP_SUB2_MENU;_rowCount=0;for(uint16_t i=0;i<64&&_rowCount<kMaxRows;++i){uint16_t sc=0;if(!ST25R3916Experimental::felicaSearchService(dev,tag,i,sc))break;char l[16];snprintf(l,sizeof(l),"Service %u",i);char v[10];snprintf(v,sizeof(v),"0x%04X",sc);_rowLabels[_rowCount]=l;_rowValues[_rowCount]=v;_rows[_rowCount]={_rowLabels[_rowCount].c_str(),_rowValues[_rowCount]};++_rowCount;}dev.deactivate();_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_RESULT;render();return;}
  if(group==2){String sh=InputTextAction::popup("Service code (4 hex)","000B",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}uint8_t sb[2];size_t sl=0;if(!st25ParseHex(sh,sb,2,sl)||sl!=2){dev.deactivate();_showStatusAndReturn("Need 4 hex chars", STATE_EXP_SUB2_MENU, 1600); return;}uint16_t service=(uint16_t)((sb[0]<<8)|sb[1]);if(index==2){uint16_t kv=0;if(!ST25R3916Experimental::felicaRequestService(dev,tag,service,kv)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB2_MENU, 1600); return;}_rowCount=0;char sc[10],ks[10];snprintf(sc,sizeof(sc),"0x%04X",service);snprintf(ks,sizeof(ks),"0x%04X",kv);_rowLabels[0]="Service Code";_rowValues[0]=sc;_rows[0]={_rowLabels[0].c_str(),_rowValues[0]};_rowLabels[1]="Key Version";_rowValues[1]=ks;_rows[1]={_rowLabels[1].c_str(),_rowValues[1]};_rowCount=2;dev.deactivate();_expResultReturn=STATE_EXP_SUB2_MENU;_advancedOperationTitle="Service Details";_scrollView.resetScroll();_scrollView.setRows(_rows,_rowCount);_state=STATE_EXP_RESULT;render();return;}int block=InputNumberAction::popup("Block",0,255,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalSub2Menu();return;}uint8_t d[16];if(!ST25R3916Experimental::felicaReadBlock(dev,tag,service,(uint16_t)block,d)){dev.deactivate();_showStatusAndReturn("Failed", STATE_EXP_SUB2_MENU, 1600); return;}dev.deactivate();_expResultReturn=STATE_EXP_SUB2_MENU;_showExperimentalHex("Service Data",d,16);return;}
  if(group==3&&(index==0||index==1)){String sh=InputTextAction::popup("Service code (4 hex)",index==0?"000B":"0009",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t sb[2];size_t sl=0;if(!st25ParseHex(sh,sb,2,sl)||sl!=2){dev.deactivate();_showStatusAndReturn("Need 4 hex chars", STATE_EXP_ADVANCED_MENU, 1600); return;}uint16_t service=(uint16_t)((sb[0]<<8)|sb[1]);int block=InputNumberAction::popup("Block",0,255,0);if(InputNumberAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}uint8_t d[16]={};if(!ST25R3916Experimental::felicaReadBlock(dev,tag,service,(uint16_t)block,d)){dev.deactivate();if(index==1){char err[40];snprintf(err,sizeof(err),"Failed to read block %u",(unsigned)block);_showStatusAndReturn(err, STATE_EXP_ADVANCED_MENU, 1800);}else{_showStatusAndReturn("Failed", STATE_EXP_ADVANCED_MENU, 1600);}return;}if(index==0){dev.deactivate();_expResultReturn=STATE_EXP_ADVANCED_MENU;_showExperimentalHex("Memory",d,16);return;}String h=InputTextAction::popup("Block data (32 hex)",st25Hex(d,16).c_str(),InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){dev.deactivate();_showExperimentalAdvancedMenu();return;}size_t dl=0;if(!st25ParseHex(h,d,16,dl)||dl!=16){dev.deactivate();_showStatusAndReturn("Need 32 hex chars", STATE_EXP_ADVANCED_MENU, 1600); return;}bool ok=ST25R3916Experimental::felicaWriteBlock(dev,tag,service,(uint16_t)block,d);dev.deactivate();_showStatusAndReturn(ok?"Block written":"Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}
  dev.deactivate();_experimentalRawCommand();
#endif
}

void ST25R3916Screen::_experimentalType4bAction(uint8_t index) { if(index==0)_experimentalSendApdu(false); else _experimentalRawCommand(); }

void ST25R3916Screen::_experimentalSendApdu(bool desfire) {
#if defined(DEVICE_HAS_ST25R3916)
  String h=InputTextAction::popup(desfire?"APDU (hex)":"ISO-DEP APDU (hex)",desfire?"906000000000":"00A4040000",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_showExperimentalAdvancedMenu();return;}uint8_t tx[240];size_t tl=0;if(!st25ParseHex(h,tx,sizeof(tx),tl)||!tl){_showStatusAndReturn("Invalid APDU", STATE_EXP_ADVANCED_MENU, 1600); return;}_state=STATE_EXP_WORKING;_advancedOperationTitle="APDU Response";render();StatusBar::refresh();_renderTagPrompt();ST25R3916Backend dev;if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_ADVANCED_MENU, 1600); return;}ST25R3916Backend::ScanResult tag;if(desfire || _expFamily==EXP_TYPE4A){const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_A,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalAdvancedMenu();return;}if(!tag.isoDep){_showStatusAndReturn("Tag not supported",STATE_EXP_ADVANCED_MENU,1600);return;}}else{const auto scanResult=st25ScanType4b(dev,tag,true);if(scanResult!=St25Type4bScanResult::FOUND){st25ShowType4bScanError(scanResult);_showExperimentalAdvancedMenu();return;}}if(desfire&&!ST25R3916Experimental::desfireProbe(dev)){dev.deactivate();_showStatusAndReturn("Tag not supported", STATE_EXP_ADVANCED_MENU, 1600); return;}uint8_t rx[512]={};size_t n=0;bool ok=dev.isoDepTransceive(tx,tl,rx,sizeof(rx),n);dev.deactivate();if(!ok){_showStatusAndReturn("Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}_expResultReturn=STATE_EXP_ADVANCED_MENU;_showExperimentalHex("APDU Response",rx,n);
#endif
}

void ST25R3916Screen::_experimentalRawCommand() {
#if defined(DEVICE_HAS_ST25R3916)
  String h=InputTextAction::popup("Raw command (hex)","",InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_showExperimentalAdvancedMenu();return;}uint8_t tx[240];size_t tl=0;if(!st25ParseHex(h,tx,sizeof(tx),tl)||!tl){_showStatusAndReturn("Invalid command", STATE_EXP_ADVANCED_MENU, 1600); return;}_state=STATE_EXP_WORKING;_advancedOperationTitle="Raw Response";render();StatusBar::refresh();_renderTagPrompt();ST25R3916Backend dev;if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_ADVANCED_MENU, 1600); return;}ST25R3916Backend::ScanResult tag;if(_expFamily==EXP_TYPE4B){const auto scanResult=st25ScanType4b(dev,tag,false);if(scanResult!=St25Type4bScanResult::FOUND){st25ShowType4bScanError(scanResult);_showExperimentalAdvancedMenu();return;}}else{const auto waitResult=st25WaitForTag(dev,ST25R3916Backend::TECH_F,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalAdvancedMenu();return;}}uint8_t rx[512]={};size_t n=0;bool ok=(_expFamily==EXP_TYPE4B&&tag.isoDep)?dev.isoDepTransceive(tx,tl,rx,sizeof(rx),n):dev.activeRfTransceive(tx,tl,rx,sizeof(rx),n,250);dev.deactivate();if(!ok){_showStatusAndReturn("Failed", STATE_EXP_ADVANCED_MENU, 1600); return;}_expResultReturn=STATE_EXP_ADVANCED_MENU;_showExperimentalHex("Raw Response",rx,n);
#endif
}

void ST25R3916Screen::_experimentalReadNdef() {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_EXP_WORKING;_advancedOperationTitle="Read NDEF";render();_renderTagPrompt();ST25R3916Backend dev;if(!st25Begin(dev, _interface)){_showStatusAndReturn("ST25R3916 not detected", STATE_EXP_NDEF_MENU, 1600); return;}ST25R3916Backend::ScanResult tag;if(_expFamily==EXP_TYPE4B){const auto scanResult=st25ScanType4b(dev,tag,true);if(scanResult!=St25Type4bScanResult::FOUND){st25ShowType4bScanError(scanResult);_showExperimentalNdefMenu();return;}}else{uint16_t mask=_expFamily==EXP_NFCV?ST25R3916Backend::TECH_V:_expFamily==EXP_FELICA?ST25R3916Backend::TECH_F:ST25R3916Backend::TECH_A;const auto waitResult=st25WaitForTag(dev,mask,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);_showExperimentalNdefMenu();return;}}if (_expFamily == EXP_TYPE4A && (tag.technology != ST25R3916Backend::Technology::NFC_A || !tag.isoDep)) { dev.deactivate(); _showStatusAndReturn("Tag not supported", STATE_EXP_NDEF_MENU, 1600); return; }uint8_t b[kMaxNdefBytes]={};size_t n=0,cap=0;bool ok=false;if(_expFamily==EXP_NFCV)ok=ST25R3916Experimental::typeVReadNdef(dev,tag,b,sizeof(b),n,cap);else if(_expFamily==EXP_FELICA)ok=ST25R3916Experimental::felicaReadNdef(dev,tag,b,sizeof(b),n,cap);else if(tag.isoDep)ok=ST25R3916Experimental::type4ReadNdef(dev,b,sizeof(b),n,cap);dev.deactivate();if(!ok){_showStatusAndReturn("Failed", STATE_EXP_NDEF_MENU, 2000); return;}_ndefCapacity=cap;_ndefExperimentalTarget=true;_showNdefDetails(tag.nfcid,tag.nfcidLen,b,n);
#endif
}

bool ST25R3916Screen::_experimentalWriteNdef(const uint8_t* ndef, size_t ndefLen, bool formatOnly) {
#if defined(DEVICE_HAS_ST25R3916)
  _state=STATE_EXP_WORKING;_advancedOperationTitle=formatOnly?"Format NDEF":"Write NDEF";render();_renderTagPrompt();ST25R3916Backend dev;if(!st25Begin(dev, _interface)){ShowStatusAction::show("ST25R3916 not detected", 1600);return false;}ST25R3916Backend::ScanResult tag;if(_expFamily==EXP_TYPE4B){const auto scanResult=st25ScanType4b(dev,tag,true);if(scanResult!=St25Type4bScanResult::FOUND){st25ShowType4bScanError(scanResult);return false;}}else{uint16_t mask=_expFamily==EXP_NFCV?ST25R3916Backend::TECH_V:_expFamily==EXP_FELICA?ST25R3916Backend::TECH_F:ST25R3916Backend::TECH_A;const auto waitResult=st25WaitForTag(dev,mask,tag,5000,true);if(waitResult!=St25WaitResult::FOUND){if(waitResult==St25WaitResult::TIMEOUT)ShowStatusAction::show("Tag not detected",1600);return false;}}size_t cap=0;bool ok=false;if(_expFamily==EXP_NFCV)ok=ST25R3916Experimental::typeVWriteNdef(dev,tag,ndef,ndefLen,cap,formatOnly);else if(_expFamily==EXP_FELICA){if(formatOnly){dev.deactivate();ShowStatusAction::show("Requires provisioned Type 3 NDEF", 1600);return false;}ok=ST25R3916Experimental::felicaWriteNdef(dev,tag,ndef,ndefLen,cap);}else if(tag.isoDep){if(formatOnly){dev.deactivate();ShowStatusAction::show("Requires provisioned Type 4 NDEF",2000);return false;}ok=ST25R3916Experimental::type4WriteNdef(dev,ndef,ndefLen,cap);}dev.deactivate();_ndefCapacity=cap;ShowStatusAction::show(ok?(formatOnly?"NDEF formatted":"NDEF written"):"Failed",1600);return ok;
#else
  return false;
#endif
}

void ST25R3916Screen::_experimentalEraseNdef(bool formatOnly) {
  static const uint8_t empty = 0;
  if(formatOnly){if(_experimentalWriteNdef(&empty,0,true))_showExperimentalNdefMenu();else _showExperimentalNdefMenu();return;}
  static const InputSelectAction::Option o[]={{"Erase NDEF","erase"}};const char*c=InputSelectAction::popup("Erase NDEF?",o,1,nullptr);if(!c){_showExperimentalNdefMenu();return;}if(_experimentalWriteNdef(&empty,0,false))_showExperimentalNdefMenu();else _showExperimentalNdefMenu();
}

void ST25R3916Screen::_showDeviceInfo() {
#if defined(DEVICE_HAS_ST25R3916)
  ST25R3916Backend dev;
  const char* iface = st25InterfaceName(_interface);
  bool ok = st25Begin(dev, _interface);

  _rowCount = 0;
  auto add = [&](const char* label, const String& value) {
    if (_rowCount >= kMaxRows) return;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
    ++_rowCount;
  };

  if (!ok) {
    add("Status", "Not detected");
    add("I2C", String("0x") + String(kSt25I2cAddr, HEX));
    add("SPI CS", String(ST25R3916_CS_PIN));
    _scrollView.resetScroll();
    _scrollView.setRows(_rows, _rowCount);
    _state = STATE_DEVICE_INFO;
    render();
    return;
  }

  const auto& info = dev.info();
  const uint8_t typeCode = (uint8_t)((info.chipId >> 3) & 0x1FU);
  const uint8_t revCode = (uint8_t)(info.chipId & 0x07U);
  String family;
  if (typeCode == 0x05) family = "ST25R3916/17";
  else if (typeCode == 0x06) family = "ST25R3916B/17B/19B";
  else family = "ST25R39xx";

  String revision = String((unsigned)revCode);
  if (typeCode == 0x05 && revCode == 0x02) revision += " (3.1)";
  else if (typeCode == 0x06 && revCode == 0x01) revision += " (4.1)";

  char id[8]; snprintf(id, sizeof(id), "0x%02X", info.chipId);
  add("Family", family);
  add("IC Identity", id);
  add("Type Code", String((unsigned)typeCode));
  add("Revision", revision);
  add("Interface", iface ? iface : "--");
  if (iface && strcmp(iface, "I2C") == 0) {
    char addr[8]; snprintf(addr, sizeof(addr), "0x%02X", kSt25I2cAddr);
    add("I2C Address", addr);
    add("IRQ", "Polling");
  } else {
    add("SPI Clock", String((unsigned long)(ST25R3916_SPI_HZ / 1000000UL)) + " MHz");
    add("CS / IRQ", String(ST25R3916_CS_PIN) + " / " + String(ST25R3916_IRQ_PIN));
  }
  add("RFAL", info.initialized ? "Ready" : (String("Error ") + String(info.initCode)));
  add("FIFO", "512 bytes");
  add("Reader", "NFC-A / B / F / V");
  add("Card Emu", "NFC-A / NFC-F");

  dev.end();
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  _state = STATE_DEVICE_INFO;
  render();
#endif
}
