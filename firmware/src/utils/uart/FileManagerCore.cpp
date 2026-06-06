#include "utils/uart/FileManagerCore.h"
#include "core/Device.h"
#include "FirmwareVersion.h"

void FileManagerCore::reset() {
  resetParser();
  if (_putFile) _putFile.close();
  _putActive = false;
  _putPath = "";
  if (_getFile) _getFile.close();
  _getActive = false;
  _getSeq = 0;
  _getCtx = 0;
}

void FileManagerCore::pump() {
  if (!_getActive || !_getFile) return;
  // Streaming a 300 KB file as a single blocking loop starves the BLE TX
  // queue and trips the task watchdog. Instead, send one chunk per pump()
  // (per main-loop iteration) so other work — including BLE notification
  // draining — gets a chance to run between frames.
  static uint8_t buf[2048];
  uint32_t want = _getChunkSize;
  if (want > sizeof(buf)) want = sizeof(buf);
  int n = _getFile.read(buf, want);
  if (n <= 0) {
    // Drained — emit the zero-length terminator and close.
    sendFrame(_getCtx, T_GET_CHUNK, _getSeq, nullptr, 0);
    _getFile.close();
    _getActive = false;
    return;
  }
  sendFrame(_getCtx, T_GET_CHUNK, _getSeq, buf, (uint32_t)n);
}

void FileManagerCore::onFrame(uint8_t ctx, uint8_t type, uint8_t seq, uint8_t* payload, uint32_t len) {
  if (ctx != CTX_FM) return; // another codec owns this context
  _ctx = ctx;
  switch (type) {
    case C_INFO:      _handleInfo(seq); break;
    case C_LS:        _handleLs(seq, (const char*)payload); break;
    case C_STAT:      _handleStat(seq, (const char*)payload); break;
    case C_GET:       _handleGet(seq, (const char*)payload); break;
    case C_PUT_BEGIN: _handlePutBegin(seq, payload, len); break;
    case C_PUT_CHUNK: _handlePutChunk(seq, payload, len); break;
    case C_PUT_END:   _handlePutEnd(seq); break;
    case C_RM:        _handleRm(seq, (const char*)payload); break;
    case C_MV:        _handleMv(seq, payload, len); break;
    case C_MKDIR:     _handleMkdir(seq, (const char*)payload); break;
    case C_TOUCH:     _handleTouch(seq, (const char*)payload); break;
    default:          sendErr(_ctx, seq, "unknown command");
  }
}

void FileManagerCore::_handleInfo(uint8_t seq) {
  uint64_t total = (Uni.Storage && Uni.Storage->isAvailable()) ? Uni.Storage->totalBytes() : 0;
  uint64_t used  = (Uni.Storage && Uni.Storage->isAvailable()) ? Uni.Storage->usedBytes()  : 0;
  uint32_t heap  = (uint32_t)ESP.getFreeHeap();
  char buf[256];
  int n = snprintf(buf, sizeof(buf),
    "{\"name\":\"UniGeek\",\"version\":\"%s\",\"total\":%llu,\"used\":%llu,\"heap\":%u}",
    FIRMWARE_VERSION,
    (unsigned long long)total,
    (unsigned long long)used,
    (unsigned)heap);
  if (n < 0) n = 0;
  if ((size_t)n > sizeof(buf)) n = sizeof(buf);
  sendFrame(_ctx, T_OK, seq, (const uint8_t*)buf, (uint32_t)n);
}

void FileManagerCore::_handleLs(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  String p = (path && *path) ? String(path) : String("/");
  fs::File dir = Uni.Storage->open(p.c_str(), FILE_READ);
  if (!dir || !dir.isDirectory()) { sendErr(_ctx, seq, "not a directory"); return; }

  String resp;
  resp.reserve(1024);
  while (true) {
    fs::File f = dir.openNextFile();
    if (!f) break;
    String row = (f.isDirectory() ? "DIR:" : "FILE:");
    row += String(f.name());
    row += ":";
    row += String(f.size());
    row += "\n";
    if (resp.length() + row.length() > MAX_PAYLOAD - 64) { f.close(); break; }
    resp += row;
    f.close();
  }
  dir.close();
  sendFrame(_ctx, T_OK, seq, (const uint8_t*)resp.c_str(), resp.length());
}

void FileManagerCore::_handleStat(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (!path || !*path) { sendErr(_ctx, seq, "no path"); return; }
  if (!Uni.Storage->exists(path)) { sendErr(_ctx, seq, "not found"); return; }
  fs::File f = Uni.Storage->open(path, FILE_READ);
  if (!f) { sendErr(_ctx, seq, "open failed"); return; }
  bool   isDir = f.isDirectory();
  size_t sz    = isDir ? 0 : f.size();
  f.close();
  char buf[64];
  int  n = snprintf(buf, sizeof(buf), "%s:%u", isDir ? "DIR" : "FILE", (unsigned)sz);
  sendFrame(_ctx, T_OK, seq, (const uint8_t*)buf, (uint32_t)n);
}

void FileManagerCore::_handleGet(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (!path || !*path)                              { sendErr(_ctx, seq, "no path");    return; }
  if (!Uni.Storage->exists(path))                   { sendErr(_ctx, seq, "not found");  return; }

  fs::File f = Uni.Storage->open(path, FILE_READ);
  if (!f)              { sendErr(_ctx, seq, "open failed"); return; }
  if (f.isDirectory()) { f.close(); sendErr(_ctx, seq, "is directory"); return; }

  // Set up streaming state. Actual chunk emission happens in pump() so the
  // main loop can keep running between frames (avoids BLE TX-queue starvation
  // and watchdog timeouts on big reads). A new GET supersedes any in-flight
  // GET on this transport.
  if (_getFile) _getFile.close();
  _getFile   = f;
  _getActive = true;
  _getSeq    = seq;
  _getCtx    = _ctx;
}

void FileManagerCore::_handlePutBegin(uint8_t seq, const uint8_t* payload, uint32_t len) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (len < 5) { sendErr(_ctx, seq, "bad payload"); return; }
  String path((const char*)(payload + 4));
  if (path.length() == 0) { sendErr(_ctx, seq, "no path"); return; }

  int sl = path.lastIndexOf('/');
  if (sl > 0) Uni.Storage->makeDir(path.substring(0, sl).c_str());

  if (_putFile) _putFile.close();
  _putFile = Uni.Storage->open(path.c_str(), FILE_WRITE);
  if (!_putFile) { _putActive = false; sendErr(_ctx, seq, "open failed"); return; }
  _putPath   = path;
  _putActive = true;
  sendOk(_ctx, seq);
}

void FileManagerCore::_handlePutChunk(uint8_t seq, const uint8_t* data, uint32_t len) {
  if (!_putActive || !_putFile) { sendErr(_ctx, seq, "no upload active"); return; }
  size_t w = _putFile.write(data, len);
  if (w != len) { sendErr(_ctx, seq, "short write"); return; }
  sendOk(_ctx, seq);
}

void FileManagerCore::_handlePutEnd(uint8_t seq) {
  if (_putFile) _putFile.close();
  _putActive = false;
  _putPath   = "";
  sendOk(_ctx, seq);
}

bool FileManagerCore::_removeDirectory(const String& path) {
  fs::File dir = Uni.Storage->open(path.c_str(), FILE_READ);
  if (!dir || !dir.isDirectory()) return false;
  fs::File f = dir.openNextFile();
  while (f) {
    String fp = String(f.path());
    if (f.isDirectory()) _removeDirectory(fp);
    else                 Uni.Storage->deleteFile(fp.c_str());
    f = dir.openNextFile();
  }
  dir.close();
  return Uni.Storage->removeDir(path.c_str());
}

void FileManagerCore::_handleRm(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (!path || !*path)                              { sendErr(_ctx, seq, "no path");    return; }
  if (!Uni.Storage->exists(path))                   { sendErr(_ctx, seq, "not found");  return; }
  fs::File f     = Uni.Storage->open(path, FILE_READ);
  bool     isDir = f && f.isDirectory();
  if (f) f.close();
  bool ok = isDir ? _removeDirectory(String(path)) : Uni.Storage->deleteFile(path);
  ok ? sendOk(_ctx, seq) : sendErr(_ctx, seq, "delete failed");
}

void FileManagerCore::_handleMv(uint8_t seq, const uint8_t* payload, uint32_t len) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  uint32_t i = 0;
  while (i < len && payload[i] != 0) i++;
  if (i >= len) { sendErr(_ctx, seq, "bad payload"); return; }
  String src((const char*)payload);
  String dst((const char*)(payload + i + 1));
  if (src.length() == 0 || dst.length() == 0) { sendErr(_ctx, seq, "bad src/dst"); return; }
  if (!Uni.Storage->exists(src.c_str()))      { sendErr(_ctx, seq, "src not found"); return; }
  bool ok = Uni.Storage->renameFile(src.c_str(), dst.c_str());
  ok ? sendOk(_ctx, seq) : sendErr(_ctx, seq, "rename failed");
}

void FileManagerCore::_handleMkdir(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (!path || !*path)                              { sendErr(_ctx, seq, "no path");    return; }
  bool ok = Uni.Storage->makeDir(path);
  ok ? sendOk(_ctx, seq) : sendErr(_ctx, seq, "mkdir failed");
}

void FileManagerCore::_handleTouch(uint8_t seq, const char* path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { sendErr(_ctx, seq, "no storage"); return; }
  if (!path || !*path)                              { sendErr(_ctx, seq, "no path");    return; }
  fs::File f = Uni.Storage->open(path, FILE_WRITE);
  if (!f) { sendErr(_ctx, seq, "open failed"); return; }
  f.close();
  sendOk(_ctx, seq);
}
