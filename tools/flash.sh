#!/usr/bin/env bash
# flash.sh — build, upload and monitor, without remembering the FQBN.
#
#   tools/flash.sh p1              build + upload + monitor the Day 1 sketch
#   tools/flash.sh p2              build + upload + monitor the production one
#   tools/flash.sh p2 --ota        upload over Wi-Fi to titan-bridge.local
#   tools/flash.sh p2 --build      compile only
#   tools/flash.sh p2 -p /dev/tty.usbserial-1420
#
# Needs arduino-cli and the esp32 core:
#   arduino-cli core install esp32:esp32 \
#     --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:-p2}"; shift || true
PORT=""; OTA=0; BUILD_ONLY=0; OTA_HOST="titan-bridge.local"

while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port) PORT="$2"; shift 2 ;;
    --ota)     OTA=1; shift ;;
    --host)    OTA_HOST="$2"; shift 2 ;;
    --build)   BUILD_ONLY=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$TARGET" in
  p1) SKETCH=firmware/titan_bridge_p1
      FQBN="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,UploadMode=default" ;;
  p2) SKETCH=firmware/titan_bridge_p2
      FQBN="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,UploadMode=default,PartitionScheme=min_spiffs" ;;
  *)  echo "usage: $0 {p1|p2} [options]" >&2; exit 2 ;;
esac

# USBMode=default IS USB-OTG (TinyUSB). The board definition's naming is
# unhelpful; USBMode=hwcdc is the other one.

echo "==> compiling $SKETCH"
arduino-cli compile --fqbn "$FQBN" "$SKETCH"
[ "$BUILD_ONLY" = 1 ] && exit 0

if [ "$OTA" = 1 ]; then
  echo "==> uploading over the network to $OTA_HOST"
  arduino-cli upload --fqbn "$FQBN" -p "$OTA_HOST" --protocol network "$SKETCH"
  exit 0
fi

if [ -z "$PORT" ]; then
  # Pick the first plausible USB serial device. The DevKitC-1's UART port
  # shows up as a CP2102 or a CH340 depending on the board revision.
  PORT=$(ls /dev/tty.usbserial-* /dev/tty.SLAB_USBtoUART* /dev/tty.wchusbserial* \
            /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1 || true)
  [ -z "$PORT" ] && { echo "no serial port found — pass -p /dev/..." >&2; exit 1; }
  echo "==> using $PORT"
fi

echo "==> uploading"
arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH"

echo "==> monitor (Ctrl-C to exit). Line ending must be Newline."
exec arduino-cli monitor -p "$PORT" -c baudrate=115200
