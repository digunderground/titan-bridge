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
// Day 1 tells you which one the projector actually binds. Until then, leave
// this at BOTH: writing into a channel nothing is listening on costs nothing,
// and the target architecture in the plan (§5) uses both projector USB ports
// simultaneously anyway — native USB for HID, the CH340 dongle for serial.
// --------------------------------------------------------------------------
#define CH_NATIVE_CDC   0x01      // ESP32-S3 native USB -> projector USB port
#define CH_UART1        0x02      // ESP32 UART1 -> CH340/CP2102 -> projector USB

#define SERIAL_CHANNELS (CH_NATIVE_CDC | CH_UART1)

// --------------------------------------------------------------------------
// Pins
// --------------------------------------------------------------------------
#define P2_TX_PIN        17       // ESP32 TX1  -> adapter RXD
#define P2_RX_PIN        18       // ESP32 RX1  <- adapter TXD  (3V3! see docs/03)
#define IR_RX_PIN        15       // TSOP38238 OUT
#define IR_TX_PIN         4       // 940 nm LED anode -> 100R -> here
#define STATUS_LED_PIN   48       // DevKitC-1 onboard WS2812. -1 to disable.

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
// Liveness is inferred from the temperature query: a reply means awake,
// silence means asleep. Nothing else on this projector reports state.
// --------------------------------------------------------------------------
#define POLL_INTERVAL_MS      10000UL   // how often to ask for temperature
#define POLL_REPLY_TIMEOUT_MS  1500UL   // how long a reply may take
#define POLL_MISSES_TO_SLEEP       3    // consecutive misses before "asleep"

#define WAKE_ATTEMPTS              3    // 'wakeup' frames before giving up
#define WAKE_SETTLE_MS         4000UL   // wait after wake before re-testing
#define POWEROFF_CONFIRM_MS     900UL   // power key -> confirmation dialog -> OK
#define POWEROFF_SETTLE_MS     6000UL   // wait after off before re-testing

// Set to 1 once Day 1 Test 5 proves the USB ports die in standby. The bridge
// then stops pretending it can wake the projector and says so in /api/status,
// instead of silently failing.
#define USB_DEAD_IN_STANDBY        0

// --------------------------------------------------------------------------
// Macro timing (plan §6). The OSD animates; commands sent mid-transition get
// eaten. These are starting points — tune per macro, fastest reliable value.
// --------------------------------------------------------------------------
#define MACRO_STEP_MS            220    // default gap between macro steps
#define MACRO_ANCHOR_MS          400    // gap during the anchor sequence
#define MACRO_MAX_STEPS          128

// --------------------------------------------------------------------------
// Roku ECP emulation (plan §5, "The Roku trick")
// --------------------------------------------------------------------------
#define ECP_PORT                8060
#define UI_PORT                   80
#define ECP_ENABLE                 1
#define SSDP_NOTIFY_INTERVAL_MS 60000UL

// --------------------------------------------------------------------------
// IR receive. Set to 0 if Day 1 Test 6 found no receiver and you never wired
// a TSOP38238 — the pin then stays free.
// --------------------------------------------------------------------------
#define IR_RX_ENABLE               1
#define IR_REPEAT_GAP_MS         250    // ignore NEC repeats closer than this

// --------------------------------------------------------------------------
// Log ring buffer shown in the web UI and /api/log
// --------------------------------------------------------------------------
#define LOG_LINES                 60
#define LOG_LINE_MAX              96

// Firmware identity, reported in /api/status and the Roku device-info.
#define FW_VERSION "titan-bridge-p2 1.0.0"
