#!/usr/bin/env python3
"""One-command installer for Icy OS.

Usage:
    python3 tools/install.py                # flash firmware + upload data/ to SD
    python3 tools/install.py --no-flash     # only upload data/ to SD
    python3 tools/install.py --port /dev/cu.usbserial-XXXXXXXX

The ESP32 acts as the BRAIN (firmware lives in flash).
The micro-SD card is the STORAGE (the OS web assets and modules live on it).
This script flashes the brain and then copies the OS to the SD card over the
same USB/serial cable, so you do not need to connect to the Icy-OS Wi-Fi AP.
"""

import argparse
import glob
import os
import subprocess
import sys
import time

import serial

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FILES = [
    "index.html",
    "style.css",
    "ui.js",
    "service-worker.js",
    "manifest.json",
    "favicon.svg",
    "wallpaper.jpg",
]


def find_port():
    patterns = ["/dev/cu.usbserial-*", "/dev/tty.usbserial-*",
                "/dev/ttyUSB*", "/dev/ttyACM*"]
    for p in patterns:
        matches = glob.glob(p)
        if matches:
            return matches[0]
    return None


def wait_for_marker(ser, keywords, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            line = ser.readline().decode("utf-8", errors="replace").rstrip()
        except Exception as e:
            print("  serial error:", e)
            return False
        if line:
            print("  ->", line)
            if any(k in line for k in keywords):
                return True
    return False


def upload_one(ser, local_path, remote_name):
    size = os.path.getsize(local_path)
    print(f"[{remote_name}] {size} bytes")

    ser.write(f"__ICY_UPLOAD__ {size} {remote_name}\n".encode())
    ser.flush()

    t0 = time.time()
    ready = False
    while time.time() - t0 < 20:
        line = ser.readline().decode("utf-8", errors="replace").rstrip()
        if line:
            print("  ->", line)
            if line.startswith("READY "):
                ready = True
                break
            if line.startswith("FAIL "):
                return False, line
        if not ser.is_open:
            return False, "serial closed"
    if not ready:
        return False, "timeout waiting for READY"

    with open(local_path, "rb") as f:
        while True:
            chunk = f.read(1024)
            if not chunk:
                break
            ser.write(chunk)
            ser.flush()

    t0 = time.time()
    while time.time() - t0 < 60:
        line = ser.readline().decode("utf-8", errors="replace").rstrip()
        if line:
            print("  ->", line)
            if line.startswith("OK "):
                return True, line
            if line.startswith("FAIL "):
                return False, line
    return False, "timeout waiting for OK"


def main():
    parser = argparse.ArgumentParser(description="Flash Icy OS firmware and copy OS to SD.")
    parser.add_argument("--port", default=find_port(), help="USB/serial port of the ESP32")
    parser.add_argument("--no-flash", action="store_true", help="Skip firmware flashing")
    parser.add_argument("--data", default=os.path.join(PROJECT_DIR, "data"), help="OS asset folder")
    parser.add_argument("--files", default=",".join(DEFAULT_FILES), help="Comma-separated file list")
    args = parser.parse_args()

    if not args.port:
        print("No USB/serial port found. Connect the ESP32 or use --port.")
        sys.exit(1)

    if not args.no_flash:
        print("[1/3] Flashing firmware...")
        r = subprocess.run(["pio", "run", "-t", "upload", "-d", PROJECT_DIR], check=False)
        if r.returncode != 0:
            print("Firmware flash failed.")
            sys.exit(1)
        time.sleep(1)

    files = [f.strip() for f in args.files.split(",") if f.strip()]

    print(f"[2/3] Opening {args.port}...")
    with serial.Serial(args.port, 115200, timeout=0.5) as ser:
        ser.reset_input_buffer()
        print("[3/3] Waiting for Icy OS to boot and mount SD...")
        if not wait_for_marker(ser, ["__ICY_OS_READY__"], timeout=60):
            print("Boot/SD ready marker not seen. Is the device connected?")
            sys.exit(1)

        # Give the main loop a moment
        time.sleep(0.5)

        failed = []
        for name in files:
            local = os.path.join(args.data, name)
            if not os.path.exists(local):
                print(f"[{name}] File not found, skipping: {local}")
                continue
            ok, msg = upload_one(ser, local, name)
            if not ok:
                print(f"[{name}] FAILED: {msg}")
                failed.append(name)
            else:
                print(f"[{name}] OK")

        if failed:
            print(f"Upload errors: {failed}")
            sys.exit(1)

        print("\nAll done.")
        print("Connect to the 'Icy-OS' Wi-Fi and open http://192.168.4.1/")
        print("If the old UI still appears, press Ctrl+Shift+R to clear the cache.")


if __name__ == "__main__":
    main()
