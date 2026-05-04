---
name: release
description: Create a versioned release with tag, notes, and announcement file
user-invocable: true
---

# Release Skill

Create a new firmware release. Usage: `/release <version>` (e.g. `/release 1.3.0`)

## Steps

1. **Find the previous tag**: Run `git tag --sort=-v:refname | head -1` to get the last release version.

2. **Version check**: Compare the requested version against the latest tag. If the new version is lower than or equal to the latest tag, **warn the user and stop** — do not proceed until they confirm or provide a corrected version.

3. **Build all environments**: Run `./scripts/build_all.sh` (macOS/Linux) or `scripts/build_all.bat` (Windows) to verify all board environments compile successfully. If any build fails, **stop and report the error** — do not proceed with the release.

4. **Build website**: Run `cd website && npm run build` to verify the website builds without errors. If the build fails, **stop and report the error** — do not proceed with the release. Also spot-check that any knowledge/*.md changes in this cycle render on the features pages (see **Knowledge file conventions** at the end of this file).

5. **Analyze commits**: Run `git log <prev_tag>..HEAD --oneline` to see all commits since the last release. Also check what already existed at the previous tag to avoid listing mid-development upgrades as new features.

6. **Categorize changes** into:
   - **New Boards** — only genuinely new board support
   - **New Features** — only features that didn't exist at the previous tag
   - **Improvements** — enhancements to existing features
   - **Bug Fixes** — fixes and stability improvements
   - Do NOT mention fixes or improvements for features that are NEW in this release cycle
   - Do NOT mention intermediate upgrades to features that were built during this cycle
   - Do NOT mention CI/workflow/docs-only changes

7. **Get the current supported boards list** from the release workflow matrix in `.github/workflows/release.yml`. Exclude any commented-out boards.

8. **Show the draft** tag message and announcement to the user. Wait for explicit approval before proceeding.

9. **On approval**, execute in this exact order:
   - Create announcement file at `release-notes/<version>.md`
   - Commit: `git add release-notes/<version>.md && git commit -m "📝 add release notes <version>"`
   - Create annotated git tag on the new commit using a temp file: `git tag -a <version> -F /tmp/tag_msg_<version>.txt`
   - Push: `git push origin main && git push origin <version>`

10. **Create announcement file** at `release-notes/<version>.md` — formatted for the **website parser** (`website/content/releases/index.js`, not Discord). Manual Discord posting is no longer in scope.

    The parser splits the file by H2 sections and only buckets these heading names — anything else is silently dropped:

    | Bucket  | Accepted H2 names |
    |---------|-------------------|
    | added   | `New Features`, `New Boards`, `Added` |
    | changed | `Improvements`, `Changed`, `Enhancements` |
    | fixed   | `Bug Fixes`, `Fixed`, `Fixes` |
    | removed | `Removed`, `Deprecated`, `Breaking` |
    | install | `Install`, `Download` |
    | contributors | `Contributors` |

    Inside each bucket, two item shapes parse correctly:

    1. **Blockquote item** — preferred for New Features / New Boards (gets a card layout):
       ```
       > **Feature Name**
       > One- or two-sentence description on the next line(s).
       ```
       Each `> **Bold**` line starts a new item; subsequent `>` continuation lines attach to the same item.

    2. **Bullet item** — preferred for Improvements / Bug Fixes:
       ```
       - **Label** — short description
       - free text without a label is also valid
       ```

    Required structure:
    - First line: `# UniGeek <version>`
    - Second line: `*YYYY-MM-DD*` (parser strips this; the date shown on the site comes from the git tag)
    - Then a 1–2 sentence preamble paragraph (rendered as the section intro)
    - Then the H2 sections in this order: New Features, Improvements, Bug Fixes, Install
    - Final italic line: *Built for security research and education. Use responsibly.*

    Style rules:
    - **No tables, no horizontal rules** — the parser doesn't bucket arbitrary content; tables placed under a non-bucketed heading vanish.
    - **No custom H2 sections** — "Known Issues", "Achievements", "Supported Boards" are dropped on render. Fold notable counts into the preamble or an Improvements bullet instead.
    - **Plain URLs** are fine in the Install section (rendered as raw HTML); inline `[text](url)` works inside item descriptions.
    - **Install section** must include the browser installer (https://unigeek.xid.run), the GitHub releases link, and the SD-card note (`sdcard/` to root, or in-firmware Download menu).

11. **Tag message** (written to `/tmp/tag_msg_<version>.txt`):
    - First line: `Release <version>`
    - Blank line
    - Then bullet sections: New Features, Improvements, Bug Fixes — short one-liners per item
    - Use the same temp file approach to avoid shell escaping issues

12. **Do NOT** push until the user explicitly confirms.

## Knowledge file conventions

If the release cycle touches `knowledge/*.md`, verify each file follows the renderer conventions in `website/content/features/index.js`:

- **First heading must be `# Title`** — the renderer strips the first H1; the catalog entry's `title` is used instead. Do not start a doc with H2.
- **Callouts** — use `> [!note]`, `> [!tip]`, `> [!warn]`, `> [!danger]` on their own line, content on the next line. Plain `>` blockquotes render with the default NOTE badge.
- **Tier pills** — table cells containing exactly `Bronze` / `Silver` / `Gold` / `Platinum` auto-render as coloured pills. Use these literals in achievement tables.
- **No repo-relative links** — `../docs/...` paths don't resolve on the website. Use absolute URLs or inline `code` refs.
- **Catalog sync** — any new or removed knowledge file must match a `hasDetail: true/false` change in `website/content/features/catalog.js`; slug = filename without `.md`.
- **Boards** — if boards were added/removed this cycle, `website/content/boards.js` must mirror the workflow matrix in `.github/workflows/release.yml` (see release matrix invariant).