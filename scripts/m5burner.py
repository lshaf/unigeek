#!/usr/bin/env python3
"""
M5Burner admin firmware helper.

Reads the auth token from `.env` (`m5_token=...`) and talks to
http://m5burner-api.m5stack.com/api/admin/firmware with the
`m5_auth_token` header.

Usage:
    python scripts/m5burner.py list                       # GET firmware index
    python scripts/m5burner.py upload <version>           # POST + auto-publish all m5* boards
    python scripts/m5burner.py upload <version> <env>...  # POST + auto-publish specific envs
    python scripts/m5burner.py upload <version> --dry-run # validate only
    python scripts/m5burner.py publish <version>          # PUT publish on already-uploaded m5* boards
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import sys
import urllib.error
import urllib.request
from pathlib import Path

API_URL = "http://m5burner-api.m5stack.com/api/admin/firmware"
ENV_KEY = "m5_token"

AUTHOR = "lshaf"
GITHUB = "https://github.com/lshaf/unigeek"

DESCRIPTION = (
    "UniGeek is a multi-tool firmware for ESP32 dev boards — pentesting, "
    "hardware experiments, and on-device Lua scripting all in one.\n"
    "\n"
    "Built-in tools include:\n"
    "- WebAuthn / FIDO2 USB security key (CTAP 2.1, passkeys, hmac-secret, "
    "largeBlob, BIP-39 backup)\n"
    "- WiFi recon (scan, evil-twin, EAPOL capture, deauth, karma)\n"
    "- BLE detector / analyzer / spammer\n"
    "- NFC dump and clone (PN532, MFRC522, Chameleon Ultra)\n"
    "- IR transmit/receive, Sub-GHz radio, Ducky scripts, GPS wardriving\n"
    "- Lua 5.1 scripting runner with input prompts, JSON, sprites, and SD I/O\n"
    "\n"
    "Drop in the SD-card bundle from the website to enable file-backed "
    "features. Full catalog, screenshots, and install guide at "
    "https://unigeek.xid.run"
)

# Mapping: PIO env → M5Burner project metadata.
M5_BOARDS: dict[str, dict[str, str]] = {
    "m5_cardputer":     {"name": "Cardputer",       "category": "cardputer"},
    "m5_cardputer_adv": {"name": "Cardputer Adv",   "category": "cardputer"},
    "m5_cores3":        {"name": "CoreS3",          "category": "cores3"},
    "m5stickcplus_11":  {"name": "StickC Plus 1.1", "category": "stickc"},
    "m5stickcplus_2":   {"name": "StickC Plus 2",   "category": "stickc"},
    "m5sticks3":        {"name": "StickS3",         "category": "sticks3"},
}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def load_env(path: Path) -> dict[str, str]:
    """Minimal .env parser: KEY=VALUE per line, quotes/escapes ignored."""
    if not path.is_file():
        return {}
    out: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        out[key.strip()] = value.strip().strip('"').strip("'")
    return out


def auth_token() -> str:
    env = load_env(repo_root() / ".env")
    token = os.environ.get(ENV_KEY) or env.get(ENV_KEY, "")
    if not token:
        sys.exit(f"error: {ENV_KEY} not set in .env or environment")
    return token


def request(method: str, url: str, *, token: str, body: bytes | None = None,
            content_type: str | None = None) -> tuple[int, bytes]:
    headers = {"m5_auth_token": token}
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()


def build_multipart(fields: dict[str, str], file_field: str,
                    file_path: Path) -> tuple[bytes, str]:
    """Build a multipart/form-data body. Returns (body, content_type)."""
    boundary = "----PythonBoundary" + secrets.token_hex(8)
    parts: list[bytes] = []
    for name, value in fields.items():
        parts.append(f"--{boundary}\r\n".encode())
        parts.append(
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode()
        )
        parts.append(value.encode("utf-8"))
        parts.append(b"\r\n")
    parts.append(f"--{boundary}\r\n".encode())
    parts.append(
        f'Content-Disposition: form-data; name="{file_field}"; '
        f'filename="{file_path.name}"\r\n'.encode()
    )
    parts.append(b"Content-Type: application/octet-stream\r\n\r\n")
    parts.append(file_path.read_bytes())
    parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


# ── commands ──────────────────────────────────────────────────────────

def cmd_list(_args: argparse.Namespace) -> int:
    status, body = request("GET", API_URL, token=auth_token())
    print(f"HTTP {status}")
    try:
        print(json.dumps(json.loads(body), indent=2, ensure_ascii=False))
    except json.JSONDecodeError:
        sys.stdout.buffer.write(body)
    return 0 if 200 <= status < 300 else 1


def _publish_one(token: str, fid: str, bin_id: str) -> tuple[int, bytes]:
    """PUT /api/admin/firmware/<fid>/publish/<bin_id>/1 — flips published=true."""
    url = f"{API_URL}/{fid}/publish/{bin_id}/1"
    return request("PUT", url, token=token)


def _print_response(status: int, body: bytes, indent: str = "  ") -> None:
    print(f"{indent}HTTP {status}")
    try:
        parsed = json.loads(body)
        print(f"{indent}{json.dumps(parsed, ensure_ascii=False)}")
    except json.JSONDecodeError:
        tail = body[:500].decode(errors="replace")
        print(f"{indent}{tail}")


def _resolve_targets(envs: list[str]) -> list[str]:
    if not envs:
        return list(M5_BOARDS.keys())
    unknown = [e for e in envs if e not in M5_BOARDS]
    if unknown:
        sys.exit(
            f"error: unknown env(s): {', '.join(unknown)}\n"
            f"       known: {', '.join(M5_BOARDS.keys())}"
        )
    return list(envs)


def _preflight(targets: list[str], version: str) -> tuple[list[Path], list[str], list[str]]:
    """Validate every binary exists and contains the expected version string.

    Returns (good_paths, missing_paths, mismatched_paths).
    The version check looks for the version followed by a NUL byte so that
    e.g. asking for '1.7' won't accidentally match '1.7.3\\0' in the .bin.
    """
    builds = repo_root() / "builds"
    needle = version.encode("utf-8") + b"\x00"
    good: list[Path] = []
    missing: list[str] = []
    mismatched: list[str] = []
    for env_key in targets:
        bin_path = builds / f"unigeek-{env_key}.bin"
        if not bin_path.is_file():
            missing.append(str(bin_path))
            continue
        if needle not in bin_path.read_bytes():
            mismatched.append(str(bin_path))
            continue
        good.append(bin_path)
    return good, missing, mismatched


def cmd_upload(args: argparse.Namespace) -> int:
    targets = _resolve_targets(args.envs)
    version = args.version

    good, missing, mismatched = _preflight(targets, version)
    if missing or mismatched:
        if missing:
            print("missing .bin files:", file=sys.stderr)
            for p in missing:
                print(f"  {p}", file=sys.stderr)
        if mismatched:
            print(
                f"\nversion '{version}' not baked into:", file=sys.stderr
            )
            for p in mismatched:
                print(f"  {p}", file=sys.stderr)
            print(
                "\nrebuild with the version flag, e.g.:\n"
                f"  PLATFORMIO_BUILD_SRC_FLAGS=-DFIRMWARE_VERSION='\"{version}\"' "
                "./scripts/build_all.sh",
                file=sys.stderr,
            )
        return 1

    print(f"pre-flight ok: {len(good)} bins, version '{version}'")
    for env_key, bin_path in zip(targets, good):
        spec = M5_BOARDS[env_key]
        size = bin_path.stat().st_size
        print(
            f"  {env_key:<18} → UniGeek {spec['name']:<16} "
            f"({spec['category']:<9}) {size:>8} bytes"
        )
    if args.dry_run:
        print("\n--dry-run: not uploading")
        return 0

    token = auth_token()
    failed: list[str] = []
    for env_key, bin_path in zip(targets, good):
        spec = M5_BOARDS[env_key]
        proj = f"UniGeek {spec['name']}"
        fields = {
            "name":        proj,
            "description": DESCRIPTION,
            "category":    spec["category"],
            "author":      AUTHOR,
            "version":     version,
            "github":      GITHUB,
            "cover":       "null",
        }
        body, ctype = build_multipart(fields, "firmware", bin_path)
        print(f"\n→ POST {proj} ({bin_path.stat().st_size} bytes)")
        status, resp = request("POST", API_URL, token=token,
                               body=body, content_type=ctype)
        _print_response(status, resp)
        if not (200 <= status < 300):
            failed.append(env_key)
            continue

        # Auto-publish: extract fid + this version's file id from the upload
        # response and PUT the publish endpoint.
        try:
            data = json.loads(resp)
        except json.JSONDecodeError:
            print("  (cannot auto-publish: response not JSON)")
            failed.append(env_key)
            continue
        fid = data.get("fid")
        target = next(
            (v for v in data.get("versions", []) if v.get("version") == version),
            None,
        )
        if not fid or not target:
            print("  (cannot auto-publish: missing fid / version in response)")
            failed.append(env_key)
            continue
        bin_id = target.get("file")
        if not bin_id:
            print("  (cannot auto-publish: missing file id)")
            failed.append(env_key)
            continue
        print(f"  → PUT publish {bin_id}")
        pstatus, pbody = _publish_one(token, fid, bin_id)
        _print_response(pstatus, pbody, indent="    ")
        if not (200 <= pstatus < 300):
            failed.append(env_key)

    if failed:
        print(f"\nfailed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nall uploads ok")
    return 0


def cmd_publish(args: argparse.Namespace) -> int:
    """PUT publish on already-uploaded m5* entries that match `version`."""
    version = args.version
    token = auth_token()

    status, body = request("GET", API_URL, token=token)
    if status != 200:
        print(f"failed to fetch firmware index: HTTP {status}", file=sys.stderr)
        return 1
    try:
        firmwares = json.loads(body)
    except json.JSONDecodeError:
        print("firmware index is not JSON", file=sys.stderr)
        return 1

    target_names = {f"UniGeek {spec['name']}" for spec in M5_BOARDS.values()}
    failed: list[str] = []
    matched = 0
    for fw in firmwares:
        name = fw.get("name", "")
        if name not in target_names:
            continue
        matched += 1
        fid = fw.get("fid")
        target = next(
            (v for v in fw.get("versions", []) if v.get("version") == version),
            None,
        )
        if not target:
            print(f"  [{name}] no version '{version}' uploaded yet, skip")
            continue
        if target.get("published"):
            print(f"  [{name}] v{version} already published, skip")
            continue
        bin_id = target.get("file")
        if not fid or not bin_id:
            print(f"  [{name}] missing fid / file id, skip", file=sys.stderr)
            failed.append(name)
            continue
        print(f"\n→ PUBLISH {name} v{version} ({bin_id})")
        status, resp = _publish_one(token, fid, bin_id)
        _print_response(status, resp)
        if not (200 <= status < 300):
            failed.append(name)

    if matched == 0:
        print("no UniGeek m5* firmwares found in M5Burner; "
              "did you run `upload` first?", file=sys.stderr)
        return 1
    if failed:
        print(f"\nfailed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nall publishes ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_list = sub.add_parser("list", help="GET the firmware index")
    p_list.set_defaults(func=cmd_list)

    p_up = sub.add_parser("upload", help="POST firmware to M5Burner")
    p_up.add_argument("version", help='version string (e.g. "1.7.3"); '
                                       "must match the value baked into the .bin")
    p_up.add_argument("envs", nargs="*",
                      help=f"specific envs to upload (default: all "
                           f"{len(M5_BOARDS)} m5* boards)")
    p_up.add_argument("--dry-run", action="store_true",
                      help="validate everything but don't POST")
    p_up.set_defaults(func=cmd_upload)

    p_pub = sub.add_parser("publish",
                           help="PUT publish on already-uploaded m5* entries")
    p_pub.add_argument("version",
                       help="version to publish (must already be uploaded)")
    p_pub.set_defaults(func=cmd_publish)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
