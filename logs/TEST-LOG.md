# Test log

Paste console transcripts here. Every line the p1 sketch prints is timestamped
in `[seconds.milliseconds]` form for exactly this reason — the gaps between
lines are the evidence, particularly around standby.

---

## Pre-Day-1 findings — 2026-09-04

Established before any bridge hardware was flashed, with a USB keyboard and the
projector's own menu.

| Finding | Result | How |
|---|---|---|
| Serial Port Control exists in this firmware | **yes, enabled** | Settings → General |
| Projector's USB host accepts an HID keyboard | **yes** | plugged in a plain USB keyboard |
| OSD responds to keyboard navigation | **yes, "perfectly"** | arrow keys / Enter moved the menu |

**Why it matters.** Test 1 asks whether the projector will enumerate and obey a
USB HID device. A retail keyboard answers the host-stack half of that question
outright: the port enumerates, the HID driver loads, and the OSD consumes key
events. What remains unproven is only whether the **ESP32-S3's TinyUSB HID**
is accepted on the same terms — a much narrower risk than "does HID work at
all", and no longer on the critical path, since menu navigation runs over
serial instruction `0x07` and HID is needed only for Menu and manual focus.

It also means the menu tree can be mapped by hand, tonight, with the keyboard —
see `docs/10-hid-key-probe.md`. That is Sunday's job, done at typing speed
instead of one 250 ms macro step at a time.

**Still unknown:** everything on the serial side. Test 2 (does the daemon bind)
is the next question, and it is the one the whole project turns on.

---

## Bridge bring-up — 2026-09-04, no projector attached

HiLetgo ESP-WROOM-32, `titan_bridge_p2` built with `BOARD=3` (`CH_UART0`), so
the projector link runs through the board's **own onboard USB-serial chip**.
Laptop on the far end of that link running `tools/fake_projector.py`, standing
in for the projector.

```
[0.422] link up: UART0/onboard-bridge @ 115200 8N1
[5.325] TX 2A 2A 02 13 00 15
[5.403] RX 2A 2A 03 13 01 00 17  temp=normal
[6.830] power state -> awake
```

`/api/status`: `power=awake  temp=normal  rx=14  lastrx="2A 2A 03 13 01 00 17"`
Wi-Fi `Nexus` → 10.0.0.215, rssi −55 dBm. mDNS and Roku ECP both up
(`TB9E9C8544` on :8060).

| Layer | Result |
|---|---|
| Board boots, poll loop at `POLL_INTERVAL_MS` | ✅ 10.0 s, measured |
| 2A2A frame builder + checksum | ✅ `2A 2A 02 13 00 15`, `(2+0x13+0x00)&0xFF = 0x15` |
| TX through the onboard bridge chip | ✅ |
| RX framing and decode | ✅ `temp=normal` |
| Power state machine → awake | ✅ on reply |
| Power state machine → asleep | ✅ at 29.0 s, after `POLL_MISSES_TO_SLEEP` = 3 unanswered |
| Wi-Fi, mDNS, REST API, Roku ECP | ✅ |
| ROM boot chatter poisoning the stream | ✅ harmless — resynced away by the `2A 2A` search |

**Everything in the chain is now proven except the projector.** Test 2 is the
single remaining unknown: whether its serial daemon binds the bridge chip.

---

## Test 1 — HID via the bridge: **PASSED** — 2026-09-05

**The projector is under control.** ESP32-S3-DevKitC-1, `titan_bridge_p2` built
with `BOARD=1`, native USB port straight into a projector USB port, board
powered from that same port, commanded entirely over Wi-Fi from another room.

```
[69.634] key channel -> hid
[71.703] HID home (0x4A)      -> OSD opened
[74.779] HID down (0x51)      -> selection moved
[76.857] HID down (0x51)
[79.026] HID down (0x51)
[81.116] HID up (0x52)
[83.186] HID menu (0x65)
```

Observed on screen: **the menu opened and the selection moved.**

Build that did it:

```
esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=16M,PartitionScheme=min_spiffs
-DBOARD=1
```

`USBMode=default` (USB-OTG/TinyUSB) is not optional — under the `hwcdc`
default the HID interface never enumerates and none of this happens.

### What this settles

The plan had serial as the primary channel and HID as a gap-filler for Menu
and manual focus. **That is now inverted.** Serial does not exist on this
projector; HID does, and it carries the navigation the macro engine needs.
`keychan hid` makes that the routing for `anchor` and every `k:` step, so the
counted paths in `docs/05` and `docs/10` work unchanged.

The native USB port also enumerates as `Titan Bridge` / `DIY` (VID 0x303A).

### Still not working, and now confirmed twice

`cdc=false` throughout: the projector never opened the native CDC endpoint
either. Test 2 has now failed in **both** its forms — native CDC on an S3, and
a CP2102 USB-serial adapter. Whatever XGIMI's Serial Port Control does on this
model, it does not bind a USB device.

---

## Test 2 — CDC/serial binding: **FAILED** — 2026-09-04

The projector does not bind a CP2102 as a serial device. Not a wiring problem,
not a firmware problem, not a cable.

### What was ruled out, and how

| Suspect | Ruled out by |
|---|---|
| Bridge firmware / frame format | `fake_projector.py` round-trips the same build: TX, RX, decode, power state all correct |
| Cable (charge-only) | Board enumerated as `/dev/cu.usbserial-0001` **through the projector cable** on the laptop |
| Wrong chip class | It is a **CP2102**, Silicon Labs `0x10C4`, serial `0001` — one of the three drivers (`ch341`/`cp210x`/`pl2303`) the plan expects |
| Serial Port Control off | On, verified on-screen |
| Daemon not started at boot | Full mains power-cycle, retested |
| Wrong USB port | Both 3.0 and 2.0 tried |
| Projector USB port dead | A USB **keyboard** on the same port drives the OSD perfectly |
| ID addressing | 7 header variants — `**`, `A0`, `*0`, `A*`, `A\0`, and the address before/after the header — all silent |

### The decisive observation

`uptime` climbs monotonically the entire time the board is on the projector. A
host opening a CP2102 asserts DTR, and on any ESP32 dev board that resets the
MCU — it is exactly how `esptool` reboots it (`Hard resetting via RTS pin`).
**The projector never asserted DTR, so it never opened the port.** This is not
a daemon that binds and ignores us; it is a daemon that never binds.

### Why `rx = 0` is stronger evidence than it looks

`rxBytes++` is the first statement in `rxByte()`, before the `2A 2A` header
check (`titan.cpp`). The counter therefore counts **every byte that arrives on
the wire**, whatever its header, framing or validity.

So `rx = 0` does not mean "replies arrived that we could not parse" — the
obvious worry once the projector turns out to have ID addressing. It means no
byte was ever received. The receive line is electrically silent.

Diagnostic to remember for the CH340/PL2303 attempts: if `rx` climbs while the
log shows no `RX` lines, that is bytes arriving that never frame up — which
*would* mean an unexpected header, and addressing would be back on the table.
`rx` static at 0 rules that out.

### Serial Port Control menu, as found

```
Serial Port Control Switch   ON
Serial Port ID Group          A
Serial Port ID Number         0
Serial Port Response (Group) ON
Serial Port Response (All)   ON
```

`docs/06-command-reference.md` was transcribed from XGIMI's published command
image, and **that image documents no ID field at all**. The firmware exposes
group/ID addressing the protocol document does not describe.

### The leading explanation: a missing kernel driver

An earlier theory here — that the Serial Port Control page was vestigial UI
inherited from a commercial sibling with a real DB9 — is **wrong, and is
retracted**. Checked against sources:

- XGIMI's own article is titled *"How to use the RS232 on the TITAN?"* and
  describes only enabling the setting and configuring 115200 8N1. It is real.
- The TITAN Noir has **no DB9**. Its ports are 2×HDMI, 2×USB, RJ45, 3.5 mm.
- Third-party reporting states the projector "will recognize an RS232/USB
  adapter when connected to one of its USB ports and then offer remote control
  via the serial port."

So the USB-adapter premise this project is built on is correct, and the feature
is genuinely implemented. What is far more likely is that **the projector's
kernel does not carry `cp210x`.**

Embedded Linux builds ship a subset of the USB-serial drivers. If `cp210x` is
absent, a CP2102 never becomes a `/dev/ttyUSB*` at all — so nothing opens it,
nothing asserts DTR, and the receive line stays electrically silent. Every
observation follows from that one fact, and it is consistent with the DTR
evidence rather than in tension with it.

This makes the adapters on order the decisive test, not a long shot. `ftdi_sio`
and `pl2303` are the most commonly bundled drivers in SoC vendor kernels, with
`ch341` close behind — and a **genuine FTDI** is the single likeliest to work.

### The ID settings are real, and need no change

Group + ID addressing is an RS232 daisy-chain feature for multi-projector
installs, and its presence is evidence the protocol was designed with
addressing that XGIMI's published command image simply does not document. The
`2A 2A` header is ASCII `**` — two wildcards, i.e. the broadcast form of an
addressed protocol.

`Group A / ID 0` with **Response (Group)** and **Response (All)** both ON is
already the most permissive configuration available. Nothing to change, and the
bridge implements no addressing to match it against.

### CH340 (`ch341`) — also fails, 2026-09-05

Dual path, exactly as plan §5 describes: ESP32-S3 native USB on the projector's
USB 3.0 port carrying HID, a CH340G on USB 2.0 carrying serial, its TTL side on
the bridge's UART1 (GPIO17/18, jumper on 3V3).

The wiring was proven first with the laptop standing in for the projector —
frames out, replies in, `temp=normal`, **0 bytes discarded as non-frame**. So
the adapter, the levels, the crossover and the UART1 channel are all known
good. Moved to the projector: `tx` climbing, `rx = 0`, indefinitely.

| Chip | Driver | Binds? |
|---|---|---|
| CP2102 | `cp210x` | no |
| CH340G | `ch341` | **no** |
| PL2303 | `pl2303` | untested |
| FT232RL | `ftdi_sio` | untested |

Two driver classes down. `ftdi_sio` is the one most commonly bundled in SoC
vendor kernels, so the FTDI adapters are the strongest remaining candidates —
and if they fail too, this projector does not bind USB-serial devices at all
and the serial channel does not exist on this model, whatever the
documentation implies.

None of this blocks anything: HID carries the whole product.

---

## Test 5 — standby behaviour: **ports stay powered** — 2026-09-04

Measured, not impression. The bridge was plugged into a projector USB port with
**no other power source**, and the projector was switched off. It stayed up:

```
uptime=355s  power=asleep  tx=42  rx=0
uptime=361s  power=asleep  tx=42  rx=0
uptime=367s  power=asleep  tx=43  rx=0
```

Wi-Fi held, uptime climbed straight through the power-off, no reboot.

**The USB ports remain powered in standby.** Distinguish this from mains-off,
where nothing survives — an earlier impression that the ports died was wrong.

### What it changes

`USB_DEAD_IN_STANDBY` stays **0**. The bridge survives standby, so it is still
there to wake the projector — which is the whole premise of plan §5's "power
the ESP32 from its own supply" requirement. That requirement is now a
convenience rather than a necessity, though a separate supply is still better
practice than depending on the projector's rail.

### The open question this creates

The port is alive in standby, but **can anything we send actually wake it?**

- Serial `wakeup` — unavailable, Test 2 failed on this model.
- **HID keypress — untested, and now the single most valuable experiment
  left.** A USB keyboard already drives the OSD when the projector is awake.
  If a keypress also wakes it from standby, there is discrete power-on with no
  smart plug, no HDMI-CEC, and no serial.

Test: leave a USB keyboard plugged in, put the projector in standby, press
keys. Anything that wakes it is the power-on path.

```
Date:               ____________________
Projector firmware: ____________________
Bridge firmware:    titan_bridge_p1
Projector USB port: [ ] 3.0  [ ] 2.0
Adapter:            [ ] none (native CDC)  [ ] CH340  [ ] CP2102  [ ] FTDI  [ ] DB9+MAX3232
```

## Results summary

| Test | Result | Note |
|---|---|---|
| 0 — bench sanity | | |
| 1 — USB enumeration (HID) | | |
| 2 — CDC serial binding | | |
| 3 — command sweep | | |
| 4 — HDMI3 / undocumented params | | |
| 5 — standby behaviour | | |
| 6 — IR receiver | | |

**Verdict:** ______________________________________________

**Channel chosen:** [ ] native CDC   [ ] UART1 + dongle   [ ] both

---

## Transcript

```
(paste here)
```

---

## Undocumented findings

Anything the published table does not have. These are the valuable bits — new
instruction bytes are the cheapest capability this project will ever gain.

| Instruction | Parameter | Observed effect | Frame |
|---|---|---|---|
| | | | |
