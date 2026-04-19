"use client";

const NavTable = ({ inputs }) => (
  <table className="nav-guide-table">
    <thead>
      <tr>
        <th>Input</th>
        <th>Action</th>
      </tr>
    </thead>
    <tbody>
      {inputs.map(({ input, action }) => (
        <tr key={input}>
          <td>{input}</td>
          <td className="nav-action">{action}</td>
        </tr>
      ))}
    </tbody>
  </table>
);

const NavGuideBlock = ({ boardName, label, inputs, note }) => (
  <div className="nav-guide-block">
    <div className="nav-guide-sublabel">{boardName} — {label}</div>
    <NavTable inputs={inputs} />
    {note && <div className="nav-guide-note">{note}</div>}
  </div>
);

const BoardPicker = ({ boards, selected, onSelect }) => {
  const withIssues = boards.filter((b) => b.knownIssues?.length > 0);

  return (
    <div>
      {withIssues.length > 0 && (
        <div className="install-step known-issues-section">
          <div className="step-label">⚠ Known issues</div>
          <ul className="known-issues-list">
            {withIssues.map((board) => (
              <li key={board.id}>
                <span className="known-issues-board">{board.name}</span>
                <span className="known-issues-sep">—</span>
                <span className="known-issues-text">
                  {board.knownIssues.join("; ")}
                </span>
              </li>
            ))}
          </ul>
        </div>
      )}

      <div className="install-step">
        <div className="step-label">Step 1 — Select your board</div>
        <div className="board-grid">
          {boards.map((board) => (
            <div
              key={board.id}
              className={`board-card${selected?.id === board.id ? " selected" : ""}`}
              onClick={() => onSelect(board)}
            >
              <div className="board-name">{board.name}</div>
              <div className="board-chip">{board.chip}</div>
              <div className="board-tags">
                {board.tags.map((tag) => (
                  <span key={tag} className="tag">
                    {tag}
                  </span>
                ))}
              </div>
            </div>
          ))}
        </div>
      </div>

      {selected?.nav && (
        <div className="install-step nav-guide-section">
          {selected.nav.map(([label, inputs, note], i) => (
            <NavGuideBlock key={i} boardName={selected.name} label={label} inputs={inputs} note={note} />
          ))}
        </div>
      )}
    </div>
  );
};

export default BoardPicker;
