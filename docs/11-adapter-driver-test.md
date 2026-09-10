# Adapter driver test — which USB-serial chip will the projector bind?

Test 2 failed with a CP2102 (see `logs/TEST-LOG.md`). The projector never
opened the port: no DTR assertion, no bytes, ever. The leading explanation is
that **its kernel does not carry `cp210x`** — embedded Linux builds ship a
subset of the USB-serial drivers, and a device whose driver is absent never
becomes a `/dev/ttyUSB*` at all.

That is a one-variable question, and this is how to answer it in a few minutes
per adapter.

## The candidates

| Chip | Kernel driver | Hardware |
|---|---|---|
| FT232RL | `ftdi_sio` | DSD TECH SH-U09G (USB→TTL cable) |
| FTDI | `ftdi_sio` | OIKWAN USB→DB9 (needs MAX3232 to reach TTL) |
| CH340G | `ch341` | HiLetgo module ×5 |
| PL2303TA | `pl2303` | download cable ×3 |
| CP2102 | `cp210x` | **already tested — does not bind** |

Try **FTDI first**. `ftdi_sio` and `pl2303` are the most commonly bundled in
SoC vendor kernels, `ch341` close behind, and the SH-U09G reaches TTL directly
with no DB9 or level shifting in the way.

---

## Method A — no ESP32 at all (fastest)

Two adapters, TTL sides crossed, laptop at the far end. The bridge firmware is
not involved, so nothing it does can be blamed for a failure.

```
projector USB ─ adapter A ─ TXD──RXD ─ adapter B ─ USB laptop
       (the candidate)      RXD──TXD   (any known-good one)
                            GND──GND
```

```bash
python3 tools/titan_serial.py --port /dev/cu.usbserial-XXXX --monitor
```

In another shell, or from the same REPL, send `temp`. **Bytes back = that
driver is present and the daemon binds it.** Then swap adapter A and repeat.

You have five CH340s, so adapter B can be one of those permanently.

> Cross TX to RX. Straight-through gives silence that looks exactly like a
> driver failure, and it is the easiest hour to waste here.

## Method B — through the bridge, with a console

Once a candidate binds, put the ESP32 back in the middle. Use the **UART1**
build: the adapter goes on GPIO17/18 and UART0 stays free, so you get a live
serial console instead of squinting at the web UI.

```bash
cd firmware
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DBOARD=3 -DSERIAL_CHANNELS_WANTED=CH_UART1" \
  --upload -p /dev/cu.usbserial-XXXX titan_bridge_p2
```

Wiring per `docs/03-wiring.md`: adapter TXD → GPIO18, RXD → GPIO17, GND → GND,
**VCC left unconnected**. Measure TXD-to-GND first — 3.3 V is safe, 5 V needs
the 1 kΩ/2 kΩ divider, and the ESP32's inputs are not 5 V tolerant.

---

## Reading the result

| Symptom | Meaning |
|---|---|
| `rx` climbs, log shows `RX 2A 2A …` | **Bound.** Test 2 passes. |
| `rx` climbs, log shows **no** `RX` lines | Bytes arriving that never frame up — an unexpected header. Addressing is back on the table; see the Serial Port ID notes in `TEST-LOG.md`. |
| `rx` stays 0 | Driver absent, or the daemon does not bind this chip. Next adapter. |
| The board **reboots** when you plug into the projector | The host asserted DTR — meaning it *did* open the port. Encouraging even if no data follows. |

`rx` counts every byte before any framing, so `rx = 0` genuinely means an
electrically silent line — not "replies we could not parse".

## Record it

```
Date ______________  Projector firmware ________________

| Adapter | Chip | Driver | Binds? | rx | Notes |
|---|---|---|---|---|---|
| DSD TECH SH-U09G | FT232RL | ftdi_sio | yes | 2026-09-05 | works, but back-powers over the TTL lines |
| HiLetgo module   | CH340G  | ch341    | | | |
| PL2303 cable     | PL2303TA| pl2303   | | | |
| OIKWAN + MAX3232 | FTDI    | ftdi_sio | | | |
| (tested)         | CP2102  | cp210x   | no | 0 | never opened the port |
```

## If all four fail

Then the projector does not bind USB-serial adapters at all, whatever XGIMI's
documentation implies, and the serial channel does not exist on this model.
That is a real answer, not a dead end — switch the bridge to the HID channel,
which a plain USB keyboard has already proven works:

```
keychan hid
```

or `GET /api/keychan?mode=hid`. Macro navigation and `anchor` then travel over
USB HID instead of instruction `0x07`, and the counted menu paths from
`docs/10-hid-key-probe.md` work unchanged. That needs the native-USB port on
the S3 DevKitC-1, not the onboard bridge chip.


---

## Back-powering across the TTL link (measured 2026-09-08)

**Observed:** with the ESP32 **unpowered** and the FTDI cable plugged into the
projector, a dim red LED lights on the ESP32. The adapter's TXD idles HIGH at
3.3 V and drives GPIO18; that current flows through the pin's ESD clamp diode
into the ESP32's 3.3 V rail and partially powers the board.

The physics is symmetric, so the reverse happens too — and that direction is the
one that matters. With the ESP32 on **external power** and the projector in
standby (USB rail dead), GPIO17 idles HIGH into the unpowered adapter's RXD and
back-powers it. A partially-energised USB device on the projector's port is a
plausible wake vector, though unproven.

### Two fixes

**Series resistors** — 4.7 kΩ–22 kΩ (10 kΩ ideal) in each data line. Caps the
leak at ~330 µA, far below what an FT232RL needs to run. **Never in the ground
line**, which must stay direct.

```
ESP32 GPIO17 (TX) ──[10k]── adapter RXD
adapter TXD ────────[10k]── ESP32 GPIO18 (RX)
ESP32 GND ──────────────────  adapter GND      (direct)
```

**MAX3232 + a USB↔RS232 adapter — immune by construction, and now the
recommended build.** The ESP32 drives only the MAX3232, powered from the ESP32's
own rail so never unpowered. The MAX3232↔OIKWAN link is RS232, whose receivers
are resistive (3–7 kΩ to ground) with no diode path to VCC and are rated to be
driven at ±25 V while unpowered — exactly the hot-plug case RS232 was designed
for. Neither end can parasitically power the other.

| from | to |
|---|---|
| ESP32 3.3 V | MAX3232 VCC (essential) |
| ESP32 GND | MAX3232 GND |
| ESP32 GPIO17 (TX) | MAX3232 **T1IN** |
| MAX3232 **R1OUT** | ESP32 GPIO18 (RX) |
| MAX3232 **T1OUT** | DB9 pin 2 (RxD) |
| MAX3232 **R1IN** | DB9 pin 3 (TxD) |
| MAX3232 GND | DB9 pin 5 |

**TX/RX can be reversed in two places** — the TTL side and the DB9 side — and
swapping both cancels out. A single swap gives frames out, `rx=0` forever. This
happened on the first attempt here. Breakout silkscreens are the trap: some
label TTL pins from the MCU's perspective, some from the module's.

**Still untested: PL2303TA.**
