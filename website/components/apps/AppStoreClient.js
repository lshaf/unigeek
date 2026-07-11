'use client';

import { useCallback, useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { marked } from 'marked';

import {
  CTX_FM, C_INFO,
  strBytes, bytesStr, formatBytes,
  createTransport, installFile,
} from '@/components/files/fmProtocol';

// Where installed scripts land on the SD card — mirrors the firmware's
// LuaScreen ROOT_DIR (/unigeek/lua). subghz/sample.lua -> /unigeek/lua/subghz/sample.lua.
const INSTALL_BASE = '/unigeek/lua';

// How many cards to show initially and per "Load more" click.
const PAGE_SIZE = 12;

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

// Resolve an image/link URL found inside an app's markdown doc. Absolute URLs
// pass through; root-relative and doc-relative paths are anchored to the repo's
// raw base so they load from GitHub.
function resolveDocUrl(src, rawBase, docPath) {
  if (!src) return src;
  if (/^(https?:)?\/\//i.test(src) || src.startsWith('data:')) return src;
  if (src.startsWith('/')) return `${rawBase}${src}`;
  const slash = docPath.lastIndexOf('/');
  const dir = slash >= 0 ? docPath.slice(0, slash) : '';
  return dir ? `${rawBase}/${dir}/${src}` : `${rawBase}/${src}`;
}

// Absolute URL for an app's cover image (repo-relative paths anchored to raw base).
function appImageUrl(app, rawBase) {
  if (!app?.image) return null;
  if (/^(https?:)?\/\//i.test(app.image) || app.image.startsWith('data:')) return app.image;
  return `${rawBase}/${app.image.replace(/^\//, '')}`;
}

function renderDoc(md, rawBase, docPath) {
  const html = marked.parse(md, { headerIds: false, mangle: false });
  // Rewrite <img src> and <a href> that point at repo-relative assets.
  return html
    .replace(/(<img\b[^>]*\bsrc=")([^"]+)(")/gi, (_m, a, src, c) => a + resolveDocUrl(src, rawBase, docPath) + c)
    .replace(/(<a\b[^>]*\bhref=")([^"]+)(")/gi, (_m, a, href, c) => {
      // Leave external + anchor links alone; only rewrite relative repo paths.
      if (/^(https?:)?\/\//i.test(href) || href.startsWith('#') || href.startsWith('mailto:')) {
        return a + href + c;
      }
      return a + resolveDocUrl(href, rawBase, docPath) + c;
    });
}

export default function AppStoreClient({ expectedVersion, rawBase, repo }) {
  const [status, setStatus] = useState(STATUS.IDLE);
  const [info, setInfo] = useState(null);
  const [errorMsg, setErrorMsg] = useState('');
  const [logLines, setLogLines] = useState([]);

  const [apps, setApps] = useState(null);      // null = loading, [] = empty
  const [appsError, setAppsError] = useState('');

  const [selected, setSelected] = useState(null); // app object for modal
  const [docHtml, setDocHtml] = useState('');
  const [docState, setDocState] = useState('idle'); // idle | loading | ready | none

  const [installing, setInstalling] = useState(null); // app.path being installed
  const [progress, setProgress] = useState(null);      // { value, total }
  const [installed, setInstalled] = useState({});      // path -> true
  const [activeCat, setActiveCat] = useState('all');   // category filter
  const [visible, setVisible] = useState(PAGE_SIZE);   // paginated card count

  const transportRef = useRef(null);
  const logEndRef = useRef(null);

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

  const getTransport = useCallback(() => {
    if (!transportRef.current) {
      transportRef.current = createTransport({ onLog: pushLog });
    }
    return transportRef.current;
  }, [pushLog]);

  // ── Catalog ────────────────────────────────────────────────────────────────

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const res = await fetch(`${rawBase}/apps.json`, { cache: 'no-store' });
        if (!res.ok) throw new Error(`apps.json HTTP ${res.status}`);
        const data = await res.json();
        if (cancelled) return;
        const list = Array.isArray(data?.apps) ? data.apps : [];
        setApps(list);
        pushLog(`catalog loaded: ${list.length} apps`);
      } catch (err) {
        if (cancelled) return;
        setApps([]);
        setAppsError(err?.message || String(err));
        pushLog(`catalog error: ${err?.message || err}`);
      }
    })();
    return () => { cancelled = true; };
  }, [rawBase, pushLog]);

  const categories = apps
    ? Array.from(new Set(apps.map((a) => a.category || 'Misc')))
    : [];

  const filtered = apps
    ? apps.filter((a) => activeCat === 'all' || (a.category || 'Misc') === activeCat)
    : [];
  const shown = filtered.slice(0, visible);

  // Switching category resets the pagination back to the first page.
  const selectCat = (cat) => { setActiveCat(cat); setVisible(PAGE_SIZE); };

  // ── Device connection ────────────────────────────────────────────────────────

  const fetchInfo = useCallback(async () => {
    const t = getTransport();
    const resp = await t.send(CTX_FM, C_INFO, null);
    let parsed = null;
    try { parsed = JSON.parse(bytesStr(resp)); } catch (_) { parsed = { raw: bytesStr(resp) }; }
    setInfo(parsed);
    pushLog(`device version: ${parsed?.version || 'unknown'}`);
    return parsed;
  }, [getTransport, pushLog]);

  const connectVia = useCallback(async (mode) => {
    setErrorMsg('');
    setStatus(STATUS.CONNECTING);
    try {
      const t = getTransport();
      if (mode === 'bluetooth') { await t.connectBluetooth(); pushLog('bluetooth gatt connected'); }
      else                      { await t.connectSerial();    pushLog('serial port opened @ 115200'); }
      await fetchInfo();
      setStatus(STATUS.CONNECTED);
    } catch (err) {
      const raw = err?.message || String(err);
      pushLog(`error: ${raw}`);
      const msg = /timed out|timeout/i.test(raw)
        ? (mode === 'bluetooth'
            ? 'No response from the device. Turn on “Bluetooth → Remote Device” on the device, then reconnect.'
            : 'No response from the device. Turn on “HID → USB Remote” on the device, then reconnect.')
        : raw;
      setErrorMsg(msg);
      setStatus(STATUS.ERROR);
      try { if (transportRef.current) await transportRef.current.disconnect(); } catch (_) {}
      transportRef.current = null;
    }
  }, [getTransport, fetchInfo, pushLog]);

  const onDisconnect = useCallback(async () => {
    try { if (transportRef.current) await transportRef.current.disconnect(); } catch (_) {}
    transportRef.current = null;
    setStatus(STATUS.IDLE);
    setInfo(null);
    setErrorMsg('');
    setProgress(null);
    setInstalling(null);
    pushLog('disconnected');
  }, [pushLog]);

  const isConnected = status === STATUS.CONNECTED || status === STATUS.WORKING;

  // ── Details modal ─────────────────────────────────────────────────────────────

  const openDetails = useCallback(async (app) => {
    setSelected(app);
    setDocHtml('');
    if (!app.doc) { setDocState('none'); return; }
    setDocState('loading');
    try {
      const res = await fetch(`${rawBase}/${app.doc}`, { cache: 'no-store' });
      if (!res.ok) throw new Error(`doc HTTP ${res.status}`);
      const md = await res.text();
      setDocHtml(renderDoc(md, rawBase, app.doc));
      setDocState('ready');
    } catch (err) {
      pushLog(`doc error: ${err?.message || err}`);
      setDocState('none');
    }
  }, [rawBase, pushLog]);

  const closeDetails = useCallback(() => {
    setSelected(null);
    setDocHtml('');
    setDocState('idle');
  }, []);

  useEffect(() => {
    if (!selected) return;
    const onKey = (e) => { if (e.key === 'Escape') closeDetails(); };
    document.addEventListener('keydown', onKey);
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => { document.removeEventListener('keydown', onKey); document.body.style.overflow = prev; };
  }, [selected, closeDetails]);

  // ── Install ───────────────────────────────────────────────────────────────────

  const install = useCallback(async (app) => {
    if (!isConnected) { setErrorMsg('Connect a device first.'); return; }
    const t = getTransport();
    const dest = `${INSTALL_BASE}/${app.path}`.replace(/\/+/g, '/');
    setErrorMsg('');
    setInstalling(app.path);
    setStatus(STATUS.WORKING);
    setProgress({ value: 0, total: 0 });
    try {
      pushLog(`fetch ${app.path}`);
      const res = await fetch(`${rawBase}/${app.path}`, { cache: 'no-store' });
      if (!res.ok) throw new Error(`download HTTP ${res.status}`);
      const bytes = new Uint8Array(await res.arrayBuffer());
      await installFile(t, dest, bytes, (value, total) => setProgress({ value, total }));
      pushLog(`installed ${dest} (${formatBytes(bytes.length)})`);
      setInstalled((prev) => ({ ...prev, [app.path]: true }));
    } catch (err) {
      const msg = err?.message || String(err);
      setErrorMsg(`Install failed: ${msg}`);
      pushLog(`install error: ${msg}`);
    } finally {
      setInstalling(null);
      setProgress(null);
      setStatus((s) => (s === STATUS.WORKING ? STATUS.CONNECTED : s));
    }
  }, [isConnected, getTransport, rawBase, pushLog]);

  // ── Render ────────────────────────────────────────────────────────────────────

  const progressPct = progress && progress.total
    ? Math.min(100, Math.round((progress.value / progress.total) * 100)) : 0;

  const renderInstallBtn = (app, big = false) => {
    const busy = installing === app.path;
    const done = installed[app.path];
    return (
      <button
        type="button"
        className={`fm-btn fm-btn-primary${big ? '' : ' as-card-install'}`}
        onClick={(e) => { e.stopPropagation(); install(app); }}
        disabled={!isConnected || installing !== null}
        title={isConnected ? `Install to ${INSTALL_BASE}/${app.path}` : 'Connect a device to install'}
      >
        {busy ? `Installing… ${progressPct}%` : done ? 'Reinstall' : 'Install'}
      </button>
    );
  };

  return (
    <div className="fm-wrap">
      <div className="fm-toolbar">
        <div className="fm-toolbar-left">
          {!isConnected ? (
            <>
              <button
                type="button"
                className="fm-btn fm-btn-primary"
                onClick={() => connectVia('serial')}
                disabled={!hasSerial || status === STATUS.CONNECTING}
                title={hasSerial ? 'Connect over USB serial' : 'Web Serial not supported in this browser'}
              >
                {status === STATUS.CONNECTING ? 'Connecting…' : 'Connect USB'}
              </button>
              <button
                type="button"
                className="fm-btn"
                onClick={() => connectVia('bluetooth')}
                disabled={!hasBluetooth || status === STATUS.CONNECTING}
                title={hasBluetooth ? 'Connect over Bluetooth (NUS)' : 'Web Bluetooth not supported in this browser'}
              >
                Connect Bluetooth
              </button>
            </>
          ) : (
            <>
              <span className="as-conn">
                Connected{info?.version ? ` · fw ${info.version}` : ''}
                {info?.total ? ` · ${formatBytes(info.used || 0)} / ${formatBytes(info.total)}` : ''}
              </span>
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
          You can still browse the catalog below.
        </div>
      )}

      {supported && !isConnected && (
        <div className="fm-banner fm-banner-warn">
          To install, connect the device: over <strong>USB</strong> turn on
          <strong> HID → USB Remote</strong>; over <strong>Bluetooth</strong> turn on
          <strong> Bluetooth → Remote Device</strong>. Browsing the catalog needs no device.
        </div>
      )}

      {errorMsg && <div className="fm-banner fm-banner-err">{errorMsg}</div>}

      {progress && (
        <div className="fm-progress">
          <span>installing</span>
          <div className="fm-progress-bar">
            <div className="fm-progress-fill" style={{ width: `${progressPct}%` }} />
          </div>
          <span>{formatBytes(progress.value)} / {formatBytes(progress.total)} · {progressPct}%</span>
        </div>
      )}

      <div className="as-catalog">
        {apps === null && <div className="fm-empty">Loading catalog…</div>}
        {apps !== null && appsError && (
          <div className="fm-banner fm-banner-err">
            Couldn&apos;t load the app catalog from <code>{repo}</code>: {appsError}
          </div>
        )}
        {apps !== null && !appsError && apps.length === 0 && (
          <div className="fm-empty">No apps published yet.</div>
        )}

        {apps !== null && !appsError && apps.length > 0 && (
          <>
            <div className="as-filters" role="tablist" aria-label="Filter by category">
              <button
                type="button"
                className={`as-chip${activeCat === 'all' ? ' active' : ''}`}
                onClick={() => selectCat('all')}
                role="tab"
                aria-selected={activeCat === 'all'}
              >
                All <span className="as-chip-n">{apps.length}</span>
              </button>
              {categories.map((cat) => (
                <button
                  type="button"
                  key={cat}
                  className={`as-chip${activeCat === cat ? ' active' : ''}`}
                  onClick={() => selectCat(cat)}
                  role="tab"
                  aria-selected={activeCat === cat}
                >
                  {cat} <span className="as-chip-n">{apps.filter((a) => (a.category || 'Misc') === cat).length}</span>
                </button>
              ))}
            </div>

            <div className="as-grid">
              {shown
                .map((app) => {
                  const img = appImageUrl(app, rawBase);
                  return (
                    <div
                      key={app.path}
                      className="as-card"
                      role="button"
                      tabIndex={0}
                      onClick={() => openDetails(app)}
                      onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); openDetails(app); } }}
                    >
                      {img && (
                        <div className="as-card-thumb">
                          {/* eslint-disable-next-line @next/next/no-img-element */}
                          <img src={img} alt={app.title} loading="lazy" />
                          <span className="as-card-cat">{app.category || 'Misc'}</span>
                        </div>
                      )}
                      <div className={`as-card-body${img ? '' : ' as-card-body-noimg'}`}>
                        {!img && <span className="as-card-cat-inline">{app.category || 'Misc'}</span>}
                        <div className="as-card-titles">
                          <span className="as-card-title">{app.title}</span>
                          <span className="as-card-path">{app.path}</span>
                        </div>
                        {app.description && <p className="as-card-desc">{app.description}</p>}
                        <div className="as-card-foot">
                          {app.author && <span className="as-card-author">by {app.author}</span>}
                          {renderInstallBtn(app)}
                        </div>
                      </div>
                    </div>
                  );
                })}
            </div>

            {filtered.length > visible && (
              <div className="as-more">
                <button
                  type="button"
                  className="fm-btn"
                  onClick={() => setVisible((v) => v + PAGE_SIZE)}
                >
                  Load more <span className="as-more-n">{filtered.length - visible} left</span>
                </button>
              </div>
            )}
          </>
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

      {selected && mounted && createPortal(
        <div className="fm-modal-backdrop" onClick={closeDetails}>
          <div className="fm-modal as-modal" role="dialog" aria-modal="true" onClick={(e) => e.stopPropagation()}>
            <div className="fm-modal-head">
              <div className="fm-modal-title">
                <span className="fm-modal-path">{selected.title}</span>
                <span className="fm-modal-size">{selected.category || 'Misc'}</span>
              </div>
              <button type="button" className="fm-modal-close" onClick={closeDetails} aria-label="Close">×</button>
            </div>
            <div className="fm-modal-body as-modal-body">
              {appImageUrl(selected, rawBase) && (
                /* eslint-disable-next-line @next/next/no-img-element */
                <img className="as-modal-cover" src={appImageUrl(selected, rawBase)} alt={selected.title} />
              )}
              {selected.description && <p className="as-modal-lead">{selected.description}</p>}
              {docState === 'loading' && <div className="fm-empty">Loading details…</div>}
              {docState === 'ready' && (
                <div className="as-doc" dangerouslySetInnerHTML={{ __html: docHtml }} />
              )}
              {docState === 'none' && !selected.description && (
                <div className="fm-empty">No description provided.</div>
              )}
              <p className="as-modal-target">
                Installs to <code>{`${INSTALL_BASE}/${selected.path}`}</code>
              </p>
            </div>
            <div className="fm-modal-foot">
              <span className="fm-modal-meta">{selected.author ? `by ${selected.author}` : selected.path}</span>
              <div className="fm-modal-actions">
                <button type="button" className="fm-btn fm-btn-ghost" onClick={closeDetails}>Close</button>
                {renderInstallBtn(selected, true)}
              </div>
            </div>
          </div>
        </div>,
        document.body
      )}
    </div>
  );
}
