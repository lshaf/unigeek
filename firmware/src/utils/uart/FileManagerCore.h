#pragma once

// File-manager subsystem of the Serial multi-context protocol (ctx 'F'). Owns
// the file command handlers; the frame parser / builder / CRC live in the
// shared FrameCodec base. A transport (UART, BLE NUS, …) feeds bytes via
// onByte() and provides a SendFn for outbound bytes.

#include "utils/uart/FrameCodec.h"
#include <FS.h>

class FileManagerCore : public FrameCodec {
public:
  static constexpr uint32_t MAX_PAYLOAD = 8192;

  // Subsystem context (ASCII letter; easy to spot in raw byte dumps).
  static constexpr uint8_t CTX_FM = 'F';

  static constexpr uint8_t T_GET_CHUNK = 0xF2;

  // FM command types (only valid when ctx == CTX_FM).
  static constexpr uint8_t C_INFO      = 0x01;
  static constexpr uint8_t C_LS        = 0x02;
  static constexpr uint8_t C_STAT      = 0x03;
  static constexpr uint8_t C_GET       = 0x10;
  static constexpr uint8_t C_PUT_BEGIN = 0x20;
  static constexpr uint8_t C_PUT_CHUNK = 0x21;
  static constexpr uint8_t C_PUT_END   = 0x22;
  static constexpr uint8_t C_RM        = 0x30;
  static constexpr uint8_t C_MV        = 0x31;
  static constexpr uint8_t C_MKDIR     = 0x32;
  static constexpr uint8_t C_TOUCH     = 0x33;

  FileManagerCore() : FrameCodec(_rxBuf, MAX_PAYLOAD) {}

  // Max bytes pump() reads from the file per main-loop iteration. Smaller
  // values keep each GET response frame within a single BLE notification,
  // which is critical because NimBLE's outbound mbuf pool only holds ~12
  // buffers — a 2 KB frame splits into 12 notifies and exhausts the pool,
  // so subsequent frames drop silently.
  void setGetChunkSize(uint32_t bytes) { _getChunkSize = bytes ? bytes : 1; }

  // Advance any in-flight GET by one chunk. Must be called from the same
  // task that owns the transport (main loop), once per iteration. Without
  // this, a multi-MB read would block the main task long enough to starve
  // the BLE TX queue and trip the watchdog.
  void pump();

  // Drops any in-flight PUT/GET and resets the parser. Call when the
  // transport disconnects so a half-finished transfer doesn't leave a
  // dangling fs::File.
  void reset();

protected:
  void onFrame(uint8_t ctx, uint8_t type, uint8_t seq, uint8_t* payload, uint32_t len) override;

private:
  uint8_t  _rxBuf[MAX_PAYLOAD + 1]; // +1 for null-terminator after string commands

  // Per-frame context for the handlers (set in onFrame).
  uint8_t  _ctx = 0;

  fs::File _putFile;
  String   _putPath;
  bool     _putActive  = false;

  fs::File _getFile;
  bool     _getActive  = false;
  uint8_t  _getSeq     = 0;
  uint8_t  _getCtx     = 0;
  uint32_t _getChunkSize = 2048; // default for transports that don't override

  void _handleInfo(uint8_t seq);
  void _handleLs(uint8_t seq, const char* path);
  void _handleStat(uint8_t seq, const char* path);
  void _handleGet(uint8_t seq, const char* path);
  void _handlePutBegin(uint8_t seq, const uint8_t* payload, uint32_t len);
  void _handlePutChunk(uint8_t seq, const uint8_t* data, uint32_t len);
  void _handlePutEnd(uint8_t seq);
  void _handleRm(uint8_t seq, const char* path);
  void _handleMv(uint8_t seq, const uint8_t* payload, uint32_t len);
  void _handleMkdir(uint8_t seq, const char* path);
  void _handleTouch(uint8_t seq, const char* path);

  bool _removeDirectory(const String& path);
};
