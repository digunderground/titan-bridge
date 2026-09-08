/* ===========================================================================
   config.h — every knob for titan_bridge_p2 in one place.

   Nothing in here needs editing to get a first boot: Wi-Fi is provisioned at
   runtime through the setup portal, and both serial channels are enabled by
   default. Change things here only when you know which channel won on Day 1.
   =========================================================================== */
#pragma once

// --------------------------------------------------------------------------
// Which serial channel(s) carry the 2A2A frames.
//
// Day 1 tells you which one the projector actually binds. Writing into a
// channel nothing is listening on costs nothing, and the target architecture
// in the plan (§5) uses both projector USB ports simultaneously anyway —
// native USB for HID, the CH340 dongle for serial.
//
// CH_UART0 is the "no dongle in the drawer" channel. Every dev board with an
// onboard CP2102/CH340 already *is* a USB-serial dongle: plug the board's own
// USB port into the projector and its daemon binds the bridge chip as
// /dev/ttyUSB0, exactly as it would an external module. The ESP32 then talks
// to the projector through its own bridge, over UART0.
//
// The cost is the console: UART0 is also the flashing and serial-monitor port,
// so while it is committed to the projector there is no laptop console. Use
// the web UI (http://titan-bridge.local/) instead — it carries the same log
// and command surface. See docs/09-onboard-bridge-path.md.
// --------------------------------------------------------------------------
#define CH_NATIVE_CDC   0x01      // ESP32-S3 native USB -> projector USB port
#define CH_UART1        0x02      // ESP32 UART1 -> CH340/CP2102 -> projector USB
#define CH_UART0        0x04      // ESP32 UART0 -> onboard bridge -> projector USB

// --------------------------------------------------------------------------
// Board profile. Pick the board you are actually holding; each one sets its
// own pins and its own default channel set. Override any of them below.
// --------------------------------------------------------------------------
#define BOARD_S3_DEVKITC    1     // ESP32-S3-DevKitC-1, dual USB-C  (target)
#define BOARD_HELTEC_LORA   2     // Heltec WiFi LoRa 32 V3/V4 (ESP32-S3 + CP2102)
#define BOARD_ESP32_WROOM   3     // classic ESP32 devkit (WROOM-32 + CP2102/CH340)

#ifndef BOARD
#define BOARD BOARD_S3_DEVKITC
#endif

#if   BOARD == BOARD_S3_DEVKITC
  // Two USB-C ports is the whole point: native USB to the projector, UART to
  // the laptop, so the console survives and reflashing costs no unplugging.
  #define HAS_NATIVE_USB      1
  #define P2_TX_PIN          17     // ESP32 TX1  -> adapter RXD
  #define P2_RX_PIN          18     // ESP32 RX1  <- adapter TXD  (3V3! see docs/03)
  #define IR_RX_PIN          15     // TSOP38238 OUT
  #define IR_TX_PIN           4     // 940 nm LED anode -> 100R -> here
  #define STATUS_LED_PIN     48     // onboard WS2812. -1 to disable.
  #define STATUS_LED_IS_RGB   1
  #ifndef SERIAL_CHANNELS_WANTED
  #define SERIAL_CHANNELS_WANTED (CH_NATIVE_CDC | CH_UART1)
  #endif

#elif BOARD == BOARD_HELTEC_LORA
  // ESP32-S3, so native USB exists in the silicon — but the USB-C connector is
  // wired to the CP2102, not to the S3's D+/D-. Reaching the native port means
  // soldering a USB lead to GPIO19/20, so the onboard bridge is the easy path.
  //
  // Pins are tight on this board: SX1262 owns GPIO8-14, and the OLED owns
  // GPIO17/18 — which are exactly the DevKitC's UART1 pins, so the stock
  // mapping would fight the display. UART1 is moved to free pins here.
  #define HAS_NATIVE_USB      0     // not on the connector; 1 only if you solder
  #define P2_TX_PIN           2     // free header pin
  #define P2_RX_PIN           3     // free header pin
  #define IR_RX_PIN           7     // free header pin
  #define IR_TX_PIN           6     // free header pin
  #define STATUS_LED_PIN     35     // plain white LED, NOT a WS2812
  #define STATUS_LED_IS_RGB   0
  #ifndef SERIAL_CHANNELS_WANTED
  #define SERIAL_CHANNELS_WANTED (CH_UART0)
  #endif

#elif BOARD == BOARD_ESP32_WROOM
  // Classic ESP32: no USB peripheral in the silicon at all, so native CDC and
  // the HID keyboard channel do not exist. The onboard bridge is the only way
  // to reach the projector, and it is enough for every serial test.
  #define HAS_NATIVE_USB      0
  #define P2_TX_PIN          17     // free on WROOM-32; on WROVER these two are
  #define P2_RX_PIN          18     // eaten by PSRAM — move them if you see one
  #define IR_RX_PIN          15
  #define IR_TX_PIN           4
  #define STATUS_LED_PIN      2     // the usual blue LED
  #define STATUS_LED_IS_RGB   0
  #ifndef SERIAL_CHANNELS_WANTED
  #define SERIAL_CHANNELS_WANTED (CH_UART0)
  #endif

#else
  #error "Unknown BOARD — see the board profiles above"
#endif

// A board without native USB cannot present a CDC endpoint or an HID keyboard,
// whatever the channel mask asks for — so the native bit is masked out rather
// than failing to compile. UART1 survives the mask: a classic ESP32 driving an
// external CH340 is a perfectly good Phase 2 rig.
#if HAS_NATIVE_USB
  #define HAS_HID           1
  #define SERIAL_CHANNELS  (SERIAL_CHANNELS_WANTED)
#else
  #define HAS_HID           0
  #define SERIAL_CHANNELS  (SERIAL_CHANNELS_WANTED & ~CH_NATIVE_CDC)
#endif

#if SERIAL_CHANNELS == 0
  #error "No usable serial channel: this board has no native USB, so pick CH_UART0 or CH_UART1"
#endif

// UART0 carries the projector when that channel is on, so the console cannot
// also live there. Everything moves to the web UI and the log ring.
#if (SERIAL_CHANNELS & CH_UART0)
  #define CONSOLE_ON_SERIAL 0
#else
  #define CONSOLE_ON_SERIAL 1
#endif

#define LINK_BAUD    115200

// --------------------------------------------------------------------------
// Identity
// --------------------------------------------------------------------------
#define DEVICE_NAME      "Titan Bridge"
#define MDNS_HOST        "titan-bridge"        // -> titan-bridge.local
#define SETUP_AP_PREFIX  "TitanBridge-"        // + last 4 of MAC
#define SETUP_AP_PASS    "titanbridge"         // >= 8 chars

// --------------------------------------------------------------------------
// Power state machine (plan §7 — "make both idempotent")
//
// The plan assumed liveness could be inferred from the temperature query — a
// reply means awake, silence means asleep. **Measured 2026-09-05: false on the
// TITAN Noir Max.** With the projector switched off, temperature replies keep
// arriving at exactly the same rate, reporting "normal". The serial daemon
// runs in standby, so the probe says nothing about power.
//
// Set to 1 only if a projector is shown to stop answering in standby. Left at
// 0, the probe is still used for link liveness — it just no longer pretends to
// report power, which produced a confident and wrong "awake".
// --------------------------------------------------------------------------
#define TEMP_PROBE_INDICATES_POWER 0

#define POLL_INTERVAL_MS      10000UL   // how often to ask for temperature
#define POLL_REPLY_TIMEOUT_MS  1500UL   // how long a reply may take
#define POLL_MISSES_TO_SLEEP       3    // consecutive misses before "asleep"

#define WAKE_ATTEMPTS              3    // 'wakeup' frames before giving up
#define WAKE_SETTLE_MS         4000UL   // wait after wake before re-testing
// The power key raises a 15 s power-off COUNTDOWN, and that countdown
// completes into a shutdown by itself — OK only short-circuits it. Measured
// 2026-09-06. We wait it out rather than racing to confirm; see titan.cpp.
#define POWEROFF_COUNTDOWN_MS 16000UL   // 15 s dialog + margin

// Frames ARE lost on this link. Measured 2026-09-06: a power key went out and
// the projector neither acknowledged it nor acted on it, while the setting,
// back and hdmi1 frames on either side of it were acknowledged in ~30 ms. An
// identical retransmit worked immediately.
//
// Because the projector did not act, the loss was outbound, so a retransmit
// cannot double-execute the command. ACKs arrive in 15-30 ms, so 250 ms is
// ~10x the observed latency: a silence that long means the frame is gone, not
// slow.
// Restored 2026-09-07. The original power -> 900 ms -> OK sequence was reported
// working ("works perfectly") on day one. It was replaced with a countdown-only
// wait, then a back-key nudge, on theories that were never measured against
// that baseline — and power-off went to failing 10/10. Change one thing at a
// time, against a known-good.
// The projector refuses the power key while sitting on a no-signal input
// screen — from the OEM remote as well as from serial, so it is a projector
// state, not a delivery problem. Opening the on-screen menu and closing it
// again makes the very same power frame work. Measured 2026-09-07.
#define POWEROFF_OSD_MS          700UL   // gap between the OSD wake-up presses
// Power OFF sends the power key TWICE. The first one after a power-on is
// swallowed by the projector — proven 2026-09-07 with two byte-identical
// acknowledged frames 91 s apart, the first ignored, the second raising the
// countdown. The original firmware did this by accident (its verify-retry loop
// always fired) which is why it "worked perfectly"; removing that retry is what
// broke power off.
//
// This is the ONE deliberate exception to the one-command-per-power-action
// rule, agreed with the operator. Still only power keys — no OK, no nudge, no
// prelude. Do not add anything else here.
#define POWEROFF_REPEAT_MS      2000UL   // gap between the two power keys
#define POWEROFF_ACK_WAIT_MS     400UL   // wait for the power key's ACK before trusting it
#define POWEROFF_CYCLES             2    // full power+OK cycles, as the original did
// Measured 2026-09-07 from a power-off that actually worked: the operator's OK
// landed 2.4 s after the power key. 900 ms was a guess and sat right on the
// edge of the dialog appearing, which is why power-off was intermittent for
// days. The dialog lives ~15 s, so there is no reason to be quick about it.
#define POWEROFF_CONFIRM_MS      900UL   // power key -> confirmation dialog -> OK (original)
#define POWEROFF_SETTLE_MS      6000UL   // after OK, before assuming it took

// Do NOT poll a projector believed to be off.
//
// 2026-09-07: the projector was off, the bridge sent no commands at all for
// four minutes, and it switched itself on. The ONLY thing on the wire was this
// 10 s probe — and it is excluded from the log, which is why every reading of
// "nothing was sent" was wrong.
//
// The hub re-asserting PowerOn (R-0001) was a real and separate cause, now
// fixed; it was not the only one. This gate was briefly reverted because it
// coupled to hidPowerPath() via everFrame and broke power routing. That
// coupling is now gone, so the gate can stand on its own.
//
// It costs nothing: the projector answers the probe identically in standby
// (TEMP_PROBE_INDICATES_POWER 0), so polling a sleeping projector buys no
// information whatsoever.
#define POLL_WHEN_BELIEVED_OFF     0
#define ACK_TIMEOUT_MS           250UL   // no ACK by now => assume the frame was lost
#define ACK_RETRIES                 2    // retransmits before giving up

// The HID power key is a toggle, so two presses in quick succession would undo
// each other. One intent, one toggle.
#define POWER_DEBOUNCE_MS      4000UL   // wait after off before re-testing

// Set to 1 once Day 1 Test 5 proves the USB ports die in standby. The bridge
// then stops pretending it can wake the projector and says so in /api/status,
// instead of silently failing.
//
// 2026-09-04, measured: the TITAN Noir Max **keeps its USB ports powered in
// standby**. The bridge stayed up and on Wi-Fi, powered solely by the
// projector's USB, with the projector off — uptime climbing straight through.
// So this stays 0: the port is alive and a wake path is at least possible.
//
// (An earlier impression that the ports died was wrong. Distinguish standby,
// where they stay live, from mains-off, where obviously nothing survives.)
//
// Whether anything we can *send* actually wakes it is a separate question:
// the serial channel is dead on this model (see logs/TEST-LOG.md, Test 2), so
// the remaining candidate is an HID keypress on the native USB port.
#define USB_DEAD_IN_STANDBY        0

// --------------------------------------------------------------------------
// Key channel — which transport carries menu navigation.
//
// The plan assumed serial: instruction 0x07 exposes Settings, Home, Back, OK
// and the four arrows, so the bridge can drive the OSD without a keyboard.
// On a projector where the serial daemon never binds (Test 2 on the Noir Max),
// that channel does not exist and the same navigation has to go out over USB
// HID instead — which a plain USB keyboard proves the projector accepts.
//
// Which one works is a property of the projector, not of the build, so this is
// only the startup default; `keychan` / /api/keychan changes it at runtime and
// the choice is remembered across reboots.
// --------------------------------------------------------------------------
#define KEY_CHANNEL_SERIAL  0
#define KEY_CHANNEL_HID     1

#ifndef DEFAULT_KEY_CHANNEL
#define DEFAULT_KEY_CHANNEL KEY_CHANNEL_SERIAL
#endif

// --------------------------------------------------------------------------
// Macro timing (plan §6). The OSD animates; commands sent mid-transition get
// eaten. These are starting points — tune per macro, fastest reliable value.
// --------------------------------------------------------------------------
// NVS caps a single string near 4 kB. Two stores keep macros and button
// assignments from competing for the same budget.
#define MACRO_STORE_MAX         3800
#define BUTTON_STORE_MAX        2000
#define ECP_MAP_MAX             1500

#define MACRO_STEP_MS            220    // default gap between macro steps
#define MACRO_ANCHOR_MS          400    // gap during the anchor sequence
#define MACRO_MAX_STEPS          128

// Recording captures the operator's timing, which is far slower than the OSD
// needs — a recorded macro played back the pauses you took while thinking.
// Gaps are clamped into this range when a recording is saved.
#define RECORD_MIN_GAP_MS        120
#define RECORD_MAX_GAP_MS        600

// --------------------------------------------------------------------------
// Roku ECP emulation (plan §5, "The Roku trick")
// --------------------------------------------------------------------------
#define ECP_PORT                8060
#define UI_PORT                   80
#define ECP_ENABLE                 1
// Announce often. Measured on one network: the bridge's outbound multicast is
// heard reliably while inbound M-SEARCH from some hosts never arrives at all —
// so discovery cannot depend on hearing the search. A client that listens for
// ssdp:alive finds us regardless, and 10 s of a ~200 byte datagram is nothing.
#define SSDP_NOTIFY_INTERVAL_MS 10000UL

// Announcing every 10 s forever is the second of only two things this bridge
// emits unprompted. A hub needs the announcements while it is SCANNING and not
// afterwards, so they now run for a window — after boot, or when "Announce" is
// pressed in Settings — and then stop. M-SEARCH is still answered at any time,
// which costs nothing because it only happens when someone asks.
#define SSDP_WINDOW_MS       180000UL   // 3 min of announcing, then silence

// How long after boot a power command arriving over ECP is treated as the hub
// re-asserting state rather than a person pressing a button. See ecpKey().
#define ECP_POWER_GRACE_MS     45000UL

// --------------------------------------------------------------------------
// IR receive. Set to 0 if Day 1 Test 6 found no receiver and you never wired
// a TSOP38238 — the pin then stays free.
// --------------------------------------------------------------------------
#define IR_RX_ENABLE               1
#define IR_REPEAT_GAP_MS         250    // ignore NEC repeats closer than this

// --------------------------------------------------------------------------
// Log ring buffer shown in the web UI and /api/log
// --------------------------------------------------------------------------
// The ring has to outlive a diagnostic session. At 60 lines the routine
// temperature poll alone flushed it every ~2.5 minutes, which made "nothing
// from the hub" indistinguishable from "the evidence scrolled away" — and it
// was read as the former more than once.
#define LOG_LINES                160

// Log the 10 s liveness poll. Off by default: it is the single noisiest thing
// the bridge does and says nothing unless the answer changes, which is logged
// regardless.
#define LOG_POLL_TRAFFIC           0
#define LOG_LINE_MAX              96

// Persistent event log. The in-RAM ring above is lost on every reboot — which
// is exactly how the evidence for an overnight power-loss event disappeared on
// 2026-09-06/07. This one lives in NVS and survives both reboots and power
// cuts. Only significant events go in it (boot with its reset reason, power
// commands, lost frames), so NVS wear stays negligible.
#define EVLOG_MAX               1200

// Firmware identity, reported in /api/status and the Roku device-info.
// Keep this in step with /VERSION and the git tag — the app compares it against
// the latest GitHub release to tell you whether the unit is current.
#define FW_VERSION "0.8.4-alpha"
#define FW_REPO    "digunderground/titan-bridge"
