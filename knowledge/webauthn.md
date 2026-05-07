# WebAuthn / FIDO2

Accessed from **HID > WebAuthn (USB)**. ESP32-S3 boards only.

UniGeek can act as a hardware security key, presenting itself to the host as a USB FIDO2 / WebAuthn authenticator. Browsers (Chrome, Safari, Firefox) and native apps that support WebAuthn can use it as a **passkey** for sign-in — no username, no password, just confirm on the device.

## First-time setup — Generate the master key

WebAuthn does not auto-generate the device's master key. Until you create one, the WebAuthn screen shows a "Setup needed" page and the host can't register anything.

1. Open **Utility > Manage WebAuthn**
2. Pick **BIP39 Generate** (the label switches to **BIP39 Regenerate** once a master already exists)
3. Confirm the warning, then pick a WiFi network from the scan list — internet is required so the device can sync its RTC via NTP for stronger random-number entropy
4. Wait for the sync (a few seconds) and the auto-generation
5. Write down the 24 BIP-39 words shown on screen — this is your only backup. Anyone who sees them can clone every passkey on the device.

> [!warn]
> Regenerating wipes every existing passkey on the device — old credential IDs are bound to the old master key and become uncrackable garbage. Only regenerate when you accept losing all current credentials.

## Daily use

1. Plug the device into a host computer over USB
2. Open **HID > WebAuthn (USB)**
3. The screen reads **Active** once the host has shaken hands with the FIDO HID transport
4. On the host, register the key wherever it offers "Use a security key" or "Add a passkey" (e.g. github.com → Security → Passkeys → Add)

> [!note]
> WebAuthn is a single-claim USB profile. If the device's USB has already been claimed by **HID > USB MouseKeyboard** this boot, the screen shows **USB busy** and asks you to reboot. Open WebAuthn before any other USB profile in a session.

## Manage WebAuthn menu

Under **Utility > Manage WebAuthn** you get four actions:

| Item | What it does |
|---|---|
| **BIP39 Generate** / **BIP39 Regenerate** | Creates the master key with WiFi+NTP-fueled randomness, then displays the 24-word seed. Regen wipes every existing cred. |
| **BIP39 Restore** | Re-enters a 24-word seed onto a new device (or after a wipe) so previously issued credentials still verify. Wipes any current state first. |
| **BIP39 Backup** | Re-displays the existing 24-word seed (PIN-gated when a PIN is set). Use this to verify you wrote it down correctly. |
| **Passkeys** | Lists every resident credential by RP and userName; press to delete. |

### Restore flow on different boards

- **Cardputer / cardputer_adv / t_lora_pager** (full keyboards) — type each word; ENTER commits when the prefix narrows to a single match (or when the typed text is itself a complete BIP-39 word — covers `fit` / `fitness`-style overlaps).
- **m5sticks3 / t_display_s3 / m5_cores3** (no keyboard) — pick legal next letters from a grid that auto-narrows; once ≤ N matches remain (N depends on screen size, e.g. 12 on m5sticks3, 33 on CoreS3), the grid switches to a tap-to-pick word list. CoreS3 supports finger-hover highlighting.

## Where it works

### Operating systems

| OS | Status | Notes |
|---|---|---|
| macOS 12+ | Full | All browsers; Safari uses the system-level WebAuthn API |
| Linux (libfido2 / Chrome / Firefox) | Full | Tested with `fido2-token`, Chrome, Firefox; `udev` rule may be required for non-root access |
| Windows 10/11 | **Not yet** | Windows rejects the current composite HID descriptor — fix planned in a follow-up release |
| ChromeOS | Untested | Should work since it's a Linux derivative; not yet verified |

### Browsers

| Browser | CTAP2 register | Passkey signin | hmac-secret / PRF | Manage / delete |
|---|---|---|---|---|
| Chrome 108+ | Yes | Yes | Yes | `chrome://settings/securityKeys` |
| Edge 108+ | Yes | Yes | Yes | `edge://settings/passkeys` |
| Safari 16+ (macOS) | Yes | Yes | Yes | System Settings → Passwords |
| Firefox 119+ | Yes | Partial | Yes | No built-in manager (use `fido2-token`) |

Firefox supports the protocol but its passkey UX is younger than Chrome's — discoverable signins work but the credential manager UI isn't as polished. Use libfido2 (`fido2-token -L -r`) if you need to inspect or delete creds from Firefox.

### Confirmed-working sites

These have been tested end-to-end (register + sign in + delete) on a UniGeek key:

- **github.com** — Settings → Password and authentication → Security keys / Passkeys
- **google.com** — Account → Security → 2-Step Verification → Security key (also passkey path)
- **microsoft.com** (consumer accounts) — Security → Advanced security options → Add a sign-in method
- **webauthn.io** — public test harness, supports every option
- **demo.yubico.com/webauthntest** — exposes hmac-secret / direct attestation toggles
- **passkey.io** — minimal passkey demo

Most major SaaS that advertises "Sign in with a security key" or "Sign in with a passkey" works. Sites that explicitly require attested or enterprise authenticators (rare — mostly government / banking) will reject UniGeek because the AAGUID isn't in FIDO MDS.

## Common scenarios

### Add a passkey to GitHub

1. Open **HID > WebAuthn (USB)** on the device
2. On the host: github.com → Settings → Password and authentication → Passkeys → **Add a passkey**
3. The browser prompts to use a security key — pick "USB security key"
4. The device's screen shows **Confirm: github.com** — press **ENTER**
5. Optionally name the passkey on GitHub's side (e.g. "UniGeek M5Cardputer")

To sign in next time: GitHub login page → "Sign in with a passkey" → confirm on device.

### Add a passkey to Google

1. Same device prep (open HID > WebAuthn (USB))
2. account.google.com → Security → How you sign in to Google → **Passkeys and security keys** → **Create a passkey**
3. Pick "Use another device" → "USB security key"
4. Confirm on device when the screen prompts
5. Google sets a PIN at first registration if you don't already have one — the device walks you through it via Chrome's PIN dialog

### Sign in without typing a username (passkey flow)

This requires a **discoverable** credential, which most modern passkey registrations create automatically. After registering at github.com or google.com:

1. Open the site's sign-in page
2. Click **Sign in with a passkey** (or just leave the username field empty and tab into the password field — Chrome auto-offers)
3. The device shows **Confirm** — press **ENTER**
4. The site receives both your username and the signed assertion in one round-trip — you skip the password screen entirely

If multiple accounts are registered on the same site, the browser shows an account chooser populated by the device.

### Set a PIN (host-driven)

Run from a terminal once the device is plugged in:

```bash
# Linux / macOS with libfido2 installed:
fido2-token -S "$(fido2-token -L | head -1 | awk -F: '{print $1":"$2}')"
```

You'll be prompted for a new PIN twice. The PIN must be at least 4 characters. Chrome can also set the PIN through `chrome://settings/securityKeys` → "Create a PIN".

### Reset the device

```bash
fido2-token -R "$(fido2-token -L | head -1 | awk -F: '{print $1":"$2}')"
```

You must confirm the reset on the device within a short window of plugging it in. Reset wipes everything (see the **Reset** section below).

## Confirming an action

Each registration or sign-in triggers a **Confirm** prompt on the device. Press **ENTER** to authorize, **BACK** to deny. The prompt times out after 30 seconds; an unanswered prompt is treated as denied. The device beeps and wakes the screen on every prompt.

## Setting a PIN

A PIN is optional but required by some sites (any with `userVerification: required`). Set it from the host using your platform's security-key tool:

```
fido2-token -S /dev/hidrawN          # Linux / macOS
```

Or directly through Chrome's chrome://settings/securityKeys page. The device stores only a 16-byte SHA-256 prefix of the PIN, not the PIN itself. After 8 wrong attempts the PIN is permanently blocked and the only recovery is **Reset** (which also wipes every passkey).

## Passkeys (resident credentials)

When a site registers with **Discoverable credential = Required** (also called "passkey" or "rk=true"), UniGeek stores the credential on-device and lets you sign in **without typing a username**. The browser leaves the username field empty and the device returns the saved username + signs the assertion in one step.

If the same site has multiple passkeys (e.g. two separate accounts), the browser shows an account chooser populated by the device.

## Capabilities

| Feature | Status |
|---|---|
| CTAP2 / FIDO2 — `MakeCredential`, `GetAssertion`, `GetInfo`, `Reset`, `Selection` | Yes |
| Resident credentials (`rk=true`, passkeys) | Yes |
| `GetNextAssertion` — multi-account discoverable signin | Yes |
| `CredentialManagement` — browser-side passkey manager | Yes |
| `AuthenticatorConfig` — `toggleAlwaysUv` + `setMinPINLength` | Yes |
| ClientPIN proto v1 — set / change / verify on host (10-min idle timeout on the auth token) | Yes |
| `hmac-secret` extension (PRF) — site-bound 32-byte secrets | Yes |
| `largeBlob` extension — 4 KB device-stored array, per-cred `largeBlobKey` for SSH-key-bound logins / encrypted account data | Yes |
| U2F / CTAP1 backward compat — `REGISTER`, `AUTHENTICATE`, `VERSION` | Yes |
| Algorithms — ECDSA P-256 (`alg = -7`) | Yes |
| Attestation — packed self-attestation (default) + U2F batch attestation (legacy flow) | Yes |
| Transports — USB only (no BLE) | Yes |

## Browser passkey manager

Most browsers provide a UI that lists every passkey on the device and lets you delete individual entries. This works because UniGeek implements **CredentialManagement** (CTAP 2.1 §6.8). The browser will prompt for the device PIN before showing the list.

| Browser | Path |
|---|---|
| Chrome | `chrome://settings/securityKeys` → Sign-in data |
| Edge | `edge://settings/passkeys` |
| Safari | System Settings → Passwords → Security keys |

A site-side equivalent exists at webauthn.io's "Manage" page.

## Algorithms and key data

The ES256 (P-256 ECDSA) algorithm is fixed. Each registration generates a fresh per-credential keypair. Private keys never leave the device — they are wrapped into the **credentialId** itself using AES-256-CBC keyed by the device master key, with an HMAC-SHA-256 tag binding the credentialId to a specific RP. Resident credentials additionally store the rpId, userId, and userName on the filesystem so the device can return them in passkey signins.

## Reset

Reset is a host-driven command — `fido2-token -R /dev/hidrawN` or your browser's "Reset security key" UI. It must be confirmed on the device. Reset wipes:

- Master key (every previously registered credential becomes invalid)
- Signature counter
- PIN
- All resident credentials
- U2F batch-attestation device key + cert

> [!danger]
> Reset is irreversible **without a BIP-39 backup**. If you wrote down the 24 words at generation time, you can return the device to the same master via **Utility > Manage WebAuthn > BIP39 Restore**, and previously registered credentials still verify. Without the backup, every site needs re-enrollment.

## Storage

```
/unigeek/utility/fido/master.bin       32-byte master key (created via Manage WebAuthn > BIP39 Generate)
/unigeek/utility/fido/counter.bin      4-byte big-endian global signature counter
/unigeek/utility/fido/pin.bin          retries(1) + pinLen(1) + pinHash(16) = 18 bytes
/unigeek/utility/fido/config.bin       flags(1) + minPinLen(1) + reserved(2) — alwaysUv flag etc.
/unigeek/utility/fido/u2f_priv.bin     32-byte ECDSA P-256 batch-attestation key
/unigeek/utility/fido/u2f_cert.der     ~500-byte self-signed P-256 certificate
/unigeek/utility/fido/largeblob.bin    up to 4 KB of host-encrypted blob data (largeBlob extension)
/unigeek/utility/fido/credentials/     one 386-byte file per resident credential
  <hex16_rp>_<hex16_user>.bin          hex16_rp = rpIdHash[0..7], hex16_user = sha256(userId)[0..7]
```

If an SD card is mounted at boot, all paths above sit on the SD; otherwise they land on internal LittleFS.

## AAGUID

`e96b5d29-4318-4c6e-8f8f-a4a5e2b3c1d0`

> [!note]
> When a relying party requests `attestation: "none"` (the default for most sites), Chrome strips the AAGUID from the credential before passing it to the site, replacing it with zeros. This is WebAuthn's privacy mechanism — passkey managers will show a blank AAGUID and "Provider: Unavailable" for these credentials. The device still sends the real AAGUID; the browser zeros it for the site. To verify the device's actual AAGUID, register against a site that requests `attestation: "direct"` (e.g. `demo.yubico.com/webauthntest`).

## Security caveats

> [!warn]
> Treat this authenticator as a **second** factor or as a backup key, not as the only thing standing between an attacker and your accounts. The master key is stored on internal flash with no hardware secure element. Anyone with physical access to the chip can dump flash and impersonate every passkey on the device. Set a PIN to require a separate verification step on the host side, but the PIN protects against host-side malware, not against physical extraction.

> [!note]
> Resident credentials store the username and rpId in plaintext alongside the wrapped private key. Anyone who reads the credentials directory can see which sites you've registered with and what username you used, even though they cannot derive the private key without the master.bin.

## Achievements

| Achievement | Tier |
|------------|------|
| Key in Hand | Bronze |
| Passkey Pioneer | Silver |
