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

### The leading explanation

Group + ID addressing is a **multi-projector daisy-chain feature** for
commercial installs. Finding it on a consumer home-theatre projector, attached
to a serial stack that never opens a port, suggests the whole Serial Port
Control page may be inherited UI from a shared firmware base with a commercial
sibling that has a real DB9 — present in the menu, unimplemented in the
hardware. Not proven. But it fits every observation.

### Still untested

CH340 (`ch341`) and PL2303 (`pl2303`) adapters, on order. A genuine USB-to-RS232
DB9 adapter into a MAX3232 is the last rung. If all three fail, the explanation
above is almost certainly right.

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
