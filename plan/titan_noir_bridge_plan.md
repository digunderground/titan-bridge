# XGIMI TITAN Noir Max — Control Bridge Build Plan

A weekend runbook. Day 1 is diagnosis, Day 2 is construction. Do not skip
Day 1: three of the four architectural decisions depend on results you don't
have yet, and building before you know them wastes the weekend.

---

## 0. Established facts

Everything below was verified against XGIMI's help centre, the Kickstarter
FAQ, and owner reports. It's here so you're not re-deriving it at 11pm.

**No IR.** The original TITAN has a rear IR receiver, a 3.5 mm REMOTE port for
an extender, and a published NEC codeset (header `0x21DE`). The Noir series
dropped all three. Its port list has no REMOTE jack, the bundled remote is
Bluetooth-only, and XGIMI's own "remote not working" article offers a USB
keyboard as the fallback rather than IR. We test anyway, because the test is
cheap and the codeset might still be live in firmware.

**No network control.** The original TITAN supports PJLink. XGIMI states
plainly that the Noir series does not support network-based PJLink control and
supports RS232 via USB adapter only. There is no smart OS on board, so no ADB,
no Android TV Remote protocol, no Cast.

**Serial works, over USB.** Enable it at **Settings → General → Serial Port
Control**. Plug a USB-to-RS232 or USB-to-TTL adapter into a projector USB port.
**115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.**

**Two USB ports exist** — one USB 3.0, one USB 2.0. Both are documented as
supporting storage and USB-to-RS232. This matters: it lets you run two
independent control channels at once.

**A USB keyboard is a supported input device**, with a documented map:

| Key | Function |
|---|---|
| Arrow keys | Navigation |
| Enter | OK |
| Esc | Back |
| Home | Open settings |
| Numpad + / − | Manual focus |
| Page Up / Page Down | Volume |
| Menu key | Contextual menu |

That Menu key has no equivalent in the serial command set. It's genuinely
extra reach, which is why the HID channel stays in the final design.

### Frame format

```
2A 2A <length> <instruction> <parameters...> <checksum>

length   = 1 (the instruction byte) + number of parameter bytes
checksum = (length + instruction + all parameter bytes) & 0xFF
```

Verified against XGIMI's own examples: HDMI1 is `2A2A 02 01 01 04`
(0x02+0x01+0x01 = 0x04), and standby wake is `2A2A 07 09 77 61 6B 65 75 70 9D`,
where the parameters spell "wakeup" in ASCII and the bytes sum to 0x29D → 0x9D.

---

## 1. Bill of materials

### Phase 1 — arriving

- ESP32-S3-DevKitC-1, **dual USB-C variant**. The two ports are the whole
  point: one flashes and gives you a console while the other is plugged into
  the projector. Single-port clones will fight you, because USB HID mode
  disables serial upload over the native port.
- USB-C → USB-A data cable (ESP "USB" port → projector).
- USB-C cable to your laptop (ESP "UART" port).
- HiLetgo CH340 USB-to-TTL modules, 5-pack. Good choice — CH340 is the driver
  most likely to be present in a Chinese Linux/Android build.
- TSOP38238 or TSOP4838 IR receiver, 100 Ω resistor, 4.7 µF capacitor.
- 940 nm IR LED, 100 Ω resistor.
- Breadboard, jumpers.

### Worth adding

- One **CP2102** USB-to-TTL module as chipset insurance (~£3).
- A **multimeter**. Non-negotiable for the jumper check in §4.
- A 5 V USB power supply for standalone running later.

### Only if everything else fails

- USB-to-RS232 DB9 cable + MAX3232 breakout + DB9 screw terminal. This is
  what XGIMI literally documents, so it's the highest-confidence path, just
  the most annoying to wire.

---

## 2. Pre-flight — Friday night, 30 minutes

Do this before the parts arrive. It's all software and reconnaissance.

1. **Arduino IDE + esp32 core.** Board: "ESP32S3 Dev Module". Set
   **USB Mode: USB-OTG (TinyUSB)**, **USB CDC On Boot: Disabled**,
   **Upload Mode: UART0 / Hardware CDC**. All three matter; the sketch's
   header comment explains why.
2. **Enable Serial Port Control** on the projector and confirm the menu item
   actually exists on your firmware.
3. **Record the firmware version.** Settings → system information. Write it
   down. Every macro you build in §6 is tied to this build.
4. **Photograph the whole settings menu tree.** Every page, in order, with the
   cursor position visible. You will need this to count keypresses on Day 2,
   and it is far quicker to shoot it now than to reverse-engineer it later.
5. **DHCP reservation** for the ESP32's MAC, if you're going the network route.
6. **Inspect the rear panel** under a torch for a small dark IR window near the
   indicator LED. On the original TITAN that's exactly where the receiver sits.
   If there's no window anywhere on the chassis, §3 Test 6 is a formality.

---

## 3. Day 1 — bring-up and diagnosis

Flash `titan_bridge_p1.ino`. UART port to the laptop, serial monitor at
115200, line ending set to **Newline**. Native USB port to the projector.

Run these in order. Each one closes off a branch.

### Test 0 — bench sanity

Board boots, help text appears in the console. If not, you have a board
settings problem, not a projector problem.

### Test 1 — USB enumeration

```
k down
```

Menu cursor moves? **USB enumeration works.** This is the single most
informative test in the whole runbook, because it separates "the projector
ignores the ESP32 entirely" from "the projector sees it but won't bind the
serial driver". Try `k home`, `k menu`, `k focus+` too.

### Test 2 — CDC serial binding

```
temp
```

Bytes come back in the console? **Serial is bound and you're done** — the rest
of this project is software. A reply of `2A 2A 03 13 01 00 17` is a normal
temperature status.

### Test 3 — command sweep

```
hdmi2      filmmaker      b3      blank      unblank
```

Confirms the frame format and checksum are right on real hardware.

### Test 4 — undocumented third input

```
hdmi3
```

The published table only lists HDMI1 and HDMI2 despite the unit having three
inputs, which strongly suggests the document was copied from the original
two-input TITAN. If parameter `03` switches to HDMI3, you've found something
the documentation doesn't have. If it does nothing, try `04` via `raw`.

### Test 5 — standby behaviour ⚠ run this early

The most important unknown in the entire project.

1. Put the projector into standby.
2. Watch the console. Does it report USB disconnect?
3. Send `wake`.

**If the USB ports stay powered in standby and `wake` works**, you have real
discrete power-on and the architecture is settled.

**If the ports go dead**, nothing you send can ever wake it, and power-on has
to come from elsewhere — see §7.

### Test 6 — does IR exist at all

Wire the IR LED (anode → 100 Ω → GPIO4, cathode → GND). Aim at the rear panel
from ~30 cm, projector on, then:

```
ir all
```

This fires all 27 published TITAN codes in both plausible byte orders of the
`0x21DE` header. Any OSD reaction at all — a volume bar, an input change, a
power dialog — means there's a live receiver. Repeat aiming at the front and
the bottom-front before concluding it's dead.

### Decision tree

| Test 1 (HID) | Test 2 (CDC) | What it means | Do this |
|---|---|---|---|
| ✅ | ✅ | Best case | Skip §4 entirely, go to §5 |
| ✅ | ❌ | USB fine, CDC not bound | §4 — the dongle path |
| ❌ | ❌ | Not enumerating at all | Check cable is data-capable, try the other projector USB port, then §4 |
| ❌ | ✅ | Very unlikely | Note it and carry on; serial is what matters |

The ✅/❌ case is the one to expect. The projector's serial daemon almost
certainly opens `/dev/ttyUSB*`, created by the ch341/cp210x/pl2303 drivers,
whereas a CDC-ACM device appears as `/dev/ttyACM*`. Spoofing VID/PID won't fix
it — those drivers issue vendor-specific control requests the ESP32 would
stall on.

---

## 4. Phase 2 — the dongle path

Only if Test 2 failed.

**Check the jumper first.** "Switchable 3.3 V and 5 V" is ambiguous on these
boards. Some designs switch only the VCC output pin while the CH340G itself
still runs at 5 V, leaving TXD idling at 5 V. The ESP32-S3's inputs are not
5 V tolerant.

Plug the module into a USB port with nothing else attached, jumper on 3.3 V,
and measure **TXD to GND**. An idle UART TX line sits high, so you want ~3.3 V.
If it reads ~5 V, put a divider on that line only: 1 kΩ from TXD to GPIO18,
2 kΩ from GPIO18 to GND.

### Wiring

| CH340 module | ESP32-S3 |
|---|---|
| TXD | GPIO18 |
| RXD | GPIO17 |
| GND | GND |
| VCC | **leave unconnected** |

VCC stays disconnected deliberately. You don't want the projector's USB rail
powering the bridge — the whole point is for it to stay alive when the
projector sleeps. Power the ESP32 from its own supply.

Set `USE_PHASE2_UART` to `1` in the sketch. Nothing else changes. Re-run
Tests 2, 3, 4, 5.

**Escalation order if CH340 doesn't bind:** CP2102 → genuine FTDI →
USB-RS232 DB9 + MAX3232 (ESP TX → DB9 pin 2, ESP RX → pin 3, GND → pin 5).

---

## 5. Target architecture

Once you know which channel works, build this. It uses **both** projector USB
ports, so you're not betting the project on a single binding.

```
                 ┌─────────────────────────┐
   Wi-Fi ────────┤                         │
  (Roku ECP)     │      ESP32-S3           │
                 │                         ├── native USB ──→ projector USB 2.0
   IR receiver ──┤   command table         │      (HID keyboard channel)
   (GPIO15)      │   macro engine          │
                 │   power state machine   ├── UART1 ──→ CH340 ──→ projector USB 3.0
                 └───────────┬─────────────┘              (serial channel)
                             │
                        own 5 V supply
```

Every transport is just a trigger. They all call the same `sendCmd()`. Choose
between them on reliability, not capability.

- **Roku ECP over Wi-Fi** — primary. HTTP returns a status code, so the ESP32
  can tell you whether a macro completed. IR can never do that.
- **IR receiver** — offline fallback. A dead router shouldn't cost you your
  projector remote.
- **HID + serial together** — the Menu key and manual focus only exist on the
  keyboard side; everything discrete only exists on the serial side.

### The Roku trick

The X2's Wi-Fi category is not a generic "POST to any URL" builder — SofaBaton
supports Roku, Sonos, and Philips Hue, plus a Home Assistant remote over MQTT.
So make the ESP32 answer to `ST: roku:ecp` on SSDP (239.255.255.250:1900),
serve `/query/device-info` on port 8060, and accept `POST /keypress/<Key>`.

The app then discovers it as a Roku and hands you a native button layout, with
each press arriving as a direct HTTP POST. Roku TV devices support **PowerOn
and PowerOff as discrete keys**, plus VolumeUp/Down/Mute and InputHDMI1–4 —
which maps onto this projector almost too neatly.

Suggested map:

| ECP key | Projector action |
|---|---|
| PowerOn / PowerOff | State-checked power (§6) |
| InputHDMI1/2/3 | Source select |
| Up/Down/Left/Right/Select/Back/Home | Nav passthrough |
| VolumeUp/Down/Mute | Volume |
| Play / Rev / Fwd | Picture mode presets |
| Info / InstantReplay | Brightness step up/down |
| Search / Enter | Macro slots (3D mode, lens memory) |

**Untested:** whether SofaBaton's Roku profile surfaces the TV-only keys.
Present as a Roku TV in `device-info` and see what buttons the app offers.

Network gotchas: the hub needs **2.4 GHz**; devices are addressed by IP so pin
the reservation; keep hub and ESP32 on the **same subnet**, because SSDP is
multicast and won't cross VLANs. One owner also found the hub's firmware
updater choked on a VPN'd IoT network.

---

## 6. Reaching the deep features

The serial set covers input select, seven picture modes, brightness 1–10,
screen blank, high-refresh modes, volume, mute, autofocus and standby wake.

It does **not** cover 3D mode, lens memory, iris level, keystone, or lens
shift. Instruction `0x07` is the way in, because it exposes Settings, Home,
Back, OK and all four directions as simulated key presses. The ESP32 can drive
the OSD blind.

### Anchor and navigate

Never assume where the menu is. Every macro opens by forcing a known state:

```
Back, Back, Back   (unwind whatever's on screen)
Settings           (enter at a known root)
Down × n, OK       (counted path from the photos you took in §2)
...
Back × n           (exit cleanly)
```

Without the anchor, one desynced press sends the rest of the sequence
somewhere random. With it, macros are repeatable.

Use **150–300 ms between steps**. The OSD animates, and commands sent during a
transition get eaten. Tune per macro — find the fastest reliable delay rather
than using one global value.

This is why the macro engine lives on the ESP32 and not in the SofaBaton's
activity editor. You need per-step timing control and conditional logic that a
universal remote can't express.

**The fragility, stated plainly:** these macros are positional. A firmware
update that reorders a menu breaks every one of them. Budget an evening to
re-map after major OTAs, and keep the §2 photographs versioned alongside the
firmware number.

### Mapping worksheet

For each deep feature, record: entry anchor → exact keypress sequence →
observed end state → measured minimum delay → date and firmware version.
Do this once, carefully, and the macros stop being guesswork.

---

## 7. Reliable power

- **On** is solved, assuming Test 5 passed. The wake command is discrete, and
  the fact that it's ASCII rather than a keypress suggests a separate
  always-listening path.
- **Off** is not discrete. You get a power *key*, which likely raises a
  confirmation dialog. Watch what actually appears, then build it as a
  two-step macro: power → delay → OK.
- **Make both idempotent.** Poll the temperature query as a liveness check —
  a reply means awake, silence means asleep. Now "on" and "off" mean what they
  say regardless of current state, which is more than the Bluetooth remote
  manages.

**If Test 5 failed** and the USB ports die in standby, power-on must come from
outside the bridge:

1. Smart plug plus the projector's auto-power-on-when-mains-applied setting.
2. HDMI-CEC from your AVR.

Both are documented by XGIMI as automation options. The bridge still handles
everything else once the projector is awake.

---

## 8. Command reference

Frame: `2A 2A <len> <instr> <params> <checksum>`, checksum = sum of everything
after the header, mod 256.

| Function | Instr | Param | Full frame |
|---|---|---|---|
| HDMI1 | 01 | 01 | `2A2A 02 01 01 04` |
| HDMI2 | 01 | 02 | `2A2A 02 01 02 05` |
| HDMI3 (untested) | 01 | 03 | `2A2A 02 01 03 06` |
| USB source | 01 | 14 | `2A2A 02 01 14 17` |
| Vivid | 03 | 00 | `2A2A 02 03 00 05` |
| Movie | 03 | 01 | `2A2A 02 03 01 06` |
| IMAX Enhanced | 03 | 02 | `2A2A 02 03 02 07` |
| Performance | 03 | 05 | `2A2A 02 03 05 0A` |
| TV | 03 | 07 | `2A2A 02 03 07 0C` |
| Sport | 03 | 09 | `2A2A 02 03 09 0E` |
| Filmmaker | 03 | 1B | `2A2A 02 03 1B 20` |
| Brightness 1–10 | 05 | 01–0A | `2A2A 02 05 0n ..` |
| Factory reset (keep apps) | 06 | 01 | *use with care* |
| Power key | 07 | 00 | `2A2A 02 07 00 09` |
| Source | 07 | 08 | `2A2A 02 07 08 11` |
| Up / Down / Right / Left | 07 | 09/0A/0B/0C | |
| OK / Back | 07 | 0D/0E | |
| Settings / Home | 07 | 0F/10 | |
| Volume + / − | 07 | 11/12 | |
| Auto focus / Manual focus | 07 | 13/14 | |
| Mute | 07 | 15 | |
| Standby wake | 09 | "wakeup" | `2A2A 07 09 77 61 6B 65 75 70 9D` |
| Screen off / on | 0D | 00/01 | `2A2A 02 0D 00 0F` / `..01 10` |
| High refresh off / basic | 0E | 00/01 | |
| High refresh extreme | 0E | 02 | see note |
| Temperature query | 13 | 00 | `2A2A 02 13 00 15` |

**Note on high-refresh extreme:** XGIMI's table prints parameter `01` but
gives checksum `0x12`, which only works if the parameter is `02`. Going with
the checksum. Verify on hardware.

**Temperature postbacks** arrive unprompted as `2A2A 03 13 01 XX`:
`00` normal, `01` high-temp warning, `02` high-temp shutdown warning,
`03` low-temp shutdown warning, `04` sensor abnormal. This is your only
feedback channel — log it.

### TITAN IR codeset (NEC, header 0x21DE)

Only relevant if Test 6 finds a receiver.

Power `4D`, Home `1A`, Menu `45`, Back `42`, Source `4F`, Up `0B`, Down `0E`,
Left `10`, Right `11`, OK `0D`, Settings `48` (`12` after OSD activation),
OSD `19`, Vol+ `18`, Vol− `17`, Focus+ `1C`, Focus− `1D`, Menu rotation `1E`,
Keystone `1F`, Lens shift & zoom `20`, HDMI1 `24`, HDMI2 `25`, USB `26`,
Picture Standard `41`, Movie `44`, Vivid `4A`, TV `4B`.

---

## 9. Risks and gotchas

| Risk | Mitigation |
|---|---|
| USB ports dead in standby | Test 5 first. Fall back to smart plug or CEC |
| CDC never binds | Expected. CH340 dongle is already on the way |
| 5 V logic on the CH340 TXD | Measure before wiring. 1k/2k divider if needed |
| Projector back-feeding the ESP32 | Leave the module's VCC pin disconnected |
| Both ESP USB ports powered at once | Espressif lists power inputs as mutually exclusive. Fine in practice; use a VBUS-cut cable if you want to be strict |
| SSDP not crossing VLANs | Keep hub and ESP32 on one subnet |
| IP changes break Wi-Fi control | DHCP reservation |
| OTA reorders menus | Macros break. Re-map, keep photos versioned |
| IRremoteESP8266 v2.8.6 won't build on core 3.x | Use v2.9.0+, or patch IRrecv.cpp, or drop to core 2.x |

---

## 10. After every firmware update

1. Re-pull XGIMI's RS232 help-centre article and **diff the command table
   image**. They've already revised it once. New instruction bytes are the
   cheapest capability you will ever gain — lens memory is promised in a
   future OTA and may well arrive with serial commands attached.
2. Re-run Test 5. Standby power behaviour can change.
3. Re-verify one macro per menu branch before trusting the rest.
4. Note the new firmware version against your mapping worksheet.

---

## Weekend sequence

**Saturday morning:** §2 pre-flight, then Tests 0–6. Stop and read the
decision tree.

**Saturday afternoon:** §4 if needed. Get one channel talking to the projector
reliably. That's the day's success criterion — everything after this is
software you can write at leisure.

**Sunday:** dual-channel wiring per §5, ECP emulation, then the macro engine
and menu mapping.

If Saturday ends with `temp` returning bytes, the hard part is over.
