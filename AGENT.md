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
   - Implement Keyboard.h with IKeyboard: begin(), update(), available(), peekKey(), getKey(), modifiers()
   - Add DEVICE_HAS_KEYBOARD to build_flags
   - peekKey() must NOT consume the key — NavigationImpl peeks first, only consumes nav keys (;  .  \n  \b  ,  /)
   - modifiers() returns a bitmask of IKeyboard::Modifier flags (MOD_SHIFT, MOD_FN, MOD_CTRL, MOD_ALT, MOD_OPT, MOD_CAPS)
     Track modifier state from press/release events (TCA8418) or from physical scan (GPIO matrix)
     FN/SHIFT/CTRL/ALT must be hold-based (active while key is physically held), never toggle
   - For GPIO matrix boards: add _waitRelease debounce — set true in getKey(), block in update()
     until all NON-modifier inputs are low (modifier keys must not prevent release detection)
7. If board has speaker:
   - Define `SPK_BCLK`, `SPK_WCLK`, `SPK_DOUT` in pins_arduino.h
   - Define `SPK_I2S_PORT` (I2S_NUM_0 or I2S_NUM_1) in pins_arduino.h — Cardputer boards use I2S_NUM_1
   - Add DEVICE_HAS_SOUND to build_flags
   - Instantiate SpeakerI2S (from src/core/SpeakerI2S.h) in Device.cpp, pass to Device constructor as `sound` param
   - beep() checks APP_CONFIG_NAV_SOUND before playing
   - Always guard Uni.Speaker calls with nullptr check: if (Uni.Speaker) Uni.Speaker->beep()
8. If board has RTC:
   - Define DEVICE_HAS_RTC, RTC_I2C_ADDR, RTC_REG_BASE in pins_arduino.h
   - Define RTC_WIRE only if the board's RTC is not on Wire (the default fallback in RtcManager.h)
     Example: M5StickC uses Wire1 → #define RTC_WIRE Wire1; T-Lora uses Wire → no define needed
   - Call RtcManager::syncSystemFromRtc() in main.cpp setup() after Uni.begin()
   - Call RtcManager::syncRtcFromSystem() after confirmed NTP sync
10. If board has SD card: create SPIClass with correct bus (HSPI or FSPI), call .begin() with explicit pins,
    pass to SD.begin(csPin, spi). Never use default SPI bus.
11. Always init StorageLFS — all boards have a SPIFFS/LittleFS partition
12. Define Device::boardHook() in Device.cpp — empty `void Device::boardHook() {}` if no per-frame board logic needed

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
- Do NOT call setItems() after toggling an option — it resets the highlight index; update sublabels on the array then call render() instead
- Do NOT use string comparison in onItemSelected — use index switch
- Do NOT draw StatusBar or ListScreen body directly to LCD — both use TFT_eSprite to prevent flicker
- Do NOT forget deleteSprite() after every createSprite() + pushSprite()
- Do NOT modify Device.h constructor without updating ALL board Device.cpp files
- Do NOT add SD-specific logic outside StorageSD.h — use Uni.Storage interface
- Do NOT use unqualified File type without SD.h in scope — use fs::File (from <FS.h>) for binary access via Uni.Storage->open()
- Do NOT use unicode characters in TFT drawString calls — TFT_eSPI only renders ASCII; use > instead of →, etc.
- Do NOT stop deauthing an AP based on raw EAPOL frame count — validate hasM1 && hasM2 from Key Information field parsing
- Do NOT use init() in screens — use onInit()
- Do NOT include pins_arduino.h — it is auto-included by the build system
- Do NOT call Keyboard->update() inside NavigationImpl — Device::update() does this; double-scan causes conflicts
- Do NOT use getKey() in NavigationImpl unconditionally — use peekKey() first; only consume nav keys (;  .  \n  \b  ,  /)
- Do NOT read \b directly from Uni.Keyboard in action overlays — NavigationImpl consumes it; overlays receive DIR_BACK via readDirection()
- Do NOT forget to define Device::boardHook() in every board's Device.cpp (empty stub if unused) — linker requires it
- Do NOT call lcd.fillRect() before pushing a sprite — the sprite push already overwrites the area (causes flash)
- Do NOT call Uni.Speaker directly — always null-check: if (Uni.Speaker) Uni.Speaker->beep()
- Do NOT play sound in production if DEVICE_HAS_SOUND is not defined — guard at compile time too
- Do NOT use getLocalTime() to gate NTP-dependent rendering — it returns true if ANY time is set
  (including RTC-restored time). Use sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED instead.
- Do NOT check DIR_BACK after an early-return guard in ListScreen onUpdate() — always check DIR_BACK
  first so back navigation works even when the list is empty (_effectiveCount() == 0)
- Do NOT skip the sprite push in ListScreen onRender() when the list is empty — always push (black fill)
  to clear any overlays (ShowStatusAction, etc.) that may have been drawn over the body area
- Do NOT handle only DIR_BACK in a custom (non-ListScreen) state — M5StickC default nav NEVER emits
  DIR_BACK; always handle both DIR_BACK and DIR_PRESS as "back/stop" in BaseScreen states
- Do NOT put Wire1.begin() inside AXP192::begin() on M5StickC — Device::createInstance() calls
  Wire1.begin(INTERNAL_SDA, INTERNAL_SCL) before axp.begin(); AXP192::begin() only sets clock speed

---

## Action Overlay Pattern

All actions are blocking static calls that return a value:

    String      result = InputTextAction::popup("Title");
    String      result = InputTextAction::popup("Title", "default", true);  // numberMode: digits + dot only
    int         result = InputNumberAction::popup("Title", min, max, default);
    const char* result = InputSelectAction::popup("Title", opts, count, default);
    void               ShowStatusAction::show("Message", durationMs);

For ShowStatusAction:
- durationMs = -1  block until button/key press, then wipe  (default)
- durationMs =  0  show and return immediately, no wipe
- durationMs > 0   block for that duration then wipe

All actions wipe their overlay area on close. They do NOT re-render the screen.
The calling screen is responsible for calling render() after a popup returns — including on cancel.
Exception: if the very next line navigates to a different screen, render() is not needed.

    // Correct pattern:
    String ip = InputTextAction::popup("Target IP", _lastIp, true);
    render();                   // always restore screen after overlay wipe
    if (ip.isEmpty()) return;
    // ... proceed with ip

---

## Navigation Direction Values

    DIR_UP     encoder CW / AXP btn M5StickC-default / ; key Cardputer / encoder CCW T-Lora
    DIR_DOWN   encoder CCW / BTN_B M5StickC-default / . key Cardputer / encoder CW T-Lora
    DIR_PRESS  encoder btn / BTN_A M5StickC-default / ENTER key Cardputer / encoder btn T-Lora
    DIR_BACK   BTN_A <3s M5StickC-encoder / \b key keyboard boards (consumed by NavigationImpl)
    DIR_LEFT   AXP btn M5StickC-encoder / , key Cardputer
    DIR_RIGHT  BTN_B M5StickC-encoder / / key Cardputer
    DIR_NONE   no event — wasPressed() returns false

    ListScreen handles all six automatically:
      UP/DOWN     move by 1, wraps around
      LEFT/RIGHT  page jump by visible count, clamps at ends
      PRESS       select item (or "< Back" item on no-keyboard default nav)
      BACK        call onBack()

## Non-Keyboard Back Navigation in Custom States

Custom screen states that override `onUpdate()` manually (not delegating to `ListScreen::onUpdate()`)
MUST handle **both** `DIR_BACK` and `DIR_PRESS` for the exit/stop action.
M5StickC default nav never emits `DIR_BACK` — only `DIR_UP`, `DIR_DOWN`, `DIR_PRESS`.

    // CORRECT — works on all boards
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        // exit / stop
      }
    }

    // WRONG — M5StickC users get stuck (DIR_BACK never fires in default nav)
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      // exit
    }

Custom state `onRender()` must also show a visible exit affordance:

    #ifdef DEVICE_HAS_KEYBOARD
      // hint text only — keyboard users know BACK key
      sp.drawString("BACK: Exit", bodyW() / 2, bodyH() - 2, 1);
    #else
      // rendered "< Back" bar so button-only users see what to press (BTN_A = PRESS)
      sp.fillRect(0, bodyH() - 16, bodyW(), 16, Config.getThemeColor());
      sp.setTextColor(TFT_WHITE, Config.getThemeColor());
      sp.drawString("< Back", bodyW() / 2, bodyH() - 8, 1);
    #endif

---

## M5StickC Nav Mode (DEVICE_HAS_NAV_MODE_SWITCH)

    Default nav:  AXP=UP  BTN_B=DOWN  BTN_A=PRESS  — no BACK/LEFT/RIGHT
    Encoder nav:  ROT=UP/DOWN  BTN=PRESS  BTN_A<3s=BACK  AXP=LEFT  BTN_B=RIGHT
    Hold BTN_A ≥ 3s: Device::boardHook() (in M5StickC Device.cpp) resets nav to "default"
    "< Back" item hidden in ListScreen on keyboard boards and encoder nav (DIR_BACK handles it)
    hasBackItem() default is true — only MainMenuScreen overrides it to false

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

## Migrating Screens from puteros

When the user says "migrate <category>":
1. Read BOTH .h and .cpp from `../puteros/firmware/src/os/screens/<category>/` first
2. Adapt to unigeek conventions (see CLAUDE.md Migration section for mapping table)
3. Place files in `src/screens/<category>/`
4. Wire up parent menu (e.g. MainMenuScreen case index)

Key adaptations:
- `_global->setScreen(new X())`  →  `Screen.setScreen(new X())`
- `Template::renderQRCode(data)` →  `ShowQRCodeAction::show(label, data)`
- `Template::renderStatus(msg)`  →  `ShowStatusAction::show(msg, 1500)`
- `InputTextScreen::popup()`     →  `InputTextAction::popup()`
- `onEnter(entry)` string match  →  `onItemSelected(index)` switch

## Keeping Documentation Accurate

When completing a task that changes architecture, patterns, or conventions,
update CLAUDE.md and AGENT.md immediately — do not wait for user to ask.

Triggers: new board, new interface, new UI pattern, Device constructor change,
ScreenManager change, new build flag, new library dependency, convention change.