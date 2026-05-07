# WebAuthn Site Compatibility Log

Per-RP test results. Update one row at a time as you verify each site on
real hardware. Independent of the Windows-compat work in
`WINDOWS-COMPAT.md` — these results assume the platform layer is working
(macOS / Linux / Chrome).

Legend: **OK** = full register + signin verified · **PARTIAL** = registers
but signin fails (or vice-versa) · **FAIL** = won't register · **TODO** =
not yet tested.

## Test results

| Site | Status | Browser | Notes |
|---|---|---|---|
| webauthn.io | **OK** | Chrome (macOS) | Reference test harness — both register + signin work. Discoverable + non-discoverable. |
| github.com | TODO | — | Settings → Password and authentication → Passkeys → Add a passkey |
| google.com | TODO | — | account.google.com → Security → Passkeys and security keys → Create a passkey |
| microsoft.com (consumer) | TODO | — | Security → Advanced security options → Add a sign-in method |
| apple.com | TODO | — | iCloud doesn't support FIDO security keys for personal accounts as of writing — skip |
| demo.yubico.com/webauthntest | TODO | — | Useful for direct attestation + hmac-secret toggles |
| passkey.io | TODO | — | Minimal passkey demo; always discoverable |
| webauthn.me | TODO | — | Useful for raw JSON viewing |
| 1password.com | TODO | — | Their passkey registration flow |
| dropbox.com | TODO | — | Security keys are an option |
| coinbase.com | TODO | — | Has security key support |
| reddit.com | TODO | — | New passkey support |

## Per-site investigation template

When a site fails, capture:

1. **Browser console** (DevTools → Console) — exact error message, especially
   anything containing `NotAllowedError`, `InvalidStateError`, `SecurityError`
2. **Browser flags requested** — open DevTools → Network → look at the
   navigator.credentials.create/get options being passed:
   - `attestation` (none / direct / indirect / enterprise)
   - `userVerification` (preferred / required / discouraged)
   - `requireResidentKey` / `residentKey`
   - extensions array — does the site request `largeBlob`, `credProps`, etc.?
3. **Device serial log** — Grove `Serial1` on m5sticks3, Grove on m5_cores3
   (no log capture). Look for the WA_LOG lines around the failed cmd.
4. **Compare against webauthn.io baseline** — flip the same options on
   webauthn.io. If webauthn.io passes with the same options, the issue is
   site-specific. If it also fails, our implementation has a gap.

## Known site quirks (carried over from research, verify on your build)

- **Apple iCloud / Apple ID** — accepts only Apple-signed authenticators
  for native sign-in. WebAuthn key registration only works for "Sign in
  with Apple"-using third-party apps, not iCloud itself.
- **Banks / government sites** — frequently require attested authenticators
  with AAGUID present in FIDO MDS. UniGeek's AAGUID
  `e96b5d29-4318-4c6e-8f8f-a4a5e2b3c1d0` is not in MDS, so these will
  reject with `attestation: "enterprise"` or `"direct"` flows.
- **PayPal / Coinbase** — historically required attested keys. May have
  loosened to accept self-attestation; verify.
- **GitHub** — accepts self-attestation. Should work.
- **Google** — accepts self-attestation; passkey flow is fully discoverable.

## Reproducing webauthn.io's success

If you need a known-good baseline to compare against a failing site:

1. Open `https://webauthn.io`
2. Set **Username**: anything
3. **Attestation Type**: `none` (default)
4. **Discoverable Credential**: `Required`
5. **User Verification**: `Preferred`
6. Click **Register**
7. Confirm on device
8. Reload page, leave Username empty, click **Authenticate**
9. Confirm on device — passkey signin should complete

## Continuing investigation on PC

When you pick this back up:

1. Plug device into PC, open **HID > WebAuthn (USB)** screen
2. Wait for **Active** banner
3. Try the next TODO site in the table above
4. Fill in the row with status + browser + notes
5. If it fails, run through the per-site investigation template above
6. Commit the updated table separately so progress is tracked
