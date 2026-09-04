#!/usr/bin/env python3
"""
ecp_probe.py — see the bridge the way a SofaBaton hub sees it.

Standard library only.

When the hub cannot find the bridge, this tells you which half is at fault: if
this script discovers it from a laptop on the same network and the hub does
not, the problem is the network path (VLAN, 2.4 GHz, multicast) and not the
firmware.

    python3 ecp_probe.py --discover
    python3 ecp_probe.py --host titan-bridge.local --info
    python3 ecp_probe.py --host titan-bridge.local --key PowerOn
    python3 ecp_probe.py --host titan-bridge.local --keys
"""

import argparse
import socket
import sys
import urllib.error
import urllib.request

MCAST = "239.255.255.250"
PORT = 1900

MSEARCH = (
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    'MAN: "ssdp:discover"\r\n'
    "ST: roku:ecp\r\n"
    "MX: 3\r\n"
    "\r\n"
).encode()

# The keys the bridge maps. Anything else returns "unmapped" in its log.
KNOWN_KEYS = [
    "PowerOn", "PowerOff", "Power",
    "Up", "Down", "Left", "Right", "Select", "Back", "Home", "Options",
    "VolumeUp", "VolumeDown", "VolumeMute",
    "InputHDMI1", "InputHDMI2", "InputHDMI3", "InputHDMI4", "InputAV1",
    "InputTuner", "Play", "Rev", "Fwd", "Info", "InstantReplay",
    "Search", "Enter", "FindRemote",
]


def discover(timeout=4.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    s.settimeout(timeout)
    s.sendto(MSEARCH, (MCAST, PORT))

    seen = {}
    while True:
        try:
            data, addr = s.recvfrom(2048)
        except socket.timeout:
            break
        text = data.decode("utf-8", "replace")
        loc = usn = st = ""
        for line in text.splitlines():
            low = line.lower()
            if low.startswith("location:"):
                loc = line.split(":", 1)[1].strip()
            elif low.startswith("usn:"):
                usn = line.split(":", 1)[1].strip()
            elif low.startswith("st:"):
                st = line.split(":", 1)[1].strip()
        if loc and loc not in seen:
            seen[loc] = (addr[0], st, usn)
    s.close()
    return seen


def get(url, timeout=4.0):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def post(url, timeout=4.0):
    req = urllib.request.Request(url, data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status


def tag(xml, name):
    o = xml.find(f"<{name}>")
    if o < 0:
        return ""
    o += len(name) + 2
    c = xml.find(f"</{name}>", o)
    return xml[o:c] if c > 0 else ""


def main():
    ap = argparse.ArgumentParser(description="Probe a Roku-ECP device.")
    ap.add_argument("--discover", action="store_true", help="SSDP M-SEARCH for roku:ecp")
    ap.add_argument("--host", help="host or IP of the bridge")
    ap.add_argument("--port", type=int, default=8060)
    ap.add_argument("--info", action="store_true", help="fetch and summarise device-info")
    ap.add_argument("--key", help="send one keypress")
    ap.add_argument("--keys", action="store_true", help="list the keys the bridge maps")
    ap.add_argument("--status", action="store_true", help="fetch /api/status from port 80")
    args = ap.parse_args()

    if args.keys:
        for k in KNOWN_KEYS:
            print(" ", k)
        return

    if args.discover:
        print(f"M-SEARCH ST: roku:ecp -> {MCAST}:{PORT}, waiting 4 s\n")
        found = discover()
        if not found:
            print("Nothing answered.\n")
            print("  - Is the bridge on this subnet? SSDP is multicast and does")
            print("    not cross a VLAN.")
            print("  - Is it on Wi-Fi at all? Check its console or LED.")
            print("  - Some corporate/guest networks block multicast entirely.")
            sys.exit(1)
        for loc, (ip, st, usn) in found.items():
            print(f"  {ip:16} ST={st:12} {usn}")
            print(f"  {'':16} LOCATION {loc}")
        print(f"\n{len(found)} device(s). A hub on this network should see the same.")
        return

    if not args.host:
        ap.error("--host is required (or use --discover)")

    base = f"http://{args.host}:{args.port}"

    if args.status:
        print(get(f"http://{args.host}/api/status"))
        return

    if args.key:
        try:
            code = post(f"{base}/keypress/{args.key}")
            print(f"POST {base}/keypress/{args.key} -> {code}")
            if args.key not in KNOWN_KEYS:
                print("  note: that key is not in the bridge's map — it will be")
                print("  logged as unmapped and do nothing. --keys lists the map.")
        except urllib.error.URLError as e:
            sys.exit(f"failed: {e}")
        return

    # default, and --info
    try:
        xml = get(f"{base}/query/device-info")
    except urllib.error.URLError as e:
        sys.exit(f"could not reach {base}: {e}")

    fields = ["friendly-device-name", "model-name", "vendor-name", "serial-number",
              "is-tv", "power-mode", "software-version", "wifi-mac"]
    print(f"{base}/query/device-info\n")
    for f in fields:
        print(f"  {f:22} {tag(xml, f)}")

    if tag(xml, "is-tv") != "true":
        print("\n  WARNING: is-tv is not true — a hub will offer the streaming-player")
        print("  layout, without discrete PowerOn/PowerOff/InputHDMI keys.")
    else:
        print("\n  is-tv is true, so a hub should offer the TV layout with discrete")
        print("  PowerOn / PowerOff / InputHDMI keys. Whether SofaBaton's Roku")
        print("  profile actually surfaces them is the open question.")


if __name__ == "__main__":
    main()
