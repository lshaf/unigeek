#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_USB_HID is defined
#ifdef DEVICE_HAS_USB_HID

#include "core/IStorage.h"

// UsbMscUtil — exposes the active storage backend to a USB host as a removable
// drive (USB Mass Storage Class). It bridges TinyUSB's MSC read/write callbacks
// to IStorage::readBlocks()/writeBlocks(), so the host sees the real FAT volume
// living on whatever Storage the board loaded at boot (the SD card).
//
// Only a true block device (SD card) can be exposed. LittleFS reports
// isBlockDevice()==false and begin() fails with FAIL_NOT_BLOCK_DEVICE.
//
// USB ownership: USBMSC registers its interface in its constructor, which must
// run before USB.begin(). begin() therefore claims the MASS_STORAGE USB profile
// first (see UsbProfile.h) and only constructs the USBMSC device if it wins the
// boot. If another USB feature (keyboard / WebAuthn) already started USB this
// boot, begin() fails with FAIL_USB_BUSY and the caller shows a reboot hint.
//
// Singleton: the TinyUSB MSC callbacks are plain C function pointers with no
// user argument, so a single global instance routes them.
class UsbMscUtil {
public:
  enum FailReason : uint8_t {
    FAIL_NONE = 0,
    FAIL_NOT_BLOCK_DEVICE,  // active storage is not a FAT block device (e.g. LittleFS)
    FAIL_USB_BUSY,          // another USB profile already claimed USB this boot
  };

  static UsbMscUtil& instance();

  // Start exposing `storage` as a USB drive. Idempotent: re-presents media if
  // already active. Returns false (and sets failReason()) when the storage is
  // not a block device or USB was already claimed by another profile.
  bool begin(IStorage* storage);
  // Expose a hybrid bootable ISO as a read-only USB disk (512-byte sectors).
  bool beginIso(IStorage* storage, const char* path);

  // Eject the media. The MSC interface stays in the USB descriptor (arduino-esp32
  // cannot tear USB down), but the host sees the drive removed. A later begin()
  // re-mounts without a reboot.
  void end();

  bool       isActive()       const { return _active; }
  FailReason failReason()     const { return _failReason; }
  uint64_t   capacityBytes()  const { return (uint64_t)_blockCount * _blockSize; }
  uint32_t   sectorReads()    const { return _reads; }
  uint32_t   sectorWrites()   const { return _writes; }
  bool       hostEjected()    const { return _hostEjected; }

private:
  UsbMscUtil() = default;

  // TinyUSB MSC callbacks (run on the TinyUSB task).
  static int32_t _onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  static int32_t _onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
  static bool    _onStartStop(uint8_t power_condition, bool start, bool load_eject);

  // Fixed-size read-ahead buffer; allocated only once with the singleton.
  // The TinyUSB task is the sole writer/reader of this cache.
  static constexpr uint32_t ISO_CACHE_BYTES = 64u * 1024u;
  uint8_t    _isoCache[ISO_CACHE_BYTES] = {};
  uint32_t   _isoCacheStart = 0;
  uint32_t   _isoCacheValid = 0;
  fs::File   _isoFile;
  bool       _isoMode = false;
  IStorage*  _storage     = nullptr;
  uint32_t   _blockCount  = 0;
  uint16_t   _blockSize   = 0;
  bool       _active      = false;
  bool       _hostEjected = false;
  FailReason _failReason  = FAIL_NONE;

  // Updated from the TinyUSB task, read from the UI task — display counters only.
  volatile uint32_t _reads  = 0;
  volatile uint32_t _writes = 0;
};

#endif  // DEVICE_HAS_USB_HID
