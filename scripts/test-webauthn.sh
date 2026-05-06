#!/usr/bin/env bash
#
# WebAuthn / FIDO2 end-to-end test for UniGeek
#
# Walks every implemented CTAP2 case in order, with a clear header before each
# case so you know what to look at on the device's serial log + LCD.
#
# Requires:  libfido2 (brew install libfido2)  on macOS / Linux
#            openssl (for random clientDataHash)
#
# Usage:  ./test-webauthn.sh                # run all cases
#         ./test-webauthn.sh --from 6       # skip to case 6
#         ./test-webauthn.sh --skip-pin     # skip PIN-related cases (incl. CM/ACfg)
#         ./test-webauthn.sh --no-reset     # don't reset at the end
#
# Notes:
#   - Many cases require pressing the device button (User Presence). The
#     script will pause briefly so you have time to confirm.
#   - PIN cases use a fixed test PIN (default "123456"); change TEST_PIN below
#     if your device already has a different one set.
#   - If a case fails part-way through, later cases that depend on it will
#     also fail — the script keeps going so you can see the full picture.

set -u

# ── Config ────────────────────────────────────────────────────────────
TEST_PIN="${TEST_PIN:-123456}"
TEST_PIN_NEW="${TEST_PIN_NEW:-12345678}"
RP_ID="test.example.com"
TMPDIR="$(mktemp -d /tmp/unigeek-fido-XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

# ── Args ──────────────────────────────────────────────────────────────
FROM=1
SKIP_PIN=0
NO_RESET=0
while [ $# -gt 0 ]; do
  case "$1" in
    --from)      FROM="$2"; shift 2 ;;
    --skip-pin)  SKIP_PIN=1; shift ;;
    --no-reset)  NO_RESET=1; shift ;;
    *) echo "Unknown arg: $1"; exit 2 ;;
  esac
done

# ── Colors ────────────────────────────────────────────────────────────
if [ -t 1 ]; then
  C_RED=$(tput setaf 1); C_GRN=$(tput setaf 2); C_YEL=$(tput setaf 3)
  C_BLU=$(tput setaf 4); C_DIM=$(tput dim);     C_BLD=$(tput bold)
  C_RST=$(tput sgr0)
else
  C_RED=""; C_GRN=""; C_YEL=""; C_BLU=""; C_DIM=""; C_BLD=""; C_RST=""
fi

PASS=0; FAIL=0; SKIP=0

case_header() {
  local n="$1" title="$2" what="$3"
  if [ "$n" -lt "$FROM" ]; then SKIP=$((SKIP+1)); return 1; fi
  echo
  echo "${C_BLU}${C_BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RST}"
  echo "${C_BLU}${C_BLD}  CASE $n  ▸  $title${C_RST}"
  echo "${C_BLU}  Testing: ${C_DIM}$what${C_RST}"
  echo "${C_BLU}${C_BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RST}"
  return 0
}

note() { echo "${C_YEL}  ⚠  $*${C_RST}"; }
hint() { echo "${C_DIM}  ↳  $*${C_RST}"; }

ok()   { echo "${C_GRN}  ✓ PASS${C_RST}";       PASS=$((PASS+1)); }
bad()  { echo "${C_RED}  ✗ FAIL (rc=$1)${C_RST}"; FAIL=$((FAIL+1)); }

run() {
  echo "${C_DIM}  $ $*${C_RST}"
  "$@"
  local rc=$?
  if [ $rc -eq 0 ]; then ok; else bad $rc; fi
  return $rc
}

# Run with PIN piped on stdin.
run_pin() {
  echo "${C_DIM}  $ echo PIN | $*${C_RST}"
  echo "$TEST_PIN" | "$@"
  local rc=$?
  if [ $rc -eq 0 ]; then ok; else bad $rc; fi
  return $rc
}

press_device() {
  echo "${C_YEL}  ⮕  Press the device button when prompted (will time out in 30 s)${C_RST}"
}

# Build a valid fido2-cred input file.
# args: outfile rpid username userid_text [hmac=0|1]
mk_cred_input() {
  local out="$1" rp="$2" uname="$3" uid="$4" hmac="${5:-0}"
  {
    openssl rand 32 | base64
    echo "$rp"
    echo "$uname"
    echo -n "$uid" | base64
  } > "$out"
}

# Build a fido2-assert input file.
# args: outfile rpid [credid_b64]
mk_assert_input() {
  local out="$1" rp="$2" cid="${3:-}"
  {
    openssl rand 32 | base64
    echo "$rp"
    [ -n "$cid" ] && echo "$cid"
  } > "$out"
}

# ── Pre-flight ────────────────────────────────────────────────────────
echo "${C_BLD}WebAuthn / FIDO2 end-to-end test${C_RST}"
echo "${C_DIM}Working dir: $TMPDIR${C_RST}"

if ! command -v fido2-token >/dev/null 2>&1; then
  echo "${C_RED}libfido2 not installed (need fido2-token / fido2-cred / fido2-assert)${C_RST}"
  echo "${C_DIM}macOS: brew install libfido2 — Linux: apt install libfido2-dev libfido2-tools${C_RST}"
  exit 1
fi

DEV="$(fido2-token -L | head -1 | awk -F: '{print $1":"$2}' | tr -d ' ')"
if [ -z "$DEV" ]; then
  echo "${C_RED}No FIDO device found. Open HID > WebAuthn (USB) on the device.${C_RST}"
  exit 1
fi
echo "${C_GRN}Device:${C_RST} $DEV"

# ─────────────────────────────────────────────────────────────────────
# CASE 1 — GetInfo
# ─────────────────────────────────────────────────────────────────────
if case_header 1 "GetInfo (CTAP2 0x04)" "AAGUID, options map (rk/up/credMgmt/clientPin/authnrCfg/alwaysUv/setMinPINLength), minPINLength"; then
  hint "Look for: aaguid e96b5d29..., options{rk,up,credMgmt,clientPin,authnrCfg,alwaysUv,setMinPINLength,pinUvAuthToken}, minPINLength: 4"
  run fido2-token -I "$DEV"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 2 — Reset (clean slate before functional tests)
# ─────────────────────────────────────────────────────────────────────
if case_header 2 "Reset (CTAP2 0x07)" "wipe master + counter + creds + PIN + config"; then
  note "Spec: must be confirmed within 10 s of plug-in. If you've been connected longer, replug first."
  press_device
  run fido2-token -R "$DEV"
  hint "All previously registered creds are now invalid (intended)."
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 3 — MakeCredential (non-resident, no PIN)
# ─────────────────────────────────────────────────────────────────────
if case_header 3 "MakeCredential non-resident (CTAP2 0x01)" "basic register, no rk, no PIN"; then
  mk_cred_input "$TMPDIR/mc1.in" "$RP_ID" alice user-alice
  press_device
  run fido2-cred -M -i "$TMPDIR/mc1.in" "$DEV" > "$TMPDIR/mc1.out"
  if [ -s "$TMPDIR/mc1.out" ]; then
    fido2-cred -V -i "$TMPDIR/mc1.out" > "$TMPDIR/mc1.verified" 2>&1
    hint "Verified credential id: $(awk 'NR==1 {print substr($0,1,40)"..."}' "$TMPDIR/mc1.verified" 2>/dev/null)"
    # Save credId for case 4 (GA with allowList)
    awk 'NR==1' "$TMPDIR/mc1.verified" > "$TMPDIR/mc1.credid"
  fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 4 — GetAssertion with allowList
# ─────────────────────────────────────────────────────────────────────
if case_header 4 "GetAssertion with allowList (CTAP2 0x02)" "non-discoverable signin against credId from case 3"; then
  if [ ! -s "$TMPDIR/mc1.credid" ]; then
    note "Skipping — no credId from case 3"
  else
    mk_assert_input "$TMPDIR/ga1.in" "$RP_ID" "$(cat "$TMPDIR/mc1.credid")"
    press_device
    run fido2-assert -G -i "$TMPDIR/ga1.in" "$DEV"
  fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 5 — MakeCredential resident (rk=true), user A
# ─────────────────────────────────────────────────────────────────────
if case_header 5 "MakeCredential resident, user 'alice' (rk=true)" "passkey: stored on device under /unigeek/utility/fido/credentials/"; then
  mk_cred_input "$TMPDIR/mc_a.in" "$RP_ID" alice user-alice
  press_device
  run fido2-cred -M -r -i "$TMPDIR/mc_a.in" "$DEV" > "$TMPDIR/mc_a.out"
  hint "Check serial log for: 'MC: resident cred written for rpId=$RP_ID'"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 6 — MakeCredential resident, user B (same RP, different userId)
# ─────────────────────────────────────────────────────────────────────
if case_header 6 "MakeCredential resident, user 'bob' (rk=true, same RP)" "second passkey on same RP — sets up GetNextAssertion test"; then
  mk_cred_input "$TMPDIR/mc_b.in" "$RP_ID" bob user-bob
  press_device
  run fido2-cred -M -r -i "$TMPDIR/mc_b.in" "$DEV" > "$TMPDIR/mc_b.out"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 7 — GetAssertion with empty allowList (passkey signin + GNA)
# ─────────────────────────────────────────────────────────────────────
if case_header 7 "GetAssertion empty allowList (CTAP2 0x02 + 0x08)" "discoverable signin: GA returns alice + numberOfCredentials=2, GNA returns bob"; then
  mk_assert_input "$TMPDIR/ga_empty.in" "$RP_ID"
  press_device
  hint "Expected output: TWO 'credential id:' blocks (one per resident cred)"
  run fido2-assert -G -i "$TMPDIR/ga_empty.in" "$DEV"
  hint "Serial log should show: 'GA: GNA armed credIdx=1/2' then 'GNA ok: idx=2/2'"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 8 — HMAC-secret deterministic check
# ─────────────────────────────────────────────────────────────────────
if case_header 8 "HMAC-secret extension (-h)" "PRF: same salt + same cred → same output bytes"; then
  mk_cred_input "$TMPDIR/mc_h.in" "$RP_ID" hmacuser user-hmac
  press_device
  fido2-cred -M -h -i "$TMPDIR/mc_h.in" "$DEV" > "$TMPDIR/mc_h.out" 2>/dev/null
  fido2-cred -V -h -i "$TMPDIR/mc_h.out" > "$TMPDIR/mc_h.verified" 2>/dev/null
  awk 'NR==1' "$TMPDIR/mc_h.verified" > "$TMPDIR/mc_h.credid"

  # Deterministic salt (so both runs produce identical output)
  HSALT=$(openssl rand 32 | base64)
  {
    openssl rand 32 | base64        # cdh
    echo "$RP_ID"
    cat "$TMPDIR/mc_h.credid"
    echo "$HSALT"
  } > "$TMPDIR/ga_h.in"

  press_device
  fido2-assert -G -h -t up=true -i "$TMPDIR/ga_h.in" "$DEV" > "$TMPDIR/ga_h1.out" 2>&1 || true
  press_device
  fido2-assert -G -h -t up=true -i "$TMPDIR/ga_h.in" "$DEV" > "$TMPDIR/ga_h2.out" 2>&1 || true
  if diff -q <(tail -1 "$TMPDIR/ga_h1.out") <(tail -1 "$TMPDIR/ga_h2.out") >/dev/null 2>&1; then
    ok
    hint "HMAC outputs match — extension is deterministic per cred + salt"
  else
    bad 1
    hint "Outputs differ — see $TMPDIR/ga_h{1,2}.out"
  fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 9 — ClientPIN setPIN (CTAP2 0x06 subcmd 0x03)
# ─────────────────────────────────────────────────────────────────────
if [ "$SKIP_PIN" -eq 1 ]; then
  note "Skipping all PIN-dependent cases (--skip-pin)"
elif case_header 9 "ClientPIN setPIN (subcmd 0x03)" "set initial PIN to '$TEST_PIN'"; then
  hint "fido2-token -S will prompt for new PIN twice; we pipe both via stdin"
  printf '%s\n%s\n' "$TEST_PIN" "$TEST_PIN" | fido2-token -S "$DEV"
  rc=$?
  if [ $rc -eq 0 ]; then ok; else bad $rc; fi
fi

# Skip remaining PIN cases if --skip-pin
if [ "$SKIP_PIN" -eq 0 ]; then

# ─────────────────────────────────────────────────────────────────────
# CASE 10 — ClientPIN getPINRetries (CTAP2 0x06 subcmd 0x01)
# ─────────────────────────────────────────────────────────────────────
if case_header 10 "ClientPIN getPINRetries (subcmd 0x01)" "should return 8 (kPinMaxRetries)"; then
  run fido2-token -I "$DEV"
  hint "Look for 'pin retries: 8' in the GetInfo output"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 11 — Negative: setPIN below minimum length
# ─────────────────────────────────────────────────────────────────────
if case_header 11 "Negative: setPIN with 3-char PIN (default min=4)" "must FAIL with PIN_POLICY_VIOLATION"; then
  echo "${C_DIM}  $ printf '123\\n123\\n' | fido2-token -C $DEV  (expect failure)${C_RST}"
  printf '%s\n%s\n%s\n' "$TEST_PIN" "123" "123" | fido2-token -C "$DEV" 2>&1 | tail -3
  if [ "${PIPESTATUS[1]}" -ne 0 ]; then
    ok
    hint "Correctly rejected (negative test passed)"
  else
    bad 0
    hint "Device accepted a too-short PIN — should not happen"
  fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 12 — CredentialManagement: enumerate RPs (CM subcmd 0x02/0x03)
# ─────────────────────────────────────────────────────────────────────
if case_header 12 "CM enumerateRPsBegin/GetNextRP (CTAP2 0x0A subcmds 0x02+0x03)" "list every RP with a stored resident cred"; then
  hint "Should list '$RP_ID' (one entry, since alice + bob share the RP)"
  run_pin fido2-token -L -r "$DEV"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 13 — CM: enumerate creds for RP (subcmds 0x04 / 0x05)
# ─────────────────────────────────────────────────────────────────────
if case_header 13 "CM enumerateCredentialsBegin/GetNext (subcmds 0x04+0x05)" "list every cred under '$RP_ID' — should show alice + bob (+ hmacuser)"; then
  run_pin fido2-token -L -k "$RP_ID" "$DEV"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 14 — CM: deleteCredential (subcmd 0x06)
# ─────────────────────────────────────────────────────────────────────
if case_header 14 "CM deleteCredential (subcmd 0x06)" "delete 'bob' resident cred — verify in case 13 output it's gone"; then
  # Extract bob's credId from case-13 output
  CRED_LINE=$(echo "$TEST_PIN" | fido2-token -L -k "$RP_ID" "$DEV" 2>/dev/null \
              | awk '/bob/ {print prev} {prev=$0}' | head -1)
  CRED_ID=$(echo "$CRED_LINE" | awk -F: '{print $NF}' | tr -d ' ')
  if [ -z "$CRED_ID" ]; then
    note "Could not parse bob's credId from CM output — skipping"
  else
    hint "Deleting credId: ${CRED_ID:0:30}..."
    run_pin fido2-token -D -i <(echo "$CRED_ID") "$DEV"
  fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 15 — AuthenticatorConfig: setMinPINLength (CTAP2 0x0D subcmd 0x03)
# ─────────────────────────────────────────────────────────────────────
if case_header 15 "ACfg setMinPINLength=8 (CTAP2 0x0D subcmd 0x03)" "raise minimum PIN length from 4 to 8"; then
  run_pin fido2-token -S -l 8 "$DEV"
  hint "Re-run case 1 — minPINLength field should now read 8"
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 16 — Negative: changePIN to a now-too-short PIN
# ─────────────────────────────────────────────────────────────────────
if case_header 16 "Negative: changePIN to 6-char (below new min 8)" "must FAIL with PIN_POLICY_VIOLATION"; then
  echo "${C_DIM}  $ printf '$TEST_PIN\\n123456\\n123456\\n' | fido2-token -C $DEV${C_RST}"
  printf '%s\n%s\n%s\n' "$TEST_PIN" "123456" "123456" | fido2-token -C "$DEV" 2>&1 | tail -3
  if [ "${PIPESTATUS[1]}" -ne 0 ]; then ok; else bad 0; fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 17 — ClientPIN changePIN to PIN that meets new min
# ─────────────────────────────────────────────────────────────────────
if case_header 17 "ClientPIN changePIN to 8-char PIN" "valid change above the new minimum"; then
  printf '%s\n%s\n%s\n' "$TEST_PIN" "$TEST_PIN_NEW" "$TEST_PIN_NEW" | fido2-token -C "$DEV"
  rc=$?
  if [ $rc -eq 0 ]; then ok; TEST_PIN="$TEST_PIN_NEW"; else bad $rc; fi
fi

# ─────────────────────────────────────────────────────────────────────
# CASE 18 — AuthenticatorConfig: toggleAlwaysUv (CTAP2 0x0D subcmd 0x02)
# ─────────────────────────────────────────────────────────────────────
if case_header 18 "ACfg toggleAlwaysUv (CTAP2 0x0D subcmd 0x02)" "flip alwaysUv flag — observe in GetInfo"; then
  run_pin fido2-token -S -a "$DEV"
  hint "Re-run case 1 — options.alwaysUv should now be flipped (true vs false)"
fi

fi  # end SKIP_PIN guard

# ─────────────────────────────────────────────────────────────────────
# CASE 19 — Reset (final cleanup, leaves device in pristine state)
# ─────────────────────────────────────────────────────────────────────
if [ "$NO_RESET" -eq 1 ]; then
  note "Skipping final reset (--no-reset)"
elif case_header 19 "Reset (final cleanup)" "wipe everything created during the test run"; then
  note "Replug the device first if needed (10-s plug-in window)"
  press_device
  run fido2-token -R "$DEV"
fi

# ── Summary ───────────────────────────────────────────────────────────
echo
echo "${C_BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RST}"
echo "${C_BLD}  Summary${C_RST}"
echo "  ${C_GRN}Passed:  $PASS${C_RST}"
echo "  ${C_RED}Failed:  $FAIL${C_RST}"
echo "  ${C_DIM}Skipped: $SKIP${C_RST}"
echo "${C_BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RST}"

exit $FAIL
