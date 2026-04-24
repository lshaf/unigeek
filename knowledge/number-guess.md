# Number Guess

A classic higher/lower guessing game. A random number is chosen within a range determined by the difficulty. Guess the number in as few attempts as possible — each guess tells you whether the answer is higher or lower.

---

## How to Play

After each guess, the game responds with **HIGHER** or **LOWER** in cyan to narrow down the range. Keep guessing until you find the exact number. On Extreme difficulty, you have a limited number of attempts before the game ends.

The answer is always revealed on the result screen regardless of win or loss.

---

## Difficulty

| Difficulty | Range | Attempt Limit |
|------------|-------|:-------------:|
| Easy | 0 – 99 | Unlimited |
| Medium | 0 – 999 | Unlimited |
| Hard | 0 – 9,999 | Unlimited |
| Extreme | 0 – 9,999 | 10 |

The menu screen shows the active range and attempt limit for the selected difficulty.

---

## Controls

### Keyboard Boards
*(T-Lora Pager, M5 Cardputer, M5 Cardputer ADV)*

Type digits directly. The input field expands to fit the maximum digit count for the active difficulty.

| Action | Key |
|--------|-----|
| Enter digit | `0`–`9` |
| Erase last digit | `Backspace` |
| Submit guess | `Enter` |
| Exit to menu | `Backspace` when field is empty |

> [!tip]
> On the M5 Cardputer, the top letter row (`Q`–`P`) is remapped to digits `1`–`0` so you can enter numbers without switching layers.

### Button / Encoder Boards
*(M5StickC Plus 1.1, M5StickC Plus 2, T-Display, T-Display S3, T-Embed CC1101, M5Stick S3, DIY Smoochie, CYD variants)*

Digits are cycled one at a time. When all digit slots are filled, the guess is submitted automatically.

| Action | Input |
|--------|-------|
| Cycle digit up | UP |
| Cycle digit down | DOWN |
| Confirm selected digit | SELECT |
| Erase previous digit | BACK |
| Exit to menu | Hold SELECT for 1 second (button boards) or BACK (encoder boards) |

Scroll past `9` to reach the `<` erase option (shown with a red background). Confirming `<` erases the current slot.

---

## High Scores

Top 5 scores are saved per difficulty to `/unigeek/games/numguess_<difficulty>.txt`. Scores are ranked first by fewest guesses, then by fastest time.

From the High Scores screen, press UP/DOWN to switch between difficulty leaderboards. Press BACK or SELECT to return to the menu.

---

## Achievements

| Achievement | Tier |
|------------|------|
| **First Guess** | Bronze |
| **Easy Guesser** | Bronze |
| **Mid Guesser** | Silver |
| **Hard Guesser** | Gold |
| **Extreme Guesser** | Platinum |
| **Lucky Shot** | Silver |
| **Calculated** | Gold |
| **Survivor** | Platinum |
| **Seer** | Platinum |
| **Luck God** | Platinum |
