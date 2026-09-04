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
