# Hardware Reference

## ISpeaker Interface

    Uni.Speaker->tone(freq, durationMs)        square-wave tone at Hz for ms (FreeRTOS task)
    Uni.Speaker->noTone()                      stop any playing tone
    Uni.Speaker->setVolume(0-100)              set volume % (persisted via APP_CONFIG_VOLUME)
    Uni.Speaker->beep()                        short nav feedback beep (respects APP_CONFIG_NAV_SOUND)
    Uni.Speaker->playRandomTone(shift, ms)     random note from C major scale, optional semitone shift
    Uni.Speaker->playWav(data, size)           play PROGMEM WAV (8/16-bit, mono/stereo, any rate)
    Uni.Speaker->playNotification()            play SoundNotification.h WAV
    Uni.Speaker->playWin() / playLose()        play win/lose WAV
    Uni.Speaker->playCorrectAnswer()           tone sequence: 523Hz→784Hz
    Uni.Speaker->playWrongAnswer()             tone sequence: 1109Hz×2

Guard all calls: `if (Uni.Speaker) Uni.Speaker->beep();`

Speaker implementations:
- `SpeakerI2S`    — Cardputer / Cardputer ADV (I2S, I2S_NUM_1, WAV capable)
- `SpeakerADV`    — Cardputer ADV only (extends SpeakerI2S, adds ES8311 codec init)
- `SpeakerBuzzer` — M5StickC Plus 1.1 (LEDC PWM GPIO 2, no WAV, tone sequences only)

I2S pins in pins_arduino.h:

    #define SPK_BCLK      <pin>
    #define SPK_WCLK      <pin>
    #define SPK_DOUT      <pin>
    #define SPK_I2S_PORT  I2S_NUM_1   // Cardputer boards use I2S_NUM_1
    // channel_format = I2S_CHANNEL_FMT_ALL_RIGHT (sends mono to both L+R)

---

## IKeyboard Interface

    Uni.Keyboard->available()   true when a key is buffered and ready
    Uni.Keyboard->peekKey()     read buffered key WITHOUT consuming — used by NavigationImpl
    Uni.Keyboard->getKey()      consume and return buffered key; sets _waitRelease on GPIO-matrix boards
    Uni.Keyboard->modifiers()   bitmask: MOD_SHIFT, MOD_FN, MOD_CAPS, MOD_CTRL, MOD_ALT, MOD_OPT

NavigationImpl uses `peekKey()` first; only calls `getKey()` when the key is a nav key.
`\b` (backspace) is consumed by Nav → emitted as DIR_BACK; overlays use `readDirection()` not `getKey()`.

Device::update() order: `boardHook()` → `Keyboard->update()` → `Nav->update()` each frame.
Never call `Keyboard->update()` inside NavigationImpl.

---

## Library Dependencies

    Bodmer/TFT_eSPI@^2.5.43               all boards
    adafruit/Adafruit TCA8418@^1.0.0      T-Lora Pager + m5_cardputer_adv
    mathertel/RotaryEncoder@^1.5.3        T-Lora Pager + t_embed_cc1101
    lewisxhe/XPowersLib@^0.2.0            T-Lora Pager + t_embed_cc1101
    m5stack/M5Hat-Mini-EncoderC            M5StickC Plus 2 (rotary encoder hat)
    ESP32 LEDC (built-in)                 PWM brightness/speaker on all non-I2S boards

## Reference Libraries (not in build)

    ../M5Unified     M5Stack hardware reference — speaker, display, power, board configs
    ../LilyGoLib     LilyGO T-Lora Pager hardware reference
    ../bruce         Bruce firmware — Smoochiee, T-Embed CC1101, Marauder v7 board reference
    ../pn532-python  PN532/PN532Killer CLI — wire protocol & command codes
                     reference for src/utils/nfc/pn532/ (HSU framing, ACK/NACK,
                     InListPassiveTarget, InDataExchange, magic Gen1a/Gen3 cmds).
                     Thanks to its authors and to libnfc, Proxmark3, Chameleon
                     Ultra for the underlying research.

## PN532 (HSU mode)

The PN532 module talks to UART2 (`HardwareSerial(2)`), shared with the GPS
module — only one screen owns the bus at a time. PN532 dev boards must have
their DIP/jumpers switched to **HSU** mode (not I2C/SPI). Default baud is
115200 8N1.

Pins are user-configurable via Modules → Pin Setting (`pn532_tx`, `pn532_rx`,
`pn532_baud`). On T-Lora Pager the defaults map to the external 12-pin socket
(TX=43, RX=44 = `UART1_TX/RX`); on other boards both default to -1, which
hides the "PN532 UART" entry until the user sets them.

---

## Board-Specific Constraints

- T-Lora Pager SPI (HSPI, SCK=35/MISO=33/MOSI=34) must be explicitly passed to SD.begin()
- M5 Cardputer SPI: TFT uses HSPI internally, SD uses FSPI (SCK=40/MISO=39/MOSI=14/CS=12)
- M5 Cardputer ADV: TCA8418 on Wire1 (SDA=8/SCL=9/addr=0x34); requires delay(100) after Wire1.begin()
- Cardputer keyboard _waitRelease: after getKey(), blocks until all non-modifier GPIO inputs released
- Cardputer ADV ES8311 codec on Wire1 (addr=0x18) must be initialized before any sound
- Cardputer boards use I2S_NUM_1 — define SPK_I2S_PORT I2S_NUM_1 in pins_arduino.h
- SpeakerI2S channel_format must be I2S_CHANNEL_FMT_ALL_RIGHT
- M5StickC Plus I2C: Wire1 for AXP192 + BM8563 (INTERNAL_SDA=21/INTERNAL_SCL=22)
  Wire1.begin() in createInstance(), define RTC_WIRE Wire1 in pins_arduino.h
- M5StickC Plus 2 power: GPIO 4 PWR_HOLD_PIN must stay HIGH to stay powered
- Marauder v7 navigation: GPIOs 34/35/36/39 are input-only, NO internal pull-up → use INPUT mode
- Marauder v7 power: no battery IC (returns 50%); deep sleep wakeup on GPIO34 (RTC_GPIO4)
- Brightness LEDC: use Arduino core 2.x API: ledcSetup/ledcAttachPin/ledcWrite (not ledcAttach)
- WiFi attack mode must be WIFI_MODE_APSTA — WifiAttackUtil handles this
  Never call esp_wifi_set_channel() directly — use attacker->setChannel()
- sdcard/manifest/sdcard.txt must be updated when files are added or removed from sdcard/
- sdcard/manifest/ir/categories.txt lists IR categories (folder|Display Name format)