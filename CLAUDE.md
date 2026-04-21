# CLAUDE.md
# Firmware Project - Claude AI Instructions

## Project Overview

ESP32 multi-device firmware supporting eleven boards.
Built with PlatformIO + Arduino framework + TFT_eSPI.
All hardware differences are isolated in board-specific folders.

**Detailed references:**
- `docs/HARDWARE.md`       — ISpeaker, IKeyboard, library deps, board hardware constraints
- `docs/NAVIGATION.md`     — Per-board input→direction tables (buttons, encoder, touch, keyboard)
- `docs/SCREEN_PATTERNS.md` — Rendering rules, back nav, overlays, views, config, storage, achievements
- `docs/WEBSITE.md`        — Website sync, crediting refs, release process, build gates

---

## Build System

### PlatformIO Commands

    pio run -e <env>
    pio run -e <env> -t upload
    pio device monitor
    pio run -t clean

### Platform Version

    platform = espressif32@6.13.0 (locked in platformio.ini)
    framework-arduinoespressif32 v2.0.17 (Arduino ESP32 v2.x API)
    patch.py validates platform version at build time — update EXPECTED_PLATFORM_VERSION if upgrading

### Board Environments

    m5stickcplus_11    M5StickC Plus 1.1, no keyboard, AXP192 power, LittleFS only
    m5stickcplus_2     M5StickC Plus 2, no keyboard, ADC battery + GPIO power hold, EncoderNavigation (optional encoder hat), LittleFS only
    t_lora_pager       LilyGO T-Lora Pager, TCA8418 keyboard, BQ25896/BQ27220 power, SD + LittleFS
    t_display          LilyGO T-Display, 2 buttons (double-click=select), no keyboard, LittleFS only
    t_display_s3       LilyGO T-Display S3, ESP32-S3, 2 buttons, USB HID, LittleFS only
    diy_smoochie       DIY Smoochie, 5 buttons, BQ25896 power, CC1101, 16MB flash, SD + LittleFS
    m5_cardputer       M5Stack Cardputer, 74HC138 GPIO matrix keyboard, ADC battery, SD + LittleFS
    m5_cardputer_adv   M5Stack Cardputer ADV, TCA8418 keyboard via Wire1, ADC battery, SD + LittleFS
    t_embed_cc1101     LilyGO T-Embed CC1101, rotary encoder nav, BQ25896 power, CC1101, I2S speaker, 16MB flash, SD + LittleFS
    m5_cores3_unified  M5Stack CoreS3 (M5Unified-backed), ESP32-S3, FT6336U touch nav, AW88298 I2S speaker, AXP2101 power, USB HID, SD + LittleFS
    m5sticks3          M5Stick S3, ESP32-S3, 2 buttons, ES8311 I2S speaker, M5PM1 power, IR, USB HID, LittleFS only

    Out-of-tree boards (present in firmware/boards/ but NOT released):
      m5_cores3       — bare CoreS3 (no M5Unified); kept as a reference to the unified variant
      diy_marauder    — WiFi Marauder v7 (ESP32), 5 buttons (GPIO34-39 no internal pull-up), ILI9341 240x320

### Build Flags (board-specific)

    DEVICE_HAS_KEYBOARD        defined for T-Lora Pager, m5_cardputer, m5_cardputer_adv
    DEVICE_HAS_SOUND           defined for boards with a speaker
    DEVICE_HAS_VOLUME_CONTROL  defined for boards with real volume control (I2S amp)
    DEVICE_HAS_USB_HID         defined for ESP32-S3 boards (T-Lora Pager, Cardputer, Cardputer ADV, T-Display S3, CoreS3, StickC S3)
    DEVICE_HAS_TOUCH_NAV       defined for touch-only nav boards (M5 CoreS3 / CoreS3 Unified) — gates main.cpp first-boot touch tutorial branch
    APP_MENU_POWER_OFF         defined for T-Lora Pager, M5StickC Plus 1.1 and 2, Marauder v7
    DEVICE_HAS_NAV_MODE_SWITCH defined for M5StickC Plus 1.1 and 2

    All app feature flags are defined in pins_arduino.h — NEVER in config.ini or platformio.ini
    Device identity flags (DEVICE_*) are defined in boards/_devices/*.json extra_flags:
      DEVICE_M5STICK_C_PLUS / DEVICE_T_LORA_PAGER / DEVICE_M5STICK_C_PLUS2
      DEVICE_M5_CARDPUTER / DEVICE_M5_CARDPUTER_ADV / DEVICE_DIY_SMOOCHIE
      DEVICE_T_EMBED_CC1101 / DEVICE_MARAUDER_V7
      DEVICE_M5_CORES3 / DEVICE_M5_STICKS3 / DEVICE_T_DISPLAY_S3

---

## Project Structure

    firmware/
    ├── boards/
    │   ├── _devices/               custom board JSON definitions
    │   ├── m5stickplus_11/         Device.cpp, Display.h, Navigation.h, EncoderNavigation.h, Power.h, pins_arduino.h
    │   ├── m5stickcplus_2/         Device.cpp, Display.h, Navigation.h, EncoderNavigation.h, Power.h, Speaker.h, pins_arduino.h
    │   ├── t_lora_pager/           Device.cpp, Display.h, Navigation.h/cpp, Keyboard.h/cpp, Power.h, pins_arduino.h
    │   ├── t_display/              Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h
    │   ├── t_display_s3/           Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h
    │   ├── diy_smoochie/           Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h
    │   ├── m5_cardputer/           Device.cpp, Display.h, Navigation.h, Keyboard.h, Power.h, pins_arduino.h
    │   ├── m5_cardputer_adv/       Device.cpp, Display.h, Navigation.h, Keyboard.h, Power.h, pins_arduino.h
    │   ├── t_embed_cc1101/         Device.cpp, Display.h, Navigation.h/cpp, Power.h, Speaker.h, pins_arduino.h
    │   ├── m5_cores3_unified/      Device.cpp, Display.h, Navigation.h/cpp, Power.h, Speaker.h, pins_arduino.h (M5Unified-backed)
    │   ├── m5_cores3/              Device.cpp, Display.h, Navigation.h/cpp, Power.h, Speaker.h, lib/AW9523B.h, lib/TouchFT6336U.h, pins_arduino.h (out-of-tree)
    │   ├── m5sticks3/              Device.cpp, Display.h, Navigation.h/cpp, Power.h, Speaker.h, lib/M5PM1, pins_arduino.h
    │   └── diy_marauder/           Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h (out-of-tree)
    ├── src/
    │   ├── core/                   Device.h, I*.h interfaces, Storage*.h, SpeakerI2S.h, RtcManager.h, ScreenManager.h, ConfigManager.h
    │   ├── screens/                MainMenuScreen, SettingScreen, wifi/, keyboard/, utility/
    │   ├── utils/                  WifiAttackUtil, FastWpaCrack, keyboard/ (HID/BLE/USB/DuckScript utils), nfc/
    │   ├── ui/
    │   │   ├── templates/          BaseScreen.h, ListScreen.h
    │   │   ├── views/              ScrollListView.h, LogView.h, ProgressView.h
    │   │   ├── components/         Header.h, StatusBar.h, Icon.h, QRCodeRenderer.h, BarcodeRenderer.h
    │   │   └── actions/            InputTextAction.h, InputNumberAction.h, InputSelectAction.h, ShowStatusAction.h, ShowQRCodeAction.h, ShowBarcodeAction.h
    │   └── main.cpp
    sdcard/                         sample SD card data — copy contents to SD root
    release-notes/                  version announcement notes (e.g. 1.2.0.md) — one file per release
    docs/                           HARDWARE.md, SCREEN_PATTERNS.md, WEBSITE.md

---

## Architecture

### Device Singleton

    Uni.Lcd         IDisplay&   TFT_eSPI subclass, direct draw calls
    Uni.Power       IPower&
    Uni.Nav         INavigation*
    Uni.Keyboard    IKeyboard*  nullptr on M5StickC; present on T-Lora Pager and Cardputer
    Uni.Speaker     ISpeaker*   nullptr if board has no speaker; use DEVICE_HAS_SOUND guard
    Uni.Storage     IStorage*   primary, SD if available else LittleFS; reassigned by _checkStorageFallback()
    Uni.StorageSD   IStorage*   direct SD access, nullptr on M5StickC
    Uni.StorageLFS  IStorage*   direct LittleFS access
    Uni.Spi         SPIClass*   shared SPI bus (HSPI on T-Lora Pager, nullptr on M5StickC/Cardputer)
    Uni.lcdOff      bool        true while display is off (power save) — screens should skip rendering

### Device Constructor Signature

    Device(IDisplay& lcd, IPower& power, INavigation* nav,
           IKeyboard* keyboard  = nullptr,
           IStorage*  storageSD = nullptr,
           IStorage*  storageLFS = nullptr,
           SPIClass*  spi = nullptr,
           ISpeaker*  sound = nullptr)

### Device Update Order

    Device::update() calls boardHook() → Keyboard->update() → Nav->update() each frame.
    boardHook() is defined per-board in Device.cpp — never call Keyboard->update() inside NavigationImpl.

### Speaker / Keyboard

See `docs/HARDWARE.md` for full ISpeaker and IKeyboard interface documentation.

    Guard all speaker calls: if (Uni.Speaker) Uni.Speaker->beep();

### Screen Pattern

    class MyScreen : public ListScreen {
    public:
      const char* title() override { return "Title"; }
      void onInit() override { setItems(_items); }
      void onItemSelected(uint8_t index) override {
        switch (index) {
          case 0: Screen.setScreen(new OtherScreen()); break;
        }
      }
      void onBack() override;   // implement in .cpp to avoid circular includes
    private:
      ListItem _items[2] = { {"Item A"}, {"Item B", "sublabel"} };
    };

### Navigation

    Uni.Nav->wasPressed()        true once per press/direction event
    Uni.Nav->readDirection()     consumes and returns direction
    Uni.Nav->isPressed()         true while any direction is physically held (non-consuming)
    Uni.Nav->pressDuration()     duration of last completed press (set on release)
    Uni.Nav->heldDuration()      duration of current ongoing press (0 if not held)
    Uni.Nav->drawOverlay()       optional post-render hook (default no-op); touch-nav
                                 boards override it to paint live edge indicators
                                 — called from main.cpp loop after Screen.update()
    DIR_UP / DIR_DOWN / DIR_PRESS / DIR_BACK / DIR_LEFT / DIR_RIGHT

    Always qualify as INavigation::DIR_BACK etc. — bare DIR_BACK will not compile.
    Include core/Device.h in the .cpp to get INavigation definition.

    ListScreen handles all six directions automatically.
    onBack() is ONLY on ListScreen — BaseScreen has NO onBack() virtual.

### ScreenManager

    Screen.setScreen(new MyScreen())   deletes old screen, deferred to next update()

### Rendering / Back Nav / Overlays / Storage / Config / Achievements

See `docs/SCREEN_PATTERNS.md` for full documentation.

Key rules (brief):
- NEVER call drawing functions directly from onUpdate() — call render() instead
- onBack() only on ListScreen; BaseScreen handles DIR_BACK manually in onUpdate()
- M5StickC default nav never emits DIR_BACK — handle DIR_BACK || DIR_PRESS for exit
- After any action overlay popup, call render() to restore the screen
- Config.load() must be called after _checkStorageFallback() in setup()
- Achievement.inc("id") / Achievement.setMax("id", val) — add achievements to every meaningful screen
- NEW menus/screens must NOT use a full-screen or full-body sprite that re-renders every frame —
  draw static chrome once, use per-region sprites, track per-region state, and minimize flicker
  where feasible (games/visualizers are exempt). See `docs/SCREEN_PATTERNS.md` → Partial-Redraw Pattern

### Achievement System (brief)

`#define Achievement AchievementManager::getInstance()`

    Achievement.inc("wifi_first_scan")        // auto-unlocks catalog entries
    Achievement.setMax("flappy_score_25", 25) // high-water mark
    Achievement.isUnlocked("id") / getInt("id") / getExp()

Rule: every new screen with meaningful actions must have achievements. See `docs/SCREEN_PATTERNS.md`.

Achievement rules:
- Always call `inc()` first, then `unlock()` on the returned count: `if (Achievement.inc("id") == 1) Achievement.unlock("id");`
- Place new entries near other entries of the same domain in the catalog array, not at the end
- After adding any new achievement, update the "Next available sequential ID" comment in `AchievementManager.h::catalog()`

---

## Code Conventions

- 2-space indentation
- Private members and methods prefixed with underscore _
- Use onInit() not init() — BaseScreen calls init() internally
- No Serial.print in production code
- IRAM_ATTR ISR functions must be in .cpp files not .h
- Prefer index-based onItemSelected switch over string comparison
- Use new for screen allocation — ScreenManager handles deletion
- pins_arduino.h is auto-included, never include it explicitly
- Always use `Sprite` (not `TFT_eSprite` or `LGFX_Sprite`) — IDisplay.h aliases it for both backends
- Always call deleteSprite() after createSprite() + pushSprite()
- Body sprites ALWAYS use bodyW() × bodyH() pushed at (bodyX(), bodyY())
  bodyW/bodyH already exclude the 4px screen padding — never subtract extra padding inside a sprite
- Screen .h files: declarations and data members only
  Trivial one-liners (title()) may stay inline in .h
  All onInit(), onUpdate(), onItemSelected(), onBack(), and private helper bodies go in .cpp
  Include headers only where they are actually needed, not preemptively

---

## Board-Specific Isolation Rules

- All hardware differences go in firmware/boards/<board>/
- Shared storage logic lives in src/core/StorageSD.h and src/core/StorageLFS.h
- Board Device.cpp only decides which storage to init and pass to constructor
- Use DEVICE_HAS_KEYBOARD for keyboard-conditional code
- Use DEVICE_HAS_SOUND for speaker-conditional code
- Use APP_MENU_POWER_OFF for power-off menu item
- See `docs/HARDWARE.md` for board-specific hardware constraints

---

## Board Navigation Reference

See `docs/NAVIGATION.md` — per-board physical input→direction tables for all boards.
**Update docs/NAVIGATION.md whenever any Navigation.h or Navigation.cpp is changed.**

---

## Known Constraints

- static constexpr const char*[] arrays as class members cause linker errors
  Fix: define them as static constexpr locals inside methods
- TFT_eSPI cannot render unicode characters — only ASCII printable chars work
- IRAM_ATTR inline functions cause Xtensa literal pool errors — always put in .cpp
- Sprite (TFT_eSprite/LGFX_Sprite) leaks heap if deleteSprite() is never called
- Full-body sprites can OOM on boards with large displays but small RAM (e.g. 320×240 @ 16-bit = ~150 KB)
  ALWAYS use per-block sprites (per-row, per-line, per-cell) sized to the smallest region needed
- powerOff() only works when USB is disconnected (hardware limitation)
- LittleFS uses SPIFFS subtype in partitions.csv, use LittleFS.begin() not SPIFFS.begin()
- Uni.Storage may be reassigned at runtime by _checkStorageFallback() in main.cpp when SD fails
- ListItem struct: single-field init {"Label"} zero-initializes sublabel to nullptr
- ListScreen DIR_BACK with empty list: check DIR_BACK first, before _effectiveCount() == 0 guard
  onRender() must always push the sprite even when empty — clears lingering overlays
- WiFi attack mode must be WIFI_MODE_APSTA — WifiAttackUtil handles this automatically
  Never call esp_wifi_set_channel() directly — use attacker->setChannel()
- Marauder v7 navigation: GPIOs 34/35/36/39 on ESP32 are input-only with NO internal pull-up
  Use INPUT (not INPUT_PULLUP) — hardware has external pull-ups
- Marauder v7 power: no battery IC; powerOff() uses deep sleep, wakeup via GPIO34 (RTC_GPIO4) ext0
- M5StickC Plus 2 power: GPIO 4 PWR_HOLD_PIN must stay HIGH to keep device powered
- sdcard/manifest/sdcard.txt must be updated when files are added or removed from sdcard/
- Board hardware constraints (SPI pins, I2C, LEDC, SD init) — see `docs/HARDWARE.md`

---

## Self-Updating This Document

When making changes that affect architecture, conventions, or patterns:
1. Complete the code change first
2. Show a diff of what would change in CLAUDE.md or AGENT.md
3. State why the update is needed
4. Wait for explicit approval before writing to the file

Never silently update CLAUDE.md or AGENT.md as a side effect of another task.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change,
navigation change (any Navigation.h or Navigation.cpp edit → update Board Navigation Reference).

---

## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes` or `query_graph` instead of Grep
- **Understanding impact**: `get_impact_radius` instead of manually tracing imports
- **Code review**: `detect_changes` + `get_review_context` instead of reading entire files
- **Finding relationships**: `query_graph` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview` + `list_communities`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
|------|----------|
| `detect_changes` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context` | Need source snippets for review — token-efficient |
| `get_impact_radius` | Understanding blast radius of a change |
| `get_affected_flows` | Finding which execution paths are impacted |
| `query_graph` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes` | Finding functions/classes by name or keyword |
| `get_architecture_overview` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes` for code review.
3. Use `get_affected_flows` to understand impact.
4. Use `query_graph` pattern="tests_for" to check coverage.