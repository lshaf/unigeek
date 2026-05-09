#!/usr/bin/env python3
"""
Fake DIAL receiver for testing UniGeek's Cast Bomb screen.

Pretends to be a smart TV / Chromecast on the LAN: responds to SSDP M-SEARCH
for `urn:dial-multiscreen-org:service:dial:1` and serves a tiny HTTP endpoint
that accepts the YouTube launch POST. Prints whatever video ID the device
sent.

Usage:
    python3 fake-dial.py             # use default port 56789
    python3 fake-dial.py --port 8008
    python3 fake-dial.py --name "Living Room TV"

No external dependencies — stdlib only.

Notes:
- Run on the same WiFi network as the ESP32.
- macOS / Linux: works as-is. Windows: run as Administrator (UDP 1900 bind).
- Firewall may need to allow inbound UDP 1900 + TCP <http port>.
"""

import argparse
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer


def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


# ── HTTP server ──────────────────────────────────────────────────────────────


def make_handler(ip: str, port: int, friendly_name: str):
    device_xml = (
        '<?xml version="1.0"?>\n'
        '<root xmlns="urn:schemas-upnp-org:device-1-0">\n'
        '  <specVersion><major>1</major><minor>0</minor></specVersion>\n'
        '  <device>\n'
        '    <deviceType>urn:schemas-upnp-org:device:tvdevice:1</deviceType>\n'
        f'    <friendlyName>{friendly_name}</friendlyName>\n'
        '    <manufacturer>UniGeek Test</manufacturer>\n'
        '    <modelName>FakeDIAL</modelName>\n'
        '    <UDN>uuid:fake-dial-test-1234</UDN>\n'
        '  </device>\n'
        '</root>\n'
    ).encode("utf-8")

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path in ("/dd.xml", "/dd.xml/"):
                self.send_response(200)
                self.send_header("Content-Type", "application/xml")
                self.send_header("Application-URL", f"http://{ip}:{port}/apps/")
                self.send_header("Content-Length", str(len(device_xml)))
                self.end_headers()
                self.wfile.write(device_xml)
                print(f"[HTTP] {self.client_address[0]} GET {self.path}")
                return
            self.send_response(404)
            self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8", errors="replace") if length else ""

            if self.path.startswith("/apps/YouTube"):
                print()
                print("*" * 60)
                print(f" CAST RECEIVED  from {self.client_address[0]}")
                print("*" * 60)
                print(f" Path:    {self.path}")
                print(f" Body:    {body}")
                if body.startswith("v="):
                    vid = body.split("&", 1)[0][2:]
                    print(f" Video:   {vid}")
                    print(f" URL:     https://youtu.be/{vid}")
                print("*" * 60)
                print()
                self.send_response(201)
                self.send_header(
                    "Location", f"http://{ip}:{port}/apps/YouTube/run"
                )
                self.end_headers()
                return

            self.send_response(404)
            self.end_headers()

        def log_message(self, fmt, *args):
            pass

    return Handler


# ── SSDP responder ───────────────────────────────────────────────────────────


def ssdp_responder(ip: str, http_port: int, stop_event: threading.Event):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except (AttributeError, OSError):
        pass
    sock.bind(("", 1900))

    mreq = struct.pack(
        "4s4s",
        socket.inet_aton("239.255.255.250"),
        socket.inet_aton(ip),
    )
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(0.5)

    print(f"[SSDP] Listening on 239.255.255.250:1900 (bound iface {ip})")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(8192)
        except socket.timeout:
            continue
        except OSError:
            break

        text = data.decode("utf-8", errors="replace")
        if "M-SEARCH" not in text:
            continue
        if not (
            "dial-multiscreen" in text.lower()
            or "ssdp:all" in text.lower()
            or "upnp:rootdevice" in text.lower()
        ):
            continue

        reply = (
            "HTTP/1.1 200 OK\r\n"
            "CACHE-CONTROL: max-age=1800\r\n"
            "EXT: \r\n"
            f"LOCATION: http://{ip}:{http_port}/dd.xml\r\n"
            "SERVER: Linux/3.10 UPnP/1.0 FakeDIAL/1.0\r\n"
            "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
            "USN: uuid:fake-dial-test-1234::urn:dial-multiscreen-org:service:dial:1\r\n"
            "BOOTID.UPNP.ORG: 1\r\n"
            "CONFIGID.UPNP.ORG: 1\r\n"
            "\r\n"
        )
        try:
            sock.sendto(reply.encode("utf-8"), addr)
            print(f"[SSDP] Replied to M-SEARCH from {addr[0]}:{addr[1]}")
        except OSError as e:
            print(f"[SSDP] send error: {e}")

    sock.close()


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description="Fake DIAL receiver for UniGeek Cast Bomb testing.")
    ap.add_argument("--port", type=int, default=56789, help="HTTP port for device description + apps endpoint")
    ap.add_argument("--name", type=str, default="Fake Cast Target", help="Friendly name returned in device description")
    ap.add_argument("--ip", type=str, default=None, help="Override auto-detected local IP")
    args = ap.parse_args()

    ip = args.ip or local_ip()
    port = args.port

    print(f"Local IP:        {ip}")
    print(f"HTTP port:       {port}")
    print(f"Friendly name:   {args.name}")
    print(f"Description URL: http://{ip}:{port}/dd.xml")
    print(f"Application URL: http://{ip}:{port}/apps/")
    print()
    print("Open Cast Bomb on UniGeek and tap 'Discover & Cast'.")
    print("Press Ctrl+C to stop.")
    print()

    stop_event = threading.Event()

    Handler = make_handler(ip, port, args.name)
    http = HTTPServer(("0.0.0.0", port), Handler)
    http_thread = threading.Thread(target=http.serve_forever, daemon=True)
    http_thread.start()

    ssdp_thread = threading.Thread(
        target=ssdp_responder, args=(ip, port, stop_event), daemon=True
    )
    ssdp_thread.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        stop_event.set()
        http.shutdown()


if __name__ == "__main__":
    main()
