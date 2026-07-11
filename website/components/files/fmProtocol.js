// Shared UniGeek file-manager transport + framing protocol.
//
// Extracted from FileManagerClient.js so multiple UIs (File Manager, App Store)
// can talk to the device over the same Web Serial / Web Bluetooth channel.
// Must match the firmware side: firmware/src/utils/uart/FileManagerCore.cpp
// and BleFileManager.cpp / UartFileManager.cpp.

// Mirror the same VID:PID filters as the installer (ESP32 USB-UART bridges).
export const USB_FILTERS = [
  { usbVendorId: 0x10c4, usbProductId: 0xea60 }, // CP2102
  { usbVendorId: 0x0403, usbProductId: 0x6010 }, // FT2232H
  { usbVendorId: 0x303a, usbProductId: 0x1001 }, // Espressif JTAG
  { usbVendorId: 0x303a, usbProductId: 0x0002 }, // Espressif CDC
  { usbVendorId: 0x1a86, usbProductId: 0x55d4 }, // CH9102F
  { usbVendorId: 0x1a86, usbProductId: 0x7523 }, // CH340T
  { usbVendorId: 0x0403, usbProductId: 0x6001 }, // FT232R
];

const SOF1 = 0xa5;
const SOF2 = 0x5a;

// Subsystem contexts (ASCII letters, easy to spot in raw serial). Each
// context owns its own command-code namespace; future subsystems (wifi, ble)
// can pick their own letter without colliding with FM commands.
export const CTX_FM = 'F'.charCodeAt(0); // 0x46

// Response types — same codes regardless of ctx. Responses echo the request's ctx.
const T_OK        = 0xf0;
const T_ERR       = 0xf1;
const T_GET_CHUNK = 0xf2;

// FM command types (valid only under CTX_FM).
export const C_INFO      = 0x01;
export const C_LS        = 0x02;
export const C_STAT      = 0x03;
export const C_GET       = 0x10;
export const C_PUT_BEGIN = 0x20;
export const C_PUT_CHUNK = 0x21;
export const C_PUT_END   = 0x22;
export const C_RM        = 0x30;
export const C_MV        = 0x31;
export const C_MKDIR     = 0x32;
export const C_TOUCH     = 0x33;

// Nordic UART Service UUIDs (must match firmware/utils/uart/BleFileManager.cpp).
const NUS_SVC_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX_UUID  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX_UUID  = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

// USB serial can handle big frames thanks to the 4 KB firmware RX buffer.
// BLE notifications are MTU-limited (≈180 bytes safe), so big chunks fragment
// across many ATT writes — shrink the per-chunk size to keep latency sane.
const PUT_CHUNK_SIZE_SERIAL = 1024;
const PUT_CHUNK_SIZE_BLE    = 256;
const BLE_WRITE_MAX         = 180; // ATT_MTU - 3, conservative across browsers

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    crc ^= bytes[i];
    for (let k = 0; k < 8; k++) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return (~crc) >>> 0;
}

export function buildFrame(ctx, type, seq, payload) {
  const len = payload ? payload.length : 0;
  const frame = new Uint8Array(9 + len + 4);
  frame[0] = SOF1;
  frame[1] = SOF2;
  frame[2] = ctx;
  frame[3] = type;
  frame[4] = seq;
  frame[5] = len & 0xff;
  frame[6] = (len >>> 8) & 0xff;
  frame[7] = (len >>> 16) & 0xff;
  frame[8] = (len >>> 24) & 0xff;
  if (payload) frame.set(payload, 9);
  const crc = crc32(frame.subarray(2, 9 + len));
  frame[9 + len + 0] = crc & 0xff;
  frame[9 + len + 1] = (crc >>> 8) & 0xff;
  frame[9 + len + 2] = (crc >>> 16) & 0xff;
  frame[9 + len + 3] = (crc >>> 24) & 0xff;
  return frame;
}

export function strBytes(s) {
  return new TextEncoder().encode(s);
}

export function bytesStr(b) {
  return new TextDecoder().decode(b);
}

export function formatBytes(n) {
  if (!Number.isFinite(n)) return '—';
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1024 / 1024).toFixed(2)} MB`;
}

export function normalizePath(parent, name) {
  const base = parent === '/' ? '' : parent.replace(/\/+$/, '');
  const seg = name.startsWith('/') ? name : `/${name}`;
  return `${base}${seg}`;
}

// ── Frame parser (streaming) ─────────────────────────────────────────────────
//
// Feeds raw bytes into a state machine and yields complete, CRC-verified
// frames via onFrame(ctx, type, seq, payloadUint8). Tolerant of garbage
// between frames (resyncs on SOF, drops bad CRC).
export function makeFrameParser(onFrame) {
  let state = 0; // 0=sof1 1=sof2 2=hdr 3=payload 4=crc
  const hdr = new Uint8Array(7); // ctx, type, seq, len[4]
  let hdrIdx = 0;
  let ctx = 0;
  let type = 0;
  let seq = 0;
  let len = 0;
  let payload = null;
  let payloadIdx = 0;
  const crcBuf = new Uint8Array(4);
  let crcIdx = 0;

  return (chunk) => {
    for (let i = 0; i < chunk.length; i++) {
      const b = chunk[i];
      switch (state) {
        case 0:
          if (b === SOF1) state = 1;
          break;
        case 1:
          if (b === SOF2) { state = 2; hdrIdx = 0; }
          else if (b !== SOF1) state = 0;
          break;
        case 2:
          hdr[hdrIdx++] = b;
          if (hdrIdx === 7) {
            ctx  = hdr[0];
            type = hdr[1];
            seq  = hdr[2];
            len  = hdr[3] | (hdr[4] << 8) | (hdr[5] << 16) | (hdr[6] << 24);
            if (len > 16384) { state = 0; break; } // sanity
            payloadIdx = 0;
            crcIdx = 0;
            if (len > 0) { payload = new Uint8Array(len); state = 3; }
            else { payload = new Uint8Array(0); state = 4; }
          }
          break;
        case 3:
          payload[payloadIdx++] = b;
          if (payloadIdx >= len) state = 4;
          break;
        case 4:
          crcBuf[crcIdx++] = b;
          if (crcIdx === 4) {
            const expected = (crcBuf[0] | (crcBuf[1] << 8) | (crcBuf[2] << 16) | (crcBuf[3] << 24)) >>> 0;
            // CRC over header[ctx,type,seq,len] + payload
            const buf = new Uint8Array(7 + len);
            buf.set(hdr, 0);
            if (len) buf.set(payload, 7);
            const actual = crc32(buf);
            if (actual === expected) {
              onFrame(ctx, type, seq, payload);
            }
            state = 0;
          }
          break;
      }
    }
  };
}

// ── Transport ────────────────────────────────────────────────────────────────
//
// Wraps a Web Serial port or Web Bluetooth NUS. Owns the reader/writer loops
// and a Map of pending requests keyed by seq. send() returns a Promise that
// resolves with the response payload (for OK), or rejects with the error
// message (for ERR).
//
// For GET, the caller passes onChunk; the transport yields each T_GET_CHUNK
// payload until it sees a zero-length GET_CHUNK (end-of-stream marker).
export function createTransport({ onLog } = {}) {
  let kind = null;                    // 'serial' | 'bluetooth'
  let writeBytes = null;              // (Uint8Array) => Promise<void>
  let closeFn   = null;               // () => Promise<void>
  let chunkSize = PUT_CHUNK_SIZE_SERIAL;
  let seqCounter = 0;
  const pending = new Map(); // seq -> { ctx, resolve, reject, onChunk, timer }

  const log = (msg) => onLog && onLog(msg);

  const rejectAllPending = (err) => {
    for (const [, entry] of pending) {
      if (entry.timer) clearTimeout(entry.timer);
      entry.reject(err);
    }
    pending.clear();
  };

  const dispatchFrame = (ctx, type, seq, payload) => {
    const entry = pending.get(seq);
    if (!entry) return;
    // Defensive: ctx must echo the request's ctx. Mismatches are dropped.
    if (entry.ctx !== ctx) return;
    if (type === T_OK) {
      pending.delete(seq);
      if (entry.timer) clearTimeout(entry.timer);
      entry.resolve(payload);
    } else if (type === T_ERR) {
      pending.delete(seq);
      if (entry.timer) clearTimeout(entry.timer);
      entry.reject(new Error(bytesStr(payload) || 'device error'));
    } else if (type === T_GET_CHUNK) {
      if (!entry.onChunk) return;
      if (payload.length === 0) {
        pending.delete(seq);
        if (entry.timer) clearTimeout(entry.timer);
        entry.resolve();
      } else {
        entry.onChunk(payload);
        // reset timeout on activity
        if (entry.timer) {
          clearTimeout(entry.timer);
          entry.timer = setTimeout(() => {
            pending.delete(seq);
            entry.reject(new Error('stream timeout'));
          }, 10000);
        }
      }
    }
  };

  const connectSerial = async () => {
    if (kind) return;
    if (typeof navigator === 'undefined' || !('serial' in navigator)) {
      throw new Error('Web Serial is not supported in this browser. Use Chrome or Edge on desktop.');
    }
    const port = await navigator.serial.requestPort({ filters: USB_FILTERS });
    try {
      await port.open({ baudRate: 115200, bufferSize: 16 * 1024 });
    } catch (err) {
      const raw = err?.message || String(err);
      throw new Error(
        `${raw} — close any other program using this port (pio device monitor, Arduino IDE, other browser tabs) and try again.`
      );
    }
    const reader = port.readable.getReader();
    const writer = port.writable.getWriter();
    const feed   = makeFrameParser(dispatchFrame);

    // Read loop runs detached; it ends when the reader closes/cancels.
    (async () => {
      try {
        // eslint-disable-next-line no-constant-condition
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value) feed(value);
        }
      } catch (err) {
        log(`read loop ended: ${err?.message || err}`);
      }
    })();

    writeBytes = (data) => writer.write(data);
    closeFn = async () => {
      try { await reader.cancel(); } catch (_) {}
      try { await writer.close(); } catch (_) {}
      try { await port.close();   } catch (_) {}
    };
    chunkSize = PUT_CHUNK_SIZE_SERIAL;
    kind = 'serial';
  };

  const connectBluetooth = async () => {
    if (kind) return;
    if (typeof navigator === 'undefined' || !navigator.bluetooth) {
      throw new Error('Web Bluetooth is not supported in this browser. Use Chrome / Edge on desktop or Chrome on Android.');
    }
    const device = await navigator.bluetooth.requestDevice({
      filters:        [{ services: [NUS_SVC_UUID] }],
      optionalServices: [NUS_SVC_UUID],
    });
    log(`bluetooth device: ${device.name || '(unnamed)'}`);
    const server  = await device.gatt.connect();
    const service = await server.getPrimaryService(NUS_SVC_UUID);
    const rxChar  = await service.getCharacteristic(NUS_RX_UUID); // host -> device
    const txChar  = await service.getCharacteristic(NUS_TX_UUID); // device -> host
    const feed    = makeFrameParser(dispatchFrame);

    const onValueChanged = (ev) => {
      const dv = ev.target.value;
      // DataView → Uint8Array on the same buffer slice.
      const u8 = new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength);
      feed(u8);
    };
    txChar.addEventListener('characteristicvaluechanged', onValueChanged);
    await txChar.startNotifications();

    const onDisconnected = () => {
      log('bluetooth: gatt disconnected');
      rejectAllPending(new Error('disconnected'));
      writeBytes = null;
      closeFn = null;
      kind = null;
    };
    device.addEventListener('gattserverdisconnected', onDisconnected);

    // writeValueWithoutResponse is faster but capped at ATT_MTU-3. Chunk the
    // outbound frame manually; the firmware's stream parser stitches it back.
    const useWithoutResponse = typeof rxChar.writeValueWithoutResponse === 'function';
    writeBytes = async (data) => {
      for (let off = 0; off < data.length; off += BLE_WRITE_MAX) {
        const slice = data.subarray(off, Math.min(off + BLE_WRITE_MAX, data.length));
        if (useWithoutResponse) {
          await rxChar.writeValueWithoutResponse(slice);
        } else {
          await rxChar.writeValue(slice);
        }
      }
    };
    closeFn = async () => {
      device.removeEventListener('gattserverdisconnected', onDisconnected);
      txChar.removeEventListener('characteristicvaluechanged', onValueChanged);
      try { await txChar.stopNotifications(); } catch (_) {}
      try { device.gatt.disconnect(); } catch (_) {}
    };
    chunkSize = PUT_CHUNK_SIZE_BLE;
    kind = 'bluetooth';
  };

  const disconnect = async () => {
    if (closeFn) { try { await closeFn(); } catch (_) {} }
    rejectAllPending(new Error('disconnected'));
    writeBytes = null;
    closeFn = null;
    kind = null;
  };

  const nextSeq = () => {
    // 1..255 cycle (0 reserved-ish, helps debugging)
    seqCounter = (seqCounter + 1) & 0xff;
    if (seqCounter === 0) seqCounter = 1;
    return seqCounter;
  };

  const send = async (ctx, type, payload, { onChunk, timeoutMs = 8000 } = {}) => {
    if (!writeBytes) throw new Error('not connected');
    const seq = nextSeq();
    const frame = buildFrame(ctx, type, seq, payload);
    return new Promise((resolve, reject) => {
      const entry = {
        ctx,
        resolve,
        reject,
        onChunk,
        timer: setTimeout(() => {
          if (pending.get(seq) === entry) {
            pending.delete(seq);
            reject(new Error('request timed out'));
          }
        }, timeoutMs),
      };
      pending.set(seq, entry);
      Promise.resolve(writeBytes(frame)).catch((err) => {
        pending.delete(seq);
        if (entry.timer) clearTimeout(entry.timer);
        reject(err);
      });
    });
  };

  return {
    connectSerial,
    connectBluetooth,
    disconnect,
    send,
    isConnected: () => !!writeBytes,
    getKind:     () => kind,
    getChunkSize: () => chunkSize,
  };
}

// ── High-level file install ──────────────────────────────────────────────────

// mkdir each ancestor of `filePath`. The firmware's makeDir only creates one
// level, so we walk the whole chain. mkdir on an existing directory returns an
// error frame — swallow it, since "already there" is success for our purpose.
export async function ensureDirs(transport, filePath) {
  const parts = filePath.split('/').filter(Boolean);
  parts.pop(); // drop the filename, keep only directories
  let acc = '';
  for (const p of parts) {
    acc += `/${p}`;
    try {
      await transport.send(CTX_FM, C_MKDIR, strBytes(acc));
    } catch (_) {
      // Directory already exists (or transient) — PUT_BEGIN will fail loudly
      // later if a real problem remains.
    }
  }
}

// Write `bytes` to `destPath` on the device, creating parent directories first.
// onProgress(sent, total) is called after each chunk (and once at 0).
export async function installFile(transport, destPath, bytes, onProgress) {
  await ensureDirs(transport, destPath);

  const total = bytes.length;
  const pathBytes = strBytes(destPath);
  const begin = new Uint8Array(4 + pathBytes.length);
  begin[0] = total & 0xff;
  begin[1] = (total >>> 8) & 0xff;
  begin[2] = (total >>> 16) & 0xff;
  begin[3] = (total >>> 24) & 0xff;
  begin.set(pathBytes, 4);
  await transport.send(CTX_FM, C_PUT_BEGIN, begin);

  if (onProgress) onProgress(0, total);
  let sent = 0;
  const chunkSize = transport.getChunkSize();
  while (sent < bytes.length) {
    const end = Math.min(sent + chunkSize, bytes.length);
    await transport.send(CTX_FM, C_PUT_CHUNK, bytes.subarray(sent, end));
    sent = end;
    if (onProgress) onProgress(sent, total);
  }
  await transport.send(CTX_FM, C_PUT_END, null);
}
