# Plans & Ideas

Backlog of confirmed TODOs, feature brainstorms, and prank ideas. Items move from "Ideas" → "TODO" once decided, and out of this file once shipped.

---

## TODO

- LoRa
- ST25 NFC implementation for LoRa — https://github.com/m5stack/M5Unit-NFC
- StickS3 IR receive not functional (RMT/ES8311 conflict); transmit works
- Thermal camera

---

## Feature Ideas

### High-impact, on-brand additions

**Apple Continuity decoder** *(Bluetooth)*
Mirror image of BLE Device Spam → iOS. Passively listens for Apple Continuity advertisements (AirDrop, Handoff, Nearby Action, Phone Call) and prints what nearby iPhones/Macs are silently announcing. Pairs with the existing BLE Detector.

**MIFARE DESFire reader** *(NFC PN532 / Chameleon)*
Most office badges since ~2019 are DESFire EV1/EV2, not Classic. Adding DESFire app/file enumeration would 10× the real-world usefulness of the NFC stack. Medium effort.

**Cipher & Hash Toolkit** *(Utility)*
Type/load text → live md5, sha1, sha256, base64, hex, rot13, morse, ascii85. CTF gold. Tiny code, fits on every board, no hardware deps.

**Logic Analyzer** *(Modules)*
4–8 GPIO sample at 1–10 MHz with I²C/SPI/UART decoding. CYD boards (240×320 + touch) are perfect waterfall displays.

**Offline MAC vendor database (OUI)** *(transverse upgrade)*
~50 KB packed `oui.bin` lookup. Every scanner screen — WiFi Analyzer, BLE Analyzer, BLE Detector, Wardrive — would show "Apple, Inc." instead of `b8:27:eb:...`. Single PR upgrade across the whole app.

### Medium-effort, niche but cool

**Meshtastic node** *(T-Lora Pager only)*
Already noted in `_learnings`. T-Lora Pager has the LoRa hardware sitting idle. Public-channel send/receive only.

**Universal AC remote** *(IR)*
IR send works; package it with brand-detect + a friendly cold/heat/fan UI. Different from TV-B-Gone since AC remotes are stateful (each press encodes the *full* state, not a delta).

**Multiplayer over ESP-NOW** *(Games)*
ESPNOW Chat already exists. Add 2-player Wordle race or Memory Sequence sync. Trivial protocol on top of existing transport.

**Lua HID bindings** *(Lua + HID)*
Expose `uni.hid.type("hello")`, `uni.hid.key("CTRL", "ALT", "DEL")`, `uni.hid.mouse_move(x, y)` to Lua. Lua scripts become DuckyScript-on-steroids — conditional logic, branches, loops, cryptography, all available.

### Easy wins (small additions)

- **JWT decoder** — paste a JWT, decode header+payload, check expiration
- **Color picker / RGB mixer** — useful for Lua / theme dev
- **Battery health graph** — sample voltage over weeks; spot degradation
- **Habit tracker** — already have Pomodoro; same persistence pattern
- **Pebble-style watch face** — M5StickS3 is wrist-sized; full-screen clock + battery + step count when paired with phone

### Hardware extensions (need new board work)

- **Thermal camera** — already in TODO. MLX90640 over I²C, low-res (32×24).

---

## Network Pranks

Ideas that exploit zero-auth consumer network protocols. Use only on networks you have permission to test.

### Visible reactions

**Network Identity Spoof** *(NetBIOS + mDNS)*
Register the device as 50+ phantom hostnames on the LAN. Anyone who opens "Network" in Windows or Finder sees:
```
DESKTOP-FBI-VAN
404-NOT-FOUND
johns-iphone (47)
PRINTER-DOWNSTAIRS
SCP-682
```
- **Protocol:** NetBIOS-NS broadcasts (UDP 137) + mDNS (`_smb._tcp`, `_workstation._tcp`)
- **Slot:** WiFi → Network → Hostname Spoofer
- **Effort:** Small. Both protocols are spam-friendly broadcast.

**Phantom AirDrop / Spotify Connect targets**
Bonjour-flood the LAN with fake `_airdrop._tcp`, `_spotify-connect._tcp`, `_googlecast._tcp` services pointing nowhere.
- Apple devices show 20 AirDrop targets that all fail; Spotify shows random "speakers" you can't connect to.
- Could combine with Hostname Spoof as one "mDNS Mayhem" screen.

**Printer Pranks** *(IPP / CUPS)*
Discover open print servers (IPP on TCP 631), send small joke print jobs (PDF/PS one-liners, ASCII art).
- **Slot:** WiFi → Network → Printer Spam
- **Caveat:** Use only on networks you own — wasting toner is theft in most jurisdictions.


### Subtle / atmospheric

**Captive Portal: Comic Sans Edition**
Drop a portal template that just renders "the internet is currently broken — please visit later" in Comic Sans, or a fake "system update required" page that runs forever.
- **Effort:** Pure HTML, zero firmware changes. Drop new template in `/unigeek/web/portals/`.

**mDNS Sleep Proxy hijack**
Apple devices use `_sleep-proxy._udp` to delegate Wake-on-LAN. Become the sleep proxy → never wake the original device → it stays asleep forever.
- **Caveat:** Mildly disruptive — annoying rather than funny.

**SSID History Roulette**
Combine existing **Karma Captive** + **Beacon Spam** to broadcast every popular probe-history SSID seen on the channel as real APs. Phones panic-connect to "Starbucks WiFi" / "xfinitywifi" / "Free Public WiFi" wherever they go.
- **Effort:** Small — orchestration of two existing screens.

### Themed beacon spam (zero new code)

`Beacon Attack → Spam mode (dictionary)` already supports text-file SSID lists. Just ship more lists on SD:

- `beacon_rickroll.txt` — 200 SSIDs that are Rick Astley lyric fragments
- `beacon_aphorisms.txt` — fortune-cookie quotes
- `beacon_404.txt` — error messages ("SSID not found", "Network down", "Try again later")
- `beacon_help.txt` — `HELP_IM_TRAPPED`, `CALL_THE_POLICE`, `THIS_IS_NOT_A_DRILL`
- `beacon_judging_you.txt` — `Stop_Watching_Anime_At_3AM`, `WeKnowWhatYouDownloaded`

### Ranking by laugh-per-line-of-code

| Idea | LoC | Reaction |
|---|---|---|
| Themed beacon SSID lists | 0 | Strong — 30 s to deploy |
| NetBIOS / mDNS hostname spoof | ~150 | Strong — slow burn |
| WoL Broadcaster | ~80 | Subtle but funny over time |
| Comic Sans portal | 0 (web only) | Medium |
