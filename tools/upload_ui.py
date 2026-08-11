#!/usr/bin/env python3
"""
Upload all Icy OS web UI files from data/ to the ESP32 SD card.
Run this from the project root while connected to the Icy-OS AP.
"""
import os
import sys
import http.client
import mimetypes
from pathlib import Path

HOST = "192.168.4.1"
PORT = 80
TOKEN = "admin"
DATA_DIR = Path(__file__).parent.parent / "data"

FILES = [
    "index.html",
    "style.css",
    "ui.js",
    "service-worker.js",
    "manifest.json",
    "favicon.svg",
    "wallpaper.jpg",
]


def multipart_body(filename, data):
    boundary = "----IcyOSUploadBoundary"
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: {mimetypes.guess_type(filename)[0] or 'application/octet-stream'}\r\n\r\n"
    ).encode("utf-8")
    body += data
    body += f"\r\n--{boundary}--\r\n".encode("utf-8")
    return body, boundary


def upload_one(filename, data):
    body, boundary = multipart_body(filename, data)
    conn = http.client.HTTPConnection(HOST, PORT, timeout=30)
    try:
        conn.request(
            "POST",
            f"/fs/upload?token={TOKEN}",
            body=body,
            headers={
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Content-Length": str(len(body)),
            },
        )
        resp = conn.getresponse()
        text = resp.read().decode("utf-8", errors="replace")
        return resp.status, text
    except Exception as e:
        return 0, str(e)
    finally:
        conn.close()


def main():
    if not DATA_DIR.is_dir():
        print(f"data directory not found at {DATA_DIR}", file=sys.stderr)
        sys.exit(1)

    failed = False
    for name in FILES:
        path = DATA_DIR / name
        if not path.exists():
            print(f"SKIP: {name} (not found)")
            continue
        data = path.read_bytes()
        status, text = upload_one(name, data)
        if status == 200:
            print(f"OK  : {name} ({len(data)} bytes)")
        else:
            failed = True
            print(f"FAIL: {name} -> HTTP {status}: {text}")

    if failed:
        print("\nSome uploads failed. Make sure you are connected to the Icy-OS AP.")
        sys.exit(1)
    print("\nAll files uploaded. Open http://192.168.4.1/ and clear browser cache.")


if __name__ == "__main__":
    main()
