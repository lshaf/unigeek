import Link from 'next/link';
import { FIRMWARE_VERSION, BUILD_ID, COPYRIGHT_YEAR, REPO_URL, TIKTOK_URL, DISCORD_URL } from '@/content/meta';

export default function Footer() {
  return (
    <footer className="footer">
      <div className="container">
        <div className="footer-inner">
          <div>
            <div className="brand" style={{ marginBottom: 16 }}>
              <span className="brand-mark" aria-hidden="true" />
              <span className="brand-name">
                UNI<span>GEEK</span>
              </span>
            </div>
            <div style={{ maxWidth: 360, lineHeight: 1.7 }}>
              Open-source multi-tool firmware for ESP32-family boards.
              <br />
              For research, education, and authorized testing only.
            </div>
          </div>
          <div className="footer-col">
            <h4>Product</h4>
            <Link href="/features">Features</Link>
            <Link href="/install">Install</Link>
            <Link href="/releases">Releases</Link>
          </div>
          <div className="footer-col">
            <h4>Resources</h4>
            <a href={`${REPO_URL}/tree/main/knowledge`} target="_blank" rel="noreferrer">
              Documentation
            </a>
            <a href={REPO_URL} target="_blank" rel="noreferrer">
              GitHub
            </a>
            <a href={`${REPO_URL}/issues`} target="_blank" rel="noreferrer">
              Issues
            </a>
            <a href={DISCORD_URL} target="_blank" rel="noreferrer">
              Discord
            </a>
            <a href={TIKTOK_URL} target="_blank" rel="noreferrer">
              TikTok
            </a>
          </div>
          <div className="footer-col">
            <h4>Legal</h4>
            <a href={`${REPO_URL}/blob/main/LICENSE`} target="_blank" rel="noreferrer">
              License (MIT)
            </a>
            <Link href="/#disclaimer">Disclaimer</Link>
          </div>
        </div>
        <div className="footer-bottom">
          <span>© {COPYRIGHT_YEAR} UniGeek · Open-source firmware</span>
          <span>
            Build {BUILD_ID} · v{FIRMWARE_VERSION}
          </span>
        </div>
      </div>
    </footer>
  );
}
