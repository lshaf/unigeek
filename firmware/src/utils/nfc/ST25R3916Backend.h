#pragma once

#include <Arduino.h>

#if defined(DEVICE_HAS_ST25R3916)
#include <SPI.h>
#include <Wire.h>

class RfalRfST25R3916Class;
class RfalNfcClass;
struct Crypto1State;

class ST25R3916Backend {
public:
  enum class Transport : uint8_t { I2C, SPI };

  enum TechMask : uint16_t {
    TECH_A   = 0x0001,
    TECH_B   = 0x0002,
    TECH_F   = 0x0004,
    TECH_V   = 0x0008,
    TECH_ALL = TECH_A | TECH_B | TECH_F | TECH_V,
  };

  enum class Technology : uint8_t { UNKNOWN, NFC_A, NFC_B, NFC_F, NFC_V };

  struct Info {
    bool busDetected = false;
    bool initialized = false;
    uint16_t initCode = 0xFFFF;
    uint8_t chipId = 0;
    bool is3916 = false;
    bool is3916B = false;
  };

  struct ScanResult {
    Technology technology = Technology::UNKNOWN;
    uint8_t nfcid[10] = {};
    uint8_t nfcidLen = 0;
    bool isoDep = false;
    uint8_t sak = 0;
    uint8_t atqa[2] = {};
  };

  ST25R3916Backend() = default;
  ~ST25R3916Backend();

  bool beginI2C(TwoWire* wire, uint8_t address = 0x50);
  bool beginSPI(SPIClass* spi, int csPin, int irqPin, uint32_t spiHz = 5000000);
  void end();
  bool scan(uint16_t techMask, ScanResult& result, uint32_t timeoutMs = 1500,
            bool keepActive = false);
  void deactivate();

  bool mifareClassicAuthenticate(uint8_t block, const uint8_t key[6], bool keyB = false);
  bool mifareClassicReadBlock(uint8_t block, uint8_t data[16]);
  bool mifareClassicWriteBlock(uint8_t block, const uint8_t data[16]);

  bool type2Transceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                       size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool type2ReadPages(uint8_t startPage, uint8_t data[16]);
  bool type2WritePage(uint8_t page, const uint8_t data[4]);
  bool type2WritePageRfalFirst(uint8_t page, const uint8_t data[4]);
  bool type2PwdAuth(const uint8_t pwd[4], uint8_t pack[2] = nullptr);
  uint16_t lastPwdAuthCode() const { return _lastPwdAuthCode; }
  uint16_t lastPwdAuthBits() const { return _lastPwdAuthBits; }
  uint8_t lastType2WriteAckNibble() const { return _lastType2WriteAckNibble; }
  uint16_t lastType2WriteBits() const { return _lastType2WriteBits; }
  uint16_t lastType2WriteCode() const { return _lastType2WriteCode; }

  bool startType2Emulation(const ScanResult& identity, uint8_t* dump, size_t dumpLen);
  bool startMfcUidEmulation(const ScanResult& identity);
  // Full MIFARE Classic 1K emulation from a 1024-byte dump: completes the
  // Crypto1 handshake and serves encrypted READ/WRITE. Sector keys are taken
  // from each trailer. With no key for a sector it still captures the reader's
  // {nr,ar} (mfkey32) via the same path. `dump` must outlive the emulation.
  bool startMfcDumpEmulation(const ScanResult& identity, uint8_t* dump, size_t dumpLen);
  bool emulationWorker();
  void stopEmulation();
  bool emulationActive() const { return _emulating; }

  // Captured reader nonces (mfkey32). One tuple per distinct {block,keytype,nr,ar}.
  struct MfcNonce {
    uint8_t  block;
    uint8_t  keytype;   // 0x60 KeyA / 0x61 KeyB
    uint32_t nt;        // tag nonce we presented (plaintext, static)
    uint32_t nr;        // reader nonce, encrypted as received
    uint32_t ar;        // reader answer, encrypted as received
    uint32_t uid;       // emulated UID, big-endian
  };
  uint8_t mfcNonceCount() const { return _emuNonceCount; }
  const MfcNonce& mfcNonce(uint8_t i) const { return _emuNonces[i]; }
  void mfcClearNonces() { _emuNonceCount = 0; }

  bool startReaderScan();
  bool readerScanDetected();
  void stopReaderScan();

  bool nfcATransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool nfcATransceiveBits(const uint8_t* tx, size_t txBits, uint8_t* rx,
                          size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs = 20);

  bool activeRfTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                          size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 50);
  bool isoDepTransceive(const uint8_t* tx, size_t txLen, uint8_t* rx,
                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 1200);

  const Info& info() const { return _info; }
  uint16_t lastScanCode() const { return _lastScanCode; }
  Transport transport() const { return _transport; }
  const ScanResult& activeTag() const { return _activeTag; }
  bool hasActiveTag() const { return _active; }

private:
  bool _finishBegin();
  bool _transceiveBytes(const uint8_t* tx, size_t txLen, uint8_t* rx,
                        size_t rxMaxLen, size_t& rxLen, uint32_t timeoutMs = 20);
  bool _transceivePacked(const uint8_t* txPacked, size_t txBits, uint8_t* rxPacked,
                         size_t rxMaxBits, size_t& rxBits, uint32_t timeoutMs = 8,
                         bool autoTxParity = false);
  bool _transceiveRaw9(const uint8_t* txData, const uint8_t* txParity, size_t txLen,
                       uint8_t* rxData, uint8_t* rxParity, size_t rxMaxLen,
                       size_t& rxLen, uint32_t timeoutMs = 8);
  static uint16_t _crcA(const uint8_t* data, size_t len);
  static uint64_t _key48(const uint8_t key[6]);
  static uint32_t _uid32(const ScanResult& tag);
  static uint8_t _oddParity(uint8_t value);

  Transport _transport = Transport::I2C;
  Info _info;
  RfalRfST25R3916Class* _hw = nullptr;
  RfalNfcClass* _nfc = nullptr;
  uint16_t _lastScanCode = 0xFFFF;
  uint16_t _lastPwdAuthCode = 0xFFFF;
  uint16_t _lastPwdAuthBits = 0;
  uint8_t _lastType2WriteAckNibble = 0xFF;
  uint16_t _lastType2WriteBits = 0;
  uint16_t _lastType2WriteCode = 0xFFFF;
  ScanResult _activeTag;
  bool _active = false;
  Crypto1State* _crypto = nullptr;

  bool _startNfcaListen(const ScanResult& identity);
  bool _listenRespond(const uint8_t* data, uint16_t len, bool withCrc = true);

  // --- MIFARE Classic Crypto1 emulation (synchronous) ---
  // Fixed tag nonce: a constant nt is ideal for mfkey32 and keeps nonce
  // generation out of the ISO14443-A frame-delay budget.
  static const uint32_t MFC_STATIC_NT = 0x01200145UL;
  // Entry point from emulationWorker() when an AUTH frame arrives. Runs the
  // whole nt -> {nr,ar} -> at handshake synchronously (never returning to the
  // worker between nt and at) and then serves the encrypted session.
  void _mfcHandleAuth(const uint8_t* frame, uint16_t len);
  // Blocking inner loop: serve encrypted READ (0x30) / WRITE (0xA0) until
  // HALT, field-off (EOF) or an idle timeout. The live Crypto1 cipher is passed
  // in (stack-allocated by _mfcHandleAuth); no_rx_par must be set.
  void _mfcServeSession(Crypto1State* cs);
  // Send an encrypted frame (data already XORed with keystream, parity =
  // encrypted parity), 9-bit packed, no_tx_par, no CRC, waits for TXE.
  bool _mfcTxEnc(const uint8_t* data, const uint8_t* parity, uint8_t len);
  // Blocking raw RX under no_rx_par: unpack 9-bit units, strip parity. Returns
  // true once at least `wantBytes` data bytes were received within timeoutMs.
  bool _mfcRxRaw(uint8_t* out, uint8_t wantBytes, uint32_t timeoutMs);
  const uint8_t* _mfcSectorKey(uint8_t block, uint8_t keytype) const;
  // Record one reader {nr,ar} tuple (mfkey32 material), de-duplicated.
  void _mfcRecordNonce(uint8_t block, uint8_t keytype, uint32_t nt,
                       uint32_t nr, uint32_t ar, uint32_t uid);

  bool _emulating = false;
  bool _readerScanning = false;
  bool _emuMfc = false;       // MFC identity emulation (UID or dump)
  bool _emuMfcDump = false;   // full Crypto1 emulation from a loaded dump
  uint8_t* _emuDump = nullptr;
  size_t _emuDumpLen = 0;
  ScanResult _emuIdentity;

  // 1K sector keys parsed from the dump trailers.
  uint8_t _mfcSectorCount = 0;
  uint8_t _mfcKeyA[16][6] = {};
  uint8_t _mfcKeyB[16][6] = {};
  bool _mfcHaveKeyA[16] = {};
  bool _mfcHaveKeyB[16] = {};

  static const uint8_t MFC_MAX_NONCES = 24;
  MfcNonce _emuNonces[MFC_MAX_NONCES] = {};
  uint8_t _emuNonceCount = 0;
};

#endif
