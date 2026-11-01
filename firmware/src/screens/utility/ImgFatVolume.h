#pragma once
#include "ImgPartitionDevice.h"
#include <ff.h>
#include <diskio.h>
#include <stdint.h>

// FatFs volume backed by the FILES partition of an IMG.
class ImgFatVolume {
public:
  ~ImgFatVolume() { close(); }
  bool open(IStorage& storage, const String& path, String& error);
  void close();
  bool copyFileToStorage(const String& source, IStorage& storage,
                         const String& destination, String& error);
  bool renameEntry(const String& source, const String& destination, String& error);
  bool deleteEntry(const String& path, String& error);
  bool _deleteTree(const String& path, uint8_t depth, String& error);
  bool list(const String& relativePath, String* names, bool* dirs,
            uint32_t* sizes, uint8_t capacity, uint8_t& count);
private:
  ImgPartitionDevice _device;
  FATFS _fs = {};
  uint8_t _drive = 0xff;
  String _prefix;
  static ImgFatVolume* _active;
  static DSTATUS init(BYTE);
  static DSTATUS status(BYTE);
  static DRESULT read(BYTE, BYTE*, DWORD, UINT);
  static DRESULT write(BYTE, const BYTE*, DWORD, UINT);
  static DRESULT ioctl(BYTE, BYTE, void*);
};
