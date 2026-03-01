# CLAUDE.md
# Firmware Project - Claude AI Instructions

## Project Overview

ESP32 multi-device firmware supporting M5StickC Plus 1.1 and T-Lora Pager.
Built with PlatformIO + Arduino framework + TFT_eSPI.
All hardware differences are isolated in board-specific folders.

---

## Build System

### PlatformIO Commands

    pio run -e m5stickcplus_11
    pio run -e t_lora_pager
    pio run -e m5stickcplus_11 -t upload
    pio run -e t_lora_pager -t upload
    pio device monitor
    pio run -t clean

### Board Environments

    m5stickcplus_11   M5StickC Plus 1.1, no keyboard, AXP192 power, LittleFS only
    t_lora_pager      LilyGO T-Lora Pager, TCA8418 keyboard, BQ25896/BQ27220 power, SD + LittleFS

### Build Flags (board-specific)

    DEVICE_HAS_KEYBOARD   defined for T-Lora Pager only, enables keyboard input paths
    APP_MENU_POWER_OFF    defined for T-Lora Pager and M5StickC Plus 1.1, adds Power Off in main menu

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
    │   └── t-lora-pager/           T-Lora Pager board implementation
    │       ├── boards.ini
    │       ├── Device.cpp          createInstance() with SD + LittleFS, keyboard
    │       ├── Display.h
    │       ├── Navigation.h        rotary encoder, GPIO0 long-press power-off
    │       ├── Navigation.cpp      ISR in .cpp (IRAM_ATTR requirement)
    │       ├── Keyboard.h          TCA8418 3-layer keymap
    │       ├── Keyboard.cpp        ISR in .cpp
    │       ├── Power.h             BQ25896/BQ27220 via XPowersLib
    │       └── pins_arduino.h
    ├── src/
    │   ├── core/
    │   │   ├── Device.h            Device singleton, Uni macro
    │   │   ├── IDisplay.h
    │   │   ├── INavigation.h
    │   │   ├── IPower.h
    │   │   ├── IKeyboard.h
    │   │   ├── IStorage.h
    │   │   ├── StorageSD.h         SD implementation (shared, used by both boards)
    │   │   ├── StorageLFS.h        LittleFS implementation (shared, used by both boards)
    │   │   ├── ScreenManager.h
    │   │   └── ConfigManager.h
    │   ├── screens/
    │   │   ├── MainMenuScreen.h
    │   │   ├── MainMenuScreen.cpp
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
    Uni.Keyboard    IKeyboard*  nullptr on M5StickC
    Uni.Storage     IStorage*   primary, SD if available else LittleFS; reassigned by _checkStorageFallback()
    Uni.StorageSD   IStorage*   direct SD access, nullptr on M5StickC
    Uni.StorageLFS  IStorage*   direct LittleFS access
    Uni.Spi         SPIClass*   shared SPI bus (HSPI on T-Lora Pager, nullptr on M5StickC)

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

    - ListScreen provides virtual onBack() — called on backspace key (DEVICE_HAS_KEYBOARD)
      or when user selects the auto-appended "< Back" item (no-keyboard devices)
    - hasBackItem() controls whether "< Back" appears — default true, override false for root screens
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
    void        ShowStatusAction::show("Message")           show and return immediately
    void        ShowStatusAction::show("Message", 1500)     show, block ms, then wipe
    // Long messages are automatically word-wrapped (max 5 lines, box grows to fit)

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

### Theme Color

    Config.getThemeColor()    returns uint16_t TFT color

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
- Use APP_MENU_POWER_OFF for power-off menu item

---

## Library Dependencies

    Bodmer/TFT_eSPI@^2.5.43               both boards
    adafruit/Adafruit TCA8418@^1.0.0      T-Lora only
    mathertel/RotaryEncoder@^1.5.3        T-Lora only
    lewisxhe/XPowersLib@^0.2.0            T-Lora only

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
- StorageSD self-reports failure: _available set false on open() failure, detectable via isAvailable()
- Uni.Storage may be reassigned at runtime by _checkStorageFallback() in main.cpp when SD fails
- T-Lora Pager SPI (HSPI, pins SCK=35/MISO=33/MOSI=34) must be explicitly passed to SD.begin()
  SD.begin(csPin) alone uses wrong default VSPI bus and causes sdSelectCard() failures
- ListItem struct has no default for sublabel — single-field init {"Label"} zero-initializes sublabel to nullptr

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

---

## Self-Updating This Document

This document should stay accurate as the project evolves.
When making changes that affect architecture, conventions, or patterns,
Claude CLI must propose updates to CLAUDE.md or AGENT.md and wait for
manual approval before writing them.

Triggers that require a CLAUDE.md or AGENT.md update proposal:

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