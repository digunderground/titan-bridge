# XGIMI TITAN Noir — verified command reference

Transcribed from XGIMI's own command-set image (help centre article
`53406154382361`, attachment `53406327353497`, 2110×1253) and checked
byte-for-byte against the checksum rule. Where the source document is
internally inconsistent, that is called out rather than silently corrected.

## Link settings

| | |
|---|---|
| Enable | Settings → General → **Serial Port Control** = ON |
| Transport | USB-to-RS232 or USB-to-TTL adapter in a projector USB port |
| Baud | 115200 |
| Frame | 8 data bits, no parity, 1 stop bit |
| Flow control | none |

Both projector USB ports (one 3.0, one 2.0) are documented as accepting a
USB-to-RS232 adapter, which is what makes the two-channel architecture
possible.

## Frame format

```
2A 2A <length> <instruction> <parameters...> <checksum>

length   = 1 (the instruction byte) + number of parameter bytes
checksum = (length + instruction + all parameter bytes) & 0xFF
```

The header `2A 2A` is excluded from the checksum. Worked examples from XGIMI's
own table: HDMI1 is `2A2A 02 01 01 04` (0x02+0x01+0x01 = 0x04); standby wake is
`2A2A 07 09 77 61 6B 65 75 70 9D`, where the parameters spell `wakeup` in ASCII
and the bytes sum to 0x29D → 0x9D.

---

## Source select — instruction 0x01

| Function | Param | Full frame | Firmware name |
|---|---|---|---|
| HDMI1 | 01 | `2A2A 02 01 01 04` | `hdmi1` |
| HDMI2 | 02 | `2A2A 02 01 02 05` | `hdmi2` |
| USB | 14 | `2A2A 02 01 14 17` | `usbsrc` |
| HDMI3 — **not in the document** | 03 | `2A2A 02 01 03 06` | `hdmi3` |

The published table lists two HDMI inputs on a three-HDMI projector, which
suggests it was copied from the original two-input TITAN. `hdmi3` is included
in the firmware on that suspicion. Day 1 Test 4 settles it; if `03` does
nothing, `sweep 01 00 08` walks the whole space.

## Picture mode — instruction 0x03

| Mode | Param | Frame | Name |
|---|---|---|---|
| Vivid | 00 | `2A2A 02 03 00 05` | `vivid` |
| Movie | 01 | `2A2A 02 03 01 06` | `movie` |
| IMAX Enhanced | 02 | `2A2A 02 03 02 07` | `imax` |
| Performance | 05 | `2A2A 02 03 05 0A` | `perf` |
| TV | 07 | `2A2A 02 03 07 0C` | `tvmode` |
| Sport | 09 | `2A2A 02 03 09 0E` | `sport` |
| Filmmaker | 1B | `2A2A 02 03 1B 20` | `filmmaker` |

Parameters 03, 04, 06, 08 and 0A–1A are unassigned in the document. `sweep 03
00 20` is the cheapest way to find out whether any of them do anything.

## Brightness — instruction 0x05

Levels 1–10 are parameters `01`–`0A`, checksums `08`–`11`.

| Level | Frame | Name |
|---|---|---|
| 1 | `2A2A 02 05 01 08` | `b1` |
| 5 | `2A2A 02 05 05 0C` | `b5` |
| 10 | `2A2A 02 05 0A 11` | `b10` |

There is **no brightness query**. The bridge's step-up/step-down keys track
their own index open-loop and resynchronise whenever an explicit `b1`–`b10` is
sent.

## Factory reset — instruction 0x06  ⚠

| Function | Param | Frame |
|---|---|---|
| Restore factory settings, **wiping apps** | 00 | `2A2A 02 06 00 08` |
| Restore factory settings, keeping apps | 01 | `2A2A 02 06 01 09` |

Neither is reachable by name in either firmware, and `sweep` refuses
instruction `06` outright. Send it by hand with `raw` if you ever actually
mean it.

## Simulated key press — instruction 0x07

This is the way into everything the serial set does not cover: it drives the
OSD blind. See `docs/05-menu-mapping-worksheet.md`.

| Key | Param | Frame | Name |
|---|---|---|---|
| Power | 00 | `2A2A 02 07 00 09` | `power` |
| Signal source / custom | 08 | `2A2A 02 07 08 11` | `source` |
| Up | 09 | `2A2A 02 07 09 12` | `up` |
| Down | 0A | `2A2A 02 07 0A 13` | `down` |
| Right | 0B | `2A2A 02 07 0B 14` | `right` |
| Left | 0C | `2A2A 02 07 0C 15` | `left` |
| OK | 0D | `2A2A 02 07 0D 16` | `ok` |
| Back | 0E | `2A2A 02 07 0E 17` | `back` |
| Setting | 0F | `2A2A 02 07 0F 18` | `setting` |
| Home | 10 | `2A2A 02 07 10 19` | `home` |
| Volume up | 11 | `2A2A 02 07 11 1A` | `volup` |
| Volume down | 12 | `2A2A 02 07 12 1B` | `voldn` |
| Auto focus | 13 | `2A2A 02 07 13 1C` | `autofocus` |
| Manual focus | 14 | `2A2A 02 07 14 1D` | `manfocus` |
| Mute | 15 | `2A2A 02 07 15 1E` | `mute` |

The **power key is not discrete** — it raises a confirmation dialog. The
bridge's power-off is therefore a two-step macro (power → delay → OK) with a
verification probe afterwards.

There is **no Menu key** here. The USB HID keyboard's Application/Menu key
(usage `0x65`) has no serial equivalent, which is the whole reason the HID
channel stays in the final design.

## Standby wake — instruction 0x09

```
2A2A 07 09 77 61 6B 65 75 70 9D          parameters = "wakeup" in ASCII
```

Conspicuously not a simulated key press. An ASCII payload on its own
instruction suggests a separate always-listening path in the projector's
firmware — which is exactly what you would want a wake command to be. Whether
it survives standby depends on Test 5.

## Screen blank — instruction 0x0D

| Function | Param | Frame | Name |
|---|---|---|---|
| Turn off the screen | 00 | `2A2A 02 0D 00 0F` | `blank` |
| Open screen | 01 | `2A2A 02 0D 01 10` | `unblank` |

## High refresh rate — instruction 0x0E

| Function | Param | Checksum in doc | Frame used | Name |
|---|---|---|---|---|
| Off | 00 | 10 | `2A2A 02 0E 00 10` | `hrroff` |
| Basics | 01 | 11 | `2A2A 02 0E 01 11` | `hrrbasic` |
| Extreme speed | **01** | **12** | `2A2A 02 0E 02 12` | `hrrmax` |

The document prints parameter `01` twice with two different checksums. `0x12`
is only reachable if the parameter is `02` (0x02+0x0E+0x02), and a distinct
mode needs a distinct parameter. **The firmware follows the checksum.** Verify
on hardware and, if `02` does nothing, try `raw 2A2A020E0111` and watch whether
the mode differs from `hrrbasic`.

## Temperature — instruction 0x13

Request:

```
2A2A 02 13 00 15                          firmware name: temp
```

Postbacks arrive **unprompted** as well as in reply:

| Meaning | Frame |
|---|---|
| Normal | `2A2A 03 13 01 00 17` |
| High temperature warning | `2A2A 03 13 01 01 18` |
| High temperature shutdown warning | `2A2A 03 13 01 02 19` |
| Low temperature shutdown warning | `2A2A 03 13 01 03 1A` |
| Sensor abnormal (open/short circuit) | `2A2A 03 13 01 04 1B` |

**This is the only feedback channel the projector has.** Everything the bridge
knows about power state is inferred from it: a reply means awake, silence
across three polls means asleep. Log it — a high-temperature warning is the
one thing here worth an alert.

---

## USB keyboard map (HID channel)

XGIMI documents a USB keyboard as a supported input device. These are the
usages the bridge sends.

| Key | HID usage | Function | Serial equivalent |
|---|---|---|---|
| Arrows | 0x4F–0x52 | Navigation | yes |
| Enter | 0x28 | OK | yes |
| Esc | 0x29 | Back | yes |
| Home | 0x4A | Open settings | yes |
| Page Up / Down | 0x4B / 0x4E | Volume | yes |
| Keypad + / − | 0x57 / 0x56 | Manual focus | partial (`manfocus` only) |
| **Menu / Application** | **0x65** | Contextual menu | **no** |

## TITAN IR codeset (NEC, header 0x21DE)

Published for the **original** TITAN. The Noir series dropped the rear IR
receiver, the 3.5 mm REMOTE port and the IR remote, so this is only relevant if
Day 1 Test 6 finds a live receiver anyway.

| Key | Code | Key | Code | Key | Code |
|---|---|---|---|---|---|
| Power | 4D | Home | 1A | Menu | 45 |
| Back | 42 | Source | 4F | Up | 0B |
| Down | 0E | Left | 10 | Right | 11 |
| OK | 0D | Settings | 48 | Settings (after OSD) | 12 |
| OSD | 19 | Vol+ | 18 | Vol− | 17 |
| Focus+ | 1C | Focus− | 1D | Menu rotation | 1E |
| Keystone | 1F | Lens shift & zoom | 20 | HDMI1 | 24 |
| HDMI2 | 25 | USB | 26 | Picture standard | 41 |
| Movie | 44 | Vivid | 4A | TV | 4B |

The p2 firmware **receives** IR rather than sending it, and decodes any NEC
remote — so this table matters for Test 6 and nothing else.

---

## What is not here

No serial command exists for 3D mode, lens memory, iris level, keystone or lens
shift. Instruction `0x07` is the way in: drive the OSD with counted key presses
from a known anchor. `docs/05-menu-mapping-worksheet.md` is how you record
those paths so they survive a firmware update.

## After every projector firmware update

1. Re-pull the help-centre article and **diff the command-table image**. XGIMI
   have already revised it once. New instruction bytes are the cheapest
   capability you will ever gain, and lens memory is promised in a future OTA.
2. Re-run Test 5 — standby power behaviour can change.
3. Re-verify one macro per menu branch before trusting the rest.
4. Note the new version against the mapping worksheet.
