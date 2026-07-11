# Unigotchi

A pwnagotchi-style WiFi companion that hunts WPA/WPA2 handshakes on its own and
shows its mood on a full-screen face — the same pixel-art character and speech
bubble used on the firmware's home screen. Captured handshakes are saved in the
exact same PCAP format and folder as **EAPOL Capture** (`/unigeek/wifi/eapol/`),
so the **EAPOL Brute Force** screen and offline tools (hashcat/hcxtools) read
them unchanged.

## Setup

1. Go to **WiFi > EAPOL Capture** and select **Unigotchi** at the bottom of the menu.
2. It starts in **Passive** mode. Press **OK** to open the **Options** box:
   - **Set Mode** — pick what the engine does:
     - **Passive Mode** *(default)* — listen only.
     - **Active Mode** — hunt with deauth.
     - **Pwngrid spam** — broadcast pwnagotchi advertisement beacons.
   - **Display Style** — pick how it's drawn (the capture engine is identical either way):
     - **Gotchi (face)** *(default)* — the animated hacker-head face + speech bubble.
     - **Text (log)** — a plain scrolling terminal-style event log.
     The choice is saved and restored on the next run.
3. Press **BACK** to leave.

The hacker-head face (its rank reflects your EXP, exactly like the home screen)
blinks while the speech bubble narrates what it's doing; the bottom bars track the
session totals (HS, PMKID, Deauth, Disassoc), and the top line shows the current
channel and mode.

## Modes

### Passive
Hops across the 2.4 GHz channels (1/6/11 first) and captures EAPOL frames and
PMKIDs that happen organically — no packets are transmitted. It dwells ~2.5 s per
channel so a real handshake has time to land. Slow but completely silent.

### Active
Runs an auto-attack state machine: **recon → lock → attack → listen**.
1. **Recon** — hops channels learning APs (from beacons) and their associated
   clients (from data frames).
2. **Lock** — picks the best target (recent clients + strongest signal) and parks
   on its channel.
3. **Attack** — sends one burst of **targeted deauth (Reason 7) + disassoc
   (Reason 8)** to each known client MAC (with broadcast as a fallback), using
   randomized inter-frame jitter to be less predictable to WIDS.
4. **Listen** — then stays quiet for ~8 s so the kicked client can reconnect and
   complete the 4-way handshake, with one extra nudge at the mid-point. On a
   capture the face celebrates; on a timeout it shows a "goodbye" and moves on.

Targeting real client MACs (not just broadcast) is far more reliable than a plain
broadcast deauth.

### Pwngrid spam
See below.

## Pwngrid spam

Pwngrid is the mesh/advertisement layer of [pwnagotchi](https://pwnagotchi.org):
units announce themselves by broadcasting 802.11 **beacon frames** that carry a
JSON payload (name, face, identity, pwned counts, uptime, version…) inside
vendor-tagged `0xDE` elements. Nearby pwnagotchis/palnagotchis listening for these
beacons add each other as "friends".

**Pwngrid spam** floods that advertisement space: Unigotchi broadcasts a stream of
those beacons with rotating names and faces. Depending on intent it announces your
presence, fills nearby units' friend lists, or just trolls other pwnagotchis
(same idea as Bruce / Evil-M5 PwnGridSpam).

### How it works
- On entering the mode, Unigotchi reads **`/unigeek/wifi/pwngrid.txt`** from the
  SD card. Each non-comment line is used as the `name` field of a pwngrid beacon.
  Lines starting with `#` and blank lines are ignored.
- It cycles through the names (and a small set of rotating faces) and broadcasts
  one beacon per channel hop across 1/6/11 + the rest. The bubble shows `TX: <name>`.
- Promiscuous capture is turned **off** in this mode (the radio is busy transmitting),
  so no handshakes are captured while spamming.
- If `pwngrid.txt` is missing, Unigotchi creates it with a documented default set
  on first run, so you always have a file to edit.

### `pwngrid.txt` format
```
# comments start with '#'
unigeek was here
pwned by unigeek
your beacons are mine
```
One name per line, `#` for comments, blanks ignored. Keep names short
(≤ ~50 chars). A starter file ships in `sdcard/unigeek/wifi/pwngrid.txt`.

## Storage

Handshakes/PMKIDs → `/unigeek/wifi/eapol/<BSSID>_<SSID>.pcap` (same as EAPOL
Capture). Validation pairs M1 (ANonce) with M2 (SNonce) by station MAC and works
even with no SD card inserted (counts on screen; only the PCAP needs storage).

## Achievements

Validated handshakes increment the shared EAPOL handshake achievements
(`wifi_eapol_handshake_valid`, tiers 1/5/20/50) — captures here count the same as
EAPOL Capture.
