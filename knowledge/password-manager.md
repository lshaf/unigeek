# Password Manager

Deterministic password vault built into the **HID** screens (BLE MouseKeyboard and USB MouseKeyboard).
Entries are protected by a master password and generated on the fly from a SHA256-based keystream — no plaintext passwords are ever stored.

## Setup

On first launch you are prompted to set a master password and confirm it.
All subsequent launches require you to enter the same master password to unlock the vault.

> [!tip]
> If you forget the master password, delete `/unigeek/hid/passwords/.master` and `/unigeek/hid/passwords/.vault` from the File Manager to start fresh. All stored entries will be lost.

## Main Menu

After unlocking, the screen lists all saved entries followed by an **Add New** item.

- **Select an entry** — open the View screen showing the generated password
- **Hold an entry** — popup with View / Type / Delete options
- **Add New** — open the add form

## Add Form

| Field | Options |
|-------|---------|
| Label | Any text up to 32 characters |
| Type | Alphanumeric · Alphabet only · Alphanumeric + Symbols |
| Case | Lower case · Upper case · Mixed case |
| Length | 8 – 34 characters |

Press **Save** to commit the entry. The password is deterministic — the same label, type, case, and length always produce the same password for a given master key.

## View Screen

Displays the generated password for the selected entry.

- **Press** — auto-type the password over HID to the connected host
- **Hold** — show the View / Type / Delete popup
- **Back** — return to the entry list

> [!note]
> Auto-type requires a BLE or USB HID connection. On BLE, typing is blocked if no device is connected.

## How Passwords Are Generated

Passwords are never stored. Each time you view an entry, the password is derived as:

```
seed = masterPw + "|" + label + "|" + type + "|" + caseMode + "|" + length
password = SHA256(seed) mapped to the chosen charset
```

Changing any field (including the master password) produces a completely different output.

## Storage

```
/unigeek/hid/passwords/.master    SHA256 hash of master password (32 bytes binary)
/unigeek/hid/passwords/.vault     Encrypted entry records (XOR cipher, binary)
```

The vault is XOR-encrypted with `SHA256(masterPw + "|ENC")`. Tampering with `.master` does not affect vault decryption — the encryption key is derived directly from the password you type.

## Achievements

| Achievement | Tier |
|-------------|------|
| Vault Opener | Bronze |
| Secret Keeper | Silver |
| One-Touch Login | Gold |