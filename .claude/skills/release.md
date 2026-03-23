---
name: release
description: Create a versioned release with tag, notes, and announcement file
user_invocable: true
---

# Release Skill

Create a new firmware release. Usage: `/release <version>` (e.g. `/release 1.3.0`)

## Steps

1. **Find the previous tag**: Run `git tag --sort=-v:refname | head -1` to get the last release version.

2. **Analyze commits**: Run `git log <prev_tag>..HEAD --oneline` to see all commits since the last release. Also check what already existed at the previous tag to avoid listing mid-development upgrades as new features.

3. **Categorize changes** into:
   - **New Boards** — only genuinely new board support
   - **New Features** — only features that didn't exist at the previous tag
   - **Bug Fixes** — fixes and stability improvements
   - Do NOT mention intermediate upgrades to features that were built during this cycle
   - Do NOT mention CI/workflow/docs-only changes

4. **Get the current supported boards list** from the release workflow matrix in `.github/workflows/release.yml`. Exclude any commented-out boards.

5. **Show the draft** tag message and announcement to the user. Wait for approval before proceeding.

6. **On approval**, execute:
   - Create annotated git tag: `git tag -a <version> -m "<tag message>"`
   - Push: `git push origin main && git push origin <version>`

7. **Create announcement file** at `release-notes/<version>.md` in Discord-compatible markdown:
   - No tables (use bullet lists)
   - No `---` horizontal rules
   - No clickable `[text](url)` links (write URLs inline)
   - Use `>` block quotes for feature groupings
   - Use `**bold**` for feature names
   - Keep headers as `#` `##` (work in Discord forum posts)
   - Include download link: https://github.com/lshaf/unigeek/releases
   - Include SD card note: copy `sdcard/` to SD root or use built-in Download menu
   - End with: *Built for security research and education. Use responsibly.*

8. **Do NOT** commit the announcement file automatically — the user will handle it.