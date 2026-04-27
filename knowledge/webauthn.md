# WebAuthn / FIDO2

Accessed from **HID > WebAuthn (USB)**. ESP32-S3 boards only.

UniGeek can act as a hardware security key, presenting itself to the host as a USB FIDO2 / WebAuthn authenticator. Browsers (Chrome, Safari, Firefox) and native apps that support WebAuthn can use it as a passkey for sign-in.

## Setup

1. Plug the device into a host computer over USB
2. Open **HID > WebAuthn (USB)**
3. The screen reads **Active** once at least one inbound USB transaction has been observed
4. On the host, register the key wherever it offers "Use a security key" (e.g. github.com → Security → Passkeys → Add)

## Confirming an action

Each registration or login triggers a **Confirm** prompt on the device. Press **ENTER** to authorize, **BACK** to deny. The prompt times out after 30 seconds; an unanswered prompt is treated as denied.

## Capabilities

- **CTAP2 / FIDO2** — `MakeCredential`, `GetAssertion`, `GetInfo`, `Reset`
- **U2F / CTAP1 backward compat** — `AUTHENTICATE` and `VERSION` (legacy 2FA on credentials already registered via CTAP2)
- **Algorithm** — ECDSA P-256 (`alg = -7`, `crv = 1`, COSE EC2)
- **Attestation** — packed self-attestation (no batch certificate)
- **Resident credentials (rk)** — not yet supported; credentials live inside the credentialId itself, encrypted with the device's master key
- **Client PIN** — not yet supported
- **Transports** — USB only (BLE FIDO not implemented)

> [!note]
> The credentialId is a 96-byte AES-CBC ciphertext bound to the relying-party ID by HMAC-SHA-256. The device keeps no per-credential storage on disk; everything needed to authenticate is contained in the credentialId returned to the relying party at registration. Erasing the master key (via **Reset** in the future, or wiping `/unigeek/webauthn/master.bin`) invalidates **all** previously registered credentials.

## Storage

    /unigeek/webauthn/master.bin    32-byte master key (created on first use)
    /unigeek/webauthn/counter.bin   4-byte big-endian global signature counter

## Security caveats

> [!warn]
> This authenticator is intended for tinkering, learning, and as a backup key for accounts that already have a stronger primary 2FA. Do not rely on it as your sole 2FA for high-value accounts. The master key is stored unencrypted on the device's LittleFS — anyone with physical access to the flash can extract it and impersonate any registered passkey. A future Phase 5c PIN extension will derive an encryption key from a user-set PIN to address this.

> [!note]
> The AAGUID is currently zero (`00000000-0000-0000-0000-000000000000`). Some relying parties refuse to register zero-AAGUID authenticators. A unique UniGeek AAGUID will be picked in a follow-up release.

## Achievements

| Achievement | Tier |
|------------|------|
| **Key in Hand** | Bronze |
| **Passkey Pioneer** | Silver |
