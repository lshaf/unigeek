# CLAUDE.md
# Firmware Project - Claude AI Instructions

## Project Overview

ESP32 multi-device firmware supporting M5StickC Plus 1.1, M5StickC Plus 2, T-Lora Pager, T-Display 16MB, DIY Smoochie, M5 Cardputer, and M5 Cardputer ADV.
Built with PlatformIO + Arduino framework + TFT_eSPI.
All hardware differences are isolated in board-specific folders.

---

## Build System

### PlatformIO Commands

    pio run -e m5stickcplus_11
    pio run -e m5stickcplus_2
    pio run -e t_lora_pager
    pio run -e t_display_16mb
    pio run -e diy_smoochie
    pio run -e m5_cardputer
    pio run -e m5_cardputer_adv
    pio run -e m5stickcplus_11 -t upload
    pio run -e m5stickcplus_2 -t upload
    pio run -e t_lora_pager -t upload
    pio run -e t_display_16mb -t upload
    pio run -e diy_smoochie -t upload
    pio run -e m5_cardputer -t upload
    pio run -e m5_cardputer_adv -t upload
    pio device monitor
    pio run -t clean

### Platform Version

    platform = espressif32@6.13.0 (locked in platformio.ini)
    framework-arduinoespressif32 v2.0.17 (Arduino ESP32 v2.x API)
    patch.py validates platform version at build time — update EXPECTED_PLATFORM_VERSION if upgrading

### Board Environments

    m5stickcplus_11   M5StickC Plus 1.1, no keyboard, AXP192 power, LittleFS only
    m5stickcplus_2    M5StickC Plus 2, no keyboard, ADC battery + GPIO power hold, EncoderNavigation (optional encoder hat), LittleFS only
    t_lora_pager      LilyGO T-Lora Pager, TCA8418 keyboard, BQ25896/BQ27220 power, SD + LittleFS
    t_display_16mb    LilyGO T-Display 16MB, 2 buttons (double-click=select), no keyboard, 16MB flash, LittleFS only
    diy_smoochie      DIY Smoochie, no keyboard, 16MB flash, LittleFS only
    m5_cardputer      M5Stack Cardputer, 74HC138 GPIO matrix keyboard, ADC battery, SD + LittleFS
    m5_cardputer_adv  M5Stack Cardputer ADV, TCA8418 keyboard via Wire1, ADC battery, SD + LittleFS

### Build Flags (board-specific)

    DEVICE_HAS_KEYBOARD       defined for T-Lora Pager, m5_cardputer, m5_cardputer_adv — enables keyboard input paths
    DEVICE_HAS_SOUND          defined for boards with a speaker — enables Uni.Speaker and audio paths
    DEVICE_HAS_VOLUME_CONTROL defined for boards with real volume control (I2S amp) — shows Volume in Settings
    DEVICE_HAS_USB_HID        defined for ESP32-S3 boards (T-Lora Pager, Cardputer, Cardputer ADV)
    APP_MENU_POWER_OFF        defined for T-Lora Pager, M5StickC Plus 1.1 and 2, adds Power Off in main menu
    DEVICE_HAS_NAV_MODE_SWITCH defined for M5StickC Plus 1.1 and 2 — enables nav mode setting (Default vs Encoder/Joystick)

    All app feature flags are defined in pins_arduino.h — NEVER in config.ini or platformio.ini
    Device identity flags (DEVICE_*) are defined in boards/_devices/*.json extra_flags — not in pins_arduino.h or config.ini
      DEVICE_M5STICK_C_PLUS   — M5StickC Plus 1.1 (ESP32)
      DEVICE_T_LORA_PAGER     — LilyGO T-Lora Pager (ESP32-S3)
      DEVICE_M5STICK_C_PLUS2  — M5StickC Plus 2 (ESP32)
      DEVICE_M5_CARDPUTER     — M5Stack Cardputer (ESP32-S3)
      DEVICE_M5_CARDPUTER_ADV — M5Stack Cardputer ADV (ESP32-S3)
      DEVICE_DIY_SMOOCHIE     — DIY Smoochie (ESP32-S3)

---

## Project Structure

    firmware/
    ├── boards/
    │   ├── _devices/               custom board JSON definitions
    │   ├── m5stickplus_11/         Device.cpp, Display.h, Navigation.h, EncoderNavigation.h, Power.h, pins_arduino.h
    │   ├── m5stickcplus_2/         Device.cpp, Display.h, Navigation.h, EncoderNavigation.h, Power.h, Speaker.h, pins_arduino.h
    │   ├── t_lora_pager/           Device.cpp, Display.h, Navigation.h/cpp, Keyboard.h/cpp, Power.h, pins_arduino.h
    │   ├── t_display_16mb/         Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h
    │   ├── diy_smoochie/           Device.cpp, Display.h, Navigation.h, Power.h, pins_arduino.h
    │   ├── m5_cardputer/           Device.cpp, Display.h, Navigation.h, Keyboard.h, Power.h, pins_arduino.h
    │   └── m5_cardputer_adv/       Device.cpp, Display.h, Navigation.h, Keyboard.h, Power.h, pins_arduino.h
    ├── src/
    │   ├── core/                   Device.h, I*.h interfaces, Storage*.h, SpeakerI2S.h, RtcManager.h, ScreenManager.h, ConfigManager.h
    │   ├── screens/                MainMenuScreen, SettingScreen, wifi/, keyboard/, utility/
    │   ├── utils/                  WifiAttackUtil, FastWpaCrack, keyboard/ (HID/BLE/USB/DuckScript utils), nfc/ (NFCUtility, StaticNestedAttack, DarksideAttack, crypto/)
    │   ├── ui/
    │   │   ├── templates/          BaseScreen.h, ListScreen.h
    │   │   ├── views/              ScrollListView.h, LogView.h, ProgressView.h
    │   │   ├── components/         Header.h, StatusBar.h, Icon.h, QRCodeRenderer.h, BarcodeRenderer.h
    │   │   └── actions/            InputTextAction.h, InputNumberAction.h, InputSelectAction.h, ShowStatusAction.h, ShowQRCodeAction.h, ShowBarcodeAction.h
    │   └── main.cpp
    sdcard/                         sample SD card data — copy contents to SD root
    ├── unigeek/keyboard/duckyscript/   sample DuckyScript payloads
    ├── unigeek/qrcode/                 sample QR code content files
    ├── unigeek/utility/passwords/      sample password wordlists (8+ chars for WiFi)
    ├── unigeek/nfc/dictionaries/       MIFARE Classic key dictionary files (hex format)
    └── unigeek/wifi/portals/           Evil Twin captive portal templates (google, facebook, wifi)
    release-notes/                      version announcement notes (e.g. 1.2.0.md) — one file per release

### Releases

    Binary downloads: https://github.com/lshaf/unigeek/releases
    SD card content: copy sdcard/ to SD root, or use built-in Download menu in firmware

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

### ISpeaker Interface

    Uni.Speaker->tone(freq, durationMs)        play square-wave tone at Hz for ms (FreeRTOS task)
    Uni.Speaker->noTone()                      stop any playing tone
    Uni.Speaker->setVolume(0-100)              set volume percentage (persisted via APP_CONFIG_VOLUME)
    Uni.Speaker->beep()                        short nav feedback beep (respects APP_CONFIG_NAV_SOUND)
    Uni.Speaker->playRandomTone(shift, ms)     random note from C major scale, optional semitone shift
    Uni.Speaker->playWav(data, size)           play PROGMEM WAV (8/16-bit, mono/stereo, any sample rate)
    Uni.Speaker->playNotification()            play SoundNotification.h WAV
    Uni.Speaker->playWin() / playLose()        play win/lose WAV
    Uni.Speaker->playCorrectAnswer()           tone sequence: 523Hz→784Hz
    Uni.Speaker->playWrongAnswer()             tone sequence: 1109Hz×2

    Guard all speaker calls: if (Uni.Speaker) Uni.Speaker->beep();

    Speaker implementations (board-specific):
      SpeakerI2S    — Cardputer / Cardputer ADV (I2S, I2S_NUM_1, WAV capable)
      SpeakerADV    — Cardputer ADV only (extends SpeakerI2S, adds ES8311 codec init)
      SpeakerBuzzer — M5StickC Plus 1.1 (LEDC PWM on GPIO 2, no WAV, tone sequences only)

### IKeyboard Interface

    Uni.Keyboard->available()   true when a key is buffered and ready
    Uni.Keyboard->peekKey()     read buffered key WITHOUT consuming — used by NavigationImpl
    Uni.Keyboard->getKey()      consume and return buffered key; sets _waitRelease on GPIO-matrix boards
    Uni.Keyboard->modifiers()   bitmask of currently held modifier keys (IKeyboard::Modifier enum)
                                MOD_SHIFT, MOD_FN, MOD_CAPS, MOD_CTRL, MOD_ALT, MOD_OPT

    NavigationImpl (keyboard boards) uses peekKey() to check for nav keys (;  .  \n  \b  ,  /).
    Only calls getKey() when the key is a nav key — all other keys remain for action overlays.
    \b (backspace) is consumed by Nav and emitted as DIR_BACK; action overlays receive it via
    Uni.Nav->readDirection() rather than Uni.Keyboard->getKey().

### Device Update Order

    Device::update() calls boardHook() then Keyboard->update() then Nav->update() each frame.
    boardHook() is defined per-board in Device.cpp — M5StickC uses it for the 3s BTN_A reset,
    all other boards define it empty. Never call Keyboard->update() inside NavigationImpl.

### Screen Pattern

    class MyScreen : public ListScreen {
    public:
      const char* title() override { return "Title"; }

      void onInit() override {
        setItems(_items);
      }

      void onItemSelected(uint8_t index) override {
        switch (index) {
          case 0: Screen.setScreen(new OtherScreen()); break;
        }
      }

      void onBack() override;           // implement in .cpp to avoid circular includes

    private:
      ListItem _items[2] = {
        {"Item A"},
        {"Item B", "sublabel"},
      };
    };

### Rendering Rule (Screen-Off Safe)

    onUpdate() runs ALWAYS — even when display is off (Uni.lcdOff == true).
    render() is guarded by Uni.lcdOff in BaseScreen — it early-returns when display is off.

    NEVER call drawing functions directly from onUpdate().
    Instead, change state then call render(). Let onRender() dispatch by state.

    ✗ BAD:  onUpdate() { _drawLog(); }
    ✗ BAD:  onUpdate() { onRender(); }
    ✗ BAD:  onUpdate() { _scrollView.render(...); }
    ✓ GOOD: onUpdate() { render(); }

    For ListScreen subclasses with multiple states, override onRender():
      void MyScreen::onRender() {
        if (_state == STATE_RUNNING) { _drawLog(); return; }
        ListScreen::onRender();  // default list rendering for menu states
      }

    For BaseScreen subclasses, put all drawing in onRender():
      void MyScreen::onRender() { _renderContent(); }

    If a draw helper needs dynamic data (e.g. a status message), store it in a
    member variable first, then call render() — onRender() reads the member.

### Back Navigation

    onBack() is ONLY available on ListScreen — BaseScreen has no onBack() virtual method.
    - ListScreen subclasses: override onBack() — called automatically when DIR_BACK is received
    - BaseScreen subclasses: handle DIR_BACK manually in onUpdate():
        if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) { ... }
      Never declare onBack() override on a BaseScreen subclass — it will not compile.
    - IMPORTANT: M5StickC default nav NEVER emits DIR_BACK — only DIR_UP/DOWN/PRESS.
      In any custom state that must support "go back" or "stop", handle BOTH:
        if (Uni.Nav->wasPressed()) {
          auto dir = Uni.Nav->readDirection();
          if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) { /* exit */ }
        }
      And render a device-appropriate exit affordance:
        #ifdef DEVICE_HAS_KEYBOARD
          sp.drawString("BACK: Exit", ...);
        #else
          sp.fillRect(0, bodyH() - 16, bodyW(), 16, Config.getThemeColor());
          sp.setTextColor(TFT_WHITE, Config.getThemeColor());
          sp.drawString("< Back", bodyW() / 2, bodyH() - 8, 1);
        #endif

    - hasBackItem() controls whether "< Back" appears — default true
      Only MainMenuScreen overrides it to false; all other screens use the default
    - "< Back" is hidden on keyboard boards and encoder nav (DIR_BACK handles it instead)
    - setItems() resets _selectedIndex and _scrollOffset to 0 then calls render() — ONLY call on init or when switching item arrays
    - NEVER call setItems() to refresh sublabels — update sublabels directly on the member array, then call render()
    - Implement onBack() in .cpp when it needs to instantiate a parent screen (breaks circular includes)

### ScreenManager

    Screen.setScreen(new MyScreen())   deletes old screen, deferred to next update()

### Navigation

    Uni.Nav->wasPressed()        true once per press/direction event
    Uni.Nav->readDirection()     consumes and returns direction
    Uni.Nav->isPressed()         true while any direction is physically held (non-consuming)
    Uni.Nav->pressDuration()     duration of last completed press (set on release)
    Uni.Nav->heldDuration()      duration of current ongoing press (millis since press start, 0 if not held)
    DIR_UP / DIR_DOWN / DIR_PRESS / DIR_BACK / DIR_LEFT / DIR_RIGHT

    ListScreen handles all six directions automatically:
      DIR_UP / DIR_DOWN   move selection by 1, wraps
      DIR_LEFT / DIR_RIGHT  page jump by visible-item count, clamps at ends
      DIR_PRESS           select item (or "< Back" item on no-keyboard/default-nav)
      DIR_BACK            call onBack()

### Action Overlays (all blocking)

    String      InputTextAction::popup("Title")
    String      InputTextAction::popup("Title", "default")
    String      InputTextAction::popup("Title", "default", true)   // numberMode: digits and dot only
    int         InputNumberAction::popup("Title")
    int         InputNumberAction::popup("Title", 0, 100, 50)
    const char* InputSelectAction::popup("Title", opts, count)
    const char* InputSelectAction::popup("Title", opts, count, "default_value")
    void        ShowStatusAction::show("Message")           wait for button/key press, then wipe  (default, durationMs=-1)
    void        ShowStatusAction::show("Message", 0)        show and return immediately, no wipe
    void        ShowStatusAction::show("Message", 1500)     show, block ms, then wipe
    void        ShowQRCodeAction::show("Label", "content")  blocking QR code overlay

    // After any popup, call render() to restore the screen — actions only wipe their overlay area
    // InputSelectAction: DIR_BACK dismisses and returns nullptr
    // InputTextAction numberMode=true: scroll mode shows 0-9 and . only

### Storage

    Uni.Storage->isAvailable()
    Uni.Storage->exists("/path/file.txt")
    Uni.Storage->readFile("/path/file.txt")              returns String
    Uni.Storage->writeFile("/path/file.txt", "content")  returns bool
    Uni.Storage->makeDir("/path")
    Uni.Storage->deleteFile("/path/file.txt")
    Uni.Storage->freeBytes()                             returns uint64_t free bytes
    Uni.Storage->open("/path/file.pcap", FILE_WRITE)     returns fs::File (binary access)
    Uni.Storage->open("/path/file.pcap", FILE_APPEND)    returns fs::File (binary append)
    Uni.Storage->listDir("/path", out, max)              returns uint8_t count

    Use fs::File (not File) when SD.h is not included — fs::File is available from <FS.h> alone.
    StorageSD::writeFile does NOT auto-create parent dirs — call makeDir() first.
    StorageLFS::writeFile calls _makeDir() internally, so parent creation is automatic.

### ScrollListView

    ScrollListView view;
    ScrollListView::Row rows[N] = { {"Label", "value"} };
    view.setRows(rows, N);
    view.render(bodyX(), bodyY(), bodyW(), bodyH());
    view.onNav(dir);   // DIR_UP/DIR_DOWN, returns true if consumed

    Store rows[] as a class member so values persist between renders.

### LogView

    LogView log;
    log.addLine("Scanning 192.168.1.1...");   // auto-scrolls when full (30 lines max)
    log.draw(bodyX(), bodyY(), bodyW(), bodyH());
    log.draw(bodyX(), bodyY(), bodyW(), bodyH(), statusCallback, userData);  // with status bar
    log.clear();
    log.count();

    Reusable scrolling log buffer with TFT_eSprite rendering.
    Optional StatusBarCallback draws a custom status bar at the bottom.
    Use for scanning/progress screens instead of raw _logLines[] arrays.

### ProgressView

    ProgressView::show("Message", pct)   non-blocking progress bar (0-100%), auto word-wrap, max 2 lines

### IScreen Power Inhibit

    bool inhibitPowerSave() override { return true; }   // keep display always on (no screen-off, no power-off)
    bool inhibitPowerOff()  override { return true; }   // allow screen-off but block auto power-off

    Override in screen .h files.

    inhibitPowerOff:  use when background process must keep running but screen can turn off
                      (WiFi attacks, GPS wardrive, EAPOL capture, scanning, BLE spam, etc.)
    inhibitPowerSave: use ONLY when screen must stay visible — games during play, MJPEG streaming,
                      real-time visual displays (packet monitor, chat). Implies inhibitPowerOff.

    When display is off (Uni.lcdOff == true):
    - onUpdate() still runs — background logic (GPS, EAPOL capture, Karma, etc.) keeps working
    - render() is blocked by BaseScreen guard — no wasted sprite draws
    - On wake-up, main.cpp forces render() to restore the display immediately

### Config System

    Config.load(Uni.Storage)              load from /unigeek/config (call in setup() after storage init)
    Config.save(Uni.Storage)              write to /unigeek/config (call after any set())
    Config.get("key", "default")          returns String value
    Config.set("key", "value")            sets in-memory value (must call save() to persist)
    Config.getThemeColor()                maps APP_CONFIG_PRIMARY_COLOR name → uint16_t TFT color

    All config keys are #defined in ConfigManager.h as APP_CONFIG_* / APP_CONFIG_*_DEFAULT pairs.

### Long-press Pattern for ListScreen Subclasses

    1. Add heldDuration() check in onUpdate() override BEFORE calling ListScreen::onUpdate()
    2. Set a _holdFired flag on trigger to suppress the release event
    3. On next wasPressed(), consume it + readDirection(), clear flag, return

### Migration from Evil-M5Project

    Reference: ../Evil-M5Project/Evil-Cardputer*.ino
    When asked to "learn from evil m5", read the relevant Evil-Cardputer .ino files for reference.

    Not yet ported:
      - Karma Attack         → WiFi (rogue AP that responds to probe requests)
      - Full Network Analysis → WiFi (ARP scan + port scan on connected network)

### Migration from puteros

    Reference: ../puteros/firmware/src/os/screens/
    Adapt puteros patterns to unigeek conventions:
      - Template::renderHead() / setEntries()  →  title() / setItems()
      - _global->setScreen(new X())            →  Screen.setScreen(new X())
      - InputTextScreen::popup()               →  InputTextAction::popup()
      - Template::renderStatus()               →  ShowStatusAction::show()
      - Template::renderQRCode()               →  ShowQRCodeAction::show()
      - onEnter(entry) / entry.label           →  onItemSelected(index) / switch(index)
      - Always qualify DIR_* as INavigation::DIR_BACK etc.
      - Always read both .h and .cpp from puteros before migrating

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

---

## Crediting References

When porting or referencing features from another repository, update the "Thanks To"
section in README.md with the specific features taken from that repo.
Format: list the repo link and author, then sub-bullets for each feature referenced.

    - [RepoName](url) by author
      - Feature A
      - Feature B

---

## Reference Libraries (not used in build, for hardware reference only)

    ../M5Unified     M5Stack hardware reference — speaker, display, power, board configs
    ../LilyGoLib     LilyGO T-Lora Pager hardware reference

---

## Library Dependencies

    Bodmer/TFT_eSPI@^2.5.43               all boards
    adafruit/Adafruit TCA8418@^1.0.0      T-Lora Pager + m5_cardputer_adv
    mathertel/RotaryEncoder@^1.5.3        T-Lora Pager only
    lewisxhe/XPowersLib@^0.2.0            T-Lora Pager only
    m5stack/M5Hat-Mini-EncoderC            M5StickC Plus 2 (rotary encoder hat)
    ESP32 LEDC (built-in)                 used for PWM speaker tone on Cardputer boards

---

## Known Constraints

- static constexpr const char*[] arrays as class members cause linker errors
  Fix: define them as static constexpr locals inside methods
- TFT_eSPI cannot render unicode characters — only ASCII printable chars work
  Fix: use ASCII equivalents (>, v, x, etc.) in all drawString calls
- IRAM_ATTR inline functions cause Xtensa literal pool errors — always put in .cpp
- TFT_eSprite leaks heap if deleteSprite() is never called
- powerOff() only works when USB is disconnected (hardware limitation)
- LittleFS uses SPIFFS subtype in partitions.csv, use LittleFS.begin() not SPIFFS.begin()
- Uni.Storage may be reassigned at runtime by _checkStorageFallback() in main.cpp when SD fails
- T-Lora Pager SPI (HSPI, pins SCK=35/MISO=33/MOSI=34) must be explicitly passed to SD.begin()
- M5 Cardputer SPI: TFT uses HSPI internally, SD uses FSPI with explicit pins (SCK=40/MISO=39/MOSI=14/CS=12)
- M5 Cardputer ADV: TCA8418 on Wire1 (SDA=8/SCL=9/addr=0x34); requires delay(100) after Wire1.begin()
- Cardputer keyboard _waitRelease: after getKey(), blocks until all non-modifier GPIO inputs released
- ListItem struct: single-field init {"Label"} zero-initializes sublabel to nullptr
- ListScreen uses TFT_eSprite for body rendering — eliminates flicker
- ListScreen navigation wraps around: UP at 0 → last, DOWN at last → 0
- Sublabel pointers must point into class member Strings, not temporaries
  Pattern: _nameSub = Config.get(...); _items[0].sublabel = _nameSub.c_str();
- Config.load() must be called after _checkStorageFallback() in setup()
- setup() must apply saved volume after Config.load(): if (Uni.Speaker) Uni.Speaker->setVolume(...)
- SpeakerI2S channel_format must be I2S_CHANNEL_FMT_ALL_RIGHT — sends mono to both L+R slots
- Cardputer boards use I2S_NUM_1 — define SPK_I2S_PORT I2S_NUM_1 in pins_arduino.h
- Cardputer ADV ES8311 codec on Wire1 (addr=0x18) must be initialized before any sound
- Brightness LEDC must use Arduino core 2.x API: ledcSetup/ledcAttachPin/ledcWrite (not ledcAttach)
- M5StickC Plus I2C: Wire1 for AXP192 + BM8563 (INTERNAL_SDA=21/INTERNAL_SCL=22)
  Wire1.begin() in createInstance(), define RTC_WIRE Wire1 in pins_arduino.h
- WiFi attack mode must be WIFI_MODE_APSTA — WifiAttackUtil handles this automatically
  Never call esp_wifi_set_channel() directly — use attacker->setChannel()
- ListScreen DIR_BACK with empty list: check DIR_BACK first, before _effectiveCount() == 0 guard
  onRender() must always push the sprite even when empty — clears lingering overlays
- sdcard/manifest/sdcard.txt must be updated when files are added or removed from sdcard/
- sdcard/manifest/ir/categories.txt lists IR categories (folder|Display Name format)
- sdcard/manifest/ir/cat_{folder}.txt lists files per IR category (from Flipper-IRDB repo)
- M5StickC Plus 2 power: GPIO 4 PWR_HOLD_PIN must stay HIGH to keep device powered

---

## Self-Updating This Document

This document should stay accurate as the project evolves.
When making changes that affect architecture, conventions, or patterns,
Claude CLI must update CLAUDE.md and AGENT.md when needed — do not wait for user to ask.

Triggers that require a CLAUDE.md or AGENT.md update:

- Adding a new board
- Adding a new core interface (IStorage, IDisplay, etc.)
- Adding a new UI template or action pattern
- Changing Device constructor signature
- Changing ScreenManager behavior
- Changing navigation direction values
- Adding new build flags
- Adding new library dependencies
- Changing file placement conventions
- Changing coding conventions
- Porting or referencing features from another repository

How to propose an update:
1. Complete the code change first
2. Show a diff of what would change in CLAUDE.md or AGENT.md
3. State why the update is needed
4. Wait for explicit approval before writing to the file

Never silently update CLAUDE.md or AGENT.md as a side effect of another task.