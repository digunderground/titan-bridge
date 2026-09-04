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

| Firmware name | Usage | Press this key | Effect observed |
|---|---|---|---|
| `up` | 0x52 | ↑ | |
| `down` | 0x51 | ↓ | |
| `left` | 0x50 | ← | |
| `right` | 0x4F | → | |
| `ok` | 0x28 | Enter / Return | |
| `back` | 0x29 | Esc | |
| `home` | 0x4A | Home | |
| `volup` | 0x4B | Page Up | |
| `voldn` | 0x4E | Page Down | |
| `focus+` | 0x57 | numpad **+** | |
| `focus-` | 0x56 | numpad **−** | |
| `menu` | 0x65 | the ▤ context-menu key | |

`menu` and the two `focus` entries are the ones that justify the HID channel at
all — everything else has a serial equivalent via instruction `0x07`. If those
three do nothing, the native-USB port earns its place only for whatever you
find below.

Worth a minute of wandering off-script too: **F1–F12, the media keys, and the
number row.** A projector's key handler often accepts more than its remote
exposes, and anything you find here is capability the serial table does not
have. Record hits:

```
Unexpected keys that did something: ______________________________________

_________________________________________________________________________
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
