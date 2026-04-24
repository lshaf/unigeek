# Wordle

A classic word-guessing game available in English and Indonesian. Guess a hidden 5-letter word within the allowed number of attempts. Color-coded feedback narrows down the answer with each guess.

---

## How to Play

A secret 5-letter word is chosen from the selected word database. You submit full-word guesses and receive color-coded feedback on each letter:

| Color | Meaning |
|-------|---------|
| Green | Correct letter, correct position |
| Orange | Correct letter, wrong position |
| Red | Letter not in the word |

An alphabet panel on the right side of the screen tracks which letters you have already used — used letters turn grey. On **Hard** difficulty, the alphabet panel is always dark grey regardless of what you've guessed.

The game ends when you guess the word or exhaust all attempts. The answer is revealed on the result screen.

---

## Languages

The game is available in two separate variants, each accessible from the Games menu:

- **Wordle EN** — English word database
- **Wordle ID** — Indonesian word database

High scores are tracked separately per language and per difficulty.

---

## Word Database

Each language has two database options, toggled from the menu before starting:

| Option | Description |
|--------|-------------|
| **Common** | Curated set of familiar, frequently used words |
| **Full** | Larger database including uncommon and obscure words |

---

## Difficulty

| Difficulty | Max Attempts | Alphabet Hint |
|------------|:------------:|:-------------:|
| Easy | 10 | Yes |
| Medium | 7 | Yes |
| Hard | 7 | No |

There is no time limit on any difficulty. Scores are ranked by fewest turns, then fastest time.

---

## Controls

### Keyboard Boards
*(T-Lora Pager, M5 Cardputer, M5 Cardputer ADV)*

Type letters directly. Lowercase is accepted and converted automatically.

| Action | Key |
|--------|-----|
| Enter letter | `A`–`Z` |
| Erase last letter | `Backspace` |
| Submit guess | `Enter` |
| Exit to menu | `Backspace` at position 1 |

### Button / Encoder / Touch Boards
*(M5StickC Plus 1.1, M5StickC Plus 2, T-Display, T-Display S3, T-Embed CC1101, M5 CoreS3 Unified, M5Stick S3, DIY Smoochie, CYD variants)*

Letters are cycled one at a time using UP/DOWN. An arrow above and below the active slot shows the scroll direction.

| Action | Input |
|--------|-------|
| Cycle letter up | UP |
| Cycle letter down | DOWN |
| Confirm selected letter | SELECT |
| Erase previous letter | BACK |
| Exit to menu | BACK at position 1 |

Scroll past `Z` to reach the `<` erase option (shown with a red background). Confirming `<` erases the current slot.

---

## High Scores

Top 5 scores are saved per language and per difficulty:

- `/unigeek/games/wordle_en_<difficulty>.txt`
- `/unigeek/games/wordle_id_<difficulty>.txt`

From the High Scores screen, press UP/DOWN to switch between difficulty leaderboards. Press BACK or SELECT to return to the menu.

---

## Achievements

| Achievement | Tier |
|------------|------|
| **Word Player** | Bronze |
| **Wordsmith** | Silver |
| **Word Master** | Gold |
| **Pemain Kata** | Bronze |
| **Kata Jagoan** | Silver |
| **Ahli Kata** | Gold |
| **Genius** | Platinum |
