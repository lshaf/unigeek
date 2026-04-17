# Achievements

UniGeek tracks your activity across all features through an achievement system. Every meaningful action — connecting to a network, running an attack, winning a game — can unlock an achievement and award EXP. Your total EXP determines your rank, visible on the Character Screen.

---

## How It Works

Achievements are grouped into **13 domains**, each covering a feature area. Within each domain, achievements are tiered by difficulty:

| Tier | Label | EXP Reward |
|------|-------|-----------|
| Bronze | B | +100 EXP |
| Silver | S | +300 EXP |
| Gold | G | +600 EXP |
| Platinum | P | +1 000 EXP |

Unlocking an achievement shows a toast notification at the bottom of the screen with the achievement title and EXP earned. Progress persists across reboots.

---

## Ranks

Total possible EXP across all achievements: **84 800** (199 entries: 62 × Bronze, 60 × Silver, 41 × Gold, 36 × Platinum).

| Rank | EXP Required | % of Total |
|------|-------------|-----------|
| Novice | 0 | — |
| Hacker | 8 500 | ~10 % |
| Expert | 21 000 | ~25 % |
| Elite | 42 000 | ~50 % |
| Legend | 68 000 | ~80 % |

Your current rank and a progress bar toward the next rank are shown on the **Character Screen** (accessible from the main menu). The character's hacker-head sprite also changes per rank — gray → cyan-eyes → sunglasses → gold → violet-eyed.

---

## Viewing Achievements

Go to **Utility > Achievements**. The screen has two levels:

1. **Domain list** — each of the 13 domains shows how many achievements you've unlocked (e.g. `3/10`) and total EXP earned in that domain
2. **Achievement list** — tap a domain to see all achievements inside it; each row shows the title, tier label (`B` / `S` / `G` / `P`), and how to unlock it

Locked top-tier achievements show `???` as the tier and `Unlock to reveal` as the description — their existence is hidden until earned.

---

## Setting Your Agent Title

Any unlocked achievement can be set as your **Agent Title** — a personal badge shown on the Character Screen.

**To set a title:**
1. Go to **Utility > Achievements**
2. Navigate into any domain
3. Scroll to an unlocked achievement
4. **Long-press** (hold ~0.6 s) — a "Title saved" confirmation appears

If the achievement is still locked, "Unlock first" appears instead.

The title is displayed on the Character Screen as:

```
AGENT  MyDevice            [HACKER] Credential Thief
```

If no title has been set, it shows `[RANK] No Title` by default.

---

## Domain Summary

| # | Domain | Achievements |
|---|--------|-------------:|
| 0 | WiFi Network | 20 |
| 1 | WiFi Attacks | 43 |
| 2 | Bluetooth | 16 |
| 3 | Keyboard / HID | 6 |
| 4 | NFC | 7 |
| 5 | IR | 8 |
| 6 | Sub-GHz | 7 |
| 7 | NRF24 2.4 GHz | 4 |
| 8 | GPS | 11 |
| 9 | Utility | 10 |
| 10 | Games | 31 |
| 11 | Settings | 4 |
| 12 | Chameleon | 32 |
| | **Total** | **199** |

---

## Achievement Catalog

Each domain lists a sample. The full catalog is visible in-app under **Utility > Achievements**.

### WiFi Network (domain 0)

| Title | Tier | How to unlock |
|-------|------|--------------|
| First Contact | Bronze | Scan for nearby WiFi networks |
| Network Hopper | Silver | Connect to 5 different networks |
| Roam Lord | Gold | Connect to 20 different networks |
| ... | | *20 achievements total* |

### WiFi Attacks (domain 1)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Fake Hotspot | Bronze | Start a rogue access point |
| Dark Mirror | Silver | Start an evil twin AP attack |
| Credential Thief | Gold | Capture credentials via evil twin |
| Crypto Nemesis | Platinum | Successfully crack 50 WiFi passwords |
| ... | | *43 achievements total* |

### Bluetooth (domain 2)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Bluetooth Scout | Bronze | Scan for nearby BLE devices |
| Blue Storm | Silver | Run BLE spam continuously for 1 min |
| Broken Pairing | Gold | Find a vulnerable WhisperPair device (CVE-2025-36911) |
| ... | | *16 achievements total* |

### Keyboard / HID (domain 3)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Bluetooth Typist | Bronze | Connect as a Bluetooth HID keyboard |
| Script Kiddie | Silver | Run a DuckyScript payload |
| Automation God | Platinum | Execute 10 DuckyScript payloads |
| ... | | *6 achievements total* |

### NFC (domain 4)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Card Detected | Bronze | Read an NFC card UID |
| Key Found | Gold | Crack a valid MIFARE sector key |
| Full Dump | Gold | Dump a full NFC card memory |
| Dark Art | Platinum | Execute a MIFARE Darkside attack |
| ... | | *7 achievements total* |

### IR (domain 5)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Signal Catcher | Bronze | Capture an IR signal with the receiver |
| TV-B-Gone | Silver | Start TV-B-Gone power-off sweep |
| Screen Killer | Gold | Complete a full TV-B-Gone sweep |
| IR Librarian | Platinum | Save 20 remote files to storage |
| ... | | *8 achievements total* |

### Sub-GHz (domain 6)

| Title | Tier | How to unlock |
|-------|------|--------------|
| RF Listener | Bronze | Receive a Sub-GHz RF signal |
| Frequency Disruptor | Silver | Start RF jamming on a frequency |
| RF Library | Platinum | Save 20 RF signals to storage |
| ... | | *7 achievements total* |

### NRF24 2.4 GHz (domain 7)

| Title | Tier | How to unlock |
|-------|------|--------------|
| 2.4G Watcher | Bronze | View the 2.4 GHz spectrum analyzer |
| 2.4G Disruptor | Silver | Start an NRF24 jamming session |
| MouseJacker | Gold | Scan for MouseJack-vulnerable devices |
| ... | | *4 achievements total* |

### GPS (domain 8)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Locked On | Silver | Get your first GPS position fix |
| Mass Surveyor | Platinum | Log 3 000 networks during wardriving |
| WiGLE Titan | Platinum | Upload 100 wardrive sessions to WiGLE |
| ... | | *11 achievements total* |

> WiGLE uploads from both GPS > Wardriving and WiFi > Network > WiGLE count toward the same milestones.

### Utility (domain 9)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Bus Detective | Bronze | Run an I2C bus scan |
| I2C Discovery | Silver | Find a device on the I2C bus |
| QR Scribe | Bronze | Generate a QR code from typed text |
| ... | | *10 achievements total* |

### Games (domain 10)

| Title | Tier | How to unlock |
|-------|------|--------------|
| First Flight | Bronze | Play Flappy Bird for the first time |
| Air Master | Gold | Score 25 points in Flappy Bird |
| Pipe God | Platinum | Score 100 points in Flappy Bird |
| Genius | Platinum | Solve Wordle correctly on the 1st guess |
| Memory God | Platinum | Win Memory extreme 5 times with a new high score |
| ... | | *31 achievements total* |

### Settings (domain 11)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Identity | Bronze | Change your device name in Settings |
| My Colors | Bronze | Change the UI theme color in Settings |
| Lock Down | Bronze | Set up a PIN lock for the device |
| Self Check | Bronze | View device status and hardware info |
| | | *4 achievements total* |

### Chameleon (domain 12)

| Title | Tier | How to unlock |
|-------|------|--------------|
| Chameleon Link | Silver | Connect to a ChameleonUltra via Bluetooth |
| Field Intel | Bronze | View ChameleonUltra device info |
| Slot Tinkerer | Bronze | Open the per-slot editor |
| Transplant | Gold | Write HF or LF content into a Chameleon slot |
| Slot Inspector | Bronze | View current emulator content of a Chameleon slot |
| Dictionary Diver II | Gold | Run a Chameleon MF Classic dictionary attack |
| Keyring | Gold | Recover 10+ MF Classic keys via Chameleon |
| Full Impression | Gold | Dump a full MF Classic card via Chameleon |
| Magic Detector | Gold | Detect a magic/gen card via Chameleon |
| Honey Harvest | Gold | Export MFKey32 detection records to SD |
| Blank Re-writer | Gold | Write an LF ID to a T5577 tag |
| Lockpick LF | Gold | Clear a locked T5577 tag password |
| Identity Library | Platinum | Clone 10 cards to ChameleonUltra slots |
| ... | | *32 achievements total* |
