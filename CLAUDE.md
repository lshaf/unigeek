# CLAUDE.md
# Firmware Project - Claude AI Instructions

## Project Overview

ESP32 multi-device firmware supporting M5StickC Plus 1.1, T-Lora Pager, M5 Cardputer, and M5 Cardputer ADV.
Built with PlatformIO + Arduino framework + TFT_eSPI.
All hardware differences are isolated in board-specific folders.

---

## Build System

### PlatformIO Commands

    pio run -e m5stickcplus_11
    pio run -e t_lora_pager
    pio run -e m5_cardputer
    pio run -e m5_cardputer_adv
    pio run -e m5stickcplus_11 -t upload
    pio run -e t_lora_pager -t upload
    pio run -e m5_cardputer -t upload
    pio run -e m5_cardputer_adv -t upload
    pio device monitor
    pio run -t clean

### Board Environments

    m5stickcplus_11   M5StickC Plus 1.1, no keyboard, AXP192 power, LittleFS only
    t_lora_pager      LilyGO T-Lora Pager, TCA8418 keyboard, BQ25896/BQ27220 power, SD + LittleFS
    m5_cardputer      M5Stack Cardputer, 74HC138 GPIO matrix keyboard, ADC battery, SD + LittleFS
    m5_cardputer_adv  M5Stack Cardputer ADV, TCA8418 keyboard via Wire1, ADC battery, SD + LittleFS

### Build Flags (board-specific)

    DEVICE_HAS_KEYBOARD       defined for T-Lora Pager, m5_cardputer, m5_cardputer_adv — enables keyboard input paths
    DEVICE_HAS_SOUND          defined for boards with a speaker — enables Uni.Speaker and audio paths
    DEVICE_HAS_VOLUME_CONTROL defined for boards with real volume control (I2S amp) — shows Volume in Settings
                              NOT defined for buzzer boards (M5StickC Plus) where setVolume() is a no-op
    APP_MENU_POWER_OFF        defined for T-Lora Pager and M5StickC Plus 1.1, adds Power Off in main menu
    DEVICE_HAS_NAV_MODE_SWITCH defined for M5StickC Plus 1.1 — enables nav mode setting (Default vs Encoder)
                              BTN_A short press = back in encoder mode; hold 3s resets to default (main.cpp safety)

---

## Project Structure

    firmware/
    ├── boards/
    │   ├── _devices/               custom board JSON definitions
    │   ├── m5stickplus_11/         M5StickC board implementation
    │   │   ├── boards.ini
    │   │   ├── Device.cpp          createInstance() with AXP192, LittleFS
    │   │   ├── Display.h
    │   │   ├── Navigation.h
    │   │   ├── Power.h
    │   │   └── pins_arduino.h
    │   ├── t_lora_pager/           T-Lora Pager board implementation
    │   │   ├── boards.ini
    │   │   ├── Device.cpp          createInstance() with SD + LittleFS, keyboard
    │   │   ├── Display.h
    │   │   ├── Navigation.h        rotary encoder, GPIO0 long-press power-off
    │   │   ├── Navigation.cpp      ISR in .cpp (IRAM_ATTR requirement)
    │   │   ├── Keyboard.h          TCA8418 3-layer keymap (fn/shift/caps)
    │   │   ├── Keyboard.cpp        ISR in .cpp
    │   │   ├── Power.h             BQ25896/BQ27220 via XPowersLib
    │   │   └── pins_arduino.h
    │   ├── m5_cardputer/           M5 Cardputer board implementation
    │   │   ├── config.ini
    │   │   ├── Device.cpp          createInstance() with FSPI SD + LittleFS, keyboard
    │   │   ├── Display.h
    │   │   ├── Navigation.h        keyboard-driven (;=UP .=DOWN ENTER=PRESS)
    │   │   ├── Keyboard.h          74HC138 GPIO matrix, 2-pass shift scan, _waitRelease debounce
    │   │   ├── Power.h             ADC pin 10 battery, deep sleep power-off
    │   │   └── pins_arduino.h
    │   └── m5_cardputer_adv/       M5 Cardputer ADV board implementation
    │       ├── config.ini
    │       ├── Device.cpp          createInstance() with FSPI SD + LittleFS, keyboard
    │       ├── Display.h
    │       ├── Navigation.h        keyboard-driven (;=UP .=DOWN ENTER=PRESS)
    │       ├── Keyboard.h          TCA8418 via Wire1, mapRawKeyToPhysical, shift toggle
    │       ├── Power.h             ADC pin 10 battery, deep sleep power-off
    │       └── pins_arduino.h
    ├── src/
    │   ├── core/
    │   │   ├── Device.h            Device singleton, Uni macro
    │   │   ├── IDisplay.h
    │   │   ├── INavigation.h
    │   │   ├── IPower.h
    │   │   ├── IKeyboard.h
    │   │   ├── ISpeaker.h
    │   │   ├── SpeakerI2S.h        raw I2S speaker implementation (shared, used by Cardputer boards)
    │   │   ├── sounds/             PROGMEM WAV arrays (SoundNotification.h, SoundWin.h, SoundLose.h)
    │   │   ├── IStorage.h
    │   │   ├── StorageSD.h         SD implementation (shared, used by both boards)
    │   │   ├── StorageLFS.h        LittleFS implementation (shared, used by both boards)
    │   │   ├── ScreenManager.h
    │   │   └── ConfigManager.h     key=value persistence, #define APP_CONFIG_* keys
    │   ├── screens/
    │   │   ├── MainMenuScreen.h
    │   │   ├── MainMenuScreen.cpp
    │   │   ├── SettingScreen.h
    │   │   ├── SettingScreen.cpp   9 settings mirroring puteros, uses APP_CONFIG_* defines
    │   │   └── wifi/
    │   │       ├── WifiMenuScreen.h
    │   │       ├── WifiMenuScreen.cpp
    │   │       └── network/
    │   │           ├── NetworkMenuScreen.h
    │   │           ├── NetworkMenuScreen.cpp
    │   │           ├── WorldClockScreen.h
    │   │           └── WorldClockScreen.cpp
    │   ├── ui/
    │   │   ├── templates/
    │   │   │   ├── BaseScreen.h        header + StatusBar
    │   │   │   └── ListScreen.h        22px items, index-based selection
    │   │   ├── components/
    │   │   │   └── ScrollListView.h    scrollable label-value rows, reusable in any screen
    │   │   └── actions/
    │   │       ├── InputTextAction.h   text input overlay
    │   │       ├── InputNumberAction.h number input overlay
    │   │       ├── InputSelectAction.h select list overlay
    │   │       └── ShowStatusAction.h  status message overlay, auto word-wraps long text
    │   └── main.cpp

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

### ISpeaker Interface

    Uni.Speaker->tone(freq, durationMs)        play square-wave tone at Hz for ms (FreeRTOS task)
    Uni.Speaker->noTone()                      stop any playing tone
    Uni.Speaker->setVolume(0-100)              set volume percentage (persisted via APP_CONFIG_VOLUME)
    Uni.Speaker->beep()                        short nav feedback beep (respects APP_CONFIG_NAV_SOUND)
    Uni.Speaker->playRandomTone(shift, ms)     random note from C major scale, optional semitone shift
    Uni.Speaker->playWav(data, size)           play PROGMEM WAV (8/16-bit, mono/stereo, any sample rate)
    Uni.Speaker->playNotification()            play SoundNotification.h WAV
    Uni.Speaker->playWin()                     play SoundWin.h WAV
    Uni.Speaker->playLose()                    play SoundLose.h WAV
    Uni.Speaker->playCorrectAnswer()           tone sequence: 523Hz→784Hz
    Uni.Speaker->playWrongAnswer()             tone sequence: 1109Hz×2

    Guard all speaker calls:
      if (Uni.Speaker) Uni.Speaker->beep();

    Board pin requirements in pins_arduino.h:
      SPK_BCLK, SPK_WCLK, SPK_DOUT   — I2S pins
      SPK_I2S_PORT                    — I2S_NUM_0 for all current boards (TFT_eSPI has no I2S conflict)

    Build flag: DEVICE_HAS_SOUND — defined on boards that have a speaker

    Speaker implementations (board-specific):
      SpeakerI2S    — Cardputer / Cardputer ADV (I2S, I2S_NUM_1, WAV capable)
      SpeakerADV    — Cardputer ADV only (extends SpeakerI2S, adds ES8311 codec init)
      SpeakerBuzzer — M5StickC Plus 1.1 (LEDC PWM on GPIO 2, no WAV, tone sequences only)

### IKeyboard Interface

    Uni.Keyboard->available()   true when a key is buffered and ready
    Uni.Keyboard->peekKey()     read buffered key WITHOUT consuming — used by NavigationImpl
                                to avoid stealing text input keys
    Uni.Keyboard->getKey()      consume and return buffered key; sets _waitRelease on GPIO-matrix boards

    NavigationImpl (keyboard boards) uses peekKey() to check for nav keys (;/./ \n).
    Only calls getKey() when the key is a nav key, leaving all other keys for action overlays.

### Device Update Order

    Device::update() always calls Keyboard->update() before Nav->update().
    This ensures a single keyboard scan per frame — Nav reads the already-updated state.
    Never call Keyboard->update() inside NavigationImpl; it causes double-scan conflicts.

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
      bool hasBackItem() override { return false; }  // omit "< Back" on root screens

    private:
      ListItem _items[2] = {
        {"Item A"},
        {"Item B", "sublabel"},
      };
    };

### Back Navigation

    - ListScreen provides virtual onBack() — called on:
        - backspace key (DEVICE_HAS_KEYBOARD boards)
        - BTN_A short press when encoder nav is active (DEVICE_HAS_NAV_MODE_SWITCH boards)
        - user selects the auto-appended "< Back" item (no-keyboard, non-encoder devices)
    - hasBackItem() controls whether "< Back" appears — default true, override false for root screens
    - "< Back" is automatically hidden when encoder nav is active (BTN_A acts as back instead)
    - setItems() calls render() (full chrome + body redraw)
    - Implement onBack() in a .cpp file when it needs to instantiate a parent screen:

      // MyScreen.h
      void onBack() override;

      // MyScreen.cpp
      #include "MyScreen.h"
      #include "screens/ParentScreen.h"
      void MyScreen::onBack() { Screen.setScreen(new ParentScreen()); }

    - This pattern breaks circular includes (child .h never includes parent .h)

### ScreenManager

    Screen.setScreen(new MyScreen())   deletes old screen, deferred to next update()

### Navigation

    Uni.Nav->wasPressed()        true once per press/direction event
    Uni.Nav->readDirection()     consumes and returns direction
    DIR_UP / DIR_DOWN / DIR_PRESS

### Action Overlays (all blocking)

    String      InputTextAction::popup("Title")
    String      InputTextAction::popup("Title", "default")
    int         InputNumberAction::popup("Title")
    int         InputNumberAction::popup("Title", 0, 100, 50)
    const char* InputSelectAction::popup("Title", opts, count)
    const char* InputSelectAction::popup("Title", opts, count, "default_value")
    void        ShowStatusAction::show("Message")           wait for button/key press, then wipe  (default, durationMs=-1)
    void        ShowStatusAction::show("Message", 0)        show and return immediately, no wipe
    void        ShowStatusAction::show("Message", 1500)     show, block ms, then wipe
    // Long messages are automatically word-wrapped (max 5 lines, box grows to fit)
    // InputSelectAction: backspace (DEVICE_HAS_KEYBOARD) dismisses overlay and returns nullptr

### Storage

    Uni.Storage->isAvailable()
    Uni.Storage->exists("/path/file.txt")
    Uni.Storage->readFile("/path/file.txt")              returns String
    Uni.Storage->writeFile("/path/file.txt", "content")  returns bool
    Uni.Storage->makeDir("/path")
    Uni.Storage->deleteFile("/path/file.txt")

### ScrollListView

    ScrollListView view;
    ScrollListView::Row rows[N] = {
      {"Label", "value"},
    };
    view.setRows(rows, N);
    view.render(bodyX(), bodyY(), bodyW(), bodyH());  // call once to draw
    view.onNav(dir);                                  // call in onUpdate() with DIR_UP/DIR_DOWN

    - Renders label (left, grey) + value (right, white) rows within the given bounding box
    - Shows a scrollbar when content overflows the box height
    - onNav() returns true if the direction was consumed (scrolled), false if at boundary
    - Row.value is a String — safe for dynamic content (WiFi info, etc.)
    - Store rows[] as a class member so values persist between renders
    - If remaining height after last full row is ≥ 5px, one additional partial row is rendered (clipped by viewport)

### Config System

    Config.load(Uni.Storage)              load from /unigeek/config (call in setup() after storage init)
    Config.save(Uni.Storage)              write to /unigeek/config (call after any set())
    Config.get("key", "default")          returns String value
    Config.set("key", "value")            sets in-memory value (must call save() to persist)
    Config.getThemeColor()                maps APP_CONFIG_PRIMARY_COLOR name → uint16_t TFT color

    All config keys are defined as #defines in ConfigManager.h:
      APP_CONFIG_DEVICE_NAME / APP_CONFIG_DEVICE_NAME_DEFAULT
      APP_CONFIG_ENABLE_POWER_SAVING / APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT
      APP_CONFIG_INTERVAL_DISPLAY_OFF / APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT
      APP_CONFIG_INTERVAL_POWER_OFF / APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT
      APP_CONFIG_BRIGHTNESS / APP_CONFIG_BRIGHTNESS_DEFAULT
      APP_CONFIG_VOLUME / APP_CONFIG_VOLUME_DEFAULT
      APP_CONFIG_NAV_SOUND / APP_CONFIG_NAV_SOUND_DEFAULT
      APP_CONFIG_PRIMARY_COLOR / APP_CONFIG_PRIMARY_COLOR_DEFAULT
      APP_CONFIG_NAV_MODE / APP_CONFIG_NAV_MODE_DEFAULT   ("default" or "encoder", M5StickC only)

    File path: /unigeek/config (key=value format, one per line)
    StorageLFS::writeFile auto-creates parent dirs; StorageSD does not — call makeDir("/unigeek") first.

### Power Saving

    Implemented in main.cpp loop(). Uses static locals _lcdOff and _lastActive.
    Activity sources: Uni.Nav->isPressed() (non-consuming, all boards)
                    + Uni.Keyboard->available() (non-consuming, keyboard boards only)
    Display off:  setBrightness(0) after APP_CONFIG_INTERVAL_DISPLAY_OFF seconds idle
    Power off:    Uni.Power.powerOff() after display_off + APP_CONFIG_INTERVAL_POWER_OFF seconds idle
    Wake:         restores brightness; consumes the wake-up key/nav event so screen doesn't act on it
    Screen.update() is skipped entirely while display is off (no background input processing)

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
- Screen .h files: declarations and data members only
  Trivial one-liners (title(), hasBackItem()) may stay inline in .h
  All onInit(), onUpdate(), onItemSelected(), onBack(), and private helper bodies go in .cpp
  Only include headers needed for types used directly in the .h (base class, member variable types)
  Action headers (InputTextAction.h, ShowStatusAction.h, etc.) belong in .cpp, not .h

---

## Board-Specific Isolation Rules

- All hardware differences go in firmware/boards/<board>/
- Shared storage logic lives in src/core/StorageSD.h and src/core/StorageLFS.h
- Board Device.cpp only decides which storage to init and pass to constructor
- Use DEVICE_HAS_KEYBOARD for keyboard-conditional code
- Use DEVICE_HAS_SOUND for speaker-conditional code
- Use APP_MENU_POWER_OFF for power-off menu item

---

## Reference Libraries (not used in build, for hardware reference only)

    ../M5Unified     M5Stack hardware reference — speaker, display, power, board configs
    ../LilyGoLib     LilyGO T-Lora Pager hardware reference

    When implementing board-specific features, always check these first:
    - M5Unified/src/M5Unified.cpp   board speaker/mic/button configs, codec init sequences
    - LilyGoLib                     T-Lora Pager peripheral initialization

---

## Library Dependencies

    Bodmer/TFT_eSPI@^2.5.43               all boards
    adafruit/Adafruit TCA8418@^1.0.0      T-Lora Pager + m5_cardputer_adv
    mathertel/RotaryEncoder@^1.5.3        T-Lora Pager only
    lewisxhe/XPowersLib@^0.2.0            T-Lora Pager only
    ESP32 LEDC (built-in)                 used for PWM speaker tone on Cardputer boards

---

## Known Constraints

- static constexpr const char*[] arrays as class members cause linker errors
  Fix: define them as static constexpr locals inside methods
- IRAM_ATTR inline functions cause Xtensa literal pool errors
  Fix: always put them in .cpp files
- TFT_eSprite leaks heap if deleteSprite() is never called
- powerOff() only works when USB is disconnected (hardware limitation)
- LittleFS uses SPIFFS subtype in partitions.csv, use LittleFS.begin() not SPIFFS.begin()
- LittleFS/SD makeDir() must create directories recursively — LittleFS.mkdir() fails if parent dirs missing
- StorageSD self-reports failure: _available set false on writeFile() open() failure, detectable via isAvailable()
  readFile() does NOT set _available=false — file-not-found (first boot) is not an SD failure
- Uni.Storage may be reassigned at runtime by _checkStorageFallback() in main.cpp when SD fails
- T-Lora Pager SPI (HSPI, pins SCK=35/MISO=33/MOSI=34) must be explicitly passed to SD.begin()
  SD.begin(csPin) alone uses wrong default VSPI bus and causes sdSelectCard() failures
- M5 Cardputer SPI: TFT uses HSPI internally (TFT_MOSI=35/TFT_SCLK=36), SD uses FSPI with explicit pins
  (SCK=40/MISO=39/MOSI=14/CS=12). Must create SPIClass sdSpi(FSPI) and call sdSpi.begin() before SD.begin()
- M5 Cardputer ADV: TCA8418 on Wire1 (SDA=8/SCL=9/addr=0x34); requires delay(100) after Wire1.begin()
  and enableInterrupts() after matrix(); guard update() with _ready flag in case begin() failed
- M5 Cardputer keyboard _waitRelease: after getKey() consumes a key, update() blocks until all GPIO inputs
  are released to prevent the same physical keypress registering multiple times
- peekKey() / getKey() pattern: NavigationImpl always peeks first; only calls getKey() for nav keys (;/./ \n)
  All other keys remain in the buffer for action overlays (InputTextAction, InputNumberAction, etc.)
- ListItem struct has no default for sublabel — single-field init {"Label"} zero-initializes sublabel to nullptr
- ListScreen uses TFT_eSprite for body rendering — eliminates flicker on highlight change
  (full body rendered to sprite then pushed in one operation, no visible clear-then-draw flash)
- ListScreen navigation wraps around: UP at index 0 goes to last item, DOWN at last item goes to 0
- StatusBar uses TFT_eSprite (WIDTH × lcd.height()) — all slots rendered offscreen then pushed once to avoid flicker
- StatusBar slots are 20×20px boxes centered in the 32px sidebar, spaced at 6px start + 24px per slot
- StorageSD::writeFile does NOT auto-create parent directories — call makeDir("/dir") before writeFile()
  StorageLFS::writeFile calls _makeDir() internally, so parent creation is automatic
- SettingScreen sublabels are stored as class member Strings (_nameSub, _brightSub, etc.)
  Sublabel const char* pointers must point into class member Strings, not temporaries
  Pattern: _nameSub = Config.get(...); _items[0].sublabel = _nameSub.c_str();
- Config.load() must be called after _checkStorageFallback() in setup() so correct storage is used
- setup() must apply saved volume after Config.load(): `if (Uni.Speaker) Uni.Speaker->setVolume(Config.get(...))`
  Speaker->begin() runs before Config.load() so volume would default to compile-time value otherwise
- Power saving in main.cpp reads config on every loop() — acceptable since map lookup is in-memory
- SpeakerI2S channel_format must be I2S_CHANNEL_FMT_ALL_RIGHT — sends mono to both L+R slots
  I2S_CHANNEL_FMT_ONLY_LEFT produces silence on amps wired to the right channel (Cardputer NS4168)
- Cardputer/Cardputer ADV I2S speaker uses I2S_NUM_1 — confirmed by M5Unified source
  SpeakerI2S.h defaults to I2S_NUM_0; define SPK_I2S_PORT I2S_NUM_1 in pins_arduino.h for Cardputer boards
- Cardputer ADV has an ES8311 audio codec on Wire1 (SDA=8/SCL=9, addr=0x18) that must be initialized
  before any sound. Uses SpeakerADV (board/m5_cardputer_adv/core/Speaker.h) which extends SpeakerI2S
  and sends 8 I2C register writes in begin(). Wire1 is ready because Keyboard::begin() runs first.
- ESP32-S3 HSPI warning (`spiAttachMISO: HSPI Does not have default pins`) is harmless when TFT_MISO=-1
- Brightness LEDC must use Arduino core 2.x API: ledcSetup(ch, freq, bits) + ledcAttachPin(pin, ch) + ledcWrite(ch, duty)
  ledcAttach(pin, freq, bits) is core 3.x only — will not compile on ESP32 Arduino 2.x

---

## Partitions

M5StickC Plus:
nvs      24KB
app0     3.6MB   OTA
spiffs   384KB   LittleFS

T-Lora Pager:
nvs       24KB
app0      4.6MB   OTA
spiffs    11.4MB  LittleFS
coredump  64KB

M5 Cardputer / M5 Cardputer ADV (partitions_8mb.csv):
nvs      24KB
app0     4MB     OTA
spiffs   3.9MB   LittleFS

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

How to propose an update:
1. Complete the code change first
2. Show a diff of what would change in CLAUDE.md or AGENT.md
3. State why the update is needed
4. Wait for explicit approval before writing to the file

Never silently update CLAUDE.md or AGENT.md as a side effect of another task.