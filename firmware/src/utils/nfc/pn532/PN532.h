#pragma once

#include <Arduino.h>
#include "IPN532Transport.h"

class PN532
{
public:
  enum Cmd : uint8_t {
    CMD_DIAGNOSE                  = 0x00,
    CMD_GET_FIRMWARE_VERSION      = 0x02,
    CMD_GET_GENERAL_STATUS        = 0x04,
    CMD_READ_REGISTER             = 0x06,
    CMD_WRITE_REGISTER            = 0x08,
    CMD_SAM_CONFIGURATION         = 0x14,
    CMD_RF_CONFIGURATION          = 0x32,
    CMD_IN_DATA_EXCHANGE          = 0x40,
    CMD_IN_COMMUNICATE_THRU       = 0x42,
    CMD_IN_DESELECT               = 0x44,
    CMD_IN_LIST_PASSIVE_TARGET    = 0x4A,
    CMD_IN_RELEASE                = 0x52,
    CMD_TG_INIT_AS_TARGET         = 0x8C,
    CMD_TG_GET_INITIATOR_COMMAND  = 0x88,
    CMD_TG_RESPONSE_TO_INITIATOR  = 0x90,
    CMD_KILLER_DETECT             = 0xAA,
    CMD_KILLER_SET_WORK_MODE      = 0xAC,
    CMD_KILLER_SET_EMULATOR_DATA  = 0x1E,
  };

  enum KillerWorkMode : uint8_t {
    KILLER_READER   = 0x01,
    KILLER_EMULATOR = 0x02,
    KILLER_SNIFFER  = 0x03,
  };

  enum KillerTagType : uint8_t {
    KILLER_MFC1K    = 0x01,
    KILLER_NTAG     = 0x02,
    KILLER_ISO15693 = 0x03,
    KILLER_EM4100   = 0x04,
  };

  enum Bitrate : uint8_t {
    BR_106_TYPE_A = 0x00,
    BR_212_FELICA = 0x01,
    BR_424_FELICA = 0x02,
    BR_106_TYPE_B = 0x03,
    BR_106_INNOVATRON_JEWEL = 0x04,
    BR_ISO15693   = 0x05,
    BR_EM4100     = 0x06,
  };

  enum MifareSubCmd : uint8_t {
    MF_AUTH_A    = 0x60,
    MF_AUTH_B    = 0x61,
    MF_READ      = 0x30,
    MF_WRITE_16  = 0xA0,
    MF_WRITE_4   = 0xA2,  // Ultralight / NTAG
  };

  struct FirmwareInfo {
    uint8_t ic        = 0;
    uint8_t version   = 0;
    uint8_t revision  = 0;
    uint8_t support   = 0;
    bool    valid     = false;
  };

  struct Target14A {
    uint8_t  uid[10] = {0};
    uint8_t  uidLen  = 0;
    uint16_t atqa    = 0;
    uint8_t  sak     = 0;
    uint8_t  ats[32] = {0};
    uint8_t  atsLen  = 0;
  };

  struct Target15 {
    uint8_t uid[8] = {0};
    uint8_t dsfid  = 0;
    bool    valid  = false;
  };

  struct TargetLF {
    uint8_t uid[8] = {0};
    uint8_t uidLen = 0;
  };

  explicit PN532(IPN532Transport& transport) : _t(transport) {}

  // Lifecycle
  bool init();   // wakeup + SAM normal-mode
  bool getFirmwareVersion(FirmwareInfo& out);
  bool isPN532Killer(uint8_t& killerCode);  // returns true if 0xAA cmd is supported

  // Discovery
  bool listPassiveTarget14A(Target14A& out, uint32_t timeoutMs = 1000);
  bool listPassiveTarget15(Target15& out, uint32_t timeoutMs = 1000);
  bool listPassiveTargetEM4100(TargetLF& out, uint32_t timeoutMs = 1000);

  bool inRelease(uint8_t target = 0x01);
  bool inDeselect(uint8_t target = 0x01);

  // Mifare Classic
  bool mifareAuth(uint8_t block, bool useKeyB,
                  const uint8_t key[6], const uint8_t uid[4]);
  bool mifareRead(uint8_t block, uint8_t out[16]);
  bool mifareWrite(uint8_t block, const uint8_t data[16]);

  // Mifare Ultralight / NTAG
  bool ultralightRead(uint8_t page, uint8_t out[16]);
  bool ultralightWrite4(uint8_t page, const uint8_t data[4]);

  // Raw bridges
  // inDataExchange: payload = target byte + Mifare/ISO sub-command + params
  bool inDataExchange(const uint8_t* payload, size_t payloadLen,
                      uint8_t* out, size_t outCap, size_t& outLen,
                      uint32_t timeoutMs = 500);
  // inCommunicateThru: payload sent to RF directly, includes CRC if appended
  bool inCommunicateThru(const uint8_t* payload, size_t payloadLen,
                         uint8_t* out, size_t outCap, size_t& outLen,
                         uint32_t timeoutMs = 500);

  // Card emulation (passive target — stock PN532)
  bool tgInitAsTarget(uint16_t atqa, uint8_t sak,
                      const uint8_t* uid, uint8_t uidLen,
                      uint32_t timeoutMs = 5000);

  // PN532Killer slot management
  bool killerSetWorkMode(KillerWorkMode mode, KillerTagType tagType, uint8_t slot);
  bool killerUploadEmulatorData(KillerTagType tagType, uint8_t slot,
                                const uint8_t* data, size_t dataLen);

  // Magic card detection (Gen1a / Gen3)
  bool isGen1a();
  bool gen3SetUid(const uint8_t uid[7], uint8_t uidLen);
  bool gen3LockUid();

  IPN532Transport::Result lastError() const { return _last; }

private:
  IPN532Transport& _t;
  IPN532Transport::Result _last = IPN532Transport::OK;

  bool _ok(IPN532Transport::Result r) { _last = r; return r == IPN532Transport::OK; }
};
