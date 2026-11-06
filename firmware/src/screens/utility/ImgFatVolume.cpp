#include "ImgFatVolume.h"
#include <diskio_impl.h>
#include <esp_err.h>
#include <string.h>

ImgFatVolume* ImgFatVolume::_active = nullptr;

DSTATUS ImgFatVolume::init(BYTE drive) { return status(drive); }
DSTATUS ImgFatVolume::status(BYTE drive) {
  return _active && _active->_drive == drive && _active->_device.isOpen() ? 0 : STA_NOINIT;
}
DRESULT ImgFatVolume::read(BYTE drive, BYTE* dst, DWORD sector, UINT count) {
  if (!_active || _active->_drive != drive || !dst || !count) return RES_PARERR;
  if (uint64_t(sector) + count > _active->_device.sectorCount()) return RES_PARERR;
  for (UINT i = 0; i < count; ++i)
    if (!_active->_device.readSector(sector + i, dst + size_t(i) * 512)) return RES_ERROR;
  return RES_OK;
}
DRESULT ImgFatVolume::write(BYTE drive, const BYTE* src, DWORD sector, UINT count) {
  if (!_active || _active->_drive != drive || !src || !count) return RES_PARERR;
  if (uint64_t(sector) + count > _active->_device.sectorCount()) return RES_PARERR;
  for (UINT i = 0; i < count; ++i)
    if (!_active->_device.writeSector(sector + i, src + size_t(i) * 512)) return RES_ERROR;
  return RES_OK;
}
DRESULT ImgFatVolume::ioctl(BYTE drive, BYTE cmd, void* buffer) {
  if (!_active || _active->_drive != drive) return RES_PARERR;
  if (cmd == CTRL_SYNC) return _active->_device.sync() ? RES_OK : RES_NOTRDY;
  if (!buffer) return RES_PARERR;
  switch (cmd) {
    case GET_SECTOR_COUNT: *static_cast<DWORD*>(buffer) = _active->_device.sectorCount(); return RES_OK;
    case GET_SECTOR_SIZE: *static_cast<WORD*>(buffer) = 512; return RES_OK;
    case GET_BLOCK_SIZE: *static_cast<DWORD*>(buffer) = 1; return RES_OK;
    default: return RES_PARERR;
  }
}

bool ImgFatVolume::open(IStorage& storage, const String& path, String& error) {
  close();
  if (_active) { error = "IMG volume busy"; return false; }
  if (!_device.open(storage, path, error)) return false;
  BYTE drive;
  if (ff_diskio_get_drive(&drive) != ESP_OK) {
    error = "No free FatFs drive"; _device.close(); return false;
  }
  _drive = drive;
  _active = this;
  static const ff_diskio_impl_t driver = {init, status, read, write, ioctl};
  ff_diskio_register(_drive, &driver);
  _prefix = String(_drive) + ":";
  if (f_mount(&_fs, _prefix.c_str(), 1) != FR_OK) {
    error = "Cannot mount FILES"; close(); return false;
  }
  return true;
}

void ImgFatVolume::close() {
  if (_drive != 0xff) {
    f_mount(nullptr, _prefix.c_str(), 0);
    ff_diskio_register(_drive, nullptr);
    _drive = 0xff;
  }
  if (_active == this) _active = nullptr;
  _device.close();
  _prefix = "";
}

bool ImgFatVolume::list(const String& relativePath, String* names, bool* dirs,
                        uint32_t* sizes, uint8_t capacity, uint8_t& count) {
  count = 0;
  if (_drive == 0xff) return false;
  String fullPath = _prefix + relativePath;
  FF_DIR directory;
  if (f_opendir(&directory, fullPath.c_str()) != FR_OK) return false;
  FILINFO info;
  bool ok = true;
  while (count < capacity) {
    FRESULT rc = f_readdir(&directory, &info);
    if (rc != FR_OK) { ok = false; break; }
    if (!info.fname[0]) break;
    if (!strcmp(info.fname, ".") || !strcmp(info.fname, "..")) continue;
    names[count] = info.fname;
    dirs[count] = (info.fattrib & AM_DIR) != 0;
    sizes[count] = info.fsize;
    ++count;
  }
  if (f_closedir(&directory) != FR_OK) ok = false;
  return ok;
}

bool ImgFatVolume::renameEntry(const String& source, const String& destination, String& error) {
  if (_drive == 0xff) { error = "IMG not mounted"; return false; }
  if (source.isEmpty() || destination.isEmpty() || source == "/" || destination == "/") {
    error = "Invalid path"; return false;
  }
  FILINFO info;
  const String from = _prefix + source;
  const String to = _prefix + destination;
  FRESULT rc = f_stat(to.c_str(), &info);
  if (rc == FR_OK) { error = "Already exists"; return false; }
  if (rc != FR_NO_FILE) { error = "Destination error (" + String(int(rc)) + ")"; return false; }
  rc = f_rename(from.c_str(), to.c_str());
  if (rc != FR_OK) { error = "Rename failed (" + String(int(rc)) + ")"; return false; }
  return true;
}

bool ImgFatVolume::_deleteTree(const String& path, uint8_t depth, String& error) {
  // Bounded recursion avoids exhausting the ESP32 task stack on malformed trees.
  if (depth >= 16) { error = "Folder nesting too deep"; return false; }
  const String full = _prefix + path;
  FILINFO stat;
  FRESULT rc = f_stat(full.c_str(), &stat);
  if (rc != FR_OK) { error = "Cannot inspect entry (" + String(int(rc)) + ")"; return false; }
  if (stat.fattrib & AM_DIR) {
    FF_DIR dir;
    rc = f_opendir(&dir, full.c_str());
    if (rc != FR_OK) { error = "Cannot open folder (" + String(int(rc)) + ")"; return false; }
    bool ok = true;
    while (ok) {
      FILINFO child;
      rc = f_readdir(&dir, &child);
      if (rc != FR_OK) { error = "Cannot read folder (" + String(int(rc)) + ")"; ok = false; break; }
      if (!child.fname[0]) break;
      if (!strcmp(child.fname, ".") || !strcmp(child.fname, "..")) continue;
      // A directory cannot be removed until all of its children are gone.
      if (!_deleteTree(path + "/" + child.fname, depth + 1, error)) ok = false;
    }
    const FRESULT closeRc = f_closedir(&dir);
    if (closeRc != FR_OK) { error = "Cannot close folder"; return false; }
    if (!ok) return false;
  }
  rc = f_unlink(full.c_str());
  if (rc != FR_OK) { error = "Delete failed (" + String(int(rc)) + ")"; return false; }
  return true;
}

bool ImgFatVolume::deleteEntry(const String& path, String& error) {
  if (_drive == 0xff) { error = "IMG not mounted"; return false; }
  if (path.isEmpty() || path == "/" || path.indexOf("..") >= 0 ||
      path.indexOf('\\') >= 0 || path.indexOf(':') >= 0) {
    error = "Invalid path"; return false;
  }
  return _deleteTree(path, 0, error);
}

// Stream directly from the IMG FatFs volume to the SD filesystem.
// Destination is created exclusively; never overwrite an existing file.
bool ImgFatVolume::copyFileToStorage(const String& source, IStorage& storage,
                                     const String& destination, String& error) {
  if (_drive == 0xff) { error = "IMG not mounted"; return false; }
  if (source.isEmpty() || source == "/" || destination.isEmpty() ||
      destination[0] != '/' || destination.indexOf("..") >= 0 ||
      destination.indexOf('\\') >= 0 || destination.indexOf(':') >= 0) {
    error = "Invalid path"; return false;
  }
  if (storage.exists(destination.c_str())) { error = "Already exists"; return false; }
  FIL input;
  FRESULT rc = f_open(&input, (_prefix + source).c_str(), FA_READ);
  if (rc != FR_OK) { error = "Cannot open IMG file"; return false; }
  // Use w (truncate) only after checking existence; no existing destination is touched.
  fs::File output = storage.open(destination.c_str(), "w");
  if (!output) { f_close(&input); error = "Cannot create SD file"; return false; }
  uint8_t buffer[1024];
  bool ok = true;
  while (true) {
    UINT readCount = 0;
    rc = f_read(&input, buffer, sizeof(buffer), &readCount);
    if (rc != FR_OK) { error = "IMG read failed"; ok = false; break; }
    if (!readCount) break;
    if (output.write(buffer, readCount) != readCount) {
      error = "SD write failed"; ok = false; break;
    }
  }
  output.flush();
  if (output.getWriteError()) { error = "SD flush failed"; ok = false; }
  output.close();
  if (f_close(&input) != FR_OK) { error = "IMG close failed"; ok = false; }
  if (!ok && !storage.deleteFile(destination.c_str()))
    error += "; partial SD file may remain";
  return ok;
}

