# Wiring

Two phases. Phase 1 is one cable and an optional LED. Phase 2 adds the USB-TTL
dongle that most likely turns out to be necessary.

See `hardware/wiring.svg` for the picture.

---

## Phase 1 — bring-up

| From | To | Note |
|---|---|---|
| ESP32-S3-DevKitC-1 **UART** USB-C | laptop | flashing and the console |
| ESP32-S3-DevKitC-1 **USB** USB-C | projector USB-A | must be a *data* cable |
| IR LED anode | 100 Ω → GPIO4 | Test 6 only |
| IR LED cathode | GND | |

The dual-USB-C board is the whole point. USB HID mode disables serial upload
over the native port, so a single-port clone forces you to unplug the
projector every time you reflash.

**The cable matters.** A charge-only USB-C-to-A cable will produce a perfect
imitation of "the projector ignores the ESP32 entirely". If Test 1 fails,
change the cable before you change anything else.

---

## Dual path — both projector ports at once  (2026-09-05)

This is the target architecture in plan §5, and it is what to build now that
HID works and serial does not: **native USB carries HID on one projector port,
a CH340 carries serial on the other.** They are independent, so the working
channel keeps working while the experimental one is tested.

![dual path](../hardware/wiring-dualpath.svg)

No firmware change is needed. `BOARD=1` already sets
`SERIAL_CHANNELS = CH_NATIVE_CDC | CH_UART1`, so the bridge has been
transmitting `2A2A` frames out GPIO17 all along — into nothing. The adapter
just gives them somewhere to go.

| From | To | Note |
|---|---|---|
| ESP32-S3 **USB** (native) | projector USB 2.0 | HID + power. Already working. |
| CH340 **TXD** | ESP32 **GPIO18** | measure this line first |
| CH340 **RXD** | ESP32 **GPIO17** | |
| CH340 **GND** | ESP32 **GND** | common reference, not optional |
| CH340 **VCC** | **leave unconnected** | |
| CH340 **USB-A** | projector USB 3.0 | |

**What success looks like.** `rx` climbs and the log shows `RX 2A 2A …`. The
web UI's Source & picture buttons stop being dimmed by themselves, because
they key off whether a valid frame has ever arrived.

**What "bytes but no frames" means.** If `rx` climbs while no `RX` line
appears, something is talking but not in the frame format we expect — which
would put the Serial Port ID addressing back on the table. See
`logs/TEST-LOG.md`.

---

## Phase 2 — the USB-TTL dongle

Only needed if Day 1 Test 2 failed, which is the expected outcome. The
projector's serial daemon almost certainly opens `/dev/ttyUSB*` — created by
the ch341/cp210x/pl2303 drivers — while a CDC-ACM device appears as
`/dev/ttyACM*`.

### The jumper check — do this before anything is connected

"Switchable 3.3 V and 5 V" is ambiguous on these modules. Some designs switch
only the VCC **output** pin while the CH340G itself still runs at 5 V, leaving
TXD idling at 5 V. **The ESP32-S3's inputs are not 5 V tolerant.**

1. Set the jumper to 3.3 V.
2. Plug the module into a USB port with nothing else attached.
3. Measure **TXD to GND** with the multimeter.

An idle UART TX line sits high, so you want to read about **3.3 V**.

- **~3.3 V** → wire TXD straight to GPIO18.
- **~5 V** → put a divider on that line *only*:
  1 kΩ from TXD to GPIO18, 2 kΩ from GPIO18 to GND. (Gives ~3.33 V.)

Nothing else needs a divider: GPIO17 is an output driving the module's RXD,
and a 3.3 V signal comfortably exceeds a CH340's input threshold.

### Connections

| CH340 module | ESP32-S3 | Why |
|---|---|---|
| TXD | GPIO18 | via divider if it measured 5 V |
| RXD | GPIO17 | |
| GND | GND | common reference, not optional |
| VCC | **leave unconnected** | |
| USB-A | projector USB 3.0 port | |

**VCC stays disconnected deliberately.** You do not want the projector's USB
rail powering the bridge — the entire point is for the ESP32 to stay alive
while the projector sleeps, so that it is still there to wake it. Power the
ESP32 from its own 5 V supply.

That also means: once you stop watching the console, move the ESP32's UART
port from the laptop to a 5 V USB supply. It keeps running and you reach it
over Wi-Fi.

### If the CH340 does not bind

Escalate in this order. Each step costs a few pounds and removes one variable.

1. **CP2102** — different driver (`cp210x`), the second most likely to be
   present in a Chinese Android build.
2. **Genuine FTDI** — `ftdi_sio`. Beware counterfeits; a fake FT232 may be
   bricked by a driver update.
3. **USB-RS232 DB9 cable + MAX3232 breakout.** This is what XGIMI literally
   documents, so it is the highest-confidence path — just the most annoying to
   wire.

```
ESP32 GPIO17 (TX) ──► MAX3232 T1IN     MAX3232 T1OUT ──► DB9 pin 2 (RXD)
ESP32 GPIO18 (RX) ◄── MAX3232 R1OUT    MAX3232 R1IN  ◄── DB9 pin 3 (TXD)
ESP32 3V3         ──► MAX3232 VCC      GND common    ──► DB9 pin 5
```

The MAX3232 (not MAX3232**5**, not MAX232) is the 3.3 V part. Fit the four
0.1 µF charge-pump capacitors if your breakout does not have them.

Note the crossover: the ESP32's TX reaches the projector on the pin the
projector *receives* on. If nothing works and the wiring looks right, swap
pins 2 and 3 — DB9 gender changers and null-modem adapters make this easy to
get backwards.

---

## IR receiver — the offline fallback channel

| TSOP38238 pin | To |
|---|---|
| OUT | GPIO15 |
| GND | GND |
| VS | 3V3, through 100 Ω |

Plus **4.7 µF from VS to GND**, as close to the sensor as you can manage. The
resistor and capacitor are not optional decoration: they form the supply
filter the datasheet asks for, and without them a receiver picks up switching
noise from the ESP32's own regulator and reports phantom codes.

Output is active low and idles high. The p2 firmware decodes any NEC remote,
so you can bind spare buttons on a remote you already own — press one, read the
code out of the log, then `irmap 21DE:4D m:movie`.

---

## Pinout summary

| GPIO | Direction | Function | Notes |
|---|---|---|---|
| 4 | out | IR LED (p1 only) | 100 Ω in series |
| 15 | in | IR receiver (p2) | TSOP38238 OUT |
| 17 | out | UART1 TX → adapter RXD | |
| 18 | in | UART1 RX ← adapter TXD | 3.3 V only — check first |
| 48 | out | onboard WS2812 status LED | `STATUS_LED_PIN` = −1 to disable |

All configurable at the top of `firmware/titan_bridge_p2/config.h`.

Avoid GPIO19/20 (native USB D−/D+), 39–42 (JTAG), 26–32 (SPI flash/PSRAM), and
0/3/45/46 (strapping). GPIO35/36/37 are unavailable on octal-PSRAM boards.

---

## Power

- The ESP32 runs from its own 5 V supply, permanently, independent of the
  projector.
- Espressif list the DevKitC-1's two USB ports as mutually exclusive power
  inputs. In practice both being live is fine, but if you want to be strict,
  use a VBUS-cut cable on the projector side. That also guarantees no
  back-feed.
- Roughly 150 mA idle, brief peaks around 500 mA on Wi-Fi transmit. Any decent
  1 A supply is plenty; a phone charger you already trust is a better choice
  than the cheapest thing in the drawer.

## Status LED

The onboard WS2812 on GPIO48 shows the power state at a glance:

| Colour | Meaning |
|---|---|
| blue | setup access point — no Wi-Fi credentials yet |
| amber | power state unknown (no reply yet) |
| green | projector awake |
| dim red | projector asleep |
| purple | a macro is running |
