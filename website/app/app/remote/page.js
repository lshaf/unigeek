import RemoteAccessClient from '@/components/remote/RemoteAccessClient';
import { FIRMWARE_VERSION } from '@/content/meta';

export const metadata = {
  title: 'Remote Access — UniGeek',
  description: 'Mirror a connected UniGeek\'s screen in the browser and control it over USB serial.',
};

export default function RemotePage() {
  return (
    <>
      <header className="page-header">
        <div className="page-header-meta">
          <span>// remote · access · uart</span>
          <span>Website {FIRMWARE_VERSION === 'dev' ? 'dev' : `v${FIRMWARE_VERSION}`}</span>
        </div>
        <h1 className="page-title">Remote Access</h1>
        <p className="page-subtitle">
          Mirror a connected UniGeek&rsquo;s screen in the browser and drive it from your keyboard
          &mdash; navigate, type, and tap. No WiFi, no password &mdash; just connect over USB or
          Bluetooth. Enable <em>HID &rarr; USB Remote</em> (or <em>Bluetooth &rarr; Remote Device</em>)
          on the device first.
        </p>
      </header>

      <RemoteAccessClient />
    </>
  );
}
