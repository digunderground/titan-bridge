# Keyboard probe and hand-mapping

A USB keyboard drives this projector's OSD. That makes two jobs possible
tonight, with no bridge hardware and no firmware:

1. **Verify the `HIDKEYS` table**, which is currently guesswork.
2. **Count the menu paths** for the features serial cannot reach — at typing
   speed, instead of one 250 ms macro step at a time.

Print this. Fill in the boxes. Both tables go straight into code.

---

## 1 — Which usages does it honour?

`HIDKEYS[]` in `firmware/titan_bridge_p2/titan.cpp` asserts twelve HID usage
codes. The arrows and Enter are safe bets. **The rest are assumptions nobody
has tested**, and four of them are the interesting ones: the projector may map
volume and focus to something else entirely, or to nothing.

Press each physical key with the OSD open and note what happens.

**RESULTS — measured on a TITAN Noir Max, 2026-09-05.** Every usage below was
fired individually through `/api/hidraw` and watched on the OSD.

| Usage | Physical key | Result | Firmware name |
|---|---|---|---|
| 0x52 / 0x51 / 0x50 / 0x4F | ↑ ↓ ← → | **works** | `up` `down` `left` `right` |
| 0x28 | Enter | **works** | `ok` |
| 0x29 | Esc | **works** | `back` |
| 0x4A | Home | **works — opens the OSD** | `menu`, `home` |
| **0x66** | **Keyboard Power** | **works — switches OFF *and* wakes** | **`power`** |
| 0x80 / 0x81 | Volume Up / Down | **works** | `volup` `voldn` |
| 0x4B / 0x4E | Page Up / Down | **works** | `pgup` `pgdn` |
| 0x57 / 0x56 | numpad + / − | **works** | `focus+` `focus-` |
| 0x7F | Mute | sent, effect unconfirmed | `mute` |
| 0x4D | End | unconfirmed | — |
| **0x65** | context-menu ▤ | **nothing** | *removed* |

### The two that mattered

**0x66 is discrete power, in both directions.** It switches the projector off,
and it wakes it from standby. That is the capability this whole project exists
to obtain, and it needs no serial channel, no smart plug and no HDMI-CEC.

**0x65 did nothing, and used to be the `menu` binding.** The macro `anchor`
routed `k:setting` to it, so every anchored macro would have failed silently
on the HID channel. `0x4A` is the real menu key. This is exactly the trap this
worksheet existed to catch, and it was caught before a single macro was written
against it.

`menu` and the two `focus` entries are the ones that justify the HID channel at
all — everything else has a serial equivalent via instruction `0x07`. If those
three do nothing, the native-USB port earns its place only for whatever you
find below.

Worth a minute of wandering off-script too: **F1–F12, the media keys, and the
number row.** A projector's key handler often accepts more than its remote
exposes, and anything you find here is capability the serial table does not
have. Record hits:

Found by wandering off-script: **0x66 (Keyboard Power)** and the keyboard-page
volume usages **0x80 / 0x81**, none of which were in the original table. The
projector's key handler accepts more than its own remote exposes.

Still worth trying: F1–F12 (0x3A–0x45), Insert/Delete (0x49/0x4C), Tab (0x2B),
Space (0x2C), and the Consumer Control usage page — which needs a second HID
report descriptor and would reach media-transport keys the keyboard page
cannot.

```
Further keys that did something: _________________________________________
```

---

## 2 — Count the paths

These five have no serial command. They are the entire reason the macro engine
exists, and each one is just a counted key sequence from a known starting
point.

**Anchor first, every time.** Esc three times to unwind whatever is on screen,
then enter at the root. Count from there and never from "wherever it was".

For each feature: anchor, walk to it with the arrows, and write down the exact
presses. `d3` means Down three times.

### 3D mode
```
Path:   anchor → ______________________________________________________
Script: anchor; ________________________________________________________
```

### Lens memory (save / recall)
```
Path:   anchor → ______________________________________________________
Script: anchor; ________________________________________________________
```

### Iris level
```
Path:   anchor → ______________________________________________________
Script: anchor; ________________________________________________________
```

### Keystone
```
Path:   anchor → ______________________________________________________
Script: anchor; ________________________________________________________
```

### Lens shift
```
Path:   anchor → ______________________________________________________
Script: anchor; ________________________________________________________
```

The script grammar is in `docs/05-menu-mapping-worksheet.md`: `s:<cmd>` for a
serial key, `h:<key>` for an HID key, `d<ms>` for a delay, `tok*<n>` to repeat.
A path counted here becomes a `macdef` line with no translation.

---

## 3 — Two things to notice while you are in there

**Does the cursor wrap?** If pressing Down at the bottom of a list returns to
the top, then `down*20` always lands on the last item regardless of where it
started — a free way to make a macro self-correcting instead of merely counted.

```
Lists wrap top/bottom?   [ ] yes   [ ] no
```

**How long does each transition take?** The OSD animates and eats keys sent
mid-transition. Hold a key down and watch whether the cursor keeps up, or
count how long a submenu takes to settle. That number is `MACRO_STEP_MS`, and
guessing it wrong is the most likely cause of a macro that works four times
out of five.

```
Submenu open/close feels like: ________ ms     Cursor step: ________ ms
```

---

## Why this survives the firmware

Every path counted here is tied to the **projector firmware version**, not to
the bridge. Record that version at the top of `logs/TEST-LOG.md` before you
start. When XGIMI pushes an update, these counts are the first thing to
re-verify — and the reason the anchor exists is so that re-verification is a
five-minute job rather than a rewrite.
