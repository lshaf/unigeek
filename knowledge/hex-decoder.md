# HEX Decoder

A Wordle-style puzzle game where you guess a randomly generated 4-character hexadecimal code. Each guess gives color-coded feedback to help narrow down the answer.

---

## How to Play

A secret 4-character code is generated from the hex alphabet `0–9` and `A–F`. You submit guesses and receive feedback on each character:

| Color | Meaning |
|-------|---------|
| Green | Correct character, correct position |
| Orange | Correct character, wrong position |
| Red | Character not in the code |

A 2×2 dot summary appears beside each submitted guess row, showing the same color hints sorted from least to most helpful — useful for a quick glance without re-reading each cell.

On **Hard** and **Extreme** difficulties, color hints are hidden. All submitted tiles appear dark grey regardless of correctness.

The game ends when you guess correctly, run out of attempts (Easy/Medium), or the timer runs out. The answer is always revealed on the result screen.

---

## Difficulty

| Difficulty | Max Attempts | Time Limit | Color Hints |
|------------|:------------:|:----------:|:-----------:|
| Easy | 14 | 3:00 | Yes |
| Medium | 7 | 1:30 | Yes |
| Hard | Unlimited | 3:00 | No |
| Extreme | Unlimited | 1:30 | No |

The difficulty selector cycles on the menu screen. High scores are tracked separately per difficulty.

---

## Controls

### Keyboard Boards
*(T-Lora Pager, M5 Cardputer, M5 Cardputer ADV)*

Type hex characters directly — `0`–`9` and `A`–`F` (lowercase accepted). The cursor advances automatically after each character.

| Action | Key |
|--------|-----|
| Enter character | `0`–`9`, `A`–`F` |
| Erase last character | `Backspace` |
| Submit guess | `Enter` |
| Exit to menu | `Backspace` at position 1 |

### Button / Encoder / Touch Boards
*(M5StickC Plus 1.1, M5StickC Plus 2, T-Display, T-Display S3, T-Embed CC1101, M5 CoreS3 Unified, M5Stick S3, DIY Smoochie, CYD variants)*

Characters are cycled one at a time using UP/DOWN. The active slot shows the current character with a teal highlight. An arrow above and below the active slot indicates the scroll direction.

| Action | Input |
|--------|-------|
| Cycle character up | UP |
| Cycle character down | DOWN |
| Confirm selected character | SELECT |
| Erase previous character | BACK |
| Exit to menu | BACK at position 1 |

Scroll past `F` to reach the `<` erase option (shown with a red background). Confirming `<` erases the current slot instead of entering a character.

---

## High Scores

Top 5 scores are saved per difficulty to `/unigeek/games/decoder_<difficulty>.txt`. Scores are ranked first by fewest turns, then by fastest time.

The result screen shows your final turn count and time. If you placed in the top 5, your new rank is displayed (`New #1 Best!`, etc.).

From the High Scores screen, press UP/DOWN to switch between difficulty leaderboards. Press BACK or SELECT to return to the menu.

---

## Achievements

| Achievement | Tier |
|------------|------|
| **Hex Curious** | Bronze |
| **Hex Solver** | Silver |
| **Hex Legend** | Gold |
