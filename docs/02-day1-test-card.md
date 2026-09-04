# Day 1 test card

Print this. Run the tests in order, in this order, writing results in the
boxes. Each test closes off a branch; skipping ahead wastes the afternoon.

**Setup:** flash `firmware/titan_bridge_p1`, UART port to the laptop, serial
monitor at 115200 with line ending **Newline**, native USB port to the
projector. Projector: Settings → General → **Serial Port Control = ON**.

Record the projector firmware version first — every macro you build later is
tied to this build.

```
Date ______________  Projector firmware ________________  Bridge: p1
Projector USB port used:  [ ] USB 3.0   [ ] USB 2.0
Cable:  [ ] known data cable   [ ] unknown
```

---

## Test 0 — bench sanity

The board boots and prints the help text on the UART console.

```
[ ] pass   [ ] fail
```

Fail here is a board-settings problem, not a projector problem. Check USB Mode
= USB-OTG (TinyUSB), CDC On Boot = Disabled, Upload Mode = UART0.

---

## Test 1 — USB enumeration  ← the single most informative test

```
k down
```

Watch the projector's on-screen menu, not the console.

```
Menu cursor moved?   [ ] yes   [ ] no
Also try:  k home    [ ] yes   [ ] no
           k menu    [ ] yes   [ ] no
           k focus+  [ ] yes   [ ] no
```

**Yes** = the projector enumerates the ESP32 as a USB device. Everything after
this is about whether its serial daemon will bind, not about whether USB works
at all.

---

## Test 2 — CDC serial binding

```
temp
```

```
Bytes came back in the console?   [ ] yes   [ ] no
Exact bytes: ______________________________________________
```

`2A 2A 03 13 01 00 17` is a normal temperature status. The sketch decodes it
for you and prints `temp=normal`.

Also note whether the console printed a **`CDC: host OPENED the port`** line at
any point — that is the projector's serial daemon binding, and it is the
clearest possible signal.

```
Saw "CDC: host OPENED the port"?   [ ] yes   [ ] no
```

**If yes, the hard part of this project is over** and the rest is software.

---

## Test 3 — command sweep

Only meaningful if Test 2 passed. Confirms the frame format and checksum are
right on real hardware.

```
hdmi2        [ ] input changed
filmmaker    [ ] picture mode changed
b3           [ ] brightness dropped
blank        [ ] screen went dark
unblank      [ ] screen came back
```

---

## Test 4 — the undocumented third input

XGIMI's table lists HDMI1, HDMI2 and USB only, despite the unit having three
HDMI inputs. That smells like a document copied from the two-input original
TITAN.

```
hdmi3        [ ] switched to HDMI3   [ ] nothing
```

If nothing, walk the whole parameter space:

```
sweep 01 00 08
```

```
Parameters that did something: ____________________________
```

---

## Test 5 — standby behaviour  ⚠ the most important unknown

Run this **early**. Three of the architectural decisions depend on it.

```
poll 5
```

Leave that running. Then:

1. Put the projector into standby with its own remote.
2. Watch the console for 60 seconds.

```
Console showed "USB: SUSPEND"?        [ ] yes   [ ] no
Console showed "USB: stopped"?        [ ] yes   [ ] no
Temperature replies stopped?          [ ] yes   [ ] no
Board rebooted / lost power?          [ ] yes   [ ] no
```

3. Now send:

```
wake
```

```
Projector woke?   [ ] yes   [ ] no
```

**Ports stay powered and `wake` works** → you have real discrete power-on and
the architecture is settled. Leave `USB_DEAD_IN_STANDBY` at 0 in
`firmware/titan_bridge_p2/config.h`.

**Ports go dead** → nothing you send can ever wake it. Set
`USB_DEAD_IN_STANDBY` to 1 so the bridge stops pretending, and get power-on
from a smart plug (with the projector's power-on-when-mains-applied setting) or
HDMI-CEC from the AVR. See plan §7.

---

## Test 6 — does IR exist at all

Wire the IR LED: anode → 100 Ω → GPIO4, cathode → GND. Projector on, LED aimed
at the rear panel from about 30 cm.

```
ir all
```

This fires all 27 published TITAN codes in both plausible byte orders of the
`0x21DE` header, 700 ms apart, with the name of each printed as it goes.

```
Any OSD reaction at all?     [ ] yes  (which key: ______________ )   [ ] no
Repeat aimed at the FRONT:          [ ] yes   [ ] no
Repeat aimed at BOTTOM-FRONT:       [ ] yes   [ ] no
Found a dark IR window on the chassis?   [ ] yes, at ____________   [ ] no
```

---

## Decision tree

| Test 1 (HID) | Test 2 (CDC) | Meaning | Next |
|---|---|---|---|
| ✅ | ✅ | Best case | Skip Phase 2 entirely — flash `titan_bridge_p2` |
| ✅ | ❌ | USB fine, CDC not bound | **Expected.** Go to the dongle path below |
| ❌ | ❌ | Not enumerating | Data-capable cable? Other projector USB port? Then dongle path |
| ❌ | ✅ | Very unlikely | Note it and carry on; serial is what matters |

The ✅/❌ case is the one to expect. The projector's serial daemon almost
certainly opens `/dev/ttyUSB*`, created by the ch341/cp210x/pl2303 drivers,
whereas a CDC-ACM device appears as `/dev/ttyACM*`. Spoofing VID/PID will not
fix it: those drivers issue vendor-specific control requests the ESP32 would
stall on.

---

## Phase 2 — the dongle path

Only if Test 2 failed.

**Measure before you wire.** See `docs/03-wiring.md` §"The jumper check". A
CH340 module set to 3.3 V may still idle its TXD at 5 V, and the ESP32-S3's
inputs are not 5 V tolerant.

```
TXD-to-GND measured: ________ V     [ ] ~3.3 V, wire direct
                                    [ ] ~5 V, fitted 1k/2k divider
```

Wire it, set `USE_PHASE2_UART` to `1` in `titan_bridge_p1.ino`, reflash,
and re-run Tests 2, 3, 4, 5.

```
CH340 bound?     [ ] yes   [ ] no
If no, escalate: [ ] CP2102   [ ] genuine FTDI   [ ] USB-RS232 DB9 + MAX3232
```

---

## Success criterion for the day

**`temp` returns bytes.** If Saturday ends there, everything after it is
software you can write at leisure.

Copy the whole console transcript into `logs/TEST-LOG.md` — every line is
timestamped for exactly this reason.
