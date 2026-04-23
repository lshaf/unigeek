// NEXT_PUBLIC_FIRMWARE_VERSION is injected by the release.yml workflow
// (`github.ref_name`). Strip any leading `v` so `v1.6.0` and `1.6.0` both work.
// The fallback is only used for local `next dev` / unpinned builds.
const RAW_FIRMWARE_VERSION =
  process.env.NEXT_PUBLIC_FIRMWARE_VERSION || 'dev';
export const FIRMWARE_VERSION = RAW_FIRMWARE_VERSION.replace(/^v/i, '');
export const FIRMWARE_CHANNEL = 'stable';
export const BUILD_ID = '20260421';
export const COPYRIGHT_YEAR = 2026;
export const REPO_URL = 'https://github.com/lshaf/unigeek';
