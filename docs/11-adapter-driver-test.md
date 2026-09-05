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
| DSD TECH SH-U09G | FT232RL | ftdi_sio | | | |
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
