#include "Ctaphid.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include "USBFidoUtil.h"
#include "WebAuthnLog.h"

#include <string.h>

namespace webauthn {

static constexpr uint8_t  kInitDataMax = 57;
static constexpr uint8_t  kContDataMax = 59;
static constexpr uint8_t  kProtoVersion = 2;  // CTAPHID protocol version

// Firmware version reported in the CTAPHID_INIT response. Bumped per
// release; currently picked to match the user-facing 1.7.x line.
static constexpr uint8_t  kVerMajor = 1;
static constexpr uint8_t  kVerMinor = 7;
static constexpr uint8_t  kVerBuild = 1;

static constexpr uint8_t  kCapFlags = CAPFLAG_WINK | CAPFLAG_CBOR;  // NMSG=0

Ctaphid::Ctaphid() = default;

void Ctaphid::onReport(const uint8_t* p)
{
  // Layout — both packet types start with the 4-byte channel ID:
  //   init: CID(4) | CMD(1, MSB=1) | BCNT(2 BE) | DATA[..57]
  //   cont: CID(4) | SEQ(1, MSB=0) | DATA[..59]
  uint32_t cid;
  memcpy(&cid, p, 4);
  uint8_t  marker = p[4];
  bool     isInit = (marker & 0x80) != 0;

  _lastRxMs = (uint32_t)millis();

  if (isInit) {
    uint8_t  cmd  = marker & 0x7F;
    uint16_t bcnt = ((uint16_t)p[5] << 8) | p[6];
    WA_LOG("CTAPHID rx INIT cid=%08lx cmd=0x%02x bcnt=%u",
           (unsigned long)cid, cmd, bcnt);

    // Special-case: CTAPHID_INIT may arrive on broadcast CID, and is allowed
    // to interrupt any ongoing transaction (per spec — used to recover
    // wedged sessions). It must always fit in a single packet (8B nonce).
    if (cmd == CTAPHID_INIT) {
      if (bcnt != 8 || bcnt > kInitDataMax) {
        sendError(cid, ERR_INVALID_LEN);
        _reset();
        return;
      }
      _curCid = cid;
      _handleInit(p + 7, bcnt);
      return;
    }

    // Normal init packet for any other command.
    if (cid == 0 || cid == kHidBroadcastCid) {
      sendError(cid, ERR_INVALID_CHANNEL);
      return;
    }
    if (_state == ST_ASSEMBLING && cid != _curCid) {
      // Another channel is busy — reject.
      sendError(cid, ERR_CHANNEL_BUSY);
      return;
    }
    if (bcnt > kCtapMaxMsgSize) {
      sendError(cid, ERR_INVALID_LEN);
      _reset();
      return;
    }

    _state   = ST_ASSEMBLING;
    _curCid  = cid;
    _curCmd  = cmd;
    _expLen  = bcnt;
    _gotLen  = 0;
    _expSeq  = 0;

    uint16_t take = bcnt < kInitDataMax ? bcnt : kInitDataMax;
    memcpy(_buf, p + 7, take);
    _gotLen = take;

    if (_gotLen >= _expLen) _handleAssembled();
    return;
  }

  // ── Continuation packet ───────────────────────────────────────────────
  if (_state != ST_ASSEMBLING || cid != _curCid) {
    WA_LOG("CTAPHID rx CONT dropped: state=%d cid=%08lx curCid=%08lx",
           (int)_state, (unsigned long)cid, (unsigned long)_curCid);
    return;
  }
  uint8_t seq = marker;  // already MSB=0
  WA_LOG("CTAPHID rx CONT cid=%08lx seq=%u", (unsigned long)cid, seq);
  if (seq != _expSeq) {
    sendError(cid, ERR_INVALID_SEQ);
    _reset();
    return;
  }
  _expSeq++;

  uint16_t remain = _expLen - _gotLen;
  uint16_t take   = remain < kContDataMax ? remain : kContDataMax;
  memcpy(_buf + _gotLen, p + 5, take);
  _gotLen += take;

  if (_gotLen >= _expLen) _handleAssembled();
}

void Ctaphid::tick(uint32_t nowMs)
{
  if (_state != ST_ASSEMBLING) return;
  if ((uint32_t)(nowMs - _lastRxMs) >= kCtapTxnTimeout) {
    sendError(_curCid, ERR_MSG_TIMEOUT);
    _reset();
  }
}

// ── Handlers ────────────────────────────────────────────────────────────

void Ctaphid::_handleAssembled()
{
  uint32_t cid = _curCid;
  uint8_t  cmd = _curCmd;
  uint16_t len = _gotLen;
  WA_LOG("CTAPHID dispatch cid=%08lx cmd=0x%02x len=%u",
         (unsigned long)cid, cmd, len);

  switch (cmd) {
    case CTAPHID_PING:   _sendPayload(cid, CTAPHID_PING, _buf, len); break;
    case CTAPHID_WINK:   _handleWink();                              break;
    case CTAPHID_CANCEL: _handleCancel();                            break;
    case CTAPHID_CBOR:
    case CTAPHID_MSG: {
      if (!_handler) {
        sendError(cid, ERR_INVALID_CMD);
        break;
      }
      uint8_t  respCmd = cmd;
      uint16_t respLen = _handler(cmd, _buf, len,
                                  _respBuf, sizeof(_respBuf),
                                  &respCmd, _handlerUser);
      _sendPayload(cid, respCmd, _respBuf, respLen);
      break;
    }
    default:
      sendError(cid, ERR_INVALID_CMD);
      break;
  }
  _reset();
}

void Ctaphid::_handleInit(const uint8_t* nonce, uint16_t nonceLen)
{
  // Allocate a new channel ID. If the request came on broadcast, give the
  // client a fresh CID. If it came on a specific CID, the spec says we MAY
  // still hand back a new one — we do, which has the side effect of
  // "synchronizing" any wedged session.
  uint32_t newCid = _nextCid++;

  uint8_t resp[17];
  memcpy(resp, nonce, 8);
  memcpy(resp + 8, &newCid, 4);
  resp[12] = kProtoVersion;
  resp[13] = kVerMajor;
  resp[14] = kVerMinor;
  resp[15] = kVerBuild;
  resp[16] = kCapFlags;

  _sendPayload(_curCid, CTAPHID_INIT, resp, sizeof(resp));
  _reset();
}

void Ctaphid::_handlePing()
{
  _sendPayload(_curCid, CTAPHID_PING, _buf, _gotLen);
}

void Ctaphid::_handleWink()
{
  // No-op for now. UI hook will plug in here later (Phase 8).
  _sendPayload(_curCid, CTAPHID_WINK, nullptr, 0);
}

void Ctaphid::_handleCancel()
{
  // Cancellation is observed by the in-flight CTAP2 handler via a future
  // shared flag (Phase 6). For now, just reply empty.
  _sendPayload(_curCid, CTAPHID_CANCEL, nullptr, 0);
}

// ── Outbound framing ───────────────────────────────────────────────────

void Ctaphid::sendError(uint32_t cid, uint8_t code)
{
  WA_LOG("CTAPHID tx ERROR cid=%08lx code=0x%02x", (unsigned long)cid, code);
  uint8_t b = code;
  _sendPayload(cid, CTAPHID_ERROR, &b, 1);
}

void Ctaphid::sendKeepalive(uint32_t cid, uint8_t status)
{
  uint8_t b = status;
  _sendPayload(cid, CTAPHID_KEEPALIVE, &b, 1);
}

void Ctaphid::_sendPayload(uint32_t cid, uint8_t cmd,
                           const uint8_t* data, uint16_t len)
{
  if (!_fido) { WA_LOG("CTAPHID tx aborted: no sender"); return; }
  WA_LOG("CTAPHID tx cid=%08lx cmd=0x%02x len=%u",
         (unsigned long)cid, cmd, len);

  uint8_t pkt[kHidReportSize];
  uint16_t off = 0;

  // Init packet
  memset(pkt, 0, sizeof(pkt));
  memcpy(pkt, &cid, 4);
  pkt[4] = (uint8_t)(0x80 | cmd);
  pkt[5] = (uint8_t)((len >> 8) & 0xFF);
  pkt[6] = (uint8_t)(len & 0xFF);
  uint16_t take = len < kInitDataMax ? len : kInitDataMax;
  if (take && data) memcpy(pkt + 7, data, take);
  _fido->sendReport(pkt);
  off = take;

  // Continuation packets
  uint8_t seq = 0;
  while (off < len) {
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, &cid, 4);
    pkt[4] = seq++ & 0x7F;
    uint16_t remain = len - off;
    uint16_t chunk  = remain < kContDataMax ? remain : kContDataMax;
    memcpy(pkt + 5, data + off, chunk);
    _fido->sendReport(pkt);
    off += chunk;
  }
}

void Ctaphid::_reset()
{
  _state  = ST_IDLE;
  _curCid = 0;
  _curCmd = 0;
  _expLen = 0;
  _gotLen = 0;
  _expSeq = 0;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
