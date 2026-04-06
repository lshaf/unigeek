# AGENT.md
# Firmware Project - Agentic AI Instructions

## What This Project Is

Multi-device ESP32 firmware. Seven target boards share one codebase:
M5StickC Plus 1.1, M5StickC Plus 2, T-Lora Pager, T-Display 16MB, DIY Smoochie, M5 Cardputer, M5 Cardputer ADV.
All hardware differences are isolated. Do not break this isolation.

---

## Reference Libraries

    ../M5Unified    M5Stack hardware reference (not in build)
    ../LilyGoLib    LilyGO T-Lora Pager hardware reference (not in build)

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

## BLE Scan Pattern (NimBLE 1.4.x)

    // Non-blocking scan — NEVER use start(duration, false) which blocks the main loop
    static void scanDoneCB(NimBLEScanResults) {}
    _bleScan = NimBLEDevice::getScan();
    _bleScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), true);
    _bleScan->start(1, scanDoneCB);   // 1s non-blocking scan with callback

    // Re-trigger in onUpdate():
    if (_scanning && _bleScan && !_bleScan->isScanning()) {
      _bleScan->clearResults();
      _bleScan->start(1, scanDoneCB);
    }

    // Use NimBLEAdvertisedDeviceCallbacks (NOT NimBLEScanCallbacks — doesn't exist in 1.4.x)
    // Use setAdvertisedDeviceCallbacks() (NOT setScanCallbacks())

---

## LogView Component

    LogView log;
    log.addLine("message");                                          // scrolls when full (30 lines)
    log.draw(bodyX(), bodyY(), bodyW(), bodyH());                    // simple log
    log.draw(bodyX(), bodyY(), bodyW(), bodyH(), statusCb, data);    // with status bar callback
    log.clear();

Use LogView for scanning/progress screens. Do not roll raw _logLines[] arrays.

---

## IScreen Power Inhibit

    bool inhibitPowerSave() override { return _active; }   // keep display on
    bool inhibitPowerOff()  override { return _active; }   // block auto power-off

Override in screen .h. Use for scans, streams, attacks that should not be interrupted.

---

## Common Mistakes to Avoid

- Do NOT put IRAM_ATTR functions inline in .h files — put in .cpp
- Do NOT declare static constexpr const char*[] as class members — define inside methods
- Do NOT call setItems() after toggling an option — update sublabels on the array then call render()
- Do NOT use string comparison in onItemSelected — use index switch
- Do NOT draw StatusBar or ListScreen body directly to LCD — both use TFT_eSprite
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

---

## Action Overlay Pattern

    String      result = InputTextAction::popup("Title");
    String      result = InputTextAction::popup("Title", "default", true);  // numberMode
    int         result = InputNumberAction::popup("Title", min, max, default);
    const char* result = InputSelectAction::popup("Title", opts, count, default);
    void               ShowStatusAction::show("Message", durationMs);
    void               ShowQRCodeAction::show("Label", "content");
    void               ProgressView::show("Message", percent);  // non-blocking, from ui/views/ProgressView.h

    // Always call render() after a popup returns to restore the screen

    static constexpr InputSelectAction::Option opts[] = {
      {"Label", "value"},
    };

---

## Navigation Direction Values

    DIR_UP / DIR_DOWN / DIR_PRESS / DIR_BACK / DIR_LEFT / DIR_RIGHT / DIR_NONE

    isPressed()      true while physically held (non-consuming)
    heldDuration()   ms since current press started (0 if not held)
    pressDuration()  ms of last completed press (set on release)

    ListScreen handles all six automatically:
      UP/DOWN     move by 1, wraps around
      LEFT/RIGHT  page jump by visible count, clamps at ends
      PRESS       select item
      BACK        call onBack()

## Non-Keyboard Back Navigation in Custom States

    // CORRECT — works on all boards
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { /* exit */ }
    }

    // Render exit affordance:
    #ifdef DEVICE_HAS_KEYBOARD
      sp.drawString("BACK: Exit", bodyW() / 2, bodyH() - 2, 1);
    #else
      sp.fillRect(0, bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      sp.setTextColor(TFT_WHITE, Config.getThemeColor());
      sp.drawString("< Back", bodyW() / 2, bodyH() - 8, 1);
    #endif

---

## Device Constructor Signature

    Device(IDisplay& lcd, IPower& power, INavigation* nav,
           IKeyboard* keyboard  = nullptr,
           IStorage*  storageSD = nullptr,
           IStorage*  storageLFS = nullptr,
           SPIClass*  spi = nullptr,
           ISpeaker*  sound = nullptr)

Storage primary is decided inside constructor: SD if available, else LittleFS.

---

## Sublabel Pattern

    // Pointers must point into class member Strings — NOT temporaries
    _nameSub = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
    _items[0].sublabel = _nameSub.c_str();

---

## Config System

    Config.load(Uni.Storage)    — call in setup() after _checkStorageFallback()
    Config.save(Uni.Storage)    — call after every Config.set() to persist
    Config.get(KEY, DEFAULT)    — use #define constants from ConfigManager.h
    Config.set(KEY, value)      — in-memory only until save()

---

## Build src_filter for Board .cpp Files

If adding .cpp files in a board folder, ensure boards.ini includes:

    build_src_filter = +<../boards/t-lora-pager/>

---

## Migrating Screens from puteros

    Reference: ../puteros/firmware/src/os/screens/
    Key adaptations:
    - _global->setScreen(new X())  →  Screen.setScreen(new X())
    - Template::renderQRCode()     →  ShowQRCodeAction::show()
    - Template::renderStatus()     →  ShowStatusAction::show()
    - InputTextScreen::popup()     →  InputTextAction::popup()
    - onEnter(entry) string match  →  onItemSelected(index) switch
    Always read both .h and .cpp from puteros before migrating.

---

## Website Content Sync

The website reads content directly from the repo — no copy step needed:
- `knowledge/*.md`       → feature detail pages (slug must match catalog entry)
- `release-notes/*.md`   → release notes page (auto-detected at build)
- `README.md`            → source of truth for feature catalog

Rules:
- Adding a knowledge file   → add entry to `website/content/features/catalog.js` with `hasDetail: true`
- Removing a knowledge file → set `hasDetail: false` in catalog (do NOT delete the catalog entry)
- Renaming a knowledge file → update `slug` in catalog to match new filename
- Adding a feature to README → add entry to catalog even if no knowledge file yet (`hasDetail: false`)
- Removing a feature from README → remove entry from catalog entirely
- Adding a release-notes file → no action needed

Always check catalog.js after any README.md or knowledge/ change.

---

## Keeping Documentation Accurate

When completing a task that changes architecture, patterns, or conventions,
update CLAUDE.md and AGENT.md immediately — do not wait for user to ask.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change.