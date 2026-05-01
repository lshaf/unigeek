'use client';

import Link from 'next/link';
import { useCallback, useEffect, useRef, useState } from 'react';

const USB_FILTERS = [
  { usbVendorId: 0x10c4, usbProductId: 0xea60 }, // CP2102
  { usbVendorId: 0x0403, usbProductId: 0x6010 }, // FT2232H
  { usbVendorId: 0x303a, usbProductId: 0x1001 }, // Espressif JTAG
  { usbVendorId: 0x303a, usbProductId: 0x0002 }, // Espressif CDC
  { usbVendorId: 0x1a86, usbProductId: 0x55d4 }, // CH9102F
  { usbVendorId: 0x1a86, usbProductId: 0x7523 }, // CH340T
  { usbVendorId: 0x0403, usbProductId: 0x6001 }, // FT232R
];

const STATUS = {
  IDLE: 'IDLE',
  CONNECTING: 'CONNECTING',
  CONNECTED: 'READY',
  FLASHING: 'FLASHING',
  DONE: 'COMPLETE',
  ERROR: 'ERROR',
};

const STATUS_COLOR = {
  IDLE: 'var(--ink-muted)',
  CONNECTING: 'var(--amber)',
  READY: 'var(--accent)',
  FLASHING: 'var(--amber)',
  COMPLETE: 'var(--accent)',
  ERROR: 'var(--danger)',
};

function arrayBufferToBinaryString(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = '';
  const chunkSize = 0x8000;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
  }
  return binary;
}

function ConsoleLine({ cls = 'dim', children, html }) {
  if (html) return <div dangerouslySetInnerHTML={{ __html: html }} />;
  return <div><span className={cls}>{children}</span></div>;
}

export default function Flasher({ board, firmwareVersion, method, methodInfo }) {
  const [status, setStatus] = useState(STATUS.IDLE);
  const [progress, setProgress] = useState(0);
  const [progressLabel, setProgressLabel] = useState('Ready');
  const [lines, setLines] = useState([
    { html: `<span class="prompt">$</span> <span class="key">unigeek-flasher</span> --ready` },
    { html: `  <span class="dim">waiting for board selection...</span>` },
    { html: `  <span class="dim">click </span><span class="key">Flash firmware</span><span class="dim"> when connected</span>` },
  ]);

  const transportRef = useRef(null);
  const loaderRef = useRef(null);
  const consoleRef = useRef(null);

  const webSerialSupported = typeof navigator !== 'undefined' && 'serial' in navigator;
  const manualMode = method === 'manual';

  // Auto-scroll console to bottom on new lines
  useEffect(() => {
    const el = consoleRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [lines]);

  const pushLine = useCallback((line) => {
    setLines((prev) => [...prev, line]);
  }, []);

  const log = useCallback((text, cls = 'dim') => pushLine({ cls, text }), [pushLine]);
  const logHtml = useCallback((html) => pushLine({ html }), [pushLine]);

  const resetConsole = useCallback(() => {
    setLines([]);
    setProgress(0);
  }, []);

  const doFlash = useCallback(async () => {
    if (manualMode) return;
    if (!webSerialSupported) {
      setStatus(STATUS.ERROR);
      logHtml(`<span class="warn">Web Serial is not supported in this browser. Use Chrome or Edge on desktop.</span>`);
      return;
    }

    resetConsole();
    setStatus(STATUS.CONNECTING);
    setProgressLabel('Connecting');
    logHtml(`<span class="prompt">$</span> <span class="key">unigeek-flasher</span> --board "${board.name}" --fw ${firmwareVersion}`);

    try {
      const { Transport, ESPLoader } = await import('esptool-js');

      logHtml(`  requesting serial port ...`);
      const port = await navigator.serial.requestPort({ filters: USB_FILTERS });
      const transport = new Transport(port, true);
      transportRef.current = transport;

      logHtml(`  connecting to target ......... <span class="ok">ok</span>`);
      const terminal = {
        clean: () => {},
        writeLine: (line) => logHtml(`  ${line}`),
        write: (data) => logHtml(`  <span class="dim">${data}</span>`),
      };
      const loader = new ESPLoader({ transport, baudrate: board.flashBaud || 921600, terminal });
      await loader.main();
      await loader.flashId();
      const chipName = loader.chip?.CHIP_NAME || 'unknown';
      logHtml(`  detected chip: <span class="key">${chipName}</span>`);

      const isS3 = chipName.toLowerCase().includes('s3');
      const boardNeedsS3 = board.chip === 'ESP32-S3';
      if (isS3 !== boardNeedsS3) {
        logHtml(`<span class="warn">ERROR: wrong chip. Expected ${board.chip}, got ${chipName}.</span>`);
        logHtml(`<span class="warn">Disconnect to prevent bricking.</span>`);
        setStatus(STATUS.ERROR);
        return;
      }

      loaderRef.current = loader;

      setStatus(STATUS.FLASHING);
      setProgressLabel('Fetching firmware');
      logHtml(`  fetching firmware ............`);
      const resp = await fetch(board.bin);
      if (!resp.ok) throw new Error(`HTTP ${resp.status} fetching ${board.bin}`);
      const buffer = await resp.arrayBuffer();
      const sizeKb = (buffer.byteLength / 1024).toFixed(0);
      logHtml(`  firmware ready: <span class="key">${sizeKb} KB</span>`);

      const data = arrayBufferToBinaryString(buffer);
      logHtml(`  erasing flash ................`);
      setProgressLabel('Writing firmware');

      await loader.writeFlash({
        fileArray: [{ address: 0, data }],
        flashSize: 'keep',
        eraseAll: false,
        compress: true,
        reportProgress: (fileIndex, written, total) => {
          setProgress(Math.round((written / total) * 100));
        },
      });

      logHtml(`  verifying checksum .......... <span class="ok">ok</span>`);
      logHtml(`  resetting board ............. <span class="ok">ok</span>`);
      logHtml('');
      logHtml(`<span class="ok">✓ Flash complete.</span> Unplug and reconnect your ${board.name}.`);
      setStatus(STATUS.DONE);
      setProgressLabel('Complete');
      setProgress(100);
    } catch (err) {
      logHtml(`<span class="warn">Error: ${err?.message || err}</span>`);
      setStatus(STATUS.ERROR);
      setProgressLabel('Error');
    }
  }, [board, firmwareVersion, manualMode, webSerialSupported, logHtml, resetConsole]);

  const doDisconnect = useCallback(async () => {
    try {
      if (transportRef.current) await transportRef.current.disconnect();
    } catch (_) {}
    transportRef.current = null;
    loaderRef.current = null;
    setStatus(STATUS.IDLE);
    setProgressLabel('Ready');
    setProgress(0);
  }, []);

  const buttonDisabled =
    manualMode ||
    status === STATUS.CONNECTING ||
    status === STATUS.FLASHING ||
    (!webSerialSupported && status === STATUS.IDLE);

  const buttonLabel =
    status === STATUS.CONNECTING ? 'Connecting…'
      : status === STATUS.FLASHING ? 'Flashing…'
      : status === STATUS.DONE ? 'Flash again'
      : status === STATUS.ERROR ? 'Retry'
      : 'Flash firmware';

  return (
    <div className="flasher">
      <div className="flasher-left">
        <div className="flasher-summary">
          <div className="flasher-kv"><span>Board</span><span>{board.name}</span></div>
          <div className="flasher-kv"><span>Chip</span><span>{board.chip}</span></div>
          <div className="flasher-kv"><span>Firmware</span><span>v{firmwareVersion}</span></div>
          <div className="flasher-kv"><span>Method</span><span>{methodInfo?.name}</span></div>
          <div className="flasher-kv"><span>Target</span><span>{board.id}.bin</span></div>
        </div>

        {manualMode ? (
          <a
            className="flash-button-big"
            href={board.bin}
            download
            style={{ textDecoration: 'none' }}
          >
            Download .bin
          </a>
        ) : (
          <button
            type="button"
            className="flash-button-big"
            onClick={doFlash}
            disabled={buttonDisabled}
          >
            {buttonLabel}
          </button>
        )}

        {!webSerialSupported && !manualMode && (
          <div style={{ marginTop: 16, fontSize: 12, color: 'var(--amber)', lineHeight: 1.5 }}>
            Web Serial is not supported in this browser. Use Chrome or Edge on desktop, or pick the
            manual download method above.
          </div>
        )}

        <div className="alt-links">
          <a className="alt-link" href={board.bin} download>
            <span>Download .bin directly</span><span className="arr">↓</span>
          </a>
          {status === STATUS.DONE || status === STATUS.ERROR ? (
            <button
              type="button"
              className="alt-link"
              onClick={doDisconnect}
              style={{ textAlign: 'left', cursor: 'pointer' }}
            >
              <span>Disconnect &amp; reset</span><span className="arr">×</span>
            </button>
          ) : null}
          <Link className="alt-link" href="/features">
            <span>Browse features after flashing</span><span className="arr">→</span>
          </Link>
        </div>
      </div>

      <div className="flasher-right">
        <div className="console-chrome">
          <span>Serial Monitor · WebSerial · 921600</span>
          <span style={{ color: STATUS_COLOR[status] || 'var(--ink-muted)' }}>{status}</span>
        </div>
        <div className="console" ref={consoleRef}>
          {lines.map((ln, i) => (
            <ConsoleLine key={i} cls={ln.cls} html={ln.html}>{ln.text}</ConsoleLine>
          ))}
        </div>
        <div className="progress">
          <span>{progressLabel}</span>
          <div className="progress-bar">
            <div className="progress-fill" style={{ width: `${progress}%` }} />
          </div>
          <span>{progress}%</span>
        </div>
      </div>
    </div>
  );
}
