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
#define POWEROFF_CONFIRM_MS     900UL   // power key -> confirmation dialog -> OK
#define POWEROFF_SETTLE_MS     6000UL

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

// Firmware identity, reported in /api/status and the Roku device-info.
#define FW_VERSION "titan-bridge-p2 1.0.0"
