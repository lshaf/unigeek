# AGENT.md
# Firmware Project - Agentic AI Instructions

## What This Project Is

Multi-device ESP32 firmware. Two target boards share one codebase.
All hardware differences are isolated. Do not break this isolation.

---

## Before Making Any Changes

1. Identify the target — is the change for both boards, M5StickC only, or T-Lora only?
    - Both boards  →  change goes in src/
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
3. Add pins_arduino.h with all GPIO definitions
4. Create boards.ini with PlatformIO env config
5. Storage: init in createInstance(), pass to Device constructor
6. If board has keyboard: implement Keyboard.h, Keyboard.cpp, add DEVICE_HAS_KEYBOARD flag
7. If board has SD card: init StorageSD in createInstance()
8. Always init StorageLFS — all boards have a SPIFFS/LittleFS partition

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

---

## Action Overlay Pattern

All actions are blocking static calls that return a value:

    String      result = InputTextAction::popup("Title");
    int         result = InputNumberAction::popup("Title", min, max, default);
    const char* result = InputSelectAction::popup("Title", opts, count, default);
    void               ShowStatusAction::show("Message", durationMs);

For ShowStatusAction:
- durationMs = 0   show and return immediately, no wipe
- durationMs > 0   block for that duration then wipe

All actions wipe their overlay area on close.

---

## Navigation Direction Values

    DIR_UP      encoder rotated up or BTN_A on M5StickC
    DIR_DOWN    encoder rotated down or BTN_B on M5StickC
    DIR_PRESS   encoder button pressed or power button on M5StickC

---

## Device Constructor Signature

    Device(IDisplay& lcd, IPower& power, INavigation* nav,
           IKeyboard* keyboard = nullptr,
           IStorage* storageSD = nullptr,
           IStorage* storageLFS = nullptr)

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

    setItems(ListItem (&arr)[N])          fixed size array, templated
    setItems(ListItem* arr, uint8_t count) dynamic size array
    clearItems()                           clears list and re-renders

---

## Build src_filter for Board .cpp Files

If adding .cpp files in a board folder, ensure boards.ini includes:

    build_src_filter = +<../boards/t-lora-pager/>

Otherwise .cpp files will not be compiled.

---

## Keeping Documentation Accurate

When completing a task that changes architecture, patterns, or conventions,
check if CLAUDE.md or AGENT.md needs updating.

If yes:
1. Finish the code task first
2. Output a proposed diff for the affected section
3. Ask for approval with a one-line reason
4. Only write to CLAUDE.md or AGENT.md after explicit user approval

Format for proposing updates:

    CLAUDE.md update proposed — reason: added StorageSD to core
    Section: File Placement Rules
    Change: add "Storage implementations  src/core/Storage*.h"
    Approve? (yes/no)

Never update these files silently or as part of another task.