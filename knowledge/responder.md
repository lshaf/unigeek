# Responder

LLMNR / NBT-NS name poisoning with NTLMv2 hash capture — the classic LAN
credential-harvesting attack, on the device. Reachable from **WiFi → Network →
Responder** once connected to a network.

> Available on 8 MB / 16 MB boards only (compiled out on 4 MB boards for flash
> space). Needs an active WiFi connection.

## How it works

1. **Poisoning** — Windows hosts that fail a DNS lookup fall back to **LLMNR**
   (UDP 5355) and **NBT-NS** (UDP 137) broadcasts asking "who is `\\fileserv`?".
   The device answers every such query with **its own IP**, so the victim
   connects to us instead.
2. **Capture** — a fake **SMB** server (TCP 445) runs the NTLM handshake: it
   sends an NTLM Type-2 challenge, the victim replies with a Type-3
   authenticate, and the device extracts the **NetNTLMv2** response.
3. The hash is written to **`/unigeek/wifi/responder/ntlm_hashes.txt`** in
   hashcat **mode 5600** format:
   `user::domain:serverchallenge:ntproof:blob`.

## Usage

1. Connect to the target WiFi (WiFi → Network).
2. Open **Responder**. It advertises as `UNIGEEK` and starts the LLMNR/NBT-NS/SMB
   listeners immediately.
3. The status bar shows the captured-hash count (left) and the last poisoned name
   (right); the log lists poisoned queries and captures. A capture beeps and plays
   the notification chime.
4. Press **BACK** to stop and free the sockets.

## Cracking

Copy the file off the SD card and run:

```
hashcat -m 5600 ntlm_hashes.txt wordlist.txt
```

## Notes

- Captures only land when a victim actually tries to reach a name that doesn't
  resolve (a mistyped share, a stale mapped drive, WPAD, etc.) — it is
  opportunistic, like PC Responder.
- SMB signing being *required* on the victim does not stop the hash capture (the
  hash is still sent), only its relay — which this tool does not do.
- Ported from Bruce (`modules/wifi/responder.cpp`, originally 7h30th3r0n3 /
  Evil-M5Project). mDNS poisoning and SMB-relay are not implemented.
