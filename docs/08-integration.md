# Integration — SofaBaton, Home Assistant, and the REST API

Every transport ends up calling the same macro engine. A button on the web
page, a Roku keypress from the hub, an IR code and a Home Assistant service
call all take the same path, so they behave identically and there is one place
to fix things.

---

## SofaBaton X2 — the Roku trick

The X2's Wi-Fi device category is not a generic "POST to any URL" builder. It
supports Roku, Sonos and Philips Hue, plus a Home Assistant remote over MQTT.
So the bridge answers to `ST: roku:ecp` on SSDP and serves the Roku External
Control Protocol on port 8060.

The app then discovers it as a Roku and hands you a native button layout, with
each press arriving as a direct HTTP POST. Roku **TV** devices expose
**PowerOn and PowerOff as discrete keys**, plus VolumeUp/Down/Mute and
InputHDMI1–4 — which maps onto this projector almost too neatly.

### Adding it

1. Bridge and hub on the **same subnet**, both on **2.4 GHz**. SSDP is
   multicast and will not cross a VLAN.
2. Hub app → add device → Wi-Fi → Roku → scan.
3. It should appear as **Titan Bridge**. If it does not, run
   `python3 tools/ecp_probe.py --discover` from a laptop on the same network:
   that tells you whether the problem is the bridge or the hub.

### Key map

| ECP key | Action | Firmware |
|---|---|---|
| PowerOn / PowerOff / Power | verified power, idempotent | `p:on` `p:off` `p:toggle` |
| Up / Down / Left / Right | navigation | `s:up` … |
| Select / Back / Home | OK / Back / Home | `s:ok` `s:back` `s:home` |
| Options | Settings | `s:setting` |
| VolumeUp / VolumeDown / VolumeMute | volume | `s:volup` `s:voldn` `s:mute` |
| InputHDMI1 / 2 / 3 | source select | `s:hdmi1` … |
| InputHDMI4 / InputAV1 | USB source | `s:usbsrc` |
| Play / Rev / Fwd | Filmmaker / Movie / Vivid | picture presets |
| Info / InstantReplay | brightness step up / down | open loop |
| Search | macro `tpl3d` | your 3D macro |
| Enter | macro `tpllens` | your lens-memory macro |
| FindRemote | autofocus | |

Edit `ECPMAP[]` at the top of `firmware/titan_bridge_p2/ecp.cpp`. Actions are
macro scripts, so `m:<name>` fires any saved macro and anything you can write
in a macro you can bind to a remote button.

**Untested until you try it:** whether SofaBaton's Roku profile surfaces the
TV-only keys. The bridge presents `<is-tv>true</is-tv>` in device-info
specifically to find out. If the app offers only the streaming-player layout,
the discrete power keys will not appear — in which case bind Play/Rev/Fwd to
power actions instead and accept the loss of discrete on/off from that remote.

### Gotchas

- Devices are addressed by IP. **Pin the DHCP reservation.**
- The hub needs 2.4 GHz.
- One owner found the hub's firmware updater choked on a VPN'd IoT network.

---

## Home Assistant

No MQTT, no custom component, no dependencies. Copy
`homeassistant/titan_noir.yaml` into your `packages/` directory (or paste its
contents into `configuration.yaml`), set the host, and restart.

It gives you:

- a **switch** for power, with state read back from the liveness probe
- **sensors** for power state, temperature status and last-reply age
- **buttons** for every source, picture mode and brightness preset
- a **select** for input and one for picture mode
- **script** entries for each macro
- a template **binary_sensor** that goes on when the projector reports a
  high-temperature warning — the one thing here worth an actual alert

`homeassistant/lovelace-card.yaml` is a ready-made dashboard card.

---

## REST API

Everything on port 80. All endpoints accept GET or POST and return
`text/plain` `ok` unless stated otherwise. CORS is open, so a browser console
or a dashboard on another host can drive it directly.

| Endpoint | Does |
|---|---|
| `GET /` | the web console |
| `GET /api/status` | JSON: power, temperature, counters, Wi-Fi, uptime, heap |
| `GET /api/log` | the last 60 log lines, plain text |
| `/api/cmd?name=hdmi1` | send a named projector command |
| `/api/key?name=PowerOn` | send an ECP key by name (same map as the hub) |
| `/api/hid?key=menu` | send a USB HID key — `menu` has no serial equivalent |
| `/api/raw?hex=2A2A020101%2004` | raw bytes; the checksum is **not** recalculated |
| `/api/power?state=on\|off\|toggle` | verified, idempotent power |
| `/api/macro?name=movie` | run a saved macro |
| `/api/macro?script=anchor;%20s:down*3` | run an inline script |
| `/api/macabort` | stop the running macro |
| `GET /api/macros` | JSON list of macros with their scripts |
| `/api/macdef?name=x&script=...` | save a macro to NVS |
| `/api/macdel?name=x` | delete one |
| `GET /api/irmaps` | JSON list of IR bindings |
| `/api/irmap?code=21DE:4D&action=m:movie` | bind an IR code |
| `/api/irdel?code=21DE:4D` | unbind |
| `/api/wifi?ssid=X&pass=Y` | save credentials and reboot |
| `/api/forget` | clear credentials, reboot into setup mode |
| `/api/reboot` | reboot |

### `/api/status`

```json
{
  "power": "awake",
  "temp": "normal",
  "busy": "",
  "macro": "",
  "tx": 412, "rx": 2884, "since": 1204,
  "cdc": true,
  "brightness": 7,
  "lastrx": "2A 2A 03 13 01 00 17 ",
  "irlast": "21DE:4D",
  "link": "native-CDC UART1",
  "ssid": "house", "ip": "192.168.1.42", "ap": false,
  "fw": "titan-bridge-p2 1.0.0",
  "heap": 198340, "uptime": 84213
}
```

`power` is `awake`, `asleep` or `unknown`. It is inferred from the temperature
probe — a reply means awake, three consecutive silences mean asleep. `since` is
milliseconds since the last byte arrived, or `-1` if nothing ever has.

`cdc: true` means the projector's serial daemon has the native USB port open.
That is the Day 1 Test 2 answer, available permanently.

### Roku ECP (port 8060)

| Endpoint | |
|---|---|
| `GET /` | UPnP root description |
| `GET /query/device-info` | Roku device-info XML, `is-tv=true` |
| `GET /query/apps` | the four inputs, as "apps" |
| `POST /keypress/<Key>` | the key map above |
| `POST /launch/<1..4>` | select input 1–4 |

SSDP answers `M-SEARCH` for `roku:ecp`, `ssdp:all` and `upnp:rootdevice`, and
announces itself every 60 seconds.

---

## curl examples

```bash
H=titan-bridge.local

curl -s $H/api/status | jq
curl -sX POST "$H/api/power?state=on"
curl -sX POST "$H/api/cmd?name=hdmi1"
curl -sX POST "$H/api/macro?name=movie"
curl -sX POST --get --data-urlencode "script=anchor; d500; s:down*3; s:ok" \
     "$H/api/macro"
curl -s $H/api/log
```

And the Roku side, exactly as the hub would:

```bash
curl -sX POST http://$H:8060/keypress/PowerOn
curl -s     http://$H:8060/query/device-info
```
