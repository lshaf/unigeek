import AppStoreClient from '@/components/apps/AppStoreClient';
import { FIRMWARE_VERSION, APPS_REPO, APPS_RAW_BASE } from '@/content/meta';

export const metadata = {
  title: 'App Store — UniGeek',
  description: 'Browse community Lua apps and install them straight to a connected UniGeek over USB or Bluetooth.',
};

export default function AppsPage() {
  return (
    <>
      <header className="page-header">
        <div className="page-header-meta">
          <span>// app · store · lua</span>
          <span>Website {FIRMWARE_VERSION === 'dev' ? 'dev' : `v${FIRMWARE_VERSION}`}</span>
        </div>
        <h1 className="page-title">App Store</h1>
        <p className="page-subtitle">
          Browse the community Lua app collection and install any script straight to a connected
          UniGeek. Plug in over USB (or pair over Bluetooth), pick an app, and click Install &mdash;
          it lands in <code>/unigeek/lua</code>, ready to run from the Lua Runner.
        </p>
      </header>

      <AppStoreClient
        expectedVersion={FIRMWARE_VERSION}
        rawBase={APPS_RAW_BASE}
        repo={APPS_REPO}
      />
    </>
  );
}
