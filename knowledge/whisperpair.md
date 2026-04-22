# WhisperPair

Tests nearby Bluetooth devices for the WhisperPair vulnerability (CVE-2025-36911) — a flaw in Google Fast Pair that allows unauthorized pairing without user interaction. Accessed from **Bluetooth > WhisperPair**.

## The Vulnerability

Google Fast Pair is a Bluetooth protocol (BLE service UUID `0xFE2C`) that lets accessories like headphones and speakers pair quickly with Android phones.

Pairing uses a **Key-Based Pairing (KBP)** handshake:

1. The accessory (provider) broadcasts its **ECDH public key** in BLE advertisements or exposes it via GATT.
2. The phone (seeker) reads the key, performs an ECDH exchange to derive a shared AES-128 secret, encrypts a 16-byte KBP Request, and writes it to the KBP characteristic.
3. The accessory decrypts the packet and — in a correct implementation — verifies the sender's identity before accepting.

**CVE-2025-36911:** Vulnerable devices skip the sender verification step. They accept any KBP Request that is encrypted with the correct derived key, even from a complete stranger. An attacker who reads the advertised public key can forge a valid packet and pair with the device without the owner's knowledge or any button press.

## How the Test Works

| Step | What Happens |
|------|-------------|
| 1. Scan | 5-second BLE scan; lists only devices advertising `0xFE2C` |
| 2. Connect | GATT connection to the selected device |
| 3. Locate KBP | Finds the Fast Pair service and Key-Based Pairing characteristic |
| 4. Extract key | Reads the 64-byte SECP256R1 public key from GATT or from advertisement service data (bytes 3–66, after the 3-byte Model ID) |
| 5. ECDH | Generates an ephemeral keypair; computes shared secret; uses first 16 bytes as AES-128 key |
| 6. Build packet | Constructs 16-byte KBP Request: `[type=0x00][flags=0x00][own MAC x6][random salt x8]` |
| 7. Encrypt | AES-128-ECB encrypts the packet with the derived key |
| 8. Send | Subscribes for notifications, then writes the encrypted packet to the KBP characteristic |
| 9. Verdict | If the device replies with a **KBP Response notification within 2 seconds** → **VULNERABLE**. No reply → Safe. |

The test uses our own ESP32 MAC address in the packet — an address the target device has never seen before. A correctly implemented device would reject this; a vulnerable one does not.

## KBP Packet Layout

```
Byte  0     Type   = 0x00 (Key-Based Pairing Request)
Byte  1     Flags  = 0x00 (seeker initiates)
Bytes 2–7   MAC address of the seeker (we use our own ESP32 address)
Bytes 8–15  Random salt (8 bytes, unique per attempt)
            ──────────────────────────────
            Total: 16 bytes → AES-128-ECB encrypted
```

## Cryptography

| Algorithm | Usage |
|-----------|-------|
| SECP256R1 ECDH | Key exchange — derive shared secret from peer's public key |
| AES-128-ECB | Encrypt the 16-byte KBP Request packet |
| CTR-DRBG | Cryptographic random number generation (via mbedtls) |

The public key is transmitted as 64 raw bytes (X \|\| Y coordinates, no `0x04` prefix). We prepend `0x04` internally to form a standard uncompressed EC point before importing it into mbedtls.

## What the Log Shows

```
Device:
<device name>
Connecting...
Connected
Key: GATT read      ← public key found via GATT read
  OR
Key: Ad data        ← public key found in advertisement service data
  OR
Key: none           ← key not available (test will use random fallback key)
ECDH done
Sending KBP...
Notif: YES          ← device replied → VULNERABLE
  OR
Notif: NO           ← no reply → Safe
>> VULNERABLE <<
  OR
>> Safe <<
```

## Usage

1. Go to **Bluetooth > WhisperPair**
2. The device scans for 5 seconds and lists all Fast Pair devices found
3. Select a device to test
4. The log shows each step of the handshake in real time
5. The final verdict is shown at the bottom: **VULNERABLE** or **Safe**
6. Press any button to return to the device list and test another device

## Notes

- A **VULNERABLE** result means the device accepted a forged KBP packet — it can be paired by anyone in BLE range without user interaction.
- If the public key is not available (neither in ads nor via GATT), the test falls back to a random key and will almost certainly report **Safe** — this is a test limitation, not confirmation the device is secure.
- The test does not actually complete a full pairing — it only confirms whether the device accepted the initial KBP handshake.
- Devices that have already started a legitimate pairing session may not respond during the test window.
## Achievements

| Achievement | Tier |
|------------|------|
| **Whisper Scout** | Bronze |
| **Broken Pairing** | Gold |
| **Vulnerability Hunter** | Gold |
