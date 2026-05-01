# CLAUDE.md
# Firmware Project — Claude AI Instructions

## Project

ESP32 multi-device firmware (PlatformIO + Arduino + TFT_eSPI/M5GFX). One codebase, ~18 release board envs. All hardware differences isolated under `firmware/boards/<board>/`.

## Read-first: Obsidian knowledge base

Curated project notes live in the Obsidian vault — **read these via the `obsidian` MCP** instead of grepping headers or re-reading Device.cpp / per-board files. Saves tokens; the notes are maintained.

    Vault:        ~/work/mcp-project
    Project path: project/unigeek/
    Entry point:  project/unigeek/_MOC.md   (links every other note)

Notes cover: `architecture`, `build-system`, `screen-patterns`,
`navigation-system`, `storage-system`, `achievement-system`, `config-system`,
`ui-toolkit`, `speaker-keyboard`, `conventions`, `known-pitfalls`,
`board-matrix`, `file-pointers`, `modules-index`, `knowledge-file-format`,
`mcp-graph`, `website-release`, `webauthn-progress`,
`chameleon-ultra-internals`, plus per-board notes under `boards/`.

Tools: `mcp__obsidian__read_note`, `mcp__obsidian__search_notes`,
`mcp__obsidian__list_directory`, `mcp__obsidian__write_note`.

**Never call `mcp__obsidian__delete_note`.** If a note is wrong or outdated:
1. Move it to `project/unigeek/_mistakes/` via `mcp__obsidian__move_note`.
2. Prepend a one-line comment explaining what was wrong.
3. Append a row to `project/unigeek/_corrections.md` summarising the mistake.

Read `project/unigeek/_corrections.md` whenever starting work in an area it covers.

When something non-obvious is discovered during a session — a hidden constraint, unexpected behavior, a pattern that surprised us — append a row to `project/unigeek/_learnings.md`. Read it before starting work in areas it covers.

Trust order if they disagree: **source code > Obsidian notes > CLAUDE.md**.

## Build

    pio run -e <env>
    pio run -e <env> -t upload
    pio device monitor
    pio run -t clean

`platform = espressif32@6.13.0` (locked); `framework-arduinoespressif32 v2.0.17`.
`patch.py` validates the platform version at build — update `EXPECTED_PLATFORM_VERSION` if upgrading.

## Board envs (released, default_envs)

    m5stickcplus_11   m5stickcplus_2   t_lora_pager
    t_display         t_display_s3     diy_smoochie
    m5_cardputer      m5_cardputer_adv t_embed_cc1101
    m5_cores3_unified m5sticks3
    cyd_2432w328r  cyd_2432s028  cyd_2432s028_2usb
    cyd_2432w328c  cyd_2432w328c_2  cyd_3248s035r  cyd_3248s035c

Out-of-tree (in `firmware/boards/` but NOT in default_envs / release / website):
`m5_cores3` (bare CoreS3 reference), `diy_marauder` (WiFi Marauder v7).
Details in [[board-matrix]].

## Git policy

Never run `git commit`, `git push`, or any other git write op without explicit user request. After making file edits, stop — do not stage, commit, or push. Proposing a commit message is fine; executing it is not.

## Self-updating this document

When making changes that affect architecture / conventions / patterns / a new board / a new build flag / a new library:

1. Complete the code change first.
2. Show a diff of what would change in CLAUDE.md / AGENT.md.
3. State why the update is needed.
4. Wait for explicit approval before writing to the file.
5. **Also update the matching note in the Obsidian vault** via `mcp__obsidian__write_note` so the KB does not drift from source.

Never silently update CLAUDE.md or AGENT.md as a side effect of another task.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change,
navigation change (any `Navigation.h` or `Navigation.cpp` edit — affects per-board notes too),
knowledge file added/removed (also flip the matching `website/content/features/catalog.js` row).