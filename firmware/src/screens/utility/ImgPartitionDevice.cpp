#include "ImgPartitionDevice.h"

namespace {
uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

void ImgPartitionDevice::close() {
  if (_image) _image.close();
  _startLba = 0;
  _sectors = 0;
}

bool ImgPartitionDevice::open(IStorage& storage, const String& path, String& error) {
  close();
  _image = storage.open(path.c_str(), "r+");
  if (!_image) { error = "Cannot open IMG"; return false; }
  const uint64_t bytes = _image.size();
  uint8_t mbr[512];
  if (bytes < 1024 || (bytes % 512) || !_image.seek(0) ||
      _image.read(mbr, sizeof(mbr)) != sizeof(mbr) ||
      mbr[510] != 0x55 || mbr[511] != 0xaa) {
    error = "Invalid IMG MBR";
    close();
    return false;
  }
  const uint8_t* entry = mbr + 446 + 2 * 16;
  const uint32_t start = readLe32(entry + 8);
  const uint32_t sectors = readLe32(entry + 12);
  const uint64_t end = uint64_t(start) + sectors;
  if ((entry[4] != 0x0b && entry[4] != 0x0c) || !start || !sectors ||
      end > bytes / 512 || end * 512ULL > UINT32_MAX) {
    error = "FILES partition invalid";
    close();
    return false;
  }
  _startLba = start;
  _sectors = sectors;
  return true;
}

bool ImgPartitionDevice::readSector(uint32_t relativeLba, uint8_t* destination) {
  if (!_image || !destination || relativeLba >= _sectors) return false;
  const uint64_t offset = (uint64_t(_startLba) + relativeLba) * 512ULL;
  const bool ok = offset <= UINT32_MAX && _image.seek(uint32_t(offset)) &&
                  _image.read(destination, 512) == 512;
  return ok;
}

bool ImgPartitionDevice::writeSector(uint32_t relativeLba, const uint8_t* source) {
  if (!_image || !source || relativeLba >= _sectors) return false;
  const uint64_t offset = (uint64_t(_startLba) + relativeLba) * 512ULL;
  return offset <= UINT32_MAX && _image.seek(uint32_t(offset)) &&
         _image.write(source, 512) == 512;
}

bool ImgPartitionDevice::sync() {
  if (!_image) return false;
  _image.flush();
  return true;
}
