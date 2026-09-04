#!/usr/bin/env python3
"""
titan_serial.py — talk to an XGIMI TITAN Noir from a laptop, over a USB-TTL or
USB-RS232 adapter, with no ESP32 in the way.

Why this exists: when something does not work you want to remove variables, and
the ESP32 is a variable. Plug a CH340 straight into the projector, run this,
and you find out whether the projector end works before adding anything else to
the list of suspects. It is also the fastest way to explore undocumented
commands, because you can type them.

Same command names as the firmware, same frame builder, same decoder.

    pip install pyserial

    python3 titan_serial.py --list
    python3 titan_serial.py -p /dev/tty.usbserial-1420
    python3 titan_serial.py -p COM4 --send hdmi1
    python3 titan_serial.py -p /dev/ttyUSB0 --sweep 01 00 08
    python3 titan_serial.py -p /dev/ttyUSB0 --monitor      # just listen

On the projector: Settings > General > Serial Port Control > ON.
115200 8N1, no flow control.
"""

import argparse
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")

# ---------------------------------------------------------------------------
# Command table — verified against XGIMI's published command-set image.
# name: (instruction, [parameters])
# ---------------------------------------------------------------------------
CMDS = {
    # source select
    "hdmi1": (0x01, [0x01]),
    "hdmi2": (0x01, [0x02]),
    "hdmi3": (0x01, [0x03]),          # undocumented
    "usbsrc": (0x01, [0x14]),
    # picture mode
    "vivid": (0x03, [0x00]),
    "movie": (0x03, [0x01]),
    "imax": (0x03, [0x02]),
    "perf": (0x03, [0x05]),
    "tvmode": (0x03, [0x07]),
    "sport": (0x03, [0x09]),
    "filmmaker": (0x03, [0x1B]),
    # simulated key press
    "power": (0x07, [0x00]),
    "source": (0x07, [0x08]),
    "up": (0x07, [0x09]),
    "down": (0x07, [0x0A]),
    "right": (0x07, [0x0B]),
    "left": (0x07, [0x0C]),
    "ok": (0x07, [0x0D]),
    "back": (0x07, [0x0E]),
    "setting": (0x07, [0x0F]),
    "home": (0x07, [0x10]),
    "volup": (0x07, [0x11]),
    "voldn": (0x07, [0x12]),
    "autofocus": (0x07, [0x13]),
    "manfocus": (0x07, [0x14]),
    "mute": (0x07, [0x15]),
    # standby wake — parameters spell "wakeup"
    "wake": (0x09, list(b"wakeup")),
    # screen blank
    "blank": (0x0D, [0x00]),
    "unblank": (0x0D, [0x01]),
    # high refresh rate. The document prints parameter 01 for "extreme speed"
    # but gives checksum 0x12, which is only reachable with parameter 02.
    "hrroff": (0x0E, [0x00]),
    "hrrbasic": (0x0E, [0x01]),
    "hrrmax": (0x0E, [0x02]),
    # status
    "temp": (0x13, [0x00]),
}
for _i in range(1, 11):
    CMDS[f"b{_i}"] = (0x05, [_i])

# Deliberately absent: instruction 0x06, factory reset.
#   2A2A 02 06 00 08  wipes apps      2A2A 02 06 01 09  keeps apps
# Reachable only through --raw, on purpose.

TEMP_STATUS = {
    0x00: "normal",
    0x01: "HIGH TEMPERATURE WARNING",
    0x02: "HIGH TEMPERATURE SHUTDOWN WARNING",
    0x03: "LOW TEMPERATURE SHUTDOWN WARNING",
    0x04: "SENSOR ABNORMAL (open/short circuit)",
}


def build(instr, params):
    """2A 2A <len> <instr> <params...> <checksum>"""
    params = list(params)
    length = 1 + len(params)
    body = [length, instr] + params
    checksum = sum(body) & 0xFF
    return bytes([0x2A, 0x2A] + body + [checksum])


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def describe(frame):
    if len(frame) >= 7 and frame[3] == 0x13 and frame[4] == 0x01:
        return "temperature status: " + TEMP_STATUS.get(frame[5], f"unknown 0x{frame[5]:02X}")
    if len(frame) >= 4:
        return f"instruction 0x{frame[3]:02X}"
    return ""


class Reader(threading.Thread):
    """Frame-aware receiver: 2A 2A <len> then len+1 more bytes."""

    daemon = True

    def __init__(self, port):
        super().__init__()
        self.port = port
        self.buf = bytearray()
        self.stop = threading.Event()
        self.frames = 0

    def run(self):
        while not self.stop.is_set():
            try:
                data = self.port.read(64)
            except Exception:
                return
            if not data:
                continue
            self.buf += data
            self._parse()

    def _parse(self):
        while True:
            # resync on the header
            i = self.buf.find(b"\x2a\x2a")
            if i < 0:
                if len(self.buf) > 1:
                    self.buf = self.buf[-1:]
                return
            if i:
                junk, self.buf = self.buf[:i], self.buf[i:]
                print(f"  <- {hexs(junk)}   (not a frame)")
            if len(self.buf) < 3:
                return
            want = self.buf[2] + 4
            if len(self.buf) < want:
                return
            frame, self.buf = bytes(self.buf[:want]), self.buf[want:]
            self.frames += 1
            ok = (sum(frame[2:-1]) & 0xFF) == frame[-1]
            note = describe(frame)
            flag = "" if ok else "  [CHECKSUM MISMATCH]"
            print(f"\n  <- {hexs(frame)}   {note}{flag}")


def send(port, instr, params, quiet=False):
    frame = build(instr, params)
    port.write(frame)
    port.flush()
    if not quiet:
        print(f"  -> {hexs(frame)}")


def parse_hex_arg(s):
    return int(s, 16)


def do_sweep(port, instr, lo, hi, delay):
    if instr == 0x06:
        sys.exit("refusing to sweep instruction 0x06 — that is factory reset")
    print(f"Sweeping instruction 0x{instr:02X}, parameters "
          f"0x{lo:02X}..0x{hi:02X}. Watch the screen.\n")
    for p in range(lo, hi + 1):
        print(f"  param 0x{p:02X}", end="  ")
        send(port, instr, [p])
        time.sleep(delay)
    print("\nDone. Which parameters did something?")


HELP = """
Commands
  <name>              send a projector command (see --list)
  raw 2A2A020101 04   send bytes exactly as typed, no checksum recalculation
  sweep <ins> <a> <b> walk a parameter range, 1.2 s apart, all hex
  poll [seconds]      send 'temp' repeatedly — leave running through standby
  wait                stop polling
  ?                   this help
  quit
"""


def repl(port, reader):
    poll_every = None
    poll_last = 0.0
    print(HELP)
    print("Type a command. 'temp' first — a reply means the link is up.\n")
    while True:
        try:
            if poll_every:
                # non-blocking-ish: check for input with a short timeout
                import select
                r, _, _ = select.select([sys.stdin], [], [], 0.25)
                if time.time() - poll_last >= poll_every:
                    poll_last = time.time()
                    send(port, *CMDS["temp"])
                if not r:
                    continue
                line = sys.stdin.readline()
                if not line:
                    return
            else:
                line = input("titan> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return

        line = line.strip()
        if not line:
            continue
        parts = line.split()
        cmd, args = parts[0].lower(), parts[1:]

        if cmd in ("quit", "exit"):
            return
        if cmd in ("?", "help"):
            print(HELP)
            continue
        if cmd == "wait":
            poll_every = None
            print("  polling off")
            continue
        if cmd == "poll":
            poll_every = float(args[0]) if args else 5.0
            poll_last = 0.0
            print(f"  polling 'temp' every {poll_every} s — Ctrl-C or 'wait' to stop")
            continue
        if cmd == "raw":
            data = bytes.fromhex("".join(args).replace(",", ""))
            port.write(data)
            port.flush()
            print(f"  -> {hexs(data)}   (raw)")
            continue
        if cmd == "sweep":
            if len(args) != 3:
                print("  sweep <instruction> <from> <to>   (hex)")
                continue
            do_sweep(port, *(parse_hex_arg(a) for a in args), delay=1.2)
            continue
        if cmd in CMDS:
            send(port, *CMDS[cmd])
            continue
        print(f"  unknown command '{cmd}' — '?' for help, --list for the table")


def main():
    ap = argparse.ArgumentParser(
        description="Talk to an XGIMI TITAN Noir over a serial adapter.")
    ap.add_argument("-p", "--port", help="serial device, e.g. /dev/tty.usbserial-1420")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--ports", action="store_true", help="list serial ports and exit")
    ap.add_argument("--list", action="store_true", help="list projector commands and exit")
    ap.add_argument("--send", metavar="NAME", help="send one command and exit")
    ap.add_argument("--raw", metavar="HEX", help="send raw bytes and exit")
    ap.add_argument("--sweep", nargs=3, metavar=("INSTR", "FROM", "TO"),
                    help="walk a parameter range (hex) and exit")
    ap.add_argument("--monitor", action="store_true",
                    help="only listen — useful for catching unprompted postbacks")
    ap.add_argument("--wait", type=float, default=2.0,
                    help="seconds to keep listening after a one-shot (default 2)")
    args = ap.parse_args()

    if args.ports:
        for p in list_ports.comports():
            print(f"{p.device:24} {p.description}")
        return
    if args.list:
        for name in sorted(CMDS):
            instr, params = CMDS[name]
            print(f"  {name:<12} {hexs(build(instr, params))}")
        return
    if not args.port:
        ap.error("--port is required (see --ports)")

    port = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"{args.port} @ {args.baud} 8N1")
    reader = Reader(port)
    reader.start()

    try:
        if args.raw:
            data = bytes.fromhex(args.raw.replace(" ", "").replace(",", ""))
            port.write(data)
            port.flush()
            print(f"  -> {hexs(data)}   (raw)")
            time.sleep(args.wait)
        elif args.sweep:
            do_sweep(port, *(parse_hex_arg(a) for a in args.sweep), delay=1.2)
            time.sleep(args.wait)
        elif args.send:
            if args.send not in CMDS:
                sys.exit(f"unknown command '{args.send}' — try --list")
            send(port, *CMDS[args.send])
            time.sleep(args.wait)
        elif args.monitor:
            print("Listening. Ctrl-C to stop.")
            while True:
                time.sleep(1)
        else:
            repl(port, reader)
    except KeyboardInterrupt:
        print()
    finally:
        reader.stop.set()
        time.sleep(0.3)
        port.close()
        print(f"{reader.frames} frame(s) received.")


if __name__ == "__main__":
    main()
