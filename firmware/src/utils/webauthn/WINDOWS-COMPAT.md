# WebAuthn Windows Compatibility — Working Notes

Status as of 2026-05-07: **device works on macOS / Linux / Chrome / Firefox / Safari, fails on Windows 10/11.**

## Known symptom

On Windows, `webauthn.dll` rejects UniGeek as a security key. Browsers fall
back to "no security key found" or hang on the host UI. macOS/Linux on the
same hardware enroll passkeys without complaint.

## Why

The arduino-esp32 USB stack on ESP32-S3 builds a composite USB device with:

```
Interface 0: CDC Communication   (HWCDC — Serial)
Interface 1: CDC Data
Interface 2: HID                 (FIDO, our descriptor)
```

Multiple anecdotes from FIDO-on-ESP32 projects (SoloKeys, pico-fido,
ChameleonUltra) suggest Windows webauthn.dll prefers either:

- A **HID-only** USB device (no CDC alongside), OR
- A device with the FIDO HID on its **own dedicated interface**, separate
  from any other HID class on the same composite device

Today's `USBFidoUtil` already takes exclusive ownership of arduino-esp32's
single HID interface (via `claimUsbProfile(UsbProfile::WEBAUTHN)`), so
kbd/mouse aren't on the bus. But CDC remains.

## Fix paths (by complexity)

### A. Disable HWCDC at compile time for WebAuthn-enabled boards

Set `ARDUINO_USB_CDC_ON_BOOT=0` in `platformio.ini` for the affected envs.
This removes the CDC interfaces; HID becomes the only interface.

**Trade-off:** `Serial` (HWCDC) goes away for that build — but we already
log to `Serial1` on Grove pins for `m5sticks3` since HWCDC dies once
TinyUSB takes the PHY. So the regression is small for boards that already
have a Grove debug path.

**Estimate:** 30 min. Config change + smoke test on Linux first.

### B. TinyUSB direct second-HID interface

Patch the platform's `tinyusb_config.h` to set `CFG_TUD_HID = 2`. Recompile
`libUSB.a`. Register FIDO as the second HID instance via `tud_hid_n_*`.

**Trade-off:** platform-level patch — `patch.py` would need to apply
`CFG_TUD_HID` patches before every build, OR users vendor a custom
arduino-esp32 fork. Significant maintenance overhead.

**Estimate:** 1–2 days, including platform patch + arduino-esp32 fork.

### C. Bypass arduino-esp32 USB entirely, use TinyUSB direct

Drop `USBHID` / `USB.begin()`. Call `tinyusb_driver_install()` ourselves,
provide our own `tud_descriptor_*` callbacks. Full control over the device
descriptor, configuration descriptor, every interface byte.

**Trade-off:** loses arduino-esp32's USB conveniences — every other USB
profile (kbd, mouse) on this boot would need similar treatment, OR we
keep them via arduino-esp32 and reconcile descriptors at runtime.

**Estimate:** 3–5 days. Highest reward, highest risk.

## Recommended attempt order

1. **Try A first** — cheap, might just work. Disable HWCDC for one board
   env (`m5sticks3` since it already has Serial1 debug), flash, plug into
   Windows. If `chrome://settings/securityKeys` shows the device, ship A
   for all WebAuthn-enabled boards and call it done.
2. **If A fails**, plug both UniGeek and a working FIDO key (Yubikey,
   SoloKey) into the Windows machine and capture both USB descriptors via
   `USBView.exe` or `Wireshark + USBPcap`. Diff them.
3. **Based on the diff**, decide between B and C.

## Windows test checklist

When testing on a Windows machine, capture:

- [ ] Output of `chrome://device-log` filtered to `usb` category
- [ ] Output of `chrome://settings/securityKeys` page
- [ ] Device Manager → Human Interface Devices → properties → Details tab
      → Hardware Ids (should show `HID\VID_303A&PID_xxxx`)
- [ ] Device Manager → ditto → Compatible Ids (should include
      `HID_DEVICE_UP:F1D0_U:0001`)
- [ ] `USBView.exe` dump of the full descriptor tree
- [ ] Optional: capture register attempt with `webauthn.io` and check
      what fails (browser console)

## Files to look at when implementing the fix

- `firmware/src/utils/webauthn/USBFidoUtil.cpp` — current arduino-esp32
  `USBHID` integration. The `kFidoReportDescriptor` array there is
  spec-correct (matches Yubikey 5).
- `firmware/src/utils/webauthn/UsbProfile.cpp` — single-HID-interface
  arbiter. Already prevents kbd/mouse from co-existing on the same boot.
- `platformio.ini` — `build_flags` per env. Where to set
  `ARDUINO_USB_CDC_ON_BOOT=0` for path A.
- `patch.py` — the platform validator. Can extend with TinyUSB config
  overrides for path B.

## Reference implementations

- **pico-fido** (`../pico-fido`): TinyUSB-direct FIDO HID for Pi Pico W.
  See `src/usb/usb_descriptors.c` for the descriptor template Windows
  accepts.
- **SoloKey 2**: `tinyusb_config.h` + ST32-Cube descriptors. HID-only
  composite device, no CDC.
- **ChameleonUltra**: `Application/src/usb_main.c` — sets CDC AND HID,
  works on Windows. Worth diffing the descriptor bytes against ours.
