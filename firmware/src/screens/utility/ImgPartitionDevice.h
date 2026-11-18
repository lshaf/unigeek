#pragma once

#include "core/IStorage.h"
#include <FS.h>
#include <stdint.h>

// Writable 512-byte sector view over the third MBR partition of a disk IMG.
// Owns the image handle; closing this device releases the SD file.
class ImgPartitionDevice {
public:
  ~ImgPartitionDevice() { close(); }
  bool open(IStorage& storage, const String& path, String& error);
  void close();
  bool readSector(uint32_t relativeLba, uint8_t* destination);
  bool writeSector(uint32_t relativeLba, const uint8_t* source);
  bool sync();
  bool isOpen() const { return bool(_image); }
  uint32_t sectorCount() const { return _sectors; }

private:
  fs::File _image;
  uint32_t _startLba = 0;
  uint32_t _sectors = 0;
};
