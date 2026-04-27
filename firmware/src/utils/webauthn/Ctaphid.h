#pragma once

#include <stdint.h>
#include <stddef.h>
#include "WebAuthnConfig.h"

namespace webauthn {

class USBFidoUtil;

// Ctaphid — assembles inbound 64-byte FIDO HID reports into full CTAPHID
// messages, dispatches to the upper layer (CBOR for CTAP2, MSG for U2F, or
// the built-in PING/INIT/WINK/CANCEL handlers), and chunks responses back
// out across 64-byte input reports.
//
// Ownership: holds a static reassembly buffer of size kCtapMaxMsgSize
// (~7.4 KB). One instance per device.
//
// Threading: onReport() is called from the USB ISR via USBFidoUtil's drained
// queue (so really the main loop), and tick() runs each loop iteration. The
// upper-layer handler runs synchronously on the same thread, so no
// per-handler locking is needed.
class Ctaphid {
public:
  // Application callback. Invoked once per assembled message for non-builtin
  // commands (CTAPHID_CBOR, CTAPHID_MSG). Should produce the response
  // payload in `resp` and return its length. Return 0 for empty response.
  // `respCmd` defaults to `cmd`; the handler can override it (e.g. send
  // CTAPHID_ERROR) by writing a different value.
  using HandlerFn = uint16_t (*)(uint8_t  cmd,
                                 const uint8_t* req, uint16_t reqLen,
                                 uint8_t* resp,      uint16_t respMax,
                                 uint8_t* respCmd,
                                 void*    user);

  Ctaphid();

  void setSender(USBFidoUtil* fido) { _fido = fido; }
  void setHandler(HandlerFn h, void* user = nullptr) { _handler = h; _handlerUser = user; }

  // Called once per inbound 64-byte FIDO output report (drained from
  // USBFidoUtil::poll's callback).
  void onReport(const uint8_t* buf64);

  // Drives timeouts. Call every loop iteration with millis().
  void tick(uint32_t nowMs);

  // Send a CTAPHID frame on a specific channel (used by upper layers).
  void sendError(uint32_t cid, uint8_t code);
  void sendKeepalive(uint32_t cid, uint8_t status);

private:
  enum State : uint8_t { ST_IDLE, ST_ASSEMBLING };

  USBFidoUtil* _fido        = nullptr;
  HandlerFn    _handler     = nullptr;
  void*        _handlerUser = nullptr;

  // Reassembly state
  State    _state      = ST_IDLE;
  uint32_t _curCid     = 0;
  uint8_t  _curCmd     = 0;
  uint16_t _expLen     = 0;
  uint16_t _gotLen     = 0;
  uint8_t  _expSeq     = 0;
  uint32_t _lastRxMs   = 0;

  // CID allocation. We hand out incrementing IDs starting above 0x10000000
  // so they never collide with 0x00000000 (reserved) or 0xFFFFFFFF
  // (broadcast). Clients are expected to discard stale CIDs after timeout.
  uint32_t _nextCid    = 0x10000001;

  // Reassembly buffer (sized for the spec maximum).
  uint8_t  _buf[kCtapMaxMsgSize];

  // Scratch response buffer — handlers fill this and we fragment it back
  // out. Same size cap as the request side.
  uint8_t  _respBuf[kCtapMaxMsgSize];

  // Internal handlers
  void _handleAssembled();
  void _handleInit(const uint8_t* nonce, uint16_t nonceLen);
  void _handlePing();
  void _handleWink();
  void _handleCancel();

  // Send a complete payload as init + continuation packets
  void _sendPayload(uint32_t cid, uint8_t cmd, const uint8_t* data, uint16_t len);

  void _reset();
};

}  // namespace webauthn
