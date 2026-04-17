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

3. **Build all environments**: Run `./build_all.sh` (macOS/Linux) or `build_all.bat` (Windows) to verify all board environments compile successfully. If any build fails, **stop and report the error** — do not proceed with the release.

4. **Build website**: Run `cd website && npm run build` to verify the website builds without errors. If the build fails, **stop and report the error** — do not proceed with the release.

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

10. **Create announcement file** at `release-notes/<version>.md` in Discord-compatible markdown:
    - No tables (use bullet lists)
    - No `---` horizontal rules
    - No clickable `[text](url)` links (write URLs inline)
    - Use `>` block quotes for feature groupings
    - Use `**bold**` for feature names
    - Keep headers as `#` `##` (work in Discord forum posts)
    - Include install link: https://unigeek.xid.run — browser installer, no tools required
    - Include download link: https://github.com/lshaf/unigeek/releases
    - Include SD card note: copy `sdcard/` to SD root or use built-in Download menu
    - End with: *Built for security research and education. Use responsibly.*

11. **Tag message** (written to `/tmp/tag_msg_<version>.txt`):
    - First line: `Release <version>`
    - Blank line
    - Then bullet sections: New Features, Improvements, Bug Fixes — short one-liners per item
    - Use the same temp file approach to avoid shell escaping issues

12. **Do NOT** push until the user explicitly confirms.