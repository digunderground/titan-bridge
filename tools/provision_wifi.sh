#!/usr/bin/env bash
# provision_wifi.sh — hand the bridge its Wi-Fi credentials over the UART
# console, reading them from .env so they never appear on a command line, in
# shell history, or in an agent transcript.
#
#   tools/provision_wifi.sh [/dev/cu.usbmodemXXXX]
#
# .env needs one of these pairs (first match wins):
#   WIFI_SSID / WIFI_PASSWORD
#   WIFI_SSID / WIFI_PASS
#   TITAN_WIFI_SSID / TITAN_WIFI_PASSWORD
#
# The bridge stores them in NVS, so this is a once-per-board job — reflashing
# does not erase them. `forget` on the console clears them again.

set -euo pipefail
cd "$(dirname "$0")/.."

[ -f .env ] || { echo "no .env in $(pwd)"; exit 1; }

# shellcheck disable=SC1091
set -a; . ./.env; set +a

SSID="${WIFI_SSID:-${TITAN_WIFI_SSID:-}}"
PASS="${WIFI_PASSWORD:-${WIFI_PASS:-${TITAN_WIFI_PASSWORD:-}}}"

if [ -z "$SSID" ] || [ -z "$PASS" ]; then
  echo "Could not find Wi-Fi credentials in .env."
  echo "Expected WIFI_SSID and WIFI_PASSWORD (or WIFI_PASS)."
  echo "Keys present:"
  sed -E 's/=.*/=.../' .env | grep -E '^[A-Za-z_]' | sed 's/^/  /'
  exit 1
fi

PORT="${1:-}"
if [ -z "$PORT" ]; then
  # The console is the UART bridge chip, not the S3's own native USB port.
  PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null | head -1 || true)
  [ -n "$PORT" ] || { echo "no serial port found — pass one explicitly"; exit 1; }
  echo "using $PORT (pass a port argument to override)"
fi

PY=".venv/bin/python"
command -v python3 >/dev/null || { echo "python3 required"; exit 1; }
[ -x "$PY" ] || PY=python3
$PY - "$PORT" "$SSID" "$PASS" <<'EOF'
import sys, time
try:
    import serial
except ImportError:
    sys.exit("pyserial missing — pip install -r tools/requirements.txt")

port, ssid, password = sys.argv[1], sys.argv[2], sys.argv[3]
s = serial.Serial(port, 115200, timeout=0.4)
time.sleep(0.5)
s.reset_input_buffer()
s.write(f"wifi {ssid} {password}\n".encode())
s.flush()

# Joining takes a few seconds; the bridge logs each stage.
deadline = time.time() + 25
buf = ""
while time.time() < deadline:
    buf += s.read(512).decode("utf-8", "replace")
    if "rssi" in buf or "no join" in buf:
        break
s.close()

for line in buf.splitlines():
    # Echo progress, but never the line carrying the password back.
    if password in line:
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
EOF
