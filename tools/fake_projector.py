#!/usr/bin/env python3
"""
fake_projector.py — pretend to be the TITAN Noir, so the bridge can be tested
without one.

`titan_serial.py` is the other half of this pair: it drives a real projector
from the laptop. This does the inverse — it sits on the far end of the bridge's
serial link, decodes what the firmware emits, and answers the temperature probe
so the power state machine has something to believe.

That makes it possible to prove, at a desk, with nothing else plugged in:

  * the firmware boots and its poll loop runs
  * the 2A2A frame builder and checksum are correct
  * the chosen channel actually carries bytes
  * the receive path decodes a reply and the bridge concludes it is awake

On a board flashed with CH_UART0 the bridge talks through its own onboard
USB-serial chip, so the laptop is already on the far end of the link — plug it
in and run this. On a CH_UART1 build, cross a USB-TTL adapter to the bridge's
UART1 pins instead.

    python3 tools/fake_projector.py --port /dev/cu.usbserial-0001

Opening the port asserts DTR/RTS, which resets most ESP32 dev boards. That is
expected: the board reboots and the first probe follows a couple of seconds
later. Pass --no-reset to leave the lines alone.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing — pip install -r tools/requirements.txt")

try:
    from titan_serial import describe          # one source of truth for decoding
except ImportError:
    def describe(frame):
        return f"instruction 0x{frame[3]:02X}" if len(frame) >= 4 else ""


def checksum(body):
    """The trailing byte is the sum of everything after the 2A 2A header."""
    return sum(body) & 0xFF


def build(instr, params):
    body = [1 + len(params), instr, *params]
    return bytes([0x2A, 0x2A, *body, checksum(body)])


# What a healthy projector says when asked for its temperature. The Day 1 test
# card quotes this exact frame: 2A 2A 03 13 01 00 17.
TEMP_NORMAL = build(0x13, [0x01, 0x00])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", required=True,
                    help="serial device the bridge is on, e.g. /dev/cu.usbserial-0001")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-s", "--seconds", type=float, default=0,
                    help="stop after this long (default: run until Ctrl-C)")
    ap.add_argument("--temp", default="00",
                    help="temperature byte to report: 00 normal, 01 warning, "
                         "02 shutdown warning, 04 sensor abnormal")
    ap.add_argument("--mute", action="store_true",
                    help="decode but never reply — the bridge should then decide "
                         "it is asleep after POLL_MISSES_TO_SLEEP probes")
    ap.add_argument("--no-reset", action="store_true",
                    help="clear DTR/RTS before opening, to try to avoid resetting "
                         "the board. Driver-dependent — observed to reset anyway "
                         "on macOS with the generic usbserial driver, so treat a "
                         "reboot at connect as expected either way")
    args = ap.parse_args()

    reply = build(0x13, [0x01, int(args.temp, 16)])

    if args.no_reset:
        s = serial.Serial(baudrate=args.baud, timeout=0.2)
        s.port = args.port
        s.dtr = False
        s.rts = False
        s.open()
    else:
        s = serial.Serial(args.port, args.baud, timeout=0.2)

    mode = "decoding only, never replying" if args.mute else \
           f"replying {' '.join(f'{b:02X}' for b in reply)}"
    print(f"listening on {args.port} @{args.baud} — {mode}")
    print("Ctrl-C to stop.\n")

    buf = bytearray()
    seen = 0
    dropped = 0
    deadline = time.time() + args.seconds if args.seconds else None
    t0 = time.time()
    last = None

    try:
        while deadline is None or time.time() < deadline:
            chunk = s.read(64)
            if chunk:
                buf += chunk

            # Resynchronise exactly as the firmware's decoder does: anything
            # that is not a 2A 2A header is discarded one byte at a time, so a
            # garbled burst (or the ROM boot log) recovers on the next frame.
            while len(buf) >= 3:
                if buf[0] != 0x2A or buf[1] != 0x2A:
                    buf.pop(0)
                    dropped += 1
                    continue
                need = 3 + buf[2] + 1
                if len(buf) < need:
                    break
                frame = bytes(buf[:need])
                del buf[:need]
                seen += 1

                body = frame[2:-1]
                ok = checksum(body) == frame[-1]
                now = time.time()
                gap = f"  (+{now - last:.1f}s)" if last else ""
                last = now
                print(f"[{now - t0:7.1f}s] RX {' '.join(f'{b:02X}' for b in frame)}"
                      f"   {describe(frame)}"
                      f"{'' if ok else '   ** CHECKSUM BAD **'}{gap}")

                if frame[3] == 0x13 and not args.mute:
                    s.write(reply)
                    s.flush()
                    print(f"{'':10} TX {' '.join(f'{b:02X}' for b in reply)}"
                          f"   temperature status reply")
    except KeyboardInterrupt:
        pass
    finally:
        s.close()

    print(f"\n{seen} frame(s) decoded, {dropped} byte(s) discarded as non-frame "
          f"(boot chatter is normal).")
    if not seen:
        print("Nothing arrived. Check: right port? right board profile? is the "
              "channel in config.h the one this cable is attached to?")


if __name__ == "__main__":
    main()
