'use client';

import { useState } from 'react';
import Flasher from './Flasher';
import BoardInfo from './BoardInfo';
import { toCaps, shortChip } from '@/content/board-caps';
import { FIRMWARE_VERSION } from '@/content/meta';

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

export default function InstallFlow({ boards }) {
  const [boardId, setBoardId] = useState(boards[0]?.id ?? null);
  const [method, setMethod] = useState('web');
  const [brand, setBrand] = useState('m5stack');

  const filteredBoards = brand ? boards.filter((b) => getBrand(b.id) === brand) : boards;
  const board = filteredBoards.find((b) => b.id === boardId) || filteredBoards[0];
  const methodInfo = METHODS.find((m) => m.id === method);

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
        <div className="step-sub">{filteredBoards.length} of {boards.length} boards</div>
      </div>
      <div className="brand-filter">
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
      <div className="board-grid">
        {filteredBoards.map((b) => (
          <button
            key={b.id}
            type="button"
            className={`board-card${b.id === boardId ? ' selected' : ''}`}
            onClick={() => setBoardId(b.id)}
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
          </button>
        ))}
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

      <Flasher board={board} firmwareVersion={FIRMWARE_VERSION} method={method} methodInfo={methodInfo} />
    </>
  );
}
