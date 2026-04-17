#pragma once
#include <NimBLEDevice.h>

// Singleton BLE GATT client for ChameleonUltra / ChameleonLite.
// Protocol: NRF UART service with framed commands (SOF + LRC + header + data + CRC).
class ChameleonClient {
public:
  static ChameleonClient& get();

  static constexpr const char* kSvcUUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* kRxUUID  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* kTxUUID  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

  static constexpr uint16_t CMD_GET_VERSION      = 1000;
  static constexpr uint16_t CMD_CHANGE_MODE      = 1001;
  static constexpr uint16_t CMD_GET_MODE         = 1002;
  static constexpr uint16_t CMD_SET_SLOT         = 1003;
  static constexpr uint16_t CMD_SET_SLOT_TAG_TYPE   = 1004;
  static constexpr uint16_t CMD_SET_SLOT_DATA_DEF   = 1005;
  static constexpr uint16_t CMD_SET_SLOT_ENABLE     = 1006;
  static constexpr uint16_t CMD_SET_SLOT_NICK       = 1007;
  static constexpr uint16_t CMD_GET_SLOT_NICK       = 1008;
  static constexpr uint16_t CMD_SAVE_SLOT_NICKS     = 1009;
  static constexpr uint16_t CMD_GET_CHIP_ID      = 1011;
  static constexpr uint16_t CMD_GET_BLE_ADDR     = 1012;
  static constexpr uint16_t CMD_SAVE_SETTINGS    = 1013;
  static constexpr uint16_t CMD_RESET_SETTINGS   = 1014;
  static constexpr uint16_t CMD_SET_ANIMATION    = 1015;
  static constexpr uint16_t CMD_GET_ANIMATION    = 1016;
  static constexpr uint16_t CMD_GET_GIT_VERSION  = 1017;
  static constexpr uint16_t CMD_GET_ACT_SLOT     = 1018;
  static constexpr uint16_t CMD_GET_SLOT_INFO    = 1019;
  static constexpr uint16_t CMD_GET_EN_SLOTS     = 1023;
  static constexpr uint16_t CMD_DELETE_SLOT      = 1024;
  static constexpr uint16_t CMD_GET_BATTERY      = 1025;
  static constexpr uint16_t CMD_GET_BTN_PRESS    = 1026;
  static constexpr uint16_t CMD_SET_BTN_PRESS    = 1027;
  static constexpr uint16_t CMD_GET_LBTN_PRESS   = 1028;
  static constexpr uint16_t CMD_SET_LBTN_PRESS   = 1029;
  static constexpr uint16_t CMD_BLE_CLEAR_BONDS  = 1032;
  static constexpr uint16_t CMD_GET_DEV_TYPE     = 1033;
  static constexpr uint16_t CMD_GET_DEV_SETTINGS = 1034;
  static constexpr uint16_t CMD_BLE_GET_PAIR     = 1036;
  static constexpr uint16_t CMD_BLE_SET_PAIR     = 1037;
  static constexpr uint16_t CMD_SCAN_14A         = 2000;
  static constexpr uint16_t CMD_MF1_SUPPORT      = 2001;
  static constexpr uint16_t CMD_MF1_NT_LEVEL     = 2002;
  static constexpr uint16_t CMD_MF1_CHECK_KEY    = 2007;
  static constexpr uint16_t CMD_MF1_READ_BLOCK   = 2008;
  static constexpr uint16_t CMD_MF1_WRITE_BLOCK  = 2009;
  static constexpr uint16_t CMD_HF14A_RAW        = 2010;
  static constexpr uint16_t CMD_MF1_CHECK_BLOCK  = 2015;
  static constexpr uint16_t CMD_SCAN_EM410X      = 3000;
  static constexpr uint16_t CMD_WRITE_EM410X_T5  = 3001;
  static constexpr uint16_t CMD_SCAN_HID_PROX    = 3002;
  static constexpr uint16_t CMD_WRITE_HID_T5     = 3003;
  static constexpr uint16_t CMD_SCAN_VIKING      = 3004;
  static constexpr uint16_t CMD_WRITE_VIKING_T5  = 3005;
  static constexpr uint16_t CMD_MF1_LOAD_BLOCK   = 4000;
  static constexpr uint16_t CMD_MF1_SET_ANTI_COLL   = 4001;
  static constexpr uint16_t CMD_MF1_DET_ENABLE   = 4004;
  static constexpr uint16_t CMD_MF1_DET_COUNT    = 4005;
  static constexpr uint16_t CMD_MF1_DET_RESULT   = 4006;
  static constexpr uint16_t CMD_MF1_GET_BLOCK    = 4008;
  static constexpr uint16_t CMD_SET_EM410X_ID    = 5000;
  static constexpr uint16_t CMD_GET_EM410X_ID    = 5001;
  static constexpr uint16_t CMD_SET_HID_PROX_ID  = 5002;
  static constexpr uint16_t CMD_GET_HID_PROX_ID  = 5003;
  static constexpr uint16_t CMD_SET_VIKING_ID    = 5004;
  static constexpr uint16_t CMD_GET_VIKING_ID    = 5005;

  struct SlotTypes { uint16_t hfType; uint16_t lfType; };

  // 13-byte settings bundle from CMD_GET_DEV_SETTINGS
  struct DeviceSettings {
    uint8_t settingsVersion;
    uint8_t animation;       // 0=full 1=minimal 2=none 3=symmetric
    uint8_t btnAShort;       // ButtonConfig
    uint8_t btnBShort;
    uint8_t btnALong;
    uint8_t btnBLong;
    uint8_t blePairingEnabled;
    char    pairingPin[7];   // 6 ASCII digits + NUL
  };

  bool isConnected() const;
  bool connect(const NimBLEAddress& addr);
  void disconnect();

  bool sendCommand(uint16_t cmd, const uint8_t* data, uint16_t dataLen,
                   uint8_t* respBuf, uint16_t* respLen, uint16_t* respStatus,
                   uint32_t timeoutMs = 2000,
                   uint16_t respBufSize = 256);

  bool getVersion(char* out, uint8_t maxLen);
  bool getGitVersion(char* out, uint8_t maxLen);
  bool getBattery(uint8_t* pct, uint16_t* mV);
  bool getDeviceType(uint8_t* type);
  bool getChipId(char* out, uint8_t maxLen);
  bool getActiveSlot(uint8_t* slot);
  bool setActiveSlot(uint8_t slot);
  bool getMode(uint8_t* mode);
  bool setMode(uint8_t mode);
  bool getSlotTypes(SlotTypes types[8]);
  bool getEnabledSlots(bool hfEn[8], bool lfEn[8]);

  // ── Device settings (1013..1037) ──
  bool getDeviceSettings(DeviceSettings* out);
  bool saveSettings();
  bool resetSettings();
  bool setAnimation(uint8_t mode);
  bool getAnimation(uint8_t* mode);
  bool setButtonConfig(uint8_t buttonIdx, bool longPress, uint8_t action);
  bool getButtonConfig(uint8_t buttonIdx, bool longPress, uint8_t* action);
  bool setBlePairingEnabled(bool on);
  bool clearBleBonds();

  // ── Slot edit (1004..1024) ──
  bool setSlotTagType(uint8_t slot, uint16_t tagType);
  bool setSlotDataDefault(uint8_t slot, uint16_t tagType);
  bool setSlotEnable(uint8_t slot, uint8_t freq, bool enabled);
  bool setSlotNick(uint8_t slot, uint8_t freq, const char* name);
  bool getSlotNick(uint8_t slot, uint8_t freq, char* out, uint8_t maxLen);
  bool saveSlotNicks();
  bool deleteSlot(uint8_t slot, uint8_t freq);

  // ── 14A / MF Classic ──
  bool scan14A(uint8_t uid[7], uint8_t* uidLen, uint8_t atqa[2], uint8_t* sak);
  bool mf1Support();
  bool mf1NTLevel(uint8_t* level);
  bool mf1CheckKey(uint8_t block, uint8_t keyType, const uint8_t key[6]);
  bool mf1ReadBlock(uint8_t block, uint8_t keyType, const uint8_t key[6],
                    uint8_t out[16]);
  // batch check: keyCount<=32, returns bitmap of hits (LSB=first key)
  bool mf1CheckKeysOfBlock(uint8_t block, uint8_t keyType,
                           const uint8_t* keys, uint8_t keyCount,
                           uint32_t* hitBitmap);
  bool mf1LoadBlockData(uint8_t slot, uint8_t startBlock,
                        const uint8_t* data, uint16_t dataLen);
  // Read emulator slot memory (active slot). `count` blocks of 16 B.
  bool mf1GetBlockData(uint8_t startBlock, uint8_t count, uint8_t* out);
  bool hf14ARaw(uint8_t options, uint16_t timeoutMs, uint16_t bitLen,
                const uint8_t* data, uint16_t dataBytes,
                uint8_t* respOut, uint16_t* respLen, uint16_t respBufSize);

  // ── MFKey32 detection log ──
  bool mf1SetDetectEnable(bool on);
  bool mf1GetDetectCount(uint32_t* count);
  bool mf1GetDetectRecord(uint32_t index, uint8_t out[18]);

  // ── LF ──
  bool scanEM410X(uint8_t uid[5]);
  bool scanHIDProx(uint8_t payload[13], uint8_t* payloadLen);
  bool scanViking(uint8_t uid[4], uint8_t* uidLen);
  bool writeEM410XToT5577(const uint8_t uid[5], const uint8_t newKey[4],
                          const uint8_t* oldKeys, uint8_t oldKeyCount);
  bool writeHIDProxToT5577(const uint8_t* payload, uint8_t payloadLen,
                           const uint8_t newKey[4],
                           const uint8_t* oldKeys, uint8_t oldKeyCount);
  bool writeVikingToT5577(const uint8_t uid[4], const uint8_t newKey[4],
                          const uint8_t* oldKeys, uint8_t oldKeyCount);
  bool setEM410XSlot(const uint8_t uid[5]);
  bool getEM410XSlot(uint8_t uid[5]);
  bool setHIDProxSlot(const uint8_t* payload, uint8_t payloadLen);
  bool getHIDProxSlot(uint8_t payload[13], uint8_t* payloadLen);
  bool setVikingSlot(const uint8_t uid[4], uint8_t uidLen);
  bool getVikingSlot(uint8_t uid[4], uint8_t* uidLen);

  static uint16_t inferHFTagType(uint8_t sak, const uint8_t atqa[2]);
  bool cloneHF(uint8_t slot, uint16_t tagType,
               const uint8_t* uid, uint8_t uidLen,
               const uint8_t atqa[2], uint8_t sak);

  static const char* tagTypeName(uint16_t type);

private:
  ChameleonClient() = default;

  NimBLEClient*               _client  = nullptr;
  NimBLERemoteCharacteristic* _rxChar  = nullptr;
  NimBLERemoteCharacteristic* _txChar  = nullptr;

  static volatile bool _notifyReady;
  static uint8_t       _notifyBuf[1100];
  static uint16_t      _notifyLen;

  static uint8_t _lrc(const uint8_t* d, uint16_t n);
  void _buildFrame(uint16_t cmd, const uint8_t* d, uint16_t n,
                   uint8_t* out, uint16_t* outLen);
  bool _parseFrame(const uint8_t* d, uint16_t n, uint16_t* cmd, uint16_t* status,
                   uint8_t* payload, uint16_t* payLen);
  static void _onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool);
};