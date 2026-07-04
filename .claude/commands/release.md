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

3. **Build all environments with the version baked in** *(run in background and continue with step 4 in parallel)*: Run

   ```bash
   PLATFORMIO_BUILD_SRC_FLAGS='-DFIRMWARE_VERSION='\''"<version>"'\''' ./scripts/build_all.sh
   ```

   on macOS/Linux. On Windows use PowerShell:

   ```powershell
   $env:PLATFORMIO_BUILD_SRC_FLAGS = "-DFIRMWARE_VERSION='`"<version>`"'"
   .\scripts\build_all.ps1 -Jobs 6
   ```

   `build_all.sh` and `build_all.ps1` share the same env list — keep them in sync when adding boards. Substitute `<version>` for the release version (e.g. `1.7.3`). The flag stamps `FIRMWARE_VERSION` into every `builds/unigeek-<env>.bin` so they double as M5Burner upload artefacts (`scripts/m5burner.py upload <version>`). Without it, AboutScreen falls back to `"dev"` and `m5burner.py`'s pre-flight version check will reject the bins.

   **Use `run_in_background: true`** — the build takes several minutes; you'll be notified when it finishes. Kick off step 4 (website build) in the same message so both run concurrently.

   If any build fails, **stop and report the error** — do not proceed with the release.

4. **Build website** *(run in background, parallel with step 3)*: Run `cd website && npm run build` to verify the website builds without errors. If `next: command not found`, run `npm install` first (or chain it: `cd website && npm install && npm run build`). Also use `run_in_background: true` so it runs alongside the firmware build. If the build fails, **stop and report the error** — do not proceed with the release. Also spot-check that any knowledge/*.md changes in this cycle render on the features pages (see **Knowledge file conventions** at the end of this file).

   Wait for *both* background tasks to complete before moving to step 5.

5. **Analyze commits**: Run `git log <prev_tag>..HEAD --oneline` to see all commits since the last release. Also check what already existed at the previous tag to avoid listing mid-development upgrades as new features.

6. **Categorize changes** into:
   - **New Boards** — only genuinely new board support
   - **New Features** — only features that didn't exist at the previous tag
   - **Improvements** — enhancements to existing features
   - **Bug Fixes** — fixes and stability improvements
   - Do NOT mention fixes or improvements for features that are NEW in this release cycle
   - Do NOT mention intermediate upgrades to features that were built during this cycle
   - Do NOT mention CI/workflow/docs-only changes
   - **Contributors** — collect every external contributor whose PR merged this cycle. Derive from the merge commits: `git log <prev_tag>..HEAD --merges --format="%s"` (each `Merge pull request #N from <user>/…` names the author's fork) and cross-check `git log <prev_tag>..HEAD --no-merges --format="%an <%ae>" | sort | uniq -c`. **Always exclude the repo owner/maintainer** (`lshaf` / `ads.shaf@gmail.com` / `L Shaf`) — never credit the person running the release. If there are no external contributors, omit the Contributors section entirely.

7. **Get the current supported boards list** from the release workflow matrix in `.github/workflows/release.yml`. Exclude any commented-out boards.

8. **Show the draft** tag message and announcement to the user. Wait for explicit approval before proceeding.

9. **On approval**, execute in this exact order:
   - Create announcement file at `release-notes/<version>.md`
    - Seed the board map from the workflow matrix so the new tag ships with its own row in `_boards.json`:
     ```bash
     node scripts/sync-releases.mjs --seed <version>
     ```
     This reads the `strategy.matrix.env` list from `.github/workflows/release.yml` (no network) and merges `"<version>": [...18 boards]` into `release-notes/_boards.json`. If the entry is already up-to-date, the script is a no-op.
   - Commit both files: `git add release-notes/<version>.md release-notes/_boards.json && git commit -m "📝 add release notes <version>"`
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
    - Then the H2 sections in this order: New Features, Improvements, Bug Fixes, Contributors, Install
    - **Contributors** section (`## Contributors` — a parser-recognised bucket) — one `- ` bullet per external contributor, GitHub `@handle` first with optional real name, e.g. `- **@vnxdtzip** (Vinicius Pereira) — Responder/SOCKS4, Sub-GHz suite`. List everyone whose PR merged this cycle **except the repo maintainer** (`lshaf` — never credit the release runner). Omit the whole section if there were no external contributors.
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
    - End with a `Contributors:` line crediting external PR authors by `@handle` (exclude the maintainer `lshaf`); omit if none
    - Use the same temp file approach to avoid shell escaping issues

12. **Do NOT** push until the user explicitly confirms.

13. **Publish to M5Burner**: After the push has succeeded, run

    ```bash
    python scripts/m5burner.py upload <version>
    ```

    The script's pre-flight verifies each `builds/unigeek-m5*.bin` actually contains the version string (NUL-terminated) before posting, so a forgotten `PLATFORMIO_BUILD_SRC_FLAGS` from step 3 will surface here as a hard error instead of a silent mis-labelled upload. Reads the `m5_token` from `.env` (header `m5_auth_token`); add it locally if missing — see `.env.sample`. Six entries are posted and **auto-published** (POST then PUT `/firmware/<fid>/publish/<bin_id>/1`): `UniGeek Cardputer`, `UniGeek Cardputer Adv`, `UniGeek CoreS3`, `UniGeek StickC Plus 1.1`, `UniGeek StickC Plus 2`, `UniGeek StickS3`. If any individual step fails, the script prints the failures and exits non-zero — the release itself is still good (GitHub release + tag are already up); retry with `python scripts/m5burner.py publish <version>` to flip just the still-unpublished ones, or re-run upload to retry both POST and publish.

14. **Verify the release map**: Step 9 already seeded `_boards.json` from the workflow matrix (so the tag itself is correct). After the GitHub Actions release workflow finishes uploading every `.bin` (watch with `gh run watch --exit-status` or eyeball the Releases page), run the full sync once more — this time against the actual uploaded asset list — to catch any divergence (e.g. a matrix build failed silently and one bin is missing):

    ```bash
    node scripts/sync-releases.mjs
    ```

    `git diff release-notes/_boards.json` should be **empty** at this point. If it's not, a build failed — investigate the workflow logs before committing the diff. If everything matches, no follow-up commit is needed. Safe to re-run later if you forget; the sync is idempotent.

## Knowledge file conventions

If the release cycle touches `knowledge/*.md`, verify each file follows the renderer conventions in `website/content/features/index.js`:

- **First heading must be `# Title`** — the renderer strips the first H1; the catalog entry's `title` is used instead. Do not start a doc with H2.
- **Callouts** — use `> [!note]`, `> [!tip]`, `> [!warn]`, `> [!danger]` on their own line, content on the next line. Plain `>` blockquotes render with the default NOTE badge.
- **Tier pills** — table cells containing exactly `Bronze` / `Silver` / `Gold` / `Platinum` auto-render as coloured pills. Use these literals in achievement tables.
- **No repo-relative links** — `../docs/...` paths don't resolve on the website. Use absolute URLs or inline `code` refs.
- **Catalog sync** — any new or removed knowledge file must match a `hasDetail: true/false` change in `website/content/features/catalog.js`; slug = filename without `.md`.
- **Boards** — if boards were added/removed this cycle, `website/content/boards.js` must mirror the workflow matrix in `.github/workflows/release.yml` (see release matrix invariant).