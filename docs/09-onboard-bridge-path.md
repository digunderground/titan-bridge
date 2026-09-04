# The onboard-bridge path — testing with no dongle in the drawer

Phase 2 of the plan waits on a CH340 module in the post. It does not have to.

Every ESP32 dev board with a USB socket already has a USB-serial bridge chip
soldered to it — a CP2102 or a CH340, the same part the plan tells you to buy.
It is there to carry sketches from your laptop, but it does not care which
direction it faces. **Plug the board's own USB port into the projector and the
projector binds that chip as `/dev/ttyUSB0`**, exactly as it would an external
module. The ESP32 then talks to the projector through its own bridge, over
UART0.

That is the entire Phase 2 dongle path, with a USB cable and nothing else.

```
   [ESP32]  U0TXD/U0RXD  <-->  [CP2102 on the board]  <-->  USB  <-->  [projector]
             GPIO43/44                                                  ttyUSB0
```

---

## What it costs you

**The console.** UART0 is also the flashing and serial-monitor port. While it
is committed to the projector there is no laptop console, and nothing may print
to it — a stray `Serial.println` lands in the middle of a 2A2A frame. The
firmware handles this: set `CH_UART0` and `tlog()` stops writing to UART0, the
interactive console is compiled out, and everything moves to the web UI at
`http://titan-bridge.local/` and `/api/log`.

So the order is: **flash first, then unplug from the laptop, then plug into the
projector.** Reflashing means unplugging from the projector each time. This is
precisely the inconvenience the dual-USB DevKitC-1 exists to avoid.

**The HID channel.** Menu and manual focus have no serial equivalent and need a
real USB keyboard endpoint. The bridge chip cannot provide one. On this path
`titanHid()` returns false and says why. Everything on the serial side —
`temp`, inputs, picture modes, the sweep, the power state machine — works.

---

## Board profiles

Set `BOARD` in `config.h` (or `-DBOARD=…`) before flashing:

| Board | `BOARD` | Native USB | Default channel |
|---|---|---|---|
| ESP32-S3-DevKitC-1 | `BOARD_S3_DEVKITC` | yes, second USB-C | native CDC + UART1 |
| Heltec WiFi LoRa 32 V3/V4 | `BOARD_HELTEC_LORA` | not on the connector | UART0 |
| Classic ESP32 devkit | `BOARD_ESP32_WROOM` | none in the silicon | UART0 |

A board with no native USB has the `CH_NATIVE_CDC` bit masked out of whatever
you ask for, rather than failing to build. Asking for *only* that channel on
such a board is a compile error, because the result would be a bridge with no
way to reach the projector.

### Heltec WiFi LoRa 32 V3/V4

It is an ESP32-S3, so the silicon has a native USB device port — but the USB-C
connector is wired to the CP2102, not to the S3's D+/D-. Reaching the native
port means soldering a USB lead to GPIO19/20. Until then, `HAS_NATIVE_USB` is 0.

Pins are tight. The SX1262 owns GPIO8–14 and **the OLED owns GPIO17 and 18 —
which are exactly the DevKitC's UART1 pins**, so the stock mapping would fight
the display. The profile moves UART1 to GPIO2/3 and IR to GPIO6/7.

### Classic ESP32 (WROOM-32)

No USB peripheral at all, so no native CDC and no HID — but the serial half is
complete, and the serial half is what Day 1 is actually asking about.

On a **WROVER** (the module with PSRAM) GPIO16 and GPIO17 are wired to the
PSRAM and unusable. The profile's `P2_TX_PIN 17` is wrong for that module. It
only matters if you drive an external dongle on UART1; the UART0 path never
touches those pins.

---

## The hazards, in the order they will bite you

**1. Auto-reset.** The bridge chip's DTR and RTS lines are wired to `EN` and
`GPIO0` so the IDE can reset the board into the bootloader. When the
projector's serial daemon opens the port, its driver asserts DTR — and the
board reboots. This is the same effect as an ESP32 resetting when you open a
serial monitor.

One reset at open is harmless: the board boots in about two seconds and carries
on. A daemon that opens and closes the port repeatedly gives you a boot loop.
Watch `/api/log` — the uptime in each line restarting from zero is the tell.
If it happens, a 10 µF capacitor from `EN` to `GND` is the usual fix.

**2. Boot chatter.** The ROM bootloader prints to UART0 at 115200 on every
reset, so the projector's daemon receives a paragraph of ESP-IDF banner before
the first real frame. It is almost certainly ignored — the framing is
`2A 2A <len>` and the decoder drops anything else a byte at a time — but it is
noise on the wire, and worth remembering if the first frame after a reset is
ever missed.

**3. Power.** On this path the board is powered by the projector's USB rail.
That directly contradicts plan §5, which wants the bridge alive while the
projector sleeps so that it is still there to wake it.

For Day 1 that is fine, and Test 5 still gives you its answer:

- Board keeps running through standby → **the port stays powered**, and the
  wake path is real.
- Board dies → **the port dies too**, which is the `USB_DEAD_IN_STANDBY = 1`
  branch and sends you to a smart plug or HDMI-CEC.

Either way you learn the thing you needed to learn. For anything permanent,
power the board from its own 5 V supply.

---

## Running it

1. `BOARD` set to your board in `config.h`. Flash over USB from the laptop as
   normal.
2. Power-cycle it on the laptop's USB and let it come up in setup-AP mode.
   Join `TitanBridge-XXXX` (password `titanbridge`), open `http://192.168.4.1/`,
   give it your Wi-Fi. Confirm it appears at `http://titan-bridge.local/`.
   **Do this before it goes near the projector** — provisioning it while it is
   plugged into a projector with no console is a bad first experience.
3. Projector: Settings → General → **Serial Port Control = ON**.
4. Unplug from the laptop. Plug the board into a projector USB port with a
   known **data** cable.
5. Open the web UI on a phone. Press **Probe** (`/api/cmd?name=temp`).

`2A 2A 03 13 01 00 17` coming back, or `temp=normal` in the log, is Day 1's
success criterion — reached without the dongle, and without the DevKitC.

Then run the rest of the test card from the web UI: the input commands, the
`hdmi3` question, and Test 5. Copy the log into `logs/TEST-LOG.md`.

---

## What this does not answer

Test 1 — whether the projector accepts a **USB HID keyboard** — is untouched by
this path, and the macro engine's menu navigation depends on it.

You do not need any of this hardware to answer it. **Plug a USB keyboard into
the projector.** If the arrow keys and Enter move the OSD, the HID strategy is
sound. If they do nothing, find that out now rather than on Sunday.
