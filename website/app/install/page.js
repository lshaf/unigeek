import InstallFlow from '@/components/install/InstallFlow';
import KnownIssues from '@/components/install/KnownIssues';
import Requirements from '@/components/install/Requirements';
import { BOARDS } from '@/content/boards';
import { FIRMWARE_VERSION } from '@/content/meta';
import { getAllReleases } from '@/content/releases';

export const metadata = {
  title: 'Install — UniGeek',
  description: 'Pick your board, pick a method, and flash. Web-based or manual download — both supported.',
};

function formatReleaseDate(iso) {
  if (!iso) return null;
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return null;
  return d.toLocaleDateString('en-US', { year: 'numeric', month: 'short', day: 'numeric' });
}

export default function InstallPage() {
  const releases = getAllReleases();
  const rel = releases.find((r) => r.version === FIRMWARE_VERSION);
  const dateLabel = formatReleaseDate(rel?.date);

  return (
    <>
      <header className="page-header">
        <div className="page-header-meta">
          <span>// install · flash · boot</span>
          <span>Firmware {FIRMWARE_VERSION === 'dev' ? 'dev' : `v${FIRMWARE_VERSION}`}{dateLabel ? ` · ${dateLabel}` : ''}</span>
        </div>
        <h1 className="page-title">Install firmware</h1>
        <p className="page-subtitle">
          Pick your board, pick a method, and flash. Web-based or manual download — both supported.
        </p>
      </header>

      <div className="install-wrap">
        <KnownIssues boards={BOARDS} />
        <InstallFlow boards={BOARDS} />
        <Requirements />
      </div>
    </>
  );
}
