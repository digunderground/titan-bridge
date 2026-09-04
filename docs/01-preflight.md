# Pre-flight — before the parts arrive

All software and reconnaissance. Half an hour, and it removes most of what
would otherwise go wrong on Saturday morning.

### 1. Toolchain

Install the Arduino IDE and the esp32 core, or arduino-cli. Set the board
options from `docs/04-flashing.md`. Compile `firmware/titan_bridge_p1` once
without a board attached — a compile error found now is free, and found on
Saturday it costs an hour.

### 2. Enable serial control on the projector

Settings → General → **Serial Port Control** → ON.

Confirm the menu item actually exists on your firmware. If it does not, stop:
everything downstream depends on it, and that is worth a support ticket before
the weekend rather than a surprise during it.

### 3. Record the firmware version

Settings → system information. Write it here:

```
Projector firmware: ______________________   Date: ____________
```

Every macro in §6 of the plan is tied to this build. When it changes, the
positional macros are the first thing to re-verify.

### 4. Photograph the settings menu tree

Every page, in order, with the cursor position visible. All of it — Picture,
Sound, Network, System, and every submenu you can reach.

This is the single most valuable thing you can do before the parts arrive.
Counting keypresses from photographs takes minutes; reverse-engineering the
same paths live, one 250 ms step at a time, takes an evening.

Keep them next to the firmware version — `logs/` is the obvious home, and
`docs/05-menu-mapping-worksheet.md` is where the counted paths go.

### 5. Network

- Decide the bridge's IP and put a **DHCP reservation** on it. SofaBaton
  addresses devices by IP.
- Confirm the hub and the bridge will be on the **same subnet and 2.4 GHz**.
  SSDP is multicast; it will not cross a VLAN, and the hub does not do 5 GHz.
- If your IoT network runs through a VPN, be aware one owner found the hub's
  firmware updater choked on exactly that.

### 6. Look for an IR window

Inspect the rear panel under a torch for a small dark window near the
indicator LED. On the original TITAN that is exactly where the receiver sits.
Check the front and the bottom-front bezel too.

If there is no window anywhere on the chassis, Test 6 is a formality — run it
anyway, because it costs 30 seconds, but do not spend the afternoon on it.

### 7. Read the decision tree

`docs/02-day1-test-card.md`, the table at the bottom. Knowing in advance which
result sends you where is what keeps Saturday from becoming improvisation.

---

## Before the projector arrives — testing without it

If the CH340 modules arrive before the projector, or the projector before the
ESP32, `tools/titan_serial.py` speaks the same protocol from your laptop with
nothing but a USB-TTL adapter:

```bash
python3 tools/titan_serial.py --port /dev/tty.usbserial-1420
```

Same command names as the firmware, same frame builder, same decoder. It is a
useful way to prove the projector end works before adding an ESP32 to the list
of things that might be wrong.
