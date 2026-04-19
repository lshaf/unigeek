# NAVIGATION.md
# Board Navigation Reference

Per-board physical input mapping. **Update this file whenever any Navigation.h or Navigation.cpp is changed.**

---

## m5stickcplus_11 (M5StickC Plus 1.1)
Inputs: AXP192 power button · GPIO 37 (BTN_A, front) · GPIO 39 (BTN_B, side, active LOW)

| Input                   | Direction |
|-------------------------|-----------|
| AXP power button        | DIR_UP    |
| BTN_A                   | DIR_PRESS |
| BTN_B short press (release) | DIR_DOWN |
| BTN_B hold >600ms       | DIR_BACK  |

Long-press threshold: 600 ms. DOWN emits on release; BACK emits while held. orientDir() applied to UP/DOWN.

### EncoderNavigation variant (M5Hat Mini EncoderC attached)
| Input                       | Direction  |
|-----------------------------|------------|
| Encoder CCW (value ≤ −2)    | DIR_UP     |
| Encoder CW (value ≥ +2)     | DIR_DOWN   |
| Encoder push                | DIR_PRESS  |
| BTN_A short press (release, <3s) | DIR_BACK |
| AXP power button            | DIR_LEFT   |
| BTN_B press & release       | DIR_RIGHT  |

---

## m5stickcplus_2 (M5StickC Plus 2)
Inputs: GPIO 35 (BTN_UP, top) · GPIO 37 (BTN_A, front) · GPIO 39 (BTN_B, bottom) — all active LOW

| Input                   | Direction |
|-------------------------|-----------|
| BTN_UP                  | DIR_UP    |
| BTN_A                   | DIR_PRESS |
| BTN_B short press (release) | DIR_DOWN |
| BTN_B hold >600ms       | DIR_BACK  |

Long-press threshold: 600 ms. DOWN emits on release; BACK emits while held. orientDir() applied to UP/DOWN.

### EncoderNavigation variant (M5Hat Mini EncoderC attached)
| Input                       | Direction  |
|-----------------------------|------------|
| Encoder CCW (value ≤ −2)    | DIR_UP     |
| Encoder CW (value ≥ +2)     | DIR_DOWN   |
| Encoder push                | DIR_PRESS  |
| BTN_A short press (release, <3s) | DIR_BACK |
| BTN_UP press & release      | DIR_LEFT   |
| BTN_B press & release       | DIR_RIGHT  |

---

## t_lora_pager (LilyGO T-Lora Pager)
Inputs: Rotary encoder GPIO 40/41 (ISR, FOUR3 latch) · GPIO 7 (encoder push, active LOW) · TCA8418 keyboard

| Input                    | Direction |
|--------------------------|-----------|
| Encoder CCW (≤ −2 steps) | DIR_UP    |
| Encoder CW (≥ +2 steps)  | DIR_DOWN  |
| Encoder push GPIO 7      | DIR_PRESS |
| Keyboard Enter           | DIR_PRESS |
| Keyboard Backspace       | DIR_BACK  |

Button debounce: 150 ms. Scroll threshold: 2 steps.
Note: only Enter and Backspace are mapped from the keyboard — other keys are not nav shortcuts.

---

## t_display (LilyGO T-Display)
Inputs: GPIO 0 (BTN_UP, INPUT_PULLUP) · GPIO 35 (BTN_B, INPUT_PULLUP)

| Input                     | Direction |
|---------------------------|-----------|
| BTN_UP                    | DIR_UP    |
| BTN_B single click        | DIR_DOWN  |
| BTN_B double click ≤250ms | DIR_PRESS |
| BTN_B hold >600ms         | DIR_BACK  |

Double-click window: 250 ms. Long-press threshold: 600 ms. DOWN emits after double-click timeout; BACK emits while held.

---

## t_display_s3 (LilyGO T-Display S3)
Inputs: GPIO 0 (BTN_UP) · GPIO 14 (BTN_B) — identical logic to t_display above.

| Input                     | Direction |
|---------------------------|-----------|
| BTN_UP                    | DIR_UP    |
| BTN_B single click        | DIR_DOWN  |
| BTN_B double click ≤250ms | DIR_PRESS |
| BTN_B hold >600ms         | DIR_BACK  |

---

## diy_smoochie (DIY Smoochie)
Inputs: 5 GPIO buttons, all INPUT_PULLUP, active LOW
GPIO 0→SEL · GPIO 41→UP · GPIO 40→DOWN · GPIO 38→RIGHT · GPIO 39→LEFT

| Input                    | Direction |
|--------------------------|-----------|
| BTN_UP                   | DIR_UP    |
| BTN_DOWN                 | DIR_DOWN  |
| BTN_LEFT short press     | DIR_LEFT  |
| BTN_LEFT hold >600ms     | DIR_BACK  |
| BTN_RIGHT                | DIR_RIGHT |
| BTN_SEL                  | DIR_PRESS |

Long-press threshold: 600 ms. BACK emits while held; LEFT emits on short release.

---

## m5_cardputer (M5Stack Cardputer)
Inputs: Built-in keyboard matrix via IKeyboard (74HC138 GPIO)

| Key       | Direction |
|-----------|-----------|
| ';'       | DIR_UP    |
| '.'       | DIR_DOWN  |
| ','       | DIR_LEFT  |
| '/'       | DIR_RIGHT |
| Enter     | DIR_PRESS |
| Backspace | DIR_BACK  |

suppressKeys() blocks nav during text input. isKeyHeld() maintains held direction state.

---

## m5_cardputer_adv (M5Stack Cardputer ADV)
Inputs: TCA8418 keyboard via Wire1 — identical key mapping to m5_cardputer above.

---

## t_embed_cc1101 (LilyGO T-Embed CC1101)
Inputs: Rotary encoder GPIO 4/5 (ISR, FOUR3 latch) · GPIO 0 (encoder push, active LOW) · GPIO 6 (back button, active LOW)

| Input                   | Direction |
|-------------------------|-----------|
| Encoder CW (≥ +1 step)  | DIR_UP    |
| Encoder CCW (≤ −1 step) | DIR_DOWN  |
| Encoder push GPIO 0     | DIR_PRESS |
| Back button GPIO 6      | DIR_BACK  |

Button debounce: 150 ms. Scroll threshold: 1 step.
Note: encoder direction is opposite to t_lora_pager — CW = UP here, CCW = UP on t_lora_pager.

---

## m5_cores3_unified (M5Stack CoreS3, M5Unified)
Inputs: FT6336U capacitive touch panel (320×240 landscape, via M5.Touch)

| Touch zone                                  | Direction |
|---------------------------------------------|-----------|
| Left ¼  (x < 80)                            | DIR_BACK  |
| Right ¾ + top ⅓  (x ≥ 80, y < 80)          | DIR_UP    |
| Right ¾ + middle ⅓  (x ≥ 80, 80 ≤ y < 160) | DIR_PRESS |
| Right ¾ + bottom ⅓  (x ≥ 80, y ≥ 160)      | DIR_DOWN  |

Poll rate: 20 ms. Debounce: 3 consecutive no-touch polls (~60 ms).
drawOverlay() paints edge indicators: left=red/BACK, right-top=green/UP, right-mid=blue/PRESS, right-bot=green/DOWN.

---

## m5_cores3 (bare CoreS3, out-of-tree)
Identical touch zones and logic to m5_cores3_unified. Uses custom TouchFT6336U driver instead of M5Unified.

---

## m5sticks3 (M5StickC S3)
Inputs: GPIO 11 (BTN_A, INPUT_PULLUP) · GPIO 12 (BTN_B, ISR-driven, INPUT_PULLUP)

| Input                           | Direction |
|---------------------------------|-----------|
| BTN_A                           | DIR_PRESS |
| BTN_B single press              | DIR_DOWN  |
| BTN_B double press (≤250ms)     | DIR_UP    |
| BTN_B hold >600ms               | DIR_BACK  |

ISR on BTN_B CHANGE. Debounce: 8 ms. Double-click window: 250 ms. Long-press: 600 ms.
DOWN emits after double-click timeout; UP emits on second release; BACK emits while held. BTN_A takes priority.

---

## cyd_2432w328r (CYD 2432W328R)
Inputs: XPT2046 resistive touch panel (320×240 landscape, via TFT_eSPI getTouch())

| Touch zone                                  | Direction |
|---------------------------------------------|-----------|
| Left ¼  (x < 80)                            | DIR_BACK  |
| Right ¾ + top ⅓  (x ≥ 80, y < 80)          | DIR_UP    |
| Right ¾ + middle ⅓  (x ≥ 80, 80 ≤ y < 160) | DIR_PRESS |
| Right ¾ + bottom ⅓  (x ≥ 80, y ≥ 160)      | DIR_DOWN  |

Poll rate: 20 ms. Debounce: 3 consecutive no-touch polls (~60 ms).
Calibration: X 185–3700, Y 280–3850 (set via setTouch() in begin()).
drawOverlay() paints same edge indicators as m5_cores3_unified.

---

## diy_marauder (WiFi Marauder v7, out-of-tree)
Inputs: 5 GPIO buttons — GPIOs 34/35/36/39 input-only, NO internal pull-up (use INPUT); GPIO 26 has external pull-up
GPIO 34→SEL · GPIO 36→UP · GPIO 35→DOWN · GPIO 39→RIGHT · GPIO 26→LEFT

| Input                | Direction |
|----------------------|-----------|
| BTN_UP GPIO 36       | DIR_UP    |
| BTN_DOWN GPIO 35     | DIR_DOWN  |
| BTN_RIGHT GPIO 39    | DIR_RIGHT |
| BTN_SEL GPIO 34      | DIR_PRESS |
| BTN_LEFT short press | DIR_LEFT  |
| BTN_LEFT hold >600ms | DIR_BACK  |

All active LOW. No ISR; all polled. Do not use INPUT_PULLUP on GPIOs 34/35/36/39.
