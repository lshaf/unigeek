# AGENT.md
# Firmware Project — Agentic AI Instructions

## What This Project Is

Multi-device ESP32 firmware. One codebase, ~18 release board envs (see [[board-matrix]] for the full list: M5StickC variants, T-Lora Pager, T-Display / S3, M5 Cardputer / ADV, DIY Smoochie, T-Embed CC1101, M5 CoreS3 Unified, M5Stick S3, CYD family).

All hardware differences are isolated under `firmware/boards/<board>/`. **Do not break this isolation.**

Out-of-tree (not in release): `m5_cores3` (bare CoreS3 ref), `diy_marauder` (WiFi Marauder v7).

## Read-first: Obsidian knowledge base

Curated notes for every subsystem and board live in the Obsidian vault — **read these via the `obsidian` MCP before re-reading source files** (Device.h, INavigation.h, board headers, ConfigManager keys).

    Vault:        ~/work/mcp-project
    Project path: project/unigeek/
    Entry point:  project/unigeek/_MOC.md   (links every other note)

Tools: `mcp__obsidian__read_note`, `mcp__obsidian__search_notes`,
`mcp__obsidian__list_directory`, `mcp__obsidian__write_note` (for updates).

Trust order: **source code > Obsidian notes > CLAUDE.md / AGENT.md**.

## Reference Libraries

    ../M5Unified           M5Stack hardware reference (not in build)
    ../LilyGoLib           LilyGO T-Lora Pager hardware reference (not in build)
    ../bruce               Bruce firmware — Smoochie / T-Embed CC1101 / Marauder v7 ref
    ../ChameleonUltraGUI   Chameleon Ultra protocol upstream (Flutter/Dart)
    ../MeshCore            MeshCore protocol upstream (planned T-Lora Pager port)
    ../Evil-M5Project      Evil-Cardputer*.ino — WiFi attack reference

Check these FIRST when implementing board-specific or third-party-derived features.

## Crediting References

When porting/referencing features from another repo, update README.md "Thanks To" with the repo link, author, and specific features as sub-bullets:

    - [RepoName](url) by author
      - Feature A
      - Feature B

## Git Policy

Never run `git commit`, `git push`, or any other git write op without an explicit user request. After file edits, stop — do not stage, commit, or push. Proposing a commit message is fine; executing it is not.

## Before Making Any Changes

1. Identify scope: shared (`src/`) vs board-specific (`firmware/boards/<board>/`).
2. **Read the relevant Obsidian note first** — don't grep when [[architecture]] / [[screen-patterns]] / [[navigation-system]] / [[known-pitfalls]] / etc. already summarize it.
3. Never add `Serial.print` to production code.
4. Never `#include "pins_arduino.h"` explicitly — it's auto-included.
5. Include headers only where actually needed.

## Keeping Documentation Accurate

When a task changes architecture / patterns / conventions, update CLAUDE.md, AGENT.md, **and the matching Obsidian note** immediately — do not wait for the user to ask.

Triggers: new board, new interface, new UI pattern, Device constructor change, ScreenManager change, new build flag, new library dep, convention change, navigation change (any `Navigation.h` / `Navigation.cpp` edit — also update the per-board note), knowledge file added/removed (also update `website/content/features/catalog.js`).

On every trigger, also update `~/work/mcp-project/project/unigeek/*.md` via `mcp__obsidian__write_note` so the Obsidian KB stays in sync with source.