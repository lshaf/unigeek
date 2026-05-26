import { SITE_URL } from '@/content/meta';

// Generated at build time into build/robots.txt (output: 'export').
export const dynamic = 'force-static';

export default function robots() {
  return {
    rules: { userAgent: '*', allow: '/' },
    sitemap: `${SITE_URL}/sitemap.xml`,
  };
}
