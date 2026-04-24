# Memory Sequence

A Simon Says-style memory game. Watch a sequence of characters flash on screen one at a time, then type the full sequence back from memory. Each correct round grows the sequence and increases the pace.

---

## How to Play

Each round starts with the sequence display phase: characters from `0–9` and `A–Z` flash one at a time in the center of the screen, with a dot progress bar at the bottom tracking which position is being shown. A brief gap separates each character.

When the sequence ends, the input phase begins. Enter every character in the exact order they appeared. Submitting the correct sequence advances to the next round — the sequence grows longer, display time shortens, and score multipliers increase. A wrong answer uses one mistake; the correct sequence is shown briefly so you can study it before retrying.

The game ends when you exhaust all allowed mistakes. Your final round count and score are saved as your best if they beat the previous record.

---

## Difficulty

| Difficulty | Starting Length | Display Time | Mistakes Allowed |
|------------|:--------------:|:------------:|:----------------:|
| Easy | 3 | 2.0 s | 3 |
| Medium | 4 | 1.5 s | 3 |
| Hard | 5 | 0.8 s | 2 |
| Extreme | 6 | 0.6 s | 2 |

Sequence length, display time, and score multiplier all scale up as rounds progress. The menu screen shows the starting parameters for the selected difficulty.

### Scoring

Each correct round awards points based on sequence length and a stage multiplier:

```
points = 100 × length × multiplier
       + 50 (if no mistakes this round)
       + streak bonus (streak × 10, max 100)
```

Building a streak of consecutive perfect rounds maximizes your score per round.

---

## Controls

### Keyboard Boards
*(T-Lora Pager, M5 Cardputer, M5 Cardputer ADV)*

| Action | Key |
|--------|-----|
| Enter character | `0`–`9`, `A`–`Z` |
| Erase last character | `Backspace` |
| Submit sequence | `Enter` |
| Exit to menu | `Backspace` at position 1 |

### Button / Encoder / Touch Boards
*(M5StickC Plus 1.1, M5StickC Plus 2, T-Display, T-Display S3, T-Embed CC1101, M5 CoreS3 Unified, M5Stick S3, DIY Smoochie, CYD variants)*

Characters are cycled using UP/DOWN. The active slot shows the current character with a teal highlight and arrows above and below.

| Action | Input |
|--------|-------|
| Cycle character up | UP |
| Cycle character down | DOWN |
| Confirm selected character | SELECT |
| Erase previous character | BACK |
| Exit to menu | BACK at position 1 |

Scroll past `Z` to reach the `<` erase option (shown with a red background). Confirming `<` erases the current slot.

---

## High Scores

One record is kept per difficulty: the highest round reached and the score at that point. Saved to `/unigeek/games/memseq_hs.txt`.

From the High Scores screen, press UP/DOWN to switch between difficulties. Press BACK or SELECT to return to the menu.

---

## Achievements

| Achievement | Tier |
|------------|------|
| **Memory Check** | Bronze |
| **Sharp Memory** | Silver |
| **Memory Ace** | Gold |
| **New Record** | Silver |
| **Eidetic** | Platinum |
| **Memory God** | Platinum |
