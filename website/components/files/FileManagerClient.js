'use client';

import { useCallback, useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';

import {
  CTX_FM, C_INFO, C_LS, C_GET, C_PUT_BEGIN, C_PUT_CHUNK, C_PUT_END,
  C_RM, C_MV, C_MKDIR, C_TOUCH,
  strBytes, bytesStr, formatBytes, normalizePath, createTransport,
} from './fmProtocol';


// ── Icons (Lucide-style, stroked, inherit currentColor) ──────────────────────

const iconProps = {
  width: 14, height: 14, viewBox: '0 0 24 24', fill: 'none',
  stroke: 'currentColor', strokeWidth: 2, strokeLinecap: 'round', strokeLinejoin: 'round',
};
const IconHome = (p) => (
  <svg {...iconProps} {...p}>
    <path d="M3 9.5L12 3l9 6.5V20a1 1 0 0 1-1 1h-5v-7h-6v7H4a1 1 0 0 1-1-1V9.5z"/>
  </svg>
);
const IconEye = (p) => (
  <svg {...iconProps} {...p}>
    <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
    <circle cx="12" cy="12" r="3"/>
  </svg>
);
const IconPencil = (p) => (
  <svg {...iconProps} {...p}>
    <path d="M12 20h9"/>
    <path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/>
  </svg>
);
const IconTrash = (p) => (
  <svg {...iconProps} {...p}>
    <polyline points="3 6 5 6 21 6"/>
    <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/>
    <path d="M10 11v6M14 11v6"/>
    <path d="M9 6V4a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2"/>
  </svg>
);
const IconDownload = (p) => (
  <svg {...iconProps} {...p}>
    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
    <polyline points="7 10 12 15 17 10"/>
    <line x1="12" y1="3" x2="12" y2="15"/>
  </svg>
);

function detectImageMime(bytes, name) {
  if (!bytes || bytes.length < 4) return null;
  const b = bytes;
  if (b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4e && b[3] === 0x47) return 'image/png';
  if (b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff)                  return 'image/jpeg';
  if (b[0] === 0x47 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x38) return 'image/gif';
  if (b[0] === 0x42 && b[1] === 0x4d)                                   return 'image/bmp';
  if (b.length >= 12
      && b[0] === 0x52 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x46
      && b[8] === 0x57 && b[9] === 0x45 && b[10] === 0x42 && b[11] === 0x50) return 'image/webp';
  if (b[0] === 0x00 && b[1] === 0x00 && b[2] === 0x01 && b[3] === 0x00) return 'image/x-icon';
  if (name && /\.svg$/i.test(name))                                     return 'image/svg+xml';
  return null;
}

function buildHexRows(bytes, maxBytes = 65536) {
  if (!bytes || bytes.length === 0) return [];
  const limit = Math.min(bytes.length, maxBytes);
  const rows = [];
  for (let i = 0; i < limit; i += 16) {
    const slice = bytes.subarray(i, Math.min(i + 16, limit));
    const offset = i.toString(16).padStart(8, '0');
    // Two 8-byte groups separated by a wider gap (classic xxd layout).
    const left  = [];
    const right = [];
    for (let j = 0; j < 16; j++) {
      const cell = j < slice.length ? slice[j].toString(16).padStart(2, '0') : '  ';
      (j < 8 ? left : right).push(cell);
    }
    const hex = `${left.join(' ')}  ${right.join(' ')}`;
    // Render only printable glyphs (symbols + alphanumerics, 0x21–0x7E).
    // Space, control chars, DEL, and any byte ≥ 0x80 all collapse to '.'.
    const ascii = Array.from(slice, (b) => (b > 0x20 && b < 0x7f) ? String.fromCharCode(b) : '.').join(' ');
    rows.push({ offset, hex, ascii });
  }
  if (bytes.length > maxBytes) {
    rows.push({ truncated: bytes.length - maxBytes });
  }
  return rows;
}

// ── Component ────────────────────────────────────────────────────────────────

const STATUS = {
  IDLE: 'IDLE',
  CONNECTING: 'CONNECTING',
  CONNECTED: 'READY',
  WORKING: 'WORKING',
  ERROR: 'ERROR',
};

const STATUS_COLOR = {
  IDLE: 'var(--ink-muted)',
  CONNECTING: 'var(--amber)',
  READY: 'var(--accent)',
  WORKING: 'var(--amber)',
  ERROR: 'var(--danger)',
};

export default function FileManagerClient({ expectedVersion }) {
  const [status, setStatus] = useState(STATUS.IDLE);
  const [info, setInfo] = useState(null);
  const [versionMismatch, setVersionMismatch] = useState(false);
  const [cwd, setCwd] = useState('/');
  const [entries, setEntries] = useState([]);
  const [busyMsg, setBusyMsg] = useState('');
  const [errorMsg, setErrorMsg] = useState('');
  const [logLines, setLogLines] = useState([]);
  const [progress, setProgress] = useState(null); // { label, value, total }
  const [viewer, setViewer] = useState(null); // { name, path, bytes: Uint8Array }
  const [viewMode, setViewMode] = useState('text'); // 'text' | 'hex'
  const [textContent, setTextContent] = useState('');
  const [textDirty, setTextDirty] = useState(false);
  const [savingFile, setSavingFile] = useState(false);
  const transportRef = useRef(null);
  const uploadInputRef = useRef(null);
  const logEndRef = useRef(null);
  // Holds the latest onView. onOpen reads through this ref to avoid the TDZ
  // that would happen if onOpen listed onView in its dependency array (onView
  // is declared later in this component).
  const viewRef = useRef(null);
  // Drag/drop file upload
  const [dragOver, setDragOver] = useState(false);
  const dragDepthRef = useRef(0);

  // `navigator` doesn't exist during SSR — only check after hydration to
  // avoid a server/client tree mismatch.
  const [mounted, setMounted] = useState(false);
  useEffect(() => { setMounted(true); }, []);
  const hasSerial    = mounted && typeof navigator !== 'undefined' && 'serial'    in navigator;
  const hasBluetooth = mounted && typeof navigator !== 'undefined' && 'bluetooth' in navigator;
  const supported    = hasSerial || hasBluetooth;

  const pushLog = useCallback((msg) => {
    setLogLines((prev) => [...prev.slice(-99), msg]);
  }, []);

  useEffect(() => {
    if (logEndRef.current) logEndRef.current.scrollTop = logEndRef.current.scrollHeight;
  }, [logLines]);

  // Build transport lazily once.
  const getTransport = useCallback(() => {
    if (!transportRef.current) {
      transportRef.current = createTransport({ onLog: pushLog });
    }
    return transportRef.current;
  }, [pushLog]);

  const refreshDir = useCallback(async (path) => {
    const t = getTransport();
    const resp = await t.send(CTX_FM, C_LS, strBytes(path));
    const text = bytesStr(resp);
    const lines = text.split('\n').filter(Boolean);
    const rows = lines.map((line) => {
      const i = line.indexOf(':');
      const j = line.lastIndexOf(':');
      const kind = line.slice(0, i);
      const name = line.slice(i + 1, j);
      const size = parseInt(line.slice(j + 1), 10) || 0;
      return { kind, name, size };
    });
    // sort: dirs first, then files
    rows.sort((a, b) => {
      if (a.kind !== b.kind) return a.kind === 'DIR' ? -1 : 1;
      return a.name.localeCompare(b.name);
    });
    setEntries(rows);
    setCwd(path);
  }, [getTransport]);

  const fetchInfo = useCallback(async () => {
    const t = getTransport();
    const resp = await t.send(CTX_FM, C_INFO, null);
    const text = bytesStr(resp);
    let parsed = null;
    try { parsed = JSON.parse(text); } catch (_) { parsed = { raw: text }; }
    setInfo(parsed);
    if (parsed?.version && expectedVersion && expectedVersion !== 'dev' && parsed.version !== expectedVersion) {
      setVersionMismatch(true);
      pushLog(`! firmware ${parsed.version} doesn't match website v${expectedVersion}`);
    } else {
      setVersionMismatch(false);
    }
    pushLog(`device version: ${parsed?.version || 'unknown'}`);
    return parsed;
  }, [getTransport, expectedVersion, pushLog]);

  const _connectVia = useCallback(async (mode) => {
    setErrorMsg('');
    setStatus(STATUS.CONNECTING);
    try {
      const t = getTransport();
      if (mode === 'bluetooth') {
        await t.connectBluetooth();
        pushLog('bluetooth gatt connected');
      } else {
        await t.connectSerial();
        pushLog('serial port opened @ 115200');
      }
      await fetchInfo();
      await refreshDir('/');
      setStatus(STATUS.CONNECTED);
    } catch (err) {
      const raw = err?.message || String(err);
      pushLog(`error: ${raw}`);
      // A timeout right after the port opened means nothing is answering FM
      // commands — almost always the device toggle is off.
      const msg = /timed out|timeout/i.test(raw)
        ? (mode === 'bluetooth'
            ? 'No response from the device. Turn on “Bluetooth → Remote Device” on the device, then reconnect.'
            : 'No response from the device. Turn on “HID → USB Remote” on the device, then reconnect.')
        : raw;
      setErrorMsg(msg);
      setStatus(STATUS.ERROR);
      // Release the opened port so the next Connect starts clean.
      try { if (transportRef.current) await transportRef.current.disconnect(); } catch (_) {}
      transportRef.current = null;
    }
  }, [getTransport, fetchInfo, refreshDir, pushLog]);

  const onConnectSerial    = useCallback(() => _connectVia('serial'),    [_connectVia]);
  const onConnectBluetooth = useCallback(() => _connectVia('bluetooth'), [_connectVia]);

  const onDisconnect = useCallback(async () => {
    try {
      if (transportRef.current) await transportRef.current.disconnect();
    } catch (_) {}
    transportRef.current = null;
    setStatus(STATUS.IDLE);
    setInfo(null);
    setEntries([]);
    setCwd('/');
    setVersionMismatch(false);
    setErrorMsg('');
    setProgress(null);
    pushLog('disconnected');
  }, [pushLog]);

  // ── Actions ───────────────────────────────────────────────────────────────

  const withWork = useCallback(async (label, fn) => {
    setStatus(STATUS.WORKING);
    setBusyMsg(label);
    setErrorMsg('');
    try {
      const result = await fn();
      return result;
    } catch (err) {
      const msg = err?.message || String(err);
      setErrorMsg(msg);
      pushLog(`error: ${msg}`);
      throw err;
    } finally {
      setBusyMsg('');
      setProgress(null);
      setStatus((s) => (s === STATUS.WORKING ? STATUS.CONNECTED : s));
    }
  }, [pushLog]);

  const onOpen = useCallback((row) => {
    if (row.kind === 'DIR') {
      const next = normalizePath(cwd, row.name);
      withWork('loading', () => refreshDir(next)).catch(() => {});
    } else if (viewRef.current) {
      viewRef.current(row);
    }
  }, [cwd, refreshDir, withWork]);

  const onUp = useCallback(() => {
    if (cwd === '/') return;
    const parent = cwd.replace(/\/+$/, '').replace(/\/[^/]+$/, '') || '/';
    withWork('loading', () => refreshDir(parent)).catch(() => {});
  }, [cwd, refreshDir, withWork]);

  const onRefresh = useCallback(() => {
    withWork('refreshing', () => refreshDir(cwd)).catch(() => {});
  }, [cwd, refreshDir, withWork]);

  const onMkdir = useCallback(async () => {
    const name = window.prompt('New folder name:');
    if (!name) return;
    const target = normalizePath(cwd, name);
    await withWork('creating folder', async () => {
      const t = getTransport();
      await t.send(CTX_FM, C_MKDIR, strBytes(target));
      pushLog(`mkdir ${target}`);
      await refreshDir(cwd);
    }).catch(() => {});
  }, [cwd, getTransport, refreshDir, pushLog, withWork]);

  const onTouch = useCallback(async () => {
    const name = window.prompt('New file name:');
    if (!name) return;
    const target = normalizePath(cwd, name);
    await withWork('creating file', async () => {
      const t = getTransport();
      await t.send(CTX_FM, C_TOUCH, strBytes(target));
      pushLog(`touch ${target}`);
      await refreshDir(cwd);
    }).catch(() => {});
  }, [cwd, getTransport, refreshDir, pushLog, withWork]);

  const onDelete = useCallback(async (row) => {
    const target = normalizePath(cwd, row.name);
    if (!window.confirm(`Delete ${target}?`)) return;
    await withWork(`deleting ${row.name}`, async () => {
      const t = getTransport();
      await t.send(CTX_FM, C_RM, strBytes(target));
      pushLog(`rm ${target}`);
      await refreshDir(cwd);
    }).catch(() => {});
  }, [cwd, getTransport, refreshDir, pushLog, withWork]);

  const onRename = useCallback(async (row) => {
    const src = normalizePath(cwd, row.name);
    const newName = window.prompt('New name:', row.name);
    if (!newName || newName === row.name) return;
    const dst = normalizePath(cwd, newName);
    await withWork(`renaming ${row.name}`, async () => {
      const t = getTransport();
      const buf = new Uint8Array(src.length + 1 + dst.length);
      buf.set(strBytes(src), 0);
      buf[src.length] = 0;
      buf.set(strBytes(dst), src.length + 1);
      await t.send(CTX_FM, C_MV, buf);
      pushLog(`mv ${src} -> ${dst}`);
      await refreshDir(cwd);
    }).catch(() => {});
  }, [cwd, getTransport, refreshDir, pushLog, withWork]);

  const onDownload = useCallback(async (row) => {
    const target = normalizePath(cwd, row.name);
    await withWork(`downloading ${row.name}`, async () => {
      const t = getTransport();
      const chunks = [];
      let received = 0;
      setProgress({ label: `download ${row.name}`, value: 0, total: row.size || 0 });
      await t.send(CTX_FM, C_GET, strBytes(target), {
        timeoutMs: 15000,
        onChunk: (chunk) => {
          chunks.push(chunk);
          received += chunk.length;
          setProgress((p) => p ? { ...p, value: received } : p);
        },
      });
      const blob = new Blob(chunks);
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = row.name;
      a.click();
      URL.revokeObjectURL(a.href);
      pushLog(`downloaded ${target} (${formatBytes(received)})`);
    }).catch(() => {});
  }, [cwd, getTransport, pushLog, withWork]);

  const onView = useCallback(async (row) => {
    const target = normalizePath(cwd, row.name);
    await withWork(`opening ${row.name}`, async () => {
      const t = getTransport();
      const chunks = [];
      let received = 0;
      setProgress({ label: `read ${row.name}`, value: 0, total: row.size || 0 });
      await t.send(CTX_FM, C_GET, strBytes(target), {
        timeoutMs: 15000,
        onChunk: (chunk) => {
          chunks.push(chunk);
          received += chunk.length;
          setProgress((p) => p ? { ...p, value: received } : p);
        },
      });
      // Concatenate
      const bytes = new Uint8Array(received);
      let off = 0;
      for (const c of chunks) { bytes.set(c, off); off += c.length; }
      const mime = detectImageMime(bytes, row.name);
      let imageUrl = null;
      let text = '';
      if (mime) {
        imageUrl = URL.createObjectURL(new Blob([bytes], { type: mime }));
      } else {
        try { text = new TextDecoder('utf-8', { fatal: false }).decode(bytes); } catch (_) { text = ''; }
      }
      setViewer({ name: row.name, path: target, bytes, mime, imageUrl });
      setTextContent(text);
      setTextDirty(false);
      setViewMode(mime ? 'image' : 'text');
      pushLog(`read ${target} (${formatBytes(received)})${mime ? ` · ${mime}` : ''}`);
    }).catch(() => {});
  }, [cwd, getTransport, pushLog, withWork]);
  // Keep the ref in sync so onOpen (declared earlier) can fire it.
  viewRef.current = onView;

  const closeViewer = useCallback(() => {
    if (textDirty && !window.confirm('Discard unsaved changes?')) return;
    setViewer((v) => {
      if (v?.imageUrl) URL.revokeObjectURL(v.imageUrl);
      return null;
    });
    setTextContent('');
    setTextDirty(false);
    setSavingFile(false);
  }, [textDirty]);

  const onSaveText = useCallback(async () => {
    if (!viewer) return;
    const bytes = new TextEncoder().encode(textContent);
    setSavingFile(true);
    try {
      await withWork(`saving ${viewer.name}`, async () => {
        const t = getTransport();
        const total = bytes.length;
        const pathBytes = strBytes(viewer.path);
        const begin = new Uint8Array(4 + pathBytes.length);
        begin[0] = total & 0xff;
        begin[1] = (total >>> 8) & 0xff;
        begin[2] = (total >>> 16) & 0xff;
        begin[3] = (total >>> 24) & 0xff;
        begin.set(pathBytes, 4);
        await t.send(CTX_FM, C_PUT_BEGIN, begin);
        setProgress({ label: `save ${viewer.name}`, value: 0, total });
        let sent = 0;
        const chunkSize = t.getChunkSize();
        while (sent < bytes.length) {
          const end = Math.min(sent + chunkSize, bytes.length);
          await t.send(CTX_FM, C_PUT_CHUNK, bytes.subarray(sent, end));
          sent = end;
          setProgress({ label: `save ${viewer.name}`, value: sent, total });
        }
        await t.send(CTX_FM, C_PUT_END, null);
        pushLog(`saved ${viewer.path} (${formatBytes(total)})`);
        setViewer((v) => v ? { ...v, bytes } : v);
        setTextDirty(false);
        await refreshDir(cwd);
      });
    } finally {
      setSavingFile(false);
    }
  }, [viewer, textContent, cwd, getTransport, pushLog, refreshDir, withWork]);

  // ESC closes viewer · Ctrl/Cmd+S saves in text mode · lock body scroll
  useEffect(() => {
    if (!viewer) return;
    const onKey = (e) => {
      if (e.key === 'Escape') {
        closeViewer();
        return;
      }
      // Save shortcut — only meaningful when editing text, and only if there
      // are unsaved edits to flush. Stop the browser from intercepting Cmd+S
      // as "save page".
      const isSave = (e.key === 's' || e.key === 'S') && (e.metaKey || e.ctrlKey);
      if (isSave && viewMode === 'text') {
        e.preventDefault();
        if (textDirty && !savingFile) onSaveText();
      }
    };
    document.addEventListener('keydown', onKey);
    const prevOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      document.removeEventListener('keydown', onKey);
      document.body.style.overflow = prevOverflow;
    };
  }, [viewer, viewMode, textDirty, savingFile, onSaveText, closeViewer]);

  const onUploadClick = useCallback(() => {
    if (uploadInputRef.current) uploadInputRef.current.click();
  }, []);

  const onUploadFiles = useCallback(async (files) => {
    if (!files || files.length === 0) return;
    for (let idx = 0; idx < files.length; idx++) {
      const file = files[idx];
      const target = normalizePath(cwd, file.name);
      const tag = files.length > 1 ? `${file.name} (${idx + 1}/${files.length})` : file.name;
      try {
        await withWork(`uploading ${tag}`, async () => {
          const t = getTransport();
          const total = file.size;
          // PUT_BEGIN payload: u32 total LE + path
          const pathBytes = strBytes(target);
          const begin = new Uint8Array(4 + pathBytes.length);
          begin[0] = total & 0xff;
          begin[1] = (total >>> 8) & 0xff;
          begin[2] = (total >>> 16) & 0xff;
          begin[3] = (total >>> 24) & 0xff;
          begin.set(pathBytes, 4);
          await t.send(CTX_FM, C_PUT_BEGIN, begin);
          setProgress({ label: `upload ${tag}`, value: 0, total });
          let sent = 0;
          const chunkSize = t.getChunkSize();
          const buffer = await file.arrayBuffer();
          const view = new Uint8Array(buffer);
          while (sent < view.length) {
            const end = Math.min(sent + chunkSize, view.length);
            const chunk = view.subarray(sent, end);
            await t.send(CTX_FM, C_PUT_CHUNK, chunk);
            sent = end;
            setProgress({ label: `upload ${tag}`, value: sent, total });
          }
          await t.send(CTX_FM, C_PUT_END, null);
          pushLog(`uploaded ${target} (${formatBytes(total)})`);
        });
      } catch (_) { break; } // bail on first failure
    }
    await refreshDir(cwd).catch(() => {});
    if (uploadInputRef.current) uploadInputRef.current.value = '';
  }, [cwd, getTransport, pushLog, refreshDir, withWork]);

  const dropEnabled = status === STATUS.CONNECTED || status === STATUS.WORKING;
  const onDragEnter = useCallback((e) => {
    if (!dropEnabled) return;
    if (!e.dataTransfer?.types?.includes('Files')) return;
    e.preventDefault();
    dragDepthRef.current += 1;
    setDragOver(true);
  }, [dropEnabled]);
  const onDragOver = useCallback((e) => {
    if (!dropEnabled) return;
    if (!e.dataTransfer?.types?.includes('Files')) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
  }, [dropEnabled]);
  const onDragLeave = useCallback(() => {
    dragDepthRef.current = Math.max(0, dragDepthRef.current - 1);
    if (dragDepthRef.current === 0) setDragOver(false);
  }, []);
  const onDrop = useCallback((e) => {
    e.preventDefault();
    dragDepthRef.current = 0;
    setDragOver(false);
    if (!dropEnabled) return;
    const files = e.dataTransfer?.files;
    if (files && files.length > 0) onUploadFiles(files);
  }, [dropEnabled, onUploadFiles]);

  // ── Render ────────────────────────────────────────────────────────────────

  const breadcrumbs = (() => {
    const parts = cwd.split('/').filter(Boolean);
    const segs = [{ label: '/', path: '/' }];
    let acc = '';
    for (const p of parts) {
      acc += `/${p}`;
      segs.push({ label: p, path: acc });
    }
    return segs;
  })();

  const isConnected = status === STATUS.CONNECTED || status === STATUS.WORKING;
  const usedPct = info && info.total ? Math.min(100, Math.round((info.used / info.total) * 100)) : 0;
  const progressPct = progress && progress.total ? Math.min(100, Math.round((progress.value / progress.total) * 100)) : 0;

  return (
    <div className="fm-wrap">
      <div className="fm-toolbar">
        <div className="fm-toolbar-left">
          {!isConnected ? (
            <>
              <button
                type="button"
                className="fm-btn fm-btn-primary"
                onClick={onConnectSerial}
                disabled={!hasSerial || status === STATUS.CONNECTING}
                title={hasSerial ? 'Connect over USB serial' : 'Web Serial not supported in this browser'}
              >
                {status === STATUS.CONNECTING ? 'Connecting…' : 'Connect USB'}
              </button>
              <button
                type="button"
                className="fm-btn"
                onClick={onConnectBluetooth}
                disabled={!hasBluetooth || status === STATUS.CONNECTING}
                title={hasBluetooth ? 'Connect over Bluetooth (NUS)' : 'Web Bluetooth not supported in this browser'}
              >
                Connect Bluetooth
              </button>
            </>
          ) : (
            <>
              <button type="button" className="fm-btn" onClick={onRefresh} disabled={status === STATUS.WORKING}>Refresh</button>
              <button type="button" className="fm-btn" onClick={onMkdir} disabled={status === STATUS.WORKING}>New folder</button>
              <button type="button" className="fm-btn" onClick={onTouch} disabled={status === STATUS.WORKING}>New file</button>
              <button type="button" className="fm-btn fm-btn-primary" onClick={onUploadClick} disabled={status === STATUS.WORKING}>Upload</button>
              <input
                type="file"
                multiple
                ref={uploadInputRef}
                style={{ display: 'none' }}
                onChange={(e) => onUploadFiles(e.target.files)}
              />
              <button type="button" className="fm-btn fm-btn-ghost" onClick={onDisconnect}>Disconnect</button>
            </>
          )}
        </div>
        <div className="fm-toolbar-right">
          <span className="fm-status-dot" style={{ background: STATUS_COLOR[status] }} />
          <span className="fm-status-text">{status}</span>
        </div>
      </div>

      {!supported && (
        <div className="fm-banner fm-banner-warn">
          Neither Web Serial nor Web Bluetooth is supported in this browser.
          Use Chrome / Edge on desktop, or Chrome on Android (Bluetooth only).
        </div>
      )}

      {supported && !isConnected && (
        <div className="fm-banner fm-banner-warn">
          Connecting over <strong>USB</strong>? Turn on <strong>HID &rarr; USB Remote</strong> on the
          device &mdash; it&apos;s off by default and won&apos;t respond until enabled. For
          <strong> Bluetooth</strong>, turn on <strong>Bluetooth &rarr; Remote Device</strong> so it
          starts advertising.
        </div>
      )}

      {errorMsg && (
        <div className="fm-banner fm-banner-err">
          {errorMsg}
        </div>
      )}

      {info && (
        <div className={`fm-info ${versionMismatch ? 'fm-info-mismatch' : ''}`}>
          <div className="fm-info-row">
            <span className="fm-info-label">Device</span>
            <span>{info.name || 'UniGeek'}</span>
          </div>
          <div className="fm-info-row">
            <span className="fm-info-label">Firmware</span>
            <span>
              {info.version || 'unknown'}
              {expectedVersion && expectedVersion !== 'dev' && (
                <span className="fm-info-expect">
                  {versionMismatch ? ` ≠ website v${expectedVersion}` : ` (matches v${expectedVersion})`}
                </span>
              )}
            </span>
          </div>
          <div className="fm-info-row">
            <span className="fm-info-label">Storage</span>
            <span>
              {formatBytes(info.used || 0)} / {formatBytes(info.total || 0)}
              <span className="fm-info-pct"> · {usedPct}%</span>
            </span>
          </div>
          <div className="fm-info-row">
            <span className="fm-info-label">Free heap</span>
            <span>{formatBytes(info.heap || 0)}</span>
          </div>
        </div>
      )}

      {versionMismatch && (
        <div className="fm-banner fm-banner-warn">
          The device reports firmware <strong>{info?.version}</strong>, but this website is built
          for <strong>v{expectedVersion}</strong>. The protocol may still work, but consider
          re-flashing or visiting the matching version of the site.
        </div>
      )}

      <div
        className={`fm-pane${dragOver ? ' fm-pane-dragover' : ''}`}
        onDragEnter={onDragEnter}
        onDragOver={onDragOver}
        onDragLeave={onDragLeave}
        onDrop={onDrop}
      >
        {dragOver && (
          <div className="fm-drop-overlay" aria-hidden="true">
            <div className="fm-drop-overlay-card">
              <div className="fm-drop-overlay-icon">⬇</div>
              <div className="fm-drop-overlay-title">Drop to upload</div>
              <div className="fm-drop-overlay-sub">{cwd}</div>
            </div>
          </div>
        )}
        <div className="fm-breadcrumbs">
          <button type="button" className="fm-crumb-up" onClick={onUp} disabled={cwd === '/' || !isConnected}>↑ up</button>
          {breadcrumbs.map((seg, i) => (
            <span key={i} className="fm-crumb">
              <button
                type="button"
                className={i === 0 ? 'fm-crumb-home' : ''}
                onClick={() => isConnected && withWork('loading', () => refreshDir(seg.path)).catch(() => {})}
                disabled={!isConnected}
                aria-label={i === 0 ? 'Home (root)' : seg.label}
                title={i === 0 ? 'Root' : seg.label}
              >
                {i === 0 ? <IconHome width={14} height={14} /> : seg.label}
              </button>
              {i < breadcrumbs.length - 1 && <span className="fm-crumb-sep">/</span>}
            </span>
          ))}
        </div>

        <div className="fm-list">
          {!isConnected && (
            <div className="fm-empty">Not connected. Click <strong>Connect device</strong> and pick the UniGeek serial port.</div>
          )}
          {isConnected && entries.length === 0 && (
            <div className="fm-empty">Empty directory.</div>
          )}
          {entries.map((row, i) => (
            <div className="fm-row" key={`${row.kind}:${row.name}:${i}`}>
              <button
                type="button"
                className={`fm-row-main fm-row-${row.kind === 'DIR' ? 'dir' : 'file'}`}
                onClick={() => onOpen(row)}
                disabled={status === STATUS.WORKING}
                title={row.kind === 'DIR' ? 'Open folder' : 'View / edit file'}
              >
                <span className="fm-row-icon">{row.kind === 'DIR' ? '▸' : '·'}</span>
                <span className="fm-row-name">{row.name}</span>
                <span className="fm-row-size">{row.kind === 'DIR' ? '' : formatBytes(row.size)}</span>
              </button>
              <div className="fm-row-actions">
                {row.kind === 'FILE' && (
                  <>
                    <button type="button" className="fm-act" title="View / Edit" onClick={() => onView(row)} disabled={status === STATUS.WORKING}>
                      <IconEye />
                    </button>
                    <button type="button" className="fm-act" title="Download" onClick={() => onDownload(row)} disabled={status === STATUS.WORKING}>
                      <IconDownload />
                    </button>
                  </>
                )}
                <button type="button" className="fm-act" title="Rename" onClick={() => onRename(row)} disabled={status === STATUS.WORKING}>
                  <IconPencil />
                </button>
                <button type="button" className="fm-act fm-act-danger" title="Delete" onClick={() => onDelete(row)} disabled={status === STATUS.WORKING}>
                  <IconTrash />
                </button>
              </div>
            </div>
          ))}
        </div>

        {progress && (
          <div className="fm-progress">
            <span>{progress.label}</span>
            <div className="fm-progress-bar">
              <div className="fm-progress-fill" style={{ width: `${progressPct}%` }} />
            </div>
            <span>{formatBytes(progress.value)} / {formatBytes(progress.total)} · {progressPct}%</span>
          </div>
        )}

        {busyMsg && !progress && (
          <div className="fm-progress fm-progress-indeterminate">
            <span>{busyMsg}…</span>
          </div>
        )}
      </div>

      <div className="fm-console-chrome">
        <span>
          console
          {' · '}
          {transportRef.current?.getKind?.() === 'bluetooth'
            ? 'BLE NUS'
            : transportRef.current?.getKind?.() === 'serial'
              ? 'USB · 115200'
              : 'idle'}
        </span>
      </div>
      <div className="fm-console" ref={logEndRef}>
        {logLines.length === 0
          ? <div className="fm-console-line dim">waiting for events…</div>
          : logLines.map((ln, i) => <div className="fm-console-line" key={i}>{ln}</div>)
        }
      </div>

      {viewer && mounted && createPortal(
        <div className="fm-modal-backdrop">
          <div className="fm-modal" role="dialog" aria-modal="true">
            <div className="fm-modal-head">
              <div className="fm-modal-title">
                <span className="fm-modal-path">{viewer.path}</span>
                <span className="fm-modal-size">{formatBytes(viewer.bytes.length)}{textDirty ? ' · unsaved' : ''}</span>
              </div>
              <div className="fm-modal-tabs">
                {viewer.mime ? (
                  <button type="button" className={`fm-modal-tab${viewMode === 'image' ? ' active' : ''}`} onClick={() => setViewMode('image')}>Image</button>
                ) : (
                  <button type="button" className={`fm-modal-tab${viewMode === 'text' ? ' active' : ''}`} onClick={() => setViewMode('text')}>Text</button>
                )}
                <button type="button" className={`fm-modal-tab${viewMode === 'hex' ? ' active' : ''}`} onClick={() => setViewMode('hex')}>Hex</button>
              </div>
              <button type="button" className="fm-modal-close" onClick={closeViewer} aria-label="Close">×</button>
            </div>
            <div className="fm-modal-body">
              {viewMode === 'image' && viewer.imageUrl ? (
                <div className="fm-modal-image-wrap">
                  {/* eslint-disable-next-line @next/next/no-img-element */}
                  <img src={viewer.imageUrl} alt={viewer.name} className="fm-modal-image" />
                </div>
              ) : viewMode === 'text' ? (
                <textarea
                  className="fm-modal-textarea"
                  value={textContent}
                  onChange={(e) => { setTextContent(e.target.value); setTextDirty(true); }}
                  spellCheck={false}
                  wrap="off"
                />
              ) : (() => {
                const rows = buildHexRows(viewer.bytes);
                return (
                  <div className="fm-modal-hex">
                    {rows.length === 0 ? (
                      <div className="fm-hex-empty">(empty)</div>
                    ) : rows.map((r, idx) => r.truncated ? (
                      <div key={idx} className="fm-hex-truncated">… {r.truncated.toLocaleString()} more bytes truncated …</div>
                    ) : (
                      <div key={idx} className="fm-hex-row">
                        <span className="fm-hex-offset">{r.offset}</span>
                        <span className="fm-hex-bytes">{r.hex}</span>
                        <span className="fm-hex-ascii">{r.ascii}</span>
                      </div>
                    ))}
                  </div>
                );
              })()}
            </div>
            <div className="fm-modal-foot">
              <span className="fm-modal-meta">
                {viewMode === 'image'
                  ? `image · ${viewer.mime}`
                  : viewMode === 'text'
                    ? 'UTF-8 · ⌘/Ctrl+S to save'
                    : 'read-only · 16 bytes / line · first 64 KB shown'}
              </span>
              <div className="fm-modal-actions">
                <button type="button" className="fm-btn fm-btn-ghost" onClick={closeViewer}>Close</button>
                {viewMode === 'text' && (
                  <button
                    type="button"
                    className="fm-btn fm-btn-primary"
                    onClick={onSaveText}
                    disabled={!textDirty || savingFile}
                  >
                    {savingFile ? 'Saving…' : 'Save'}
                  </button>
                )}
              </div>
            </div>
          </div>
        </div>,
        document.body
      )}
    </div>
  );
}
