# _theater-util — XGIMI TITAN Noir control bridge

An ESP32-S3 that gives an XGIMI TITAN Noir Max the discrete control it does not
ship with: real power on and off, direct input select, picture presets, and a
macro engine that drives the on-screen menu for the features XGIMI's serial set
does not expose.

The Noir has no IR receiver, no PJLink, no smart OS and therefore no ADB. What
it does have is **RS232 over a USB adapter, at 115200 8N1**, once you turn on
Settings → General → Serial Port Control. Everything here is built on that.

---

## Start here

| If you are… | Go to |
|---|---|
| waiting for parts | [`docs/01-preflight.md`](docs/01-preflight.md) |
| impatient, with any ESP32 board and no dongle | [`docs/09-onboard-bridge-path.md`](docs/09-onboard-bridge-path.md) |
| holding the parts, it's Saturday morning | [`docs/02-day1-test-card.md`](docs/02-day1-test-card.md) |
| wiring | [`docs/03-wiring.md`](docs/03-wiring.md) · [`hardware/wiring.svg`](hardware/wiring.svg) |
| flashing | [`docs/04-flashing.md`](docs/04-flashing.md) |
| mapping menus on Sunday | [`docs/05-menu-mapping-worksheet.md`](docs/05-menu-mapping-worksheet.md) |
| looking up a command | [`docs/06-command-reference.md`](docs/06-command-reference.md) |
| stuck | [`docs/07-troubleshooting.md`](docs/07-troubleshooting.md) |
| wiring up the hub or Home Assistant | [`docs/08-integration.md`](docs/08-integration.md) |
| reading the original reasoning | [`plan/titan_noir_bridge_plan.md`](plan/titan_noir_bridge_plan.md) |

## The five-minute version

```bash
# 1. flash the diagnostic sketch
tools/flash.sh p1

# 2. in the monitor (115200, line ending = Newline)
k down       # does the menu cursor move?   -> USB enumeration works
temp         # do bytes come back?          -> serial is bound, you're done
poll 5       # leave running, then put the projector into standby (Test 5)

# 3. once you know which channel won
tools/flash.sh p2
```

If `temp` returns bytes on Saturday, the hard part is over.

---

## What's here

```
firmware/
  titan_bridge_p1/    Day 1 diagnostic. USB CDC + HID keyboard + IR transmitter,
                      a console, and USB bus-event logging so Test 5 has real
                      evidence rather than a guess.
  titan_bridge_p2/    Production bridge. Dual serial channel, macro engine,
                      power state machine, Roku ECP emulation, IR receiver,
                      web UI, REST API, Wi-Fi provisioning, OTA.
docs/                 The runbook, in the order you need it.
hardware/             Wiring diagram, SVG and PNG.
tools/
  titan_serial.py     Drive the projector from a laptop with only a USB-TTL
                      adapter — no ESP32 in the way. The fastest way to remove
                      a variable when something doesn't work.
  ecp_probe.py        See the bridge the way a SofaBaton hub sees it.
  flash.sh            build / upload / monitor, without remembering the FQBN.
homeassistant/        Drop-in package and dashboard card. No MQTT, no HACS.
logs/                 Test log and menu-mapping templates. Fill these in.
plan/                 The original build plan and its reasoning.
```

## Firmware at a glance

Both sketches are **pure arduino-esp32 with no external libraries**, so there
is nothing to install before flashing. Verified to compile warning-free on
arduino-esp32 **2.0.17** and **3.3.11**, and across every configuration
permutation in `config.h`.

| | p1 | p2 |
|---|---|---|
| Purpose | answer the Day 1 questions | run the theatre |
| Flash used | 29% | 54% (Minimal SPIFFS) |
| Serial over native USB CDC | ✅ | ✅ |
| Serial over UART1 → USB-TTL | ✅ (one `#define`) | ✅ (simultaneously) |
| USB HID keyboard | ✅ | ✅ |
| IR transmit (Test 6 sweep) | ✅ | — |
| IR receive | — | ✅ |
| USB bus-event logging | ✅ | — |
| Parameter sweeping | ✅ | ✅ |
| Macro engine | — | ✅ |
| Power state machine | — | ✅ |
| Roku ECP + SSDP | — | ✅ |
| Web UI + REST API | — | ✅ |
| Wi-Fi provisioning + OTA | — | ✅ |

### The three ideas worth knowing

**One command path.** A button on the web page, a Roku keypress from the hub,
an IR code and a Home Assistant service call all compile down to the same macro
script and run through the same engine. There is one place to fix things.

**Liveness by temperature probe.** The projector's only feedback channel is its
temperature status. The bridge polls it every 10 seconds: a reply means awake,
three silences mean asleep. That is what makes power on and off *idempotent* —
they mean what they say regardless of the state you started in, which is more
than the Bluetooth remote manages.

**Anchored macros.** The serial set has no 3D, lens memory, iris or keystone.
Instruction `0x07` drives the OSD blind, so macros open by forcing a known
state — `back, back, back, settings` — then count from there. Without the
anchor, one desynced press sends the rest of the sequence somewhere random.

---

## Command reference, in brief

Frame: `2A 2A <len> <instr> <params…> <checksum>`, where `len` is 1 plus the
parameter count and the checksum is everything after the header, mod 256.

```
hdmi1 hdmi2 hdmi3? usbsrc          source select
vivid movie imax perf tvmode sport filmmaker
b1 … b10                           brightness
power source up down left right ok back setting home
volup voldn autofocus manfocus mute
wake                               standby wake, "wakeup" in ASCII
blank unblank                      screen off / on
hrroff hrrbasic hrrmax             high refresh rate
temp                               temperature status — the only feedback
```

Full table with verified frames, and the two places XGIMI's own document
contradicts itself, in [`docs/06-command-reference.md`](docs/06-command-reference.md).

## Known unknowns

These are the things the hardware has to answer. They are listed here so that
finding one of them out is progress rather than a surprise.

1. **Does CDC bind?** Probably not — the projector's daemon almost certainly
   wants `/dev/ttyUSB*`, which needs a real USB-serial chip. Hence the CH340.
2. **Do the USB ports stay powered in standby?** If not, `wake` can never
   reach the projector and power-on has to come from a smart plug or HDMI-CEC.
   Test 5, run early.
3. **Does HDMI3 exist as parameter `03`?** The document lists two HDMI inputs
   on a three-HDMI projector, which smells like a copy from the original TITAN.
4. **Is high-refresh "extreme" parameter `01` or `02`?** The document prints
   `01` and gives a checksum that only works for `02`. The firmware follows the
   checksum.
5. **Does SofaBaton's Roku profile surface the TV-only keys?** The bridge
   presents `is-tv=true` specifically to find out.

## After a projector firmware update

Re-pull XGIMI's help-centre article and diff the command-table image; re-run
Test 5; re-verify one macro per menu branch. New instruction bytes are the
cheapest capability this project will ever gain, and lens memory is promised in
a future OTA.
