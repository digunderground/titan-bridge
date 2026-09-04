# Flashing

Two firmwares. Flash `titan_bridge_p1` first — it exists to answer the Day 1
questions and does nothing else. Once you know which channel works, flash
`titan_bridge_p2` and never look back.

Both are pure arduino-esp32. **No libraries to install.**

---

## Arduino IDE

Board Manager URL, if the ESP32 boards are not already installed:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then **Tools →**

| Setting | Value | Why |
|---|---|---|
| Board | ESP32S3 Dev Module | |
| **USB Mode** | **USB-OTG (TinyUSB)** | required for the HID keyboard |
| **USB CDC On Boot** | **Disabled** | keeps `Serial` on UART0 as the console |
| **Upload Mode** | **UART0 / Hardware CDC** | flash over the UART port while the native port is busy being a keyboard |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** | p2 needs the space for OTA |
| Flash Size | 8MB (or whatever your board has) | |
| PSRAM | as your board has | |
| Upload Speed | 921600 | drop to 115200 if uploads fail |

The three USB settings are the ones that actually matter. Get any of them
wrong and the symptom is confusing — no console, or no HID, or uploads that
only work when the projector is unplugged.

Port: the **UART** USB-C connector, not the one labelled USB.

Verified against arduino-esp32 **2.0.17** and **3.3.11**; both build clean.

### Sizes

| | Default partitions | Minimal SPIFFS |
|---|---|---|
| p1 | 29% | — |
| p2 | 81% (tight) | **54%** |

p1 fits anywhere. Use Minimal SPIFFS for p2 so OTA has room.

---

## arduino-cli

If you would rather not open the IDE:

```bash
arduino-cli core install esp32:esp32 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# Phase 1 diagnostic
FQBN1=esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,UploadMode=default
arduino-cli compile --fqbn "$FQBN1" firmware/titan_bridge_p1
arduino-cli upload  --fqbn "$FQBN1" -p /dev/tty.usbserial-XXXX firmware/titan_bridge_p1

# Production
FQBN2="$FQBN1,PartitionScheme=min_spiffs"
arduino-cli compile --fqbn "$FQBN2" firmware/titan_bridge_p2
arduino-cli upload  --fqbn "$FQBN2" -p /dev/tty.usbserial-XXXX firmware/titan_bridge_p2
```

`USBMode=default` **is** USB-OTG (TinyUSB); `USBMode=hwcdc` is the other one.
The naming is unhelpful but that is what the board definition calls them.

`tools/flash.sh` wraps all of the above, finds the port, and opens the monitor.

```bash
tools/flash.sh p1          # build, upload, monitor
tools/flash.sh p2
tools/flash.sh p2 --ota    # after the first flash, over Wi-Fi
```

Finding the port on macOS:

```bash
ls /dev/tty.usb*
```

The DevKitC-1's UART port shows up as a CP2102 or CH340 depending on the
revision. If two appear, the one that vanishes when you unplug the UART cable
is the right one.

---

## Serial monitor

115200 baud, **line ending = Newline**. Without the newline setting the board
receives your commands but never sees the end of a line, and appears dead.

Type `?` for the command list.

---

## First boot of p2

No Wi-Fi credentials are compiled in — the firmware you flash is the firmware
you could publish.

1. The board comes up as an access point called **TitanBridge-XXXX**, password
   `titanbridge`, LED blue.
2. Join it and open `http://192.168.4.1/`.
3. Enter your network in the Wi-Fi panel and save. It reboots.

Or from the console:

```
wifi MyNetwork mypassword
```

After that it lives at `http://titan-bridge.local/` and its IP is shown in
`status`. **Pin a DHCP reservation** — SofaBaton addresses devices by IP, and
the hub needs to be on the same subnet as the bridge because SSDP is multicast
and will not cross VLANs.

`forget` clears the credentials and reboots into setup mode.

---

## OTA

Once p2 is on the network, subsequent flashes go over Wi-Fi. The Arduino IDE
shows `titan-bridge` under Tools → Port → Network Ports.

```bash
arduino-cli upload --fqbn "$FQBN2" -p titan-bridge.local \
  --protocol network firmware/titan_bridge_p2
```

OTA is unauthenticated on the LAN. If that bothers you, set an
`ArduinoOTA.setPassword()` in `net.cpp` — one line, at the top of `startOta()`.

---

## Recovery

If a bad flash leaves the board unresponsive: hold **BOOT**, tap **RESET**,
release BOOT. That forces the ROM bootloader regardless of what the firmware
is doing, and the next upload will take. You will need this at some point.

To wipe stored settings — Wi-Fi, macros, IR bindings — without a full erase:

```
forget          # from the console: clears Wi-Fi only
```

Full NVS wipe:

```bash
esptool.py --chip esp32s3 erase_flash
```
