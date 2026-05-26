import { SITE_URL } from '@/content/meta';
import { getDocSlugs } from '@/content/features';

// Generated at build time into build/sitemap.xml (output: 'export').
// URLs carry a trailing slash to match next.config.js `trailingSlash: true`.
export const dynamic = 'force-static';

const url = (path) => `${SITE_URL}${path}`;

export default function sitemap() {
  const now = new Date();

  const staticRoutes = [
    { path: '/', priority: 1.0 },
    { path: '/features/', priority: 0.8 },
    { path: '/install/', priority: 0.8 },
    { path: '/releases/', priority: 0.6 },
    { path: '/app/files/', priority: 0.5 },
  ].map((r) => ({
    url: url(r.path),
    lastModified: now,
    changeFrequency: 'weekly',
    priority: r.priority,
  }));

  const featureRoutes = getDocSlugs().map((slug) => ({
    url: url(`/features/${slug}/`),
    lastModified: now,
    changeFrequency: 'monthly',
    priority: 0.7,
  }));

  return [...staticRoutes, ...featureRoutes];
}
