# TOTP Auth

A time-based one-time password (TOTP) authenticator built directly into the device. Store Base32 secrets, generate live OTP codes with countdown, and manage accounts from the device — no phone required.

> [!note]
> TOTP codes require accurate system time. Connect to a WiFi network first so the device syncs time via NTP. The view screen shows "No time sync" if the clock is not set.

## How It Works

The authenticator implements RFC 6238 TOTP using HMAC-SHA1, SHA-256, or SHA-512. Each account stores a Base32-encoded secret key and configuration (digits, period, algorithm) in a `.key` file on storage. Codes are computed on-device using the mbedTLS HMAC engine.

## Adding an Account

1. Open **Utility → TOTP Auth**
2. Select **Add New**
3. Fill in:
   - **Name** — display label (used as the filename)
   - **Secret** — Base32 secret key from your authenticator app or service (spaces are stripped automatically)
   - **Digits** — 6 or 8 digits (default: 6)
   - **Period** — 30 or 60 seconds (default: 30)
   - **Algorithm** — SHA1, SHA256, or SHA512 (default: SHA1; most services use SHA1)
4. Select **Save**

> [!tip]
> Most services (Google, GitHub, Microsoft, etc.) use 6-digit, 30-second, SHA1 TOTP. Only change digits/period/algorithm if your service specifically requires it.

## Viewing a Code

Tap an account name from the list to open the code view:

| Region | Content |
|--------|---------|
| Top | Account name (grey) |
| Centre | Live OTP code (`123 456` or `1234 5678`) |
| Below code | Current time (HH:MM:SS) |
| Bottom bar | Seconds remaining + colour-coded progress bar |

The progress bar turns red when ≤ 5 seconds remain. The code refreshes automatically every second. The display stays on while the code view is open (power-save is inhibited).

Press **Back** or **Select** to return to the account list.

## Context Menu (hold)

Hold the select button on any account row (≥ 700 ms) to open a context menu:

- **View** — open the code view screen for that account
- **Delete** — remove the account and its key file permanently

## Storage

Key files are stored as plain text at:

```
/unigeek/utility/totp/<name>.key
```

File format:
```
SECRET|DIGITS|PERIOD|ALGO
```

Example: `JBSWY3DPEHPK3PXP|6|30|SHA1`

Old files that contain only the secret (no pipes) load with default values (6 digits, 30 s, SHA1).

Up to **16 accounts** can be loaded at a time.

## Achievements

| Achievement | Tier |
|-------------|------|
| Time Keeper | Bronze |
| Key Chain | Bronze |
| OTP Veteran | Silver |
