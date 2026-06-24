'use client';

import { useEffect, useMemo, useState } from 'react';
import Flasher from './Flasher';
import BoardInfo from './BoardInfo';
import { toCaps, shortChip } from '@/content/board-caps';
import { FIRMWARE_VERSION, FIRMWARE_PROXY, REPO_URL } from '@/content/meta';

const METHODS = [
  {
    id: 'web',
    label: 'A · Recommended',
    name: 'Web Flasher',
    desc: 'Flash directly from the browser via WebSerial. No drivers, no CLI. Chrome/Edge only.',
  },
  {
    id: 'manual',
    label: 'B · Manual',
    name: 'Download .bin',
    desc: 'Download the raw firmware image and flash with your own tool of choice.',
  },
];

const BRANDS = [
  { id: 'm5stack', label: 'M5Stack' },
  { id: 'lilygo', label: 'LilyGo' },
  { id: 'other', label: 'Other' },
];

function getBrand(id) {
  if (id.startsWith('m5')) return 'm5stack';
  if (id.startsWith('t_')) return 'lilygo';
  return 'other';
}

function resolveBinUrl(boardId, selectedVersion) {
  return `${FIRMWARE_PROXY}/${selectedVersion}/unigeek-${boardId}.bin`;
}

export default function InstallFlow({ boards, versions = [], latestVersion = FIRMWARE_VERSION }) {
  const [boardId, setBoardId] = useState(boards[0]?.id ?? null);
  const [method, setMethod] = useState('web');
  const [brand, setBrand] = useState('m5stack');
  const [version, setVersion] = useState(latestVersion);

  // Set of board IDs that shipped in the selected version's release. If the
  // map is missing for a version (e.g. someone hasn't run sync-releases),
  // treat every board as supported so we don't accidentally hide all of them.
  const supportedIds = useMemo(() => {
    const entry = versions.find((v) => v.version === version);
    if (!entry || !Array.isArray(entry.boards) || entry.boards.length === 0) return null;
    return new Set(entry.boards);
  }, [versions, version]);

  const isSupported = (id) => !supportedIds || supportedIds.has(id);

  const filteredBoards = brand ? boards.filter((b) => getBrand(b.id) === brand) : boards;
  const supportedCount = filteredBoards.filter((b) => isSupported(b.id)).length;
  const board = filteredBoards.find((b) => b.id === boardId) || filteredBoards.find((b) => isSupported(b.id)) || filteredBoards[0];
  const methodInfo = METHODS.find((m) => m.id === method);
  const isOldVersion = version !== latestVersion;
  const binUrl = board ? resolveBinUrl(board.id, version) : null;

  // If the user switches to an older version that drops the currently-selected
  // board, jump to the first board that does ship in that version.
  useEffect(() => {
    if (!supportedIds || !boardId) return;
    if (supportedIds.has(boardId)) return;
    const fallback = filteredBoards.find((b) => supportedIds.has(b.id)) || boards.find((b) => supportedIds.has(b.id));
    if (fallback) setBoardId(fallback.id);
  }, [supportedIds, boardId, filteredBoards, boards]);

  function handleBrandChange(newBrand) {
    const next = newBrand === brand ? null : newBrand;
    setBrand(next);
    const visible = next ? boards.filter((b) => getBrand(b.id) === next) : boards;
    if (!visible.find((b) => b.id === boardId)) setBoardId(visible[0]?.id ?? null);
  }

  return (
    <>
      {/* STEP 1 — Board */}
      <div className="step-head">
        <div className="step-num">Step 01</div>
        <div className="step-title">Select your board</div>
        <div className="step-sub">
          {supportedIds
            ? `${supportedCount} of ${filteredBoards.length} available in v${version}`
            : `${filteredBoards.length} of ${boards.length} boards`}
        </div>
      </div>
      <div className="brand-filter" style={{ justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap' }}>
        <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
          {BRANDS.map((br) => (
            <button
              key={br.id}
              type="button"
              className={`brand-btn${brand === br.id ? ' active' : ''}`}
              onClick={() => handleBrandChange(br.id)}
            >
              {br.label}
            </button>
          ))}
        </div>
        {versions.length > 0 ? (
          <label className="version-picker">
            <span>Firmware</span>
            <select
              value={version}
              onChange={(e) => setVersion(e.target.value)}
              className="version-select"
            >
              {versions.some((v) => v.version === latestVersion) ? null : (
                <option value={latestVersion}>v{latestVersion} (latest)</option>
              )}
              {versions.map((v, i) => (
                <option key={v.version} value={v.version}>
                  v{v.version}{i === 0 ? ' (latest)' : ''}
                </option>
              ))}
            </select>
          </label>
        ) : null}
      </div>
      {isOldVersion && supportedIds ? (
        <div className="version-note">
          Showing {supportedCount} board{supportedCount === 1 ? '' : 's'} that shipped in v{version}. Boards added
          in later releases are dimmed.
        </div>
      ) : null}
      <div className="board-grid">
        {filteredBoards.map((b) => {
          const supported = isSupported(b.id);
          return (
            <button
              key={b.id}
              type="button"
              disabled={!supported}
              title={supported ? undefined : `Not available in v${version}`}
              className={`board-card${b.id === boardId ? ' selected' : ''}${supported ? '' : ' unsupported'}`}
              onClick={() => { if (supported) setBoardId(b.id); }}
            >
              <div className="board-card-head">
                <div className="board-card-name">{b.name}</div>
                <div className="board-card-chip">{shortChip(b.chip)}</div>
              </div>
              <div className="board-card-caps">
                {toCaps(b.tags).map((cap) => (
                  <span key={cap} className="cap">{cap}</span>
                ))}
              </div>
              {!supported ? <div className="board-card-badge">not in v{version}</div> : null}
            </button>
          );
        })}
      </div>

      <BoardInfo board={board} />

      {/* STEP 2 — Method */}
      <div className="step-head">
        <div className="step-num">Step 02</div>
        <div className="step-title">Choose install method</div>
        <div className="step-sub">2 options</div>
      </div>
      <div className="methods" style={{ gridTemplateColumns: 'repeat(2, 1fr)' }}>
        {METHODS.map((m) => (
          <button
            key={m.id}
            type="button"
            className={`method${m.id === method ? ' selected' : ''}`}
            onClick={() => setMethod(m.id)}
          >
            <div className="method-label">{m.label}</div>
            <div className="method-name">{m.name}</div>
            <div className="method-desc">{m.desc}</div>
          </button>
        ))}
      </div>

      {/* STEP 3 — Flash */}
      <div className="step-head">
        <div className="step-num">Step 03</div>
        <div className="step-title">Connect &amp; flash</div>
        <div className="step-sub">Target verified · ready</div>
      </div>

      <Flasher
        board={board}
        firmwareVersion={version}
        method={method}
        methodInfo={methodInfo}
        binUrl={binUrl}
        isOldVersion={isOldVersion}
      />

      {/* ALTERNATIVE — flash from an Android phone (no desktop / WebSerial needed) */}
      <div className="step-head" style={{ marginTop: 56 }}>
        <div className="step-num">Alt</div>
        <div className="step-title">No desktop? Flash from Android</div>
        <div className="step-sub">USB-OTG · companion app</div>
      </div>
      <div style={{ border: '1px solid var(--line)', background: 'var(--bg-elev)', padding: 24 }}>
        <div className="method-label">Android companion</div>
        <div className="method-name" style={{ marginTop: 8 }}>Install firmware from your phone</div>
        <p className="method-desc" style={{ marginTop: 8, maxWidth: 680 }}>
          The UniGeek Android app flashes any board over a USB-OTG cable — handy when you don&rsquo;t have a
          desktop with Chrome or Edge. The same app also gives you Remote control and the File Manager over
          USB or Bluetooth.
        </p>
        <ol style={{ margin: '16px 0 0', paddingLeft: 18, fontSize: 13, lineHeight: 1.8, color: 'var(--ink-dim)' }}>
          <li>Download and sideload the APK (allow &ldquo;install unknown apps&rdquo; when prompted).</li>
          <li>Connect the board to your phone with a USB-C OTG cable.</li>
          <li>Open the app, choose <strong style={{ color: 'var(--ink)' }}>Flash firmware</strong>, pick your board, and flash.</li>
        </ol>
        <a
          className="btn btn-primary"
          style={{ marginTop: 20 }}
          href={`${REPO_URL}/releases/latest/download/unigeek-android.apk`}
        >
          Download Android app (.apk)
        </a>
      </div>
    </>
  );
}
