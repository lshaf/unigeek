# AGENT.md
# Firmware Project - Agentic AI Instructions

## What This Project Is

Multi-device ESP32 firmware. Eleven target boards share one codebase:
M5StickC Plus 1.1, M5StickC Plus 2, T-Lora Pager, T-Display, T-Display S3,
DIY Smoochie, M5 Cardputer, M5 Cardputer ADV, T-Embed CC1101,
M5 CoreS3 (Unified), M5Stick S3.
All hardware differences are isolated. Do not break this isolation.

Out-of-tree boards (present in firmware/boards/ but NOT in the release matrix):
m5_cores3 (bare CoreS3 reference), diy_marauder (WiFi Marauder v7).

---

## Reference Libraries

    ../M5Unified    M5Stack hardware reference (not in build)
    ../LilyGoLib    LilyGO T-Lora Pager hardware reference (not in build)
    ../bruce        Bruce firmware — Smoochiee, T-Embed CC1101, Marauder v7 board reference (not in build)

When implementing board-specific hardware features, check these FIRST.

---

## Crediting References

When porting or referencing features from another repo, update README.md "Thanks To"
section with the repo link, author, and specific features taken as sub-bullets.

---

## Before Making Any Changes

1. Identify the target — is the change shared or board-specific?
    - All boards      →  change goes in src/
    - Board-specific  →  change goes in firmware/boards/<board>/
2. Read CLAUDE.md for conventions before writing any code.
3. Never add Serial.print to production code.
4. Never include pins_arduino.h explicitly — it is auto-included.
5. Include headers only where they are actually needed.

---

## Adding a New Screen

1. Create src/screens/<category>/MyScreen.h and .cpp
2. Extend ListScreen or BaseScreen
3. Override title(), onInit(), onItemSelected()
4. Use Screen.setScreen(new MyScreen()) to navigate
5. Use new for allocation — ScreenManager handles deletion
6. Declare item arrays as class members, not braced-init-lists
7. Use onInit() not init()
8. **Think about achievements**: for every meaningful user action in the screen,
   add appropriate achievements to the catalog in `AchievementManager.h::catalog()`
   and hook `Achievement.inc()` / `Achievement.setMax()` / `Achievement.unlock()` calls
   at the appropriate points in the screen implementation.
   Rules:
   - Always `inc()` then `unlock()`: `if (Achievement.inc("id") == 1) Achievement.unlock("id");`
   - Place new catalog entries near other entries of the same domain — not at the end of the file
   - After adding entries, update the "Next available sequential ID" comment in `AchievementManager.h::catalog()`

See `docs/SCREEN_PATTERNS.md` for full rendering rules, overlays, LogView, config, storage, and achievement details.

---

## Adding a New Board

1. Create firmware/boards/<boardname>/
2. Implement Display.h, Navigation.h, Power.h, Device.cpp
3. Add pins_arduino.h with all GPIO definitions and TFT_eSPI config macros
4. Create config.ini (or boards.ini) with PlatformIO env config
5. Storage: init in createInstance(), pass to Device constructor
6. If board has keyboard: implement Keyboard.h with IKeyboard interface
   - peekKey() must NOT consume the key — NavigationImpl peeks first, only consumes nav keys
   - modifiers() returns bitmask of IKeyboard::Modifier flags
   - Add DEVICE_HAS_KEYBOARD to build_flags
7. If board has speaker: define SPK_BCLK/SPK_WCLK/SPK_DOUT/SPK_I2S_PORT in pins_arduino.h
   - Add DEVICE_HAS_SOUND to build_flags
   - Instantiate SpeakerI2S in Device.cpp, pass as `sound` param
8. If board has RTC: define DEVICE_HAS_RTC, RTC_I2C_ADDR, RTC_REG_BASE in pins_arduino.h
   - Define RTC_WIRE only if not on Wire (default fallback in RtcManager.h)
9. If board has SD: create SPIClass with correct bus, pass to SD.begin(csPin, spi)
10. Always init StorageLFS — all boards have a LittleFS partition
11. Define Device::boardHook() in Device.cpp — empty stub if no per-frame board logic

See `docs/HARDWARE.md` for ISpeaker, IKeyboard, I2S pin defines, and board-specific hardware constraints.

**Build gate:** Do NOT add a new board to build_all.sh / platformio.ini default_envs /
release workflow / website unless the user explicitly asks. See `docs/WEBSITE.md`.

---

## File Placement Rules

    Screen UI                    src/screens/
    UI overlays and actions      src/ui/actions/
    UI templates                 src/ui/templates/
    Interfaces                   src/core/I*.h
    Shared implementations       src/core/
    Board hardware impl          firmware/boards/<board>/
    ISR functions                Must be in .cpp not .h

---

## Storage Rules

    if (Uni.Storage && Uni.Storage->isAvailable()) {
      Uni.Storage->writeFile("/path/file.txt", content);
    }

Use Uni.StorageSD or Uni.StorageLFS only when the feature explicitly requires one.
Always null-check — Uni.StorageSD is nullptr on M5StickC.
sdcard/ directory contains sample SD card data (portals, duckyscript, passwords, qrcodes) — copy to SD root.

---

## Common Mistakes to Avoid

- Do NOT put IRAM_ATTR functions inline in .h files — put in .cpp
- Do NOT declare static constexpr const char*[] as class members — define inside methods
- Do NOT call setItems() after toggling an option — update sublabels on the array then call render()
- Do NOT use string comparison in onItemSelected — use index switch
- Do NOT draw StatusBar or ListScreen body directly to LCD — both use Sprite
- Do NOT use `TFT_eSprite` or `LGFX_Sprite` directly — always use `Sprite` (aliased in IDisplay.h for both backends)
- Do NOT forget deleteSprite() after every createSprite() + pushSprite()
- Do NOT modify Device.h constructor without updating ALL board Device.cpp files
- Do NOT use unqualified File type without SD.h — use fs::File (from <FS.h>)
- Do NOT use unicode in TFT drawString — TFT_eSPI only renders ASCII
- Do NOT use WIFI_MODE_AP for WiFi attacks — use WIFI_MODE_APSTA (WifiAttackUtil handles this)
- Do NOT call esp_wifi_set_channel() directly with WifiAttackUtil — use setChannel()
- Do NOT use init() in screens — use onInit()
- Do NOT call Keyboard->update() inside NavigationImpl — Device::update() does this
- Do NOT use getKey() in NavigationImpl unconditionally — use peekKey() first, only consume nav keys
- Do NOT read \b from Uni.Keyboard in overlays — Nav consumes it; overlays use readDirection()
- Do NOT forget Device::boardHook() in every board's Device.cpp — linker requires it
- Do NOT call lcd.fillRect() before pushing a sprite — the push already overwrites (causes flash)
- Do NOT call Uni.Speaker directly — always null-check: if (Uni.Speaker) Uni.Speaker->beep()
- Do NOT worry about sound overlap — SpeakerI2S and SpeakerBuzzer have built-in `if (_taskHandle) return` guards
- Do NOT handle only DIR_BACK in custom states — M5StickC default nav never emits DIR_BACK;
  always handle both DIR_BACK and DIR_PRESS as "back/stop"
- Do NOT check DIR_BACK after early-return guard in ListScreen onUpdate() — check DIR_BACK first
- Do NOT skip sprite push in ListScreen onRender() when empty — always push to clear overlays
- Do NOT roll custom log line arrays — use LogView from ui/views/LogView.h
- Do NOT forget inhibitPowerSave()/inhibitPowerOff() on screens with active long-running operations
- Do NOT allocate a full-screen or full-body sprite that re-renders every frame in new menus/screens —
  draw static chrome once (guarded by a `_chromeDrawn` flag), use per-region sprites, and track per-region
  state so only regions that changed get redrawn. Games/visualizers that legitimately repaint every frame
  are exempt. See `docs/SCREEN_PATTERNS.md` → Partial-Redraw Pattern
- Do NOT allocate full-body sprites even for one-shot renders — boards with large displays and small RAM
  will OOM (e.g. 320×240 @ 16-bit = ~150 KB). ALWAYS use per-block sprites (per-row, per-line, per-cell)
- Do NOT edit Navigation.h or Navigation.cpp without also updating the affected board's section
  in docs/NAVIGATION.md

---

## Keeping Documentation Accurate

When completing a task that changes architecture, patterns, or conventions,
update CLAUDE.md and AGENT.md immediately — do not wait for user to ask.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change,
navigation change (any Navigation.h or Navigation.cpp edit).

**Navigation changes specifically:** whenever any board's Navigation.h or Navigation.cpp
is added or modified (new input, changed GPIO, new direction mapping, new threshold),
update the affected board's section in `docs/NAVIGATION.md`.
Update only the affected board's table/notes — do not rewrite unrelated boards.