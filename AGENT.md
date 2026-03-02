# AGENT.md
# Firmware Project - Agentic AI Instructions

## What This Project Is

Multi-device ESP32 firmware. Four target boards share one codebase:
M5StickC Plus 1.1, T-Lora Pager, M5 Cardputer, M5 Cardputer ADV.
All hardware differences are isolated. Do not break this isolation.

---

## Reference Libraries

    ../M5Unified    M5Stack hardware reference (not in build)
    ../LilyGoLib    LilyGO T-Lora Pager hardware reference (not in build)

When implementing board-specific hardware features, check these FIRST:
- M5Unified/src/M5Unified.cpp     — speaker codec init, pin assignments, I2S config per board
- LilyGoLib                       — T-Lora Pager peripheral init

---

## Before Making Any Changes

1. Identify the target — is the change shared or board-specific?
    - All boards      →  change goes in src/
    - Board-specific  →  change goes in firmware/boards/<board>/

2. Read CLAUDE.md for conventions before writing any code.

3. Never add Serial.print to production code.

4. Never include pins_arduino.h explicitly — it is auto-included.

---

## Adding a New Screen

1. Create src/screens/<category>/MyScreen.h
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
6. If board has keyboard:
   - Implement Keyboard.h with IKeyboard: begin(), update(), available(), peekKey(), getKey()
   - Add DEVICE_HAS_KEYBOARD to build_flags
   - peekKey() must NOT consume the key — Navigation uses it to avoid stealing text input
   - For GPIO matrix boards: add _waitRelease debounce — set true in getKey(), block in update() until all inputs low
7. If board has speaker:
   - Define `SPK_BCLK`, `SPK_WCLK`, `SPK_DOUT` in pins_arduino.h
   - Define `SPK_I2S_PORT` (I2S_NUM_0 or I2S_NUM_1) in pins_arduino.h — Cardputer boards use I2S_NUM_1
   - Add DEVICE_HAS_SOUND to build_flags
   - Instantiate SpeakerI2S (from src/core/SpeakerI2S.h) in Device.cpp, pass to Device constructor as `sound` param
   - beep() checks APP_CONFIG_NAV_SOUND before playing
   - Always guard Uni.Speaker calls with nullptr check: if (Uni.Speaker) Uni.Speaker->beep()
8. If board has SD card: create SPIClass with correct bus (HSPI or FSPI), call .begin() with explicit pins,
   pass to SD.begin(csPin, spi). Never use default SPI bus.
9. Always init StorageLFS — all boards have a SPIFFS/LittleFS partition

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

Always use Uni.Storage for file operations unless specifically needing SD or LittleFS:

    if (Uni.Storage && Uni.Storage->isAvailable()) {
      Uni.Storage->writeFile("/path/file.txt", content);
    }

Use Uni.StorageSD or Uni.StorageLFS only when the feature explicitly requires one or the other.
Always null-check before using — Uni.StorageSD is nullptr on M5StickC.

---

## Common Mistakes to Avoid

- Do NOT put IRAM_ATTR functions inline in .h files — put in .cpp
- Do NOT declare static constexpr const char*[] as class members — define inside methods
- Do NOT call setItems({}) — use clearItems() instead
- Do NOT use string comparison in onItemSelected — use index switch
- Do NOT forget deleteSprite() after every createSprite() + pushSprite()
- Do NOT modify Device.h constructor without updating ALL board Device.cpp files
- Do NOT add SD-specific logic outside StorageSD.h — use Uni.Storage interface
- Do NOT use init() in screens — use onInit()
- Do NOT include pins_arduino.h — it is auto-included by the build system
- Do NOT call Keyboard->update() inside NavigationImpl — Device::update() does this; double-scan causes conflicts
- Do NOT use getKey() in NavigationImpl unconditionally — use peekKey() first; only consume nav keys (;/./ \n)
- Do NOT omit backspace handling inside action overlay _run() loops — unhandled \b freezes the overlay
- Do NOT call lcd.fillRect() before pushing a sprite — the sprite push already overwrites the area (causes flash)
- Do NOT call Uni.Speaker directly — always null-check: if (Uni.Speaker) Uni.Speaker->beep()
- Do NOT play sound in production if DEVICE_HAS_SOUND is not defined — guard at compile time too

---

## Action Overlay Pattern

All actions are blocking static calls that return a value:

    String      result = InputTextAction::popup("Title");
    int         result = InputNumberAction::popup("Title", min, max, default);
    const char* result = InputSelectAction::popup("Title", opts, count, default);
    void               ShowStatusAction::show("Message", durationMs);

For ShowStatusAction:
- durationMs = 0   block until button/key press, then wipe
- durationMs > 0   block for that duration then wipe

All actions wipe their overlay area on close.

---

## Navigation Direction Values

    DIR_UP      encoder up / BTN_A on M5StickC / ; key on Cardputer
    DIR_DOWN    encoder down / BTN_B on M5StickC / . key on Cardputer
    DIR_PRESS   encoder press / power button on M5StickC / ENTER key on Cardputer
    DIR_NONE    no event — wasPressed() returns false

---

## Device Constructor Signature

    Device(IDisplay& lcd, IPower& power, INavigation* nav,
           IKeyboard* keyboard  = nullptr,
           IStorage*  storageSD = nullptr,
           IStorage*  storageLFS = nullptr,
           SPIClass*  spi = nullptr,
           ISpeaker*  sound = nullptr)

Storage primary is decided inside constructor automatically:
- SD if storageSD is not null and isAvailable()
- else LittleFS

---

## InputSelectAction Options Pattern

    static constexpr InputSelectAction::Option opts[] = {
      {"Label A", "value_a"},
      {"Label B", "value_b"},
    };
    const char* result = InputSelectAction::popup("Choose", opts, 2);
    if (result == nullptr) { /* cancelled */ }

---

## ListScreen setItems Overloads

    setItems(ListItem (&arr)[N])           fixed size array, templated
    setItems(ListItem* arr, uint8_t count) dynamic size array

    setItems() resets _selectedIndex and _scrollOffset to 0 and calls render().
    Call render() directly (not setItems) when you want to redraw without resetting selection.

### Sublabel Pattern

    Sublabel pointers must point into class member Strings — NOT temporaries or local variables.
    If a temporary String goes out of scope, the const char* becomes a dangling pointer.

    // CORRECT — _nameSub is a class member String
    _nameSub = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
    _items[0].sublabel = _nameSub.c_str();

    // WRONG — temporary String destroyed immediately
    _items[0].sublabel = Config.get(...).c_str();  // dangling pointer

---

## Config System

    Config.load(Uni.Storage)    — call in setup() after _checkStorageFallback()
    Config.save(Uni.Storage)    — call after every Config.set() to persist
    Config.get(KEY, DEFAULT)    — use #define constants, not raw strings
    Config.set(KEY, value)      — in-memory only until save() is called

    All keys are #defined in ConfigManager.h as APP_CONFIG_* / APP_CONFIG_*_DEFAULT pairs.
    Never use raw string literals for config keys — always use the #defines.
    File stored at: /unigeek/config (key=value, one per line)

    StorageLFS auto-creates parent dirs in writeFile(). StorageSD does NOT.
    ConfigManager.save() calls makeDir("/unigeek") before writeFile() to handle both cases.

---

## Power Saving

    Power saving runs in main.cpp loop() using two static locals:
      static bool          _lcdOff    = false;
      static unsigned long _lastActive = millis();

    Activity is detected non-destructively:
      Uni.Nav->isPressed()              — true while any nav button held (all boards)
      Uni.Keyboard->available()         — true when key buffered (keyboard boards only, #ifdef guarded)

    On idle > APP_CONFIG_INTERVAL_DISPLAY_OFF: setBrightness(0), set _lcdOff = true
    On activity while _lcdOff:  restore brightness, consume wake-up key/nav, set _lcdOff = false
    On idle > display_off + APP_CONFIG_INTERVAL_POWER_OFF: Uni.Power.powerOff()
    Screen.update() is skipped while _lcdOff — no background input processing while dark

    Only active when APP_CONFIG_ENABLE_POWER_SAVING == "1" in config.

---

## Build src_filter for Board .cpp Files

If adding .cpp files in a board folder, ensure boards.ini includes:

    build_src_filter = +<../boards/t-lora-pager/>

Otherwise .cpp files will not be compiled.

---

## Keeping Documentation Accurate

When completing a task that changes architecture, patterns, or conventions,
update CLAUDE.md and AGENT.md immediately — do not wait for user to ask.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change.