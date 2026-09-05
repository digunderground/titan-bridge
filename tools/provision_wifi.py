#!/usr/bin/env python3
"""
provision_wifi.py — hand the bridge its Wi-Fi credentials over the UART
console, reading them from .env so they never appear on a command line, in
shell history, or in an agent transcript.

    python3 tools/provision_wifi.py [/dev/cu.usbmodemXXXX]

.env is parsed, not sourced. A .env is not a shell script: values legitimately
contain parentheses, spaces, `$`, quotes and `#`, all of which break `source`
and some of which would execute. This reads KEY=VALUE literally and performs
no expansion.

Looks for, first match wins:
    WIFI_SSID / WIFI_PASSWORD
    WIFI_SSID / WIFI_PASS
    TITAN_WIFI_SSID / TITAN_WIFI_PASSWORD

The bridge stores credentials in NVS, so this is a once-per-board job —
reflashing does not erase them. `forget` on the console clears them.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def parse_env(path):
    """Literal KEY=VALUE. No expansion, no execution, no shell."""
    out = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[7:].lstrip()
            if "=" not in line:
                continue
            key, val = line.split("=", 1)
            key, val = key.strip(), val.strip()
            if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
                val = val[1:-1]
            out[key] = val
    return out


def pick(env, *names):
    for n in names:
        if env.get(n):
            return env[n]
    return None


def main():
    env_path = os.path.join(ROOT, ".env")
    if not os.path.exists(env_path):
        sys.exit(f"no .env at {env_path}")

    env = parse_env(env_path)
    ssid = pick(env, "WIFI_SSID", "TITAN_WIFI_SSID")
    password = pick(env, "WIFI_PASSWORD", "WIFI_PASS", "TITAN_WIFI_PASSWORD")

    if not ssid or not password:
        print("Could not find Wi-Fi credentials in .env.")
        print("Expected WIFI_SSID with WIFI_PASSWORD (or WIFI_PASS).")
        print("Keys present (values hidden):")
        for k in env:
            print("  " + k)
        sys.exit(1)

    port = sys.argv[1] if len(sys.argv) > 1 else None
    if not port:
        import glob
        # The console lives on the UART bridge chip, not the S3's own native
        # USB port — and on this board both are named usbmodem*, so guessing
        # is unreliable. Make the human choose.
        cands = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*"))
        if len(cands) == 1:
            port = cands[0]
        else:
            print("Pass the UART console port explicitly. Candidates:")
            for c in cands:
                print("  " + c)
            sys.exit(1)

    try:
        import serial
    except ImportError:
        sys.exit("pyserial missing — pip3 install 'pyserial>=3.5'")

    print(f"provisioning via {port} — SSID {ssid!r}, password hidden")
    s = serial.Serial(port, 115200, timeout=0.4)
    time.sleep(0.5)
    s.reset_input_buffer()
    s.write(f"wifi {ssid} {password}\n".encode())
    s.flush()

    deadline = time.time() + 25
    buf = ""
    while time.time() < deadline:
        buf += s.read(512).decode("utf-8", "replace")
        if "rssi" in buf or "no join" in buf:
            break
    s.close()

    for line in buf.splitlines():
        if password in line:          # never echo the credential back
            continue
        if any(k in line for k in ("Wi-Fi", "mDNS", "UI + REST", "Roku", "rssi")):
            print("  " + line.strip())

    if "rssi" in buf:
        print("\njoined.")
    elif "no join" in buf:
        print("\nDID NOT JOIN — check the SSID is 2.4 GHz and the password is right.")
        print("The bridge falls back to its setup AP, so nothing is bricked.")
    else:
        print("\nNo clear result; check the console directly.")


if __name__ == "__main__":
    main()
