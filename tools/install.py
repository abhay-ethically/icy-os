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
import json
import os
import subprocess
import sys
import tempfile
import time

import serial

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def collect_data_files(data_dir):
    files = []
    for root, dirs, names in os.walk(data_dir):
        # skip hidden and dot directories
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for n in names:
            if n.startswith('.'): continue
            local = os.path.join(root, n)
            rel = os.path.relpath(local, data_dir)
            files.append((local, rel.replace(os.sep, '/')))
    # root files first, then deeper files, for a stable order
    files.sort(key=lambda x: x[1].count('/'))
    return files


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
    parser.add_argument("--sta-ssid", default="", help="Pre-configure saved STA Wi-Fi network (uploaded in settings.json)")
    parser.add_argument("--sta-pass", default="", help="Saved STA Wi-Fi password")
    parser.add_argument("--ntp", default="pool.ntp.org", help="NTP server for pre-configured settings")
    parser.add_argument("--no-reboot", dest="reboot", action="store_false", default=True, help="Do not reboot the ESP32 after upload")
    args = parser.parse_args()

    if not args.port:
        print("No USB/serial port found. Connect the ESP32 or use --port.")
        sys.exit(1)

    if not args.no_flash:
        print("[1/3] Flashing firmware...")
        r = subprocess.run(["pio", "run", "-t", "upload", "-d", PROJECT_DIR, "--upload-port", args.port], check=False)
        if r.returncode != 0:
            print("Firmware flash failed.")
            sys.exit(1)
        time.sleep(1)

    files = collect_data_files(args.data)
    if not files:
        print("No files found in data/.")
        sys.exit(1)

    tmp_settings = None
    if args.sta_ssid or args.sta_pass:
        tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".json")
        settings = {
            "ssid": "Icy-OS",
            "password": "Password123",
            "adminPass": "admin",
            "buzzerGPIO": -1,
            "channel": 1,
            "staSSID": args.sta_ssid,
            "staPassword": args.sta_pass,
            "ntpServer": args.ntp,
            "ntpOffset": 0
        }
        tmp.write(json.dumps(settings).encode())
        tmp.flush()
        tmp.close()
        tmp_settings = tmp.name
        files.insert(0, (tmp_settings, "settings.json"))

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
        for local, remote in files:
            ok, msg = upload_one(ser, local, remote)
            if not ok:
                print(f"[{remote}] FAILED: {msg}")
                failed.append(remote)
                break
            else:
                print(f"[{remote}] OK")

        if failed:
            print(f"Upload errors: {failed}")
            sys.exit(1)

        print("\nAll done.")

    if tmp_settings and os.path.exists(tmp_settings):
        os.unlink(tmp_settings)

    if args.reboot:
        print("Rebooting the ESP32...")
        with serial.Serial(args.port, 115200, timeout=0.5) as ser:
            ser.write(b"__ICY_REBOOT__\n")
            ser.flush()
            time.sleep(4)

    print("Connect to the 'Icy-OS' Wi-Fi and open http://192.168.4.1/")
    print("If the old UI still appears, press Ctrl+Shift+R to clear the cache.")


if __name__ == "__main__":
    main()
