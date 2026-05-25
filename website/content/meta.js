// NEXT_PUBLIC_FIRMWARE_VERSION is injected by the release.yml workflow
// (`github.ref_name`). Strip any leading `v` so `v1.6.0` and `1.6.0` both work.
// The fallback is only used for local `next dev` / unpinned builds.
const RAW_FIRMWARE_VERSION =
  process.env.NEXT_PUBLIC_FIRMWARE_VERSION || 'dev';
export const FIRMWARE_VERSION = RAW_FIRMWARE_VERSION.replace(/^v/i, '');
export const FIRMWARE_CHANNEL = 'stable';

// Cloudflare Worker that proxies github.com/lshaf/unigeek/releases/download/*
// with CORS headers. Same source for every version (latest + archive). Override
// at build time with NEXT_PUBLIC_FIRMWARE_PROXY if you need to point elsewhere.
export const FIRMWARE_PROXY = (
  process.env.NEXT_PUBLIC_FIRMWARE_PROXY || 'https://bin.unigeek.xid.run'
).replace(/\/+$/, '');
export const BUILD_ID = '20260421';
export const COPYRIGHT_YEAR = 2026;
export const REPO_URL = 'https://github.com/lshaf/unigeek';
export const TIKTOK_URL = 'https://www.tiktok.com/@llshaf';
export const DISCORD_URL = 'https://discord.gg/5shcPcQvfr';
