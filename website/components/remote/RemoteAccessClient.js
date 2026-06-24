'use client';

import { useCallback, useEffect, useRef, useState } from 'react';

// Same VID:PID filters as the installer / file manager (ESP32 USB-UART bridges).
const USB_FILTERS = [
  { usbVendorId: 0x10c4, usbProductId: 0xea60 }, // CP2102
  { usbVendorId: 0x0403, usbProductId: 0x6010 }, // FT2232H
  { usbVendorId: 0x303a, usbProductId: 0x1001 }, // Espressif JTAG
  { usbVendorId: 0x303a, usbProductId: 0x0002 }, // Espressif CDC
  { usbVendorId: 0x1a86, usbProductId: 0x55d4 }, // CH9102F
  { usbVendorId: 0x1a86, usbProductId: 0x7523 }, // CH340T
  { usbVendorId: 0x0403, usbProductId: 0x6001 }, // FT232R
];

// Nordic UART Service UUIDs — same as the File Manager / firmware BleFileManager.
// The BLE "Remote Device" link carries both ctx 'F' (files) and ctx 'S' (mirror).
const NUS_SVC_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX_UUID  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // host -> device
const NUS_TX_UUID  = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // device -> host (notify)
const BLE_WRITE_MAX = 180; // ATT_MTU - 3, conservative across browsers

const SOF1 = 0xa5;
const SOF2 = 0x5a;

// Screen-mirror protocol — must match firmware/src/core/ScreenMirror.h (ctx 'S').
const CTX_SCR = 'S'.charCodeAt(0); // 0x53
const C_START = 0x01;
const C_STOP  = 0x02;
const C_INPUT = 0x10; // [dir:1][event:1]
const C_TOUCH = 0x11; // [x:2][y:2]
const C_KEY   = 0x12; // [char:1]
const T_HELLO = 0xa0; // [w:2][h:2][format:1][caps:1]
const T_FRAME = 0xa1; // [x:2][y:2][w:2][h:2][rgb565…]
const T_FILL  = 0xa2; // [x:2][y:2][w:2][h:2][color:2]
const CAP_TOUCH    = 0x01;
const CAP_KEYBOARD = 0x02;

// INavigation::Direction
const DIR_UP = 1, DIR_DOWN = 2, DIR_LEFT = 3, DIR_RIGHT = 4, DIR_PRESS = 5, DIR_BACK = 6;

function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    crc ^= bytes[i];
    for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (~crc) >>> 0;
}

function buildFrame(ctx, type, seq, payload) {
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

// Streaming frame parser — yields CRC-verified frames, resyncs on garbage.
function makeFrameParser(onFrame) {
  let state = 0;
  const hdr = new Uint8Array(7);
  let hdrIdx = 0, ctx = 0, type = 0, seq = 0, len = 0;
  let payload = null, payloadIdx = 0;
  const crcBuf = new Uint8Array(4);
  let crcIdx = 0;

  return (chunk) => {
    for (let i = 0; i < chunk.length; i++) {
      const b = chunk[i];
      switch (state) {
        case 0: if (b === SOF1) state = 1; break;
        case 1:
          if (b === SOF2) { state = 2; hdrIdx = 0; }
          else if (b !== SOF1) state = 0;
          break;
        case 2:
          hdr[hdrIdx++] = b;
          if (hdrIdx === 7) {
            ctx = hdr[0]; type = hdr[1]; seq = hdr[2];
            len = (hdr[3] | (hdr[4] << 8) | (hdr[5] << 16) | (hdr[6] << 24)) >>> 0;
            if (len > 65536) { state = 0; break; } // sanity (frames are banded)
            payloadIdx = 0; crcIdx = 0;
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
            const buf = new Uint8Array(7 + len);
            buf.set(hdr, 0);
            if (len) buf.set(payload, 7);
            if (crc32(buf) === expected) onFrame(ctx, type, seq, payload);
            state = 0;
          }
          break;
      }
    }
  };
}

const rd16 = (p, o) => p[o] | (p[o + 1] << 8);

// Web Bluetooth's gatt.connect() is flaky on Chrome — it rejects with
// "Connection attempt failed" / NetworkError on the first try surprisingly
// often (stale link, busy radio, prior session not yet released). A short
// backoff + retry almost always succeeds on the 2nd or 3rd attempt.
async function gattConnect(device, onLog, tries = 4) {
  let lastErr;
  for (let i = 0; i < tries; i++) {
    try {
      return await device.gatt.connect();
    } catch (err) {
      lastErr = err;
      if (i < tries - 1) {
        onLog?.(`gatt connect failed (${i + 1}/${tries}), retrying…`);
        await new Promise((r) => setTimeout(r, 250 * (i + 1)));
      }
    }
  }
  throw lastErr;
}

// Status-dot palette — mirrors FileManagerClient's STATUS_COLOR.
const STATUS_COLOR = {
  idle:       'var(--ink-muted)',
  connecting: 'var(--amber)',
  connected:  'var(--accent)',
  error:      'var(--danger)',
};

export default function RemoteAccessClient() {
  const [mounted, setMounted] = useState(false);
  const [status, setStatus] = useState('idle'); // idle | connecting | connected | error
  const [streaming, setStreaming] = useState(false);
  const [caps, setCaps] = useState({ touch: false, keyboard: false, w: 0, h: 0 });
  const [errorMsg, setErrorMsg] = useState('');
  const [logLines, setLogLines] = useState([]);
  const [swap, setSwap] = useState(false);

  const canvasRef = useRef(null);
  const ctx2dRef  = useRef(null);
  const writeBytesRef = useRef(null); // async (Uint8Array) => Promise — serial or BLE
  const closeRef      = useRef(null); // async () => void — tear down the transport
  const kindRef       = useRef(null); // 'serial' | 'bluetooth'
  const sessionRef    = useRef(0);    // bumped per connect; ignores stale read-loop / gatt-drop
  const seqRef    = useRef(10);
  const capsRef   = useRef({ touch: false, keyboard: false });
  const swapRef   = useRef(false);
  const helloTimerRef = useRef(null); // detects a device with Screen Mirror off
  const gotHelloRef   = useRef(false);
  const streamingRef = useRef(false);

  const log = useCallback((msg) => {
    setLogLines((prev) => [...prev.slice(-120), msg]);
  }, []);

  useEffect(() => { swapRef.current = swap; }, [swap]);
  useEffect(() => { streamingRef.current = streaming; }, [streaming]);

  useEffect(() => { setMounted(true); }, []);
  const hasSerial    = mounted && typeof navigator !== 'undefined' && 'serial'    in navigator;
  const hasBluetooth = mounted && typeof navigator !== 'undefined' && 'bluetooth' in navigator;
  const supported    = !mounted || hasSerial || hasBluetooth;

  // ── Incoming frames → canvas ────────────────────────────────────────────────
  const onFrame = useCallback((ctx, type, seq, p) => {
    if (ctx !== CTX_SCR) return;
    const ctx2d = ctx2dRef.current;

    if (type === T_HELLO) {
      gotHelloRef.current = true;
      if (helloTimerRef.current) { clearTimeout(helloTimerRef.current); helloTimerRef.current = null; }
      const w = rd16(p, 0), h = rd16(p, 2);
      const cbyte = p[5] || 0;
      const touch = !!(cbyte & CAP_TOUCH);
      const keyboard = !!(cbyte & CAP_KEYBOARD);
      capsRef.current = { touch, keyboard };
      setCaps({ touch, keyboard, w, h });
      const cv = canvasRef.current;
      if (cv) { cv.width = w; cv.height = h; }
      log(`device ${w}×${h}${touch ? ' · touch' : ''}${keyboard ? ' · keyboard' : ''}`);
      return;
    }
    if (!ctx2d) return;

    if (type === T_FRAME) {
      const x = rd16(p, 0), y = rd16(p, 2), w = rd16(p, 4), h = rd16(p, 6);
      const sw = swapRef.current;
      const img = ctx2d.createImageData(w, h);
      const px = img.data;
      let o = 8;
      for (let i = 0; i < w * h; i++, o += 2) {
        const lo = p[o], hi = p[o + 1];
        const v = sw ? ((lo << 8) | hi) : ((hi << 8) | lo);
        const di = i * 4;
        px[di]     = ((v >> 11) & 0x1f) * 255 / 31;
        px[di + 1] = ((v >> 5)  & 0x3f) * 255 / 63;
        px[di + 2] = (v & 0x1f) * 255 / 31;
        px[di + 3] = 255;
      }
      ctx2d.putImageData(img, x, y);
      return;
    }
    if (type === T_FILL) {
      const x = rd16(p, 0), y = rd16(p, 2), w = rd16(p, 4), h = rd16(p, 6), c = rd16(p, 8);
      const r = ((c >> 11) & 0x1f) * 255 / 31;
      const g = ((c >> 5)  & 0x3f) * 255 / 63;
      const b = (c & 0x1f) * 255 / 31;
      ctx2d.fillStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
      ctx2d.fillRect(x, y, w, h);
      return;
    }
  }, [log]);

  // ── Outbound control ──────────────────────────────────────────────────────────
  const nextSeq = () => (seqRef.current = (seqRef.current + 1) & 0xff);
  const writeFrame = useCallback((type, payload) => {
    const writeBytes = writeBytesRef.current;
    if (!writeBytes) return;
    Promise.resolve(writeBytes(buildFrame(CTX_SCR, type, nextSeq(), payload))).catch(() => {});
  }, []);
  const sendDir = useCallback((dir, ev = 0) => writeFrame(C_INPUT, new Uint8Array([dir, ev])), [writeFrame]);
  const sendKey = useCallback((code) => writeFrame(C_KEY, new Uint8Array([code & 0xff])), [writeFrame]);
  const sendTouch = useCallback((x, y) => writeFrame(C_TOUCH,
    new Uint8Array([x & 0xff, (x >> 8) & 0xff, y & 0xff, (y >> 8) & 0xff])), [writeFrame]);

  // ── Connection lifecycle ──────────────────────────────────────────────────────
  const disconnect = useCallback(async () => {
    if (helloTimerRef.current) { clearTimeout(helloTimerRef.current); helloTimerRef.current = null; }
    sessionRef.current++; // invalidate any in-flight read loop / gatt-disconnect handler
    streamingRef.current = false;
    setStreaming(false);
    try { if (writeBytesRef.current) writeFrame(C_STOP, null); } catch (_) {}
    try { if (closeRef.current) await closeRef.current(); } catch (_) {}
    writeBytesRef.current = null; closeRef.current = null; kindRef.current = null;
    // Clear the mirrored screen so a stale frame doesn't linger after unplug.
    const ctx2d = ctx2dRef.current;
    if (ctx2d) ctx2d.clearRect(0, 0, ctx2d.canvas.width, ctx2d.canvas.height);
    capsRef.current = { touch: false, keyboard: false };
    setCaps({ touch: false, keyboard: false, w: 0, h: 0 });
    setStatus('idle');
  }, [writeFrame]);

  // Shared post-open setup: start mirroring and watch for a silent device.
  const _beginSession = useCallback((kind) => {
    setStatus('connected');
    log(`connected · ${kind === 'bluetooth' ? 'BLE NUS' : 'USB 115200'}`);
    // Begin mirroring immediately — the device replies with HELLO then frames.
    gotHelloRef.current = false;
    writeFrame(C_START, null);
    streamingRef.current = true;
    setStreaming(true);
    log('stream started');
    canvasRef.current?.focus();

    // No HELLO ⇒ nothing is consuming the stream. Over USB that means Screen
    // Mirror is off; over BLE it means the Remote Device service isn't on.
    helloTimerRef.current = setTimeout(() => {
      helloTimerRef.current = null;
      if (!gotHelloRef.current) {
        setErrorMsg(kind === 'bluetooth'
          ? 'No response from the device. Turn on “Bluetooth → Remote Device” on the device, then reconnect.'
          : 'No response from the device. Turn on “Screen Mirror” in the device Settings (Settings → Screen Mirror), then reconnect.');
        disconnect();
      }
    }, 3000);
  }, [log, writeFrame, disconnect]);

  const _connectSerial = useCallback(async () => {
    const port = await navigator.serial.requestPort({ filters: USB_FILTERS });
    await port.open({ baudRate: 115200, bufferSize: 64 * 1024 });
    const reader = port.readable.getReader();
    const writer = port.writable.getWriter();
    const feed   = makeFrameParser(onFrame);
    const session = ++sessionRef.current;
    writeBytesRef.current = (data) => writer.write(data);
    closeRef.current = async () => {
      try { await reader.cancel(); } catch (_) {}
      try { await writer.close(); } catch (_) {}
      try { await port.close();   } catch (_) {}
    };
    kindRef.current = 'serial';
    (async () => {
      try {
        // eslint-disable-next-line no-constant-condition
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value) feed(value);
        }
      } catch (_) {
        // reader cancelled on disconnect
      }
      if (sessionRef.current === session) disconnect();
    })();
    _beginSession('serial');
  }, [onFrame, disconnect, _beginSession]);

  const _connectBluetooth = useCallback(async () => {
    const device = await navigator.bluetooth.requestDevice({
      filters:          [{ services: [NUS_SVC_UUID] }],
      optionalServices: [NUS_SVC_UUID],
    });
    log(`bluetooth device: ${device.name || '(unnamed)'}`);
    const server  = await gattConnect(device, log);
    const service = await server.getPrimaryService(NUS_SVC_UUID);
    const rxChar  = await service.getCharacteristic(NUS_RX_UUID); // host -> device
    const txChar  = await service.getCharacteristic(NUS_TX_UUID); // device -> host
    const feed    = makeFrameParser(onFrame);
    const session = ++sessionRef.current;

    const onValueChanged = (ev) => {
      const dv = ev.target.value;
      feed(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength));
    };
    txChar.addEventListener('characteristicvaluechanged', onValueChanged);
    await txChar.startNotifications();

    const onDisconnected = () => {
      log('bluetooth: gatt disconnected');
      if (sessionRef.current === session) disconnect();
    };
    device.addEventListener('gattserverdisconnected', onDisconnected);

    // Control frames are tiny, but chunk anyway to stay under ATT_MTU-3.
    const useWithoutResponse = typeof rxChar.writeValueWithoutResponse === 'function';
    writeBytesRef.current = async (data) => {
      for (let off = 0; off < data.length; off += BLE_WRITE_MAX) {
        const slice = data.subarray(off, Math.min(off + BLE_WRITE_MAX, data.length));
        if (useWithoutResponse) await rxChar.writeValueWithoutResponse(slice);
        else                    await rxChar.writeValue(slice);
      }
    };
    closeRef.current = async () => {
      device.removeEventListener('gattserverdisconnected', onDisconnected);
      txChar.removeEventListener('characteristicvaluechanged', onValueChanged);
      try { await txChar.stopNotifications(); } catch (_) {}
      try { device.gatt.disconnect(); } catch (_) {}
    };
    kindRef.current = 'bluetooth';
    _beginSession('bluetooth');
  }, [onFrame, log, disconnect, _beginSession]);

  const connect = useCallback(async (mode) => {
    setErrorMsg('');
    const ok = mode === 'bluetooth'
      ? (typeof navigator !== 'undefined' && 'bluetooth' in navigator)
      : (typeof navigator !== 'undefined' && 'serial'    in navigator);
    if (!ok) {
      setErrorMsg(mode === 'bluetooth'
        ? 'Web Bluetooth is not supported in this browser. Use Chrome / Edge on desktop or Chrome on Android.'
        : 'Web Serial is not supported in this browser. Use Chrome or Edge on desktop.');
      return;
    }
    setStatus('connecting');
    try {
      if (mode === 'bluetooth') await _connectBluetooth();
      else                      await _connectSerial();
    } catch (err) {
      setStatus('idle');
      const raw = err?.message || String(err);
      if (!/No port selected|cancel|chooser|User cancelled/i.test(raw)) {
        setErrorMsg(mode === 'bluetooth'
          ? `${raw} — make sure “Bluetooth → Remote Device” is ON on the device and no other tab or app is already connected to it, then try again.`
          : `${raw} — close any other program using this port (pio device monitor, another tab) and retry.`);
      }
      try { if (closeRef.current) await closeRef.current(); } catch (_) {}
      writeBytesRef.current = null; closeRef.current = null; kindRef.current = null;
    }
  }, [_connectSerial, _connectBluetooth]);

  const onConnectSerial    = useCallback(() => connect('serial'),    [connect]);
  const onConnectBluetooth = useCallback(() => connect('bluetooth'), [connect]);

  const startStream = useCallback(() => {
    if (!writeBytesRef.current) return;
    writeFrame(C_START, null);
    streamingRef.current = true;
    setStreaming(true);
    log('stream started');
    canvasRef.current?.focus();
  }, [writeFrame, log]);

  const stopStream = useCallback(() => {
    writeFrame(C_STOP, null);
    streamingRef.current = false;
    setStreaming(false);
    log('stream stopped');
  }, [writeFrame, log]);

  // Tidy up on unmount.
  useEffect(() => () => { disconnect(); }, [disconnect]);

  // ── Keyboard control (capability-aware, mirrors the device's input model) ─────
  useEffect(() => {
    if (status !== 'connected') return undefined;
    let enterTimer = null, enterHeld = false;

    const onKeyDown = (e) => {
      if (!streamingRef.current || !writeBytesRef.current) return;
      const k = e.key;
      const hasKeyboard = capsRef.current.keyboard;
      if (k === 'ArrowUp')    { e.preventDefault(); return sendDir(DIR_UP); }
      if (k === 'ArrowDown')  { e.preventDefault(); return sendDir(DIR_DOWN); }
      if (k === 'ArrowLeft')  { e.preventDefault(); return sendDir(DIR_LEFT); }
      if (k === 'ArrowRight') { e.preventDefault(); return sendDir(DIR_RIGHT); }
      if (k === 'Escape')     { e.preventDefault(); return sendDir(DIR_BACK); }

      // Keyboard boards: Ctrl/Cmd modifiers map to nav (plain Enter/Bksp type).
      if (hasKeyboard && k === 'Enter'     && (e.ctrlKey || e.metaKey)) { e.preventDefault(); return sendDir(DIR_PRESS); }
      if (hasKeyboard && k === 'Backspace' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); return sendDir(DIR_BACK); }

      if (k === 'Enter') {
        e.preventDefault();
        if (hasKeyboard) return sendKey(10);
        if (e.repeat) return; // non-keyboard: tap = PRESS, hold = long-press
        enterHeld = false;
        enterTimer = setTimeout(() => { enterHeld = true; sendDir(DIR_PRESS, 1); }, 400);
        return;
      }
      if (hasKeyboard) {
        if (k === 'Backspace') { e.preventDefault(); return sendKey(8); }
        if (k.length === 1)    { e.preventDefault(); return sendKey(k.charCodeAt(0)); }
        return;
      }
      if (k === 'Backspace') { e.preventDefault(); return sendDir(DIR_BACK); }
      if (k === ' ')         { e.preventDefault(); return sendDir(DIR_PRESS); }
    };

    const onKeyUp = (e) => {
      if (e.key === 'Enter' && enterTimer !== null) {
        clearTimeout(enterTimer); enterTimer = null;
        if (!enterHeld && streamingRef.current) sendDir(DIR_PRESS, 0);
      }
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      if (enterTimer) clearTimeout(enterTimer);
    };
  }, [status, sendDir, sendKey]);

  // Canvas click → touch tap (touch boards only).
  const onCanvasClick = useCallback((e) => {
    if (!streamingRef.current || !capsRef.current.touch) return;
    const cv = canvasRef.current;
    const r = cv.getBoundingClientRect();
    const x = Math.max(0, Math.min(cv.width - 1, Math.round((e.clientX - r.left) / r.width * cv.width)));
    const y = Math.max(0, Math.min(cv.height - 1, Math.round((e.clientY - r.top) / r.height * cv.height)));
    sendTouch(x, y);
  }, [sendTouch]);

  if (!supported) {
    return (
      <div className="fm-banner fm-banner-err">
        Neither Web Serial nor Web Bluetooth is available in this browser. Use Chrome / Edge on
        desktop, or Chrome on Android (Bluetooth only).
      </div>
    );
  }

  const connected = status === 'connected';

  return (
    <div className="ra">
      <div className="fm-toolbar">
        <div className="fm-toolbar-left">
          {!connected ? (
            <>
              <button
                type="button"
                className="fm-btn fm-btn-primary"
                onClick={onConnectSerial}
                disabled={!hasSerial || status === 'connecting'}
                title={hasSerial ? 'Connect over USB serial' : 'Web Serial not supported in this browser'}
              >
                {status === 'connecting' ? 'Connecting…' : 'Connect USB'}
              </button>
              <button
                type="button"
                className="fm-btn"
                onClick={onConnectBluetooth}
                disabled={!hasBluetooth || status === 'connecting'}
                title={hasBluetooth ? 'Connect over Bluetooth (NUS)' : 'Web Bluetooth not supported in this browser'}
              >
                Connect Bluetooth
              </button>
            </>
          ) : (
            <>
              {!streaming ? (
                <button type="button" className="fm-btn fm-btn-primary" onClick={startStream}>Start stream</button>
              ) : (
                <button type="button" className="fm-btn" onClick={stopStream}>Stop stream</button>
              )}
              <button type="button" className="fm-btn fm-btn-ghost" onClick={disconnect}>Disconnect</button>
              <label className="ra-swap">
                <input type="checkbox" checked={swap} onChange={(e) => setSwap(e.target.checked)} />
                Swap bytes
              </label>
            </>
          )}
        </div>
        <div className="fm-toolbar-right">
          {connected && (
            <span className="ra-caps">
              {caps.w > 0 ? `${caps.w}×${caps.h}` : '—'}
              {caps.touch ? ' · touch' : ''}{caps.keyboard ? ' · keyboard' : ''}
            </span>
          )}
          <span className="fm-status-dot" style={{ background: STATUS_COLOR[status] || 'var(--ink-muted)' }} />
          <span className="fm-status-text">{status}</span>
        </div>
      </div>

      {errorMsg && <div className="fm-banner fm-banner-err">{errorMsg}</div>}

      <div className="ra-stage">
        <canvas
          ref={(el) => { canvasRef.current = el; ctx2dRef.current = el ? el.getContext('2d') : null; }}
          className="ra-canvas"
          width={240}
          height={135}
          tabIndex={0}
          onClick={onCanvasClick}
        />
        {!streaming && (
          <div className="ra-stage-hint">
            {connected ? 'Click “Start stream” to mirror the screen.' : 'Connect a UniGeek over USB or Bluetooth to begin.'}
          </div>
        )}
      </div>

      <p className="ra-help">
        <strong>Arrows</strong> navigate · <strong>Esc</strong> back ·{' '}
        {caps.keyboard
          ? <><strong>type</strong> into inputs · <strong>Ctrl/⌘+Enter</strong> select · <strong>Ctrl/⌘+Backspace</strong> back</>
          : <><strong>Enter</strong> select (hold = long-press) · <strong>Space</strong> press</>}
        {caps.touch ? <> · <strong>click</strong> the screen to touch</> : null}
      </p>

      <div className="fm-console">
        <div className="fm-console-chrome">// remote · access · uart</div>
        {logLines.length === 0
          ? <div className="fm-console-line dim">no activity yet</div>
          : logLines.map((line, i) => <div key={i} className="fm-console-line">{line}</div>)}
      </div>
    </div>
  );
}
