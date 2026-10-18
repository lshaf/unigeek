#include "UsbMscUtil.h"

#ifdef DEVICE_HAS_USB_HID

#include <USB.h>
#include <USBMSC.h>
#include <stdlib.h>

#ifdef DEVICE_HAS_WEBAUTHN
#include "utils/webauthn/UsbProfile.h"
#endif

// The arduino-esp32 USBMSC device. Function-local static so its constructor
// (which registers the MSC interface via tinyusb_enable_interface) runs the
// first time begin() touches it — before USB.begin() — and never again.
static USBMSC& msc()
{
  static USBMSC s_msc;
  return s_msc;
}

UsbMscUtil& UsbMscUtil::instance()
{
  static UsbMscUtil s_instance;
  return s_instance;
}

bool UsbMscUtil::begin(IStorage* storage)
{
  if (_active) return true;

  if (!storage || !storage->isBlockDevice()) {
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }

  const uint32_t count = storage->blockCount();
  const uint16_t size  = storage->blockSize();
  if (count == 0 || size == 0) {
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }

#ifdef DEVICE_HAS_WEBAUTHN
  // Claim USB for the boot before constructing USBMSC. If keyboard/mouse or
  // WebAuthn already started USB this boot, the MSC interface can't be added
  // anymore — bail out without constructing the device.
  if (!webauthn::claimUsbProfile(webauthn::UsbProfile::MASS_STORAGE)) {
    _failReason = FAIL_USB_BUSY;
    return false;
  }
#endif

  _isoMode = false;
  _storage     = storage;
  _blockCount  = count;
  _blockSize   = size;
  _hostEjected = false;

  USBMSC& m = msc();  // constructs + registers interface on first call
  m.vendorID("Unigeek");
  m.productID("Mass Storage");
  m.productRevision("1.0");
  m.onStartStop(&UsbMscUtil::_onStartStop);
  m.onRead(&UsbMscUtil::_onRead);
  m.onWrite(&UsbMscUtil::_onWrite);
  m.mediaPresent(true);
  m.begin(_blockCount, _blockSize);

  USB.begin();  // idempotent — safe if another screen already started USB

  _active     = true;
  _failReason = FAIL_NONE;
  return true;
}

bool UsbMscUtil::beginIso(IStorage* storage, const char* path)
{
  if (_active || !storage || !storage->isAvailable() || !path) {
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }
#ifdef DEVICE_HAS_WEBAUTHN
  if (!webauthn::claimUsbProfile(webauthn::UsbProfile::MSC_HID)) {
    _failReason = FAIL_USB_BUSY;
    return false;
  }
#endif
  fs::File file = storage->open(path, "r");
  if (!file || file.isDirectory()) {
    if (file) file.close();
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }
  const uint64_t bytes = file.size();
  // Arduino FS seek() and USBMSC LBA use 32-bit offsets/counts.
  if (bytes < 512 || (bytes % 512) != 0 || bytes > 0xFFFFFE00ULL) {
    file.close();
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }
  // Reserve the 64 KiB cache only for the duration of an ISO session.
  // Allocation must succeed before the USB MSC interface is exposed.
  uint8_t* cache = static_cast<uint8_t*>(malloc(ISO_CACHE_BYTES));
  if (!cache) {
    file.close();
    _failReason = FAIL_NOT_BLOCK_DEVICE;
    return false;
  }
  _isoCache = cache;
  _isoFile = file;
  _isoMode = true;
  _storage = storage;
  _blockSize = 512;
  _blockCount = (uint32_t)(bytes / 512);
  _hostEjected = false;
  _reads = _writes = 0;
  _isoCacheValid = 0;
  USBMSC& m = msc();
  m.vendorID("Unigeek");
  m.productID("Boot ISO");
  m.productRevision("1.0");
  m.onStartStop(&UsbMscUtil::_onStartStop);
  m.onRead(&UsbMscUtil::_onRead);
  m.onWrite(&UsbMscUtil::_onWrite);
  m.mediaPresent(true);
  m.begin(_blockCount, _blockSize);
  USB.begin();
  _active = true;
  _failReason = FAIL_NONE;
  return true;
}

void UsbMscUtil::end()
{
  if (!_active) return;
  USBMSC& m = msc();
  m.mediaPresent(false);  // host sees the drive ejected
  m.end();                // clear callbacks + capacity (interface stays registered)
  _active  = false;
  if (_isoFile) _isoFile.close();
  _isoMode = false;
  _isoCacheValid = 0;
  _storage = nullptr;
  free(_isoCache);
  _isoCache = nullptr;
}

int32_t UsbMscUtil::_onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  UsbMscUtil& self = instance();
  IStorage* st = self._storage;
  if (!st || !buffer) return -1;
  const uint16_t bs = self._blockSize;
  if (bs == 0) return -1;

  if (self._isoMode) {
    const uint64_t start = (uint64_t)lba * bs + offset;
    const uint64_t end = start + bufsize;
    if (!self._isoFile || !self._isoCache || end > (uint64_t)self._blockCount * bs) return -1;

    // Read-ahead cache: 64 KiB windows aligned to 512-byte sectors.
    // Keep USB callbacks bounded and never serve bytes from a failed fill.
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t copied = 0;
    while (copied < bufsize) {
      const uint32_t pos = (uint32_t)(start + copied);
      if (self._isoCacheValid == 0 || pos < self._isoCacheStart ||
          (uint64_t)pos >= (uint64_t)self._isoCacheStart + self._isoCacheValid) {
        const uint32_t window = pos & ~(ISO_CACHE_BYTES - 1u);
        const uint32_t remaining = (uint32_t)((uint64_t)self._blockCount * bs - window);
        const uint32_t toRead = remaining < ISO_CACHE_BYTES ? remaining : ISO_CACHE_BYTES;
        self._isoCacheValid = 0;
        if (!self._isoFile.seek(window)) return -1;
        if (self._isoFile.read(self._isoCache, toRead) != toRead) return -1;
        self._isoCacheStart = window;
        self._isoCacheValid = toRead;
      }
      const uint32_t within = pos - self._isoCacheStart;
      uint32_t chunk = self._isoCacheValid - within;
      if (chunk > bufsize - copied) chunk = bufsize - copied;
      memcpy(dst + copied, self._isoCache + within, chunk);
      copied += chunk;
    }
    self._reads += (bufsize + bs - 1) / bs;
    return (int32_t)bufsize;
  }
  uint8_t* dst = static_cast<uint8_t*>(buffer);

  // Fast path: TinyUSB's EP buffer (4096) >= block size (512), so reads are
  // always sector-aligned (offset == 0, bufsize a whole number of sectors).
  if (offset == 0 && (bufsize % bs) == 0) {
    const uint32_t n = bufsize / bs;
    if (!st->readBlocks(lba, dst, n)) return -1;
    self._reads += n;
    return (int32_t)bufsize;
  }

  // Safety fallback for unaligned/partial requests: stage one sector at a time.
  uint8_t sector[512];
  if (bs > sizeof(sector)) return -1;
  uint32_t done = 0;
  uint32_t curLba = lba + offset / bs;
  uint32_t curOff = offset % bs;
  while (done < bufsize) {
    if (!st->readBlocks(curLba, sector, 1)) return -1;
    self._reads += 1;
    uint32_t chunk = bs - curOff;
    if (chunk > bufsize - done) chunk = bufsize - done;
    memcpy(dst + done, sector + curOff, chunk);
    done   += chunk;
    curLba += 1;
    curOff  = 0;
  }
  return (int32_t)bufsize;
}

int32_t UsbMscUtil::_onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  UsbMscUtil& self = instance();
  if (self._isoMode) return -1;  // Never write into the ISO file.
  IStorage* st = self._storage;
  if (!st || !buffer) return -1;
  const uint16_t bs = self._blockSize;
  if (bs == 0) return -1;

  // Fast path: sector-aligned full-sector writes.
  if (offset == 0 && (bufsize % bs) == 0) {
    const uint32_t n = bufsize / bs;
    if (!st->writeBlocks(lba, buffer, n)) return -1;
    self._writes += n;
    return (int32_t)bufsize;
  }

  // Read-modify-write fallback for unaligned/partial sectors.
  uint8_t sector[512];
  if (bs > sizeof(sector)) return -1;
  uint32_t done = 0;
  uint32_t curLba = lba + offset / bs;
  uint32_t curOff = offset % bs;
  while (done < bufsize) {
    uint32_t chunk = bs - curOff;
    if (chunk > bufsize - done) chunk = bufsize - done;
    if (chunk != bs) {
      // Partial sector — preserve the bytes we aren't overwriting.
      if (!st->readBlocks(curLba, sector, 1)) return -1;
    }
    memcpy(sector + curOff, buffer + done, chunk);
    if (!st->writeBlocks(curLba, sector, 1)) return -1;
    self._writes += 1;
    done   += chunk;
    curLba += 1;
    curOff  = 0;
  }
  return (int32_t)bufsize;
}

bool UsbMscUtil::_onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
  (void)power_condition;
  // Host-initiated eject (e.g. "Safely Remove"): start=0, load_eject=1.
  if (!start && load_eject) {
    instance()._hostEjected = true;
  }
  return true;
}

#endif  // DEVICE_HAS_USB_HID
