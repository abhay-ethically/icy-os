#!/usr/bin/env python3
"""Automated smoke test for Icy OS over Wi-Fi.

Connect your computer to the 'Icy-OS' AP first, then run:
    python3 tools/test_icy.py

Needs: websocket-client (pip install websocket-client)
"""
import json
import sys
import time
import urllib.request
from urllib.error import HTTPError

try:
    import websocket
except ImportError:
    print("Please install websocket-client: pip install websocket-client")
    sys.exit(1)

HOST = "192.168.4.1"
HTTP = f"http://{HOST}"
WS = f"ws://{HOST}/ws"
TOKEN = "admin"
TIMEOUT = 10

results = []


def http_test(path, expect_code=200, content_type=None, check_text=None):
    url = f"{HTTP}{path}"
    try:
        req = urllib.request.urlopen(url, timeout=5)
        code = req.getcode()
        body = req.read(1024)
        ct = req.headers.get('Content-Type', '')
        ok = code == expect_code
        if content_type and content_type not in ct:
            ok = False
        if check_text and check_text not in body.decode('utf-8', errors='replace'):
            ok = False
        mark = 'PASS' if ok else 'FAIL'
        results.append((f"HTTP {path}", mark, f"status={code} ct={ct}"))
        return ok
    except HTTPError as e:
        results.append((f"HTTP {path}", 'FAIL', f"HTTP {e.code}: {e.reason}"))
        return False
    except Exception as e:
        results.append((f"HTTP {path}", 'FAIL', str(e)))
        return False


def ws_wait(ws, expected_type, timeout=TIMEOUT, check=None):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            raw = ws.recv()
            if not raw:
                continue
            msg = json.loads(raw)
            if msg.get('type') == expected_type:
                if check is None or check(msg):
                    return msg
        except Exception as e:
            return None
    return None


def ws_test(name, cmd, expected_type, check=None, timeout=TIMEOUT):
    try:
        ws = websocket.create_connection(WS, timeout=timeout)
        # auth
        ws.send(json.dumps({"type": "auth", "password": TOKEN}))
        auth = ws_wait(ws, 'auth', 5, lambda m: m.get('data') is True)
        if not auth:
            results.append((name, 'FAIL', 'WebSocket auth failed'))
            return False
        if cmd:
            ws.send(json.dumps({"type": "cmd", "cmd": cmd}))
        msg = ws_wait(ws, expected_type, timeout, check)
        ok = msg is not None
        results.append((name, 'PASS' if ok else 'FAIL', msg.get('data', '')[:80] if ok else 'timeout'))
        ws.close()
        return ok
    except Exception as e:
        results.append((name, 'FAIL', str(e)))
        return False


def ws_settings():
    """Fetch current settings to decide if ping/STA tests can run."""
    try:
        ws = websocket.create_connection(WS, timeout=5)
        ws.send(json.dumps({"type": "auth", "password": TOKEN}))
        ws_wait(ws, 'auth', 5, lambda m: m.get('data') is True)
        ws.send(json.dumps({"type": "settings", "action": "get"}))
        msg = ws_wait(ws, 'settings', 5)
        ws.close()
        return msg.get('data') if msg else {}
    except Exception:
        return {}


def main():
    print("Icy OS smoke test")
    print(f"Target: {HTTP} / {WS}")
    print("Make sure you are connected to the Icy-OS Wi-Fi.\n")

    # HTTP tests
    http_test("/", check_text="Icy OS")
    http_test(f"/files?token={TOKEN}&path=/favicon.svg", content_type="image/svg+xml")
    http_test("/wallpaper.jpg")

    # WebSocket tests
    ws_test("WS auth", None, "auth", lambda m: m.get('data') is True)
    ws_test("help", "help", "terminal", lambda m: "Available commands" in str(m.get('data', '')))
    ws_test("ls /", "ls /", "files", lambda m: isinstance(m.get('data', []), list))
    ws_test("wifistatus", "wifistatus", "terminal", lambda m: "AP:" in str(m.get('data', '')))

    settings = ws_settings()
    sta_status = settings.get('staStatus', 'disconnected')
    if sta_status == 'connected':
        ws_test("ping 8.8.8.8", "ping 8.8.8.8", "terminal",
                lambda m: ("resolved to" in str(m.get('data', '')) or "TCP/80" in str(m.get('data', ''))))
    else:
        results.append(("ping 8.8.8.8", "SKIP", f"STA is {sta_status}"))

    # Attack test: start beacon, wait, stop, see packets
    try:
        ws = websocket.create_connection(WS, timeout=TIMEOUT)
        ws.send(json.dumps({"type": "auth", "password": TOKEN}))
        ws_wait(ws, 'auth', 5, lambda m: m.get('data') is True)

        ws.send(json.dumps({"type": "cmd", "cmd": "attack -t beacon -s Test -ch 1"}))
        time.sleep(2)
        ws.send(json.dumps({"type": "cmd", "cmd": "stopscan"}))

        # watch for sysinfo with attack packets
        pkt_count = 0
        t0 = time.time()
        while time.time() - t0 < 5:
            raw = ws.recv()
            if raw:
                msg = json.loads(raw)
                if msg.get('type') == 'sysinfo' and msg.get('data', {}).get('attack') != 'idle':
                    pkt_count = msg['data'].get('attack_pkts', 0)
                    break
        ws.close()
        ok = pkt_count > 0
        results.append(("attack beacon", "PASS" if ok else "FAIL", f"pkts={pkt_count}"))
    except Exception as e:
        results.append(("attack beacon", "FAIL", str(e)))

    print("\n" + "-" * 60)
    pass_count = sum(1 for _, m, _ in results if m == 'PASS')
    fail_count = sum(1 for _, m, _ in results if m == 'FAIL')
    skip_count = sum(1 for _, m, _ in results if m == 'SKIP')
    for name, mark, detail in results:
        print(f"[{mark:4}] {name:25} {detail}")
    print("-" * 60)
    print(f"PASS={pass_count} FAIL={fail_count} SKIP={skip_count}")
    sys.exit(0 if fail_count == 0 else 1)


if __name__ == "__main__":
    main()
