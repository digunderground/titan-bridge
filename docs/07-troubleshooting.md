# Troubleshooting

Ordered roughly by how often each one is the actual problem.

## Nothing on the serial console

- Wrong port. The **UART** USB-C connector, not the one labelled USB.
- `USB CDC On Boot` is Enabled. Set it to Disabled — otherwise `Serial` is the
  native USB port, which is plugged into the projector.
- Baud is not 115200.
- Line ending is not **Newline**. The board receives your typing but never sees
  the end of a line, so it looks dead while working perfectly.

## Uploads fail

- `Upload Mode` must be **UART0 / Hardware CDC**. USB HID mode disables serial
  upload over the native port.
- Hold **BOOT**, tap **RESET**, release BOOT, then upload. This always works
  because it bypasses whatever the firmware is doing.
- Drop the upload speed to 115200.

## `k down` does nothing (Test 1 fails)

- **Change the cable.** A charge-only USB-C-to-A cable is indistinguishable
  from a projector that ignores you.
- Try the other projector USB port.
- Confirm `USB Mode` is USB-OTG (TinyUSB) — without it there is no HID at all.
- Check the console for a `USB: started` line. If USB never started, it is the
  board settings; if it started and the projector still does nothing, it is the
  projector.

## `temp` returns nothing (Test 2 fails)

This is the **expected** outcome, not a fault. The projector's serial daemon
almost certainly opens `/dev/ttyUSB*`, created by the ch341/cp210x/pl2303
drivers, while the ESP32's CDC-ACM device appears as `/dev/ttyACM*`.

Spoofing VID/PID will not fix it — those drivers issue vendor-specific control
requests the ESP32 would stall on. Go to the dongle path in
`docs/03-wiring.md`.

Before you do, check the obvious: **Serial Port Control is ON** in Settings →
General, and it survived the last projector firmware update.

## The CH340 does not bind either

Escalate: CP2102 → genuine FTDI → USB-RS232 DB9 + MAX3232. Each is a different
kernel driver, and one of them is present in the projector's build.

If the DB9 path also fails, swap pins 2 and 3. The crossover is easy to get
backwards and produces exactly the same symptom as a dead link.

## Garbage bytes coming back

- Baud mismatch. Everything is 115200 8N1.
- Missing common ground between the ESP32 and the adapter. This is the single
  most common cause of "it half works".
- The adapter's TXD is 5 V and you are clamping it through the ESP32's
  protection diodes. Measure it, fit the 1k/2k divider.

## The projector reacts to some commands but not others

Expected. The published table is incomplete and, in at least one place,
internally inconsistent. Use `sweep <instr> <from> <to>` to walk a parameter
range and see what actually exists. Record findings in
`docs/06-command-reference.md`.

## Macros drift or land in the wrong menu

- The macro is missing its `anchor`. Never assume where the menu is.
- The step delay is too short. Raise it to 400 ms, confirm it works, then walk
  it back down until it fails and add 50 ms.
- The projector firmware updated and reordered a menu. Re-map from your
  photographs; this is the documented cost of positional macros.
- Something else was on screen when it started. `anchor` handles that — if the
  macro does not have one, that is the bug.

## Power on does nothing

- Check `/api/status` for `"power":"asleep"` — if the bridge already thinks it
  is awake, `p:on` is a deliberate no-op.
- If Test 5 showed the USB ports die in standby, nothing you send can reach the
  projector. Set `USB_DEAD_IN_STANDBY` to 1 so the bridge says so plainly
  instead of failing silently, and drive power from a smart plug or HDMI-CEC.

## Power off leaves it on

The power key raises a confirmation dialog. The bridge sends power, waits
`POWEROFF_CONFIRM_MS`, then sends OK. If your firmware's dialog defaults to
Cancel, or takes longer to appear, tune that constant — or replace the sequence
with a macro that navigates the dialog explicitly.

## SofaBaton does not find the bridge

- Same subnet? SSDP is multicast and will not cross a VLAN.
- Hub on 2.4 GHz?
- `python3 tools/ecp_probe.py --discover` from a laptop on that network. If the
  laptop finds it and the hub does not, the problem is the network path, not
  the bridge.
- Wi-Fi power saving is disabled in firmware for exactly this reason; if you
  changed it, multicast will be missed.

## The bridge falls off the network

- DHCP reservation not pinned, and the IP moved.
- 2.4 GHz signal is marginal where the projector is. Check `rssi` in the boot
  log; below about −75 dBm, move the bridge or add an access point.
- It reconnects on its own every 15 seconds. If it is looping, the credentials
  are wrong — `forget` and re-provision.

## Phantom IR codes

Fit the 100 Ω and 4.7 µF supply filter on the TSOP38238. Without it the
receiver picks up switching noise from the ESP32's own regulator.

## After a projector firmware update, everything positional breaks

Expected, and the reason for the worksheet. Re-pull XGIMI's command-table image
and diff it first — new instruction bytes are the cheapest capability you will
ever gain, and lens memory is promised in a future OTA. Then re-run Test 5, then
re-verify one macro per menu branch.
