## File Manager

Accessed from **Utility > File Manager**. Browse, view, and manage files on the active storage (SD if present, otherwise LittleFS). Directories are listed first, then files, both sorted alphabetically. The title bar shows the current folder name (or `File Manager` at the root).

> [!note]
> Operates on `Uni.Storage` — the same primary storage used by every other module. Renames, deletes, and pastes affect real files. There is no undo.

## Browser Controls

| Input | Action |
|-------|--------|
| UP / DOWN | Move selection |
| Press (Select) | Open folder, or open text viewer for files |
| Hold Press 1s | Open the context menu for the highlighted item |
| Back | Go up one folder; exit when already at `/` |

Folder navigation remembers the selected row at each level (up to 8 deep), so going **Back** returns the cursor to the folder you came from.

## Context Menu

Hold the select button for one second over a row to open the per-item menu. Available entries depend on whether the target is a file, a directory, or empty space, and whether the clipboard holds an item.

| Entry | Shown when | Action |
|-------|-----------|--------|
| View | File | Open the text viewer (scrollable, ASCII) |
| View HEX | File | Open the hex viewer (offset · bytes · ASCII) |
| New Folder | Always | Prompt for a name and create it in the current folder |
| Rename | File or directory | Prompt for a new name |
| Delete | File or directory | Delete the file, or recursively remove the folder |
| Copy | File | Stash the file path with a `Copy` clipboard tag |
| Cut | File | Stash the file path with a `Cut` clipboard tag |
| Paste / Replace | Clipboard set | Copy or move the clipboard item into the current folder; label flips to `Replace` if a same-named file exists |
| Clear Clipboard | Clipboard set | Drop the clipboard item without acting on it |
| Close | Always | Return to the folder listing |
| Exit | Always | Leave the File Manager |

> [!tip]
> Copy reads the source file content and writes it to the destination, so it works across SD ↔ LittleFS even though `rename` does not. Cut uses `rename`, which is fast but stays on the same filesystem.

## File Viewers

**Text viewer** loads the file into RAM and splits it on newlines. Use UP / DOWN to scroll, Back to return.

**Hex viewer** streams the file from storage in fixed-width rows (8 or 16 bytes per row depending on screen width) and shows the offset, hex bytes, and printable ASCII for each row. Scroll a row at a time with UP / DOWN; Back returns to the file list.

> [!warn]
> Very large files (logs, captures, firmware blobs) can be slow to open in the text viewer because the entire file is loaded into RAM. Prefer the hex viewer for binary or multi-megabyte files.

## Storage

The File Manager works on whatever path `Uni.Storage` is rooted at — typically:

```
/                         (root of SD or LittleFS)
└── unigeek/              project files written by other modules
```

## Achievements

| Achievement | Tier |
|-------------|------|
| File Explorer | Bronze |
| Clean Sweep | Bronze |
| Duplicator | Bronze |
| Page Turner | Bronze |
| Hex Peeper | Bronze |
