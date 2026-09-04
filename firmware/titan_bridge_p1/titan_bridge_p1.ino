/* ============================================================================
   titan_bridge_p1.ino  —  XGIMI TITAN Noir (Max/Pro) control bridge, Phase 1
   ============================================================================
   DIAGNOSTIC BUILD.  Its only job is to answer Tests 0-6 in the build plan
   (plan/titan_noir_bridge_plan.md §3) in one evening.  The production
   firmware is firmware/titan_bridge_p2.

   WHAT THIS DOES
   Turns an ESP32-S3 into three things at once so you can find out which
   control channels the projector actually accepts:

     1. USB CDC-ACM serial device  -> speaks XGIMI's RS232 command set over the
                                      projector's own USB port, no dongle.
     2. USB HID keyboard           -> the nav keys XGIMI documents as a remote
                                      workaround (arrows/Enter/Esc/Home/etc).
                                      This is your enumeration canary: if the
                                      menu moves, USB works and only the serial
                                      driver binding is in question.
     3. NEC IR transmitter         -> sweeps the TITAN's published IR codeset
                                      (header 0x21DE) to settle once and for all
                                      whether the Noir has a hidden receiver.

   It also watches USB bus events (attach / suspend / resume / detach) and the
   CDC line state, printed with millisecond timestamps.  That log IS the answer
   to Test 5 — whether the projector's USB ports stay alive in standby.

   BOARD / IDE SETTINGS  (all three matter)
     Board:            "ESP32S3 Dev Module"
     USB Mode:         "USB-OTG (TinyUSB)"      <-- required for HID
     USB CDC On Boot:  "Disabled"               <-- so Serial = UART0 console
     Upload Mode:      "UART0 / Hardware CDC"
     Core:             arduino-esp32 2.x or 3.x (both handled below)
     No external libraries are required.

   CABLING (Phase 1)
     ESP32-S3-DevKitC-1 "UART" USB-C port  -> laptop   (flashing + this console)
     ESP32-S3-DevKitC-1 "USB"  USB-C port  -> projector USB-A port
     IR LED anode -> 100R -> GPIO4,  cathode -> GND    (optional but do it)

   BEFORE YOU START
     On the projector: Settings > General > Serial Port Control -> ON.
     Note the firmware version (Settings > system information) in the test log.

   CONSOLE
     Serial monitor on the UART port, 115200, line ending = Newline.
     Type "?" for the command list.  Suggested order: "k down", then "temp".

   PHASE 2
     If CDC never binds, set USE_PHASE2_UART to 1, put a CH340/CP2102 USB-TTL
     adapter in the projector's USB port, and wire its TX/RX/GND to the ESP32.
     Everything else in this sketch works unchanged.
   ========================================================================= */

#include <strings.h>
#include "USB.h"
#include "USBCDC.h"
#include "USBHIDKeyboard.h"

// ------------------------------ configuration ------------------------------
#define USE_PHASE2_UART   0      // 0 = native USB CDC, 1 = UART1 -> USB-TTL dongle
#define P2_TX_PIN        17      // ESP32 TX1 -> adapter RXD   (Phase 2 only)
#define P2_RX_PIN        18      // ESP32 RX1 <- adapter TXD   (Phase 2 only)
#define IR_LED_PIN        4
#define IR_CARRIER_HZ 38000

USBCDC         PJ;              // the CDC-ACM endpoint the projector should see
USBHIDKeyboard KB;

#if USE_PHASE2_UART
  #define LINK Serial1
#else
  #define LINK PJ
#endif

// LEDC API differs between core 2.x and 3.x — handle both.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #define IR_ATTACH()  ledcAttach(IR_LED_PIN, IR_CARRIER_HZ, 8)
  #define IR_ON()      ledcWrite(IR_LED_PIN, 85)     // ~33% duty
  #define IR_OFF()     ledcWrite(IR_LED_PIN, 0)
#else
  #define IR_CH 0
  #define IR_ATTACH()  do { ledcSetup(IR_CH, IR_CARRIER_HZ, 8); \
                            ledcAttachPin(IR_LED_PIN, IR_CH); } while (0)
  #define IR_ON()      ledcWrite(IR_CH, 85)
  #define IR_OFF()     ledcWrite(IR_CH, 0)
#endif

// ------------------------------- timestamps --------------------------------
// Every console line is stamped.  Copy-paste the whole session into
// logs/TEST-LOG.md and you have a dated record of what the projector did.

static void stamp() {
  uint32_t ms = millis();
  Serial.printf("[%3lu.%03lu] ", (unsigned long)(ms / 1000),
                                 (unsigned long)(ms % 1000));
}

// ---------------------------------------------------------------------------
// XGIMI serial frame:  2A 2A <len> <instr> <params...> <checksum>
//   len      = 1 (instruction) + number of parameter bytes
//   checksum = (len + instr + all params) & 0xFF
// Verified byte-for-byte against XGIMI's published command-set image
// (helpcenter article 53406154382361, attachment 53406327353497).
// ---------------------------------------------------------------------------

struct Cmd {
  const char *name;
  uint8_t     instr;
  uint8_t     plen;
  uint8_t     p[6];
};

static const Cmd CMDS[] = {
  // ---- source select (instr 0x01) ----
  { "hdmi1",     0x01, 1, {0x01} },
  { "hdmi2",     0x01, 1, {0x02} },
  { "hdmi3",     0x01, 1, {0x03} },   // UNDOCUMENTED: doc lists only 2 HDMI + USB
  { "usbsrc",    0x01, 1, {0x14} },

  // ---- picture mode (instr 0x03) ----
  { "vivid",     0x03, 1, {0x00} },
  { "movie",     0x03, 1, {0x01} },
  { "imax",      0x03, 1, {0x02} },
  { "perf",      0x03, 1, {0x05} },
  { "tvmode",    0x03, 1, {0x07} },
  { "sport",     0x03, 1, {0x09} },
  { "filmmaker", 0x03, 1, {0x1B} },

  // ---- brightness 1..10 (instr 0x05) ----
  { "b1",        0x05, 1, {0x01} },
  { "b2",        0x05, 1, {0x02} },
  { "b3",        0x05, 1, {0x03} },
  { "b4",        0x05, 1, {0x04} },
  { "b5",        0x05, 1, {0x05} },
  { "b6",        0x05, 1, {0x06} },
  { "b7",        0x05, 1, {0x07} },
  { "b8",        0x05, 1, {0x08} },
  { "b9",        0x05, 1, {0x09} },
  { "b10",       0x05, 1, {0x0A} },

  // ---- simulated key presses (instr 0x07) ----
  { "power",     0x07, 1, {0x00} },
  { "source",    0x07, 1, {0x08} },
  { "up",        0x07, 1, {0x09} },
  { "down",      0x07, 1, {0x0A} },
  { "right",     0x07, 1, {0x0B} },
  { "left",      0x07, 1, {0x0C} },
  { "ok",        0x07, 1, {0x0D} },
  { "back",      0x07, 1, {0x0E} },
  { "setting",   0x07, 1, {0x0F} },
  { "home",      0x07, 1, {0x10} },
  { "volup",     0x07, 1, {0x11} },
  { "voldn",     0x07, 1, {0x12} },
  { "autofocus", 0x07, 1, {0x13} },
  { "manfocus",  0x07, 1, {0x14} },
  { "mute",      0x07, 1, {0x15} },

  // ---- standby wake (instr 0x09, ASCII "wakeup") ----
  { "wake",      0x09, 6, {0x77, 0x61, 0x6B, 0x65, 0x75, 0x70} },

  // ---- screen blank (instr 0x0D) ----
  { "blank",     0x0D, 1, {0x00} },   // "turn off the screen"
  { "unblank",   0x0D, 1, {0x01} },

  // ---- high refresh rate mode (instr 0x0E) ----
  { "hrroff",    0x0E, 1, {0x00} },
  { "hrrbasic",  0x0E, 1, {0x01} },
  // XGIMI's table prints 0x01 for "extreme speed" but gives checksum 0x12,
  // which only works if the parameter is 0x02. Going with the checksum.
  { "hrrmax",    0x0E, 1, {0x02} },

  // ---- status (instr 0x13) ----
  { "temp",      0x13, 1, {0x00} },
};
static const size_t NCMDS = sizeof(CMDS) / sizeof(CMDS[0]);

// Deliberately omitted from the table: instruction 0x06, factory reset.
//   2A2A 02 06 00 08   erases apps too
//   2A2A 02 06 01 09   keeps apps
// Send either by hand with "raw" if you ever actually want it.

// ---------------------------------------------------------------------------
// TITAN published IR codeset — NEC, header/address 0x21DE.
// ---------------------------------------------------------------------------
struct IrKey { const char *name; uint8_t code; };
static const IrKey IRKEYS[] = {
  { "power",     0x4D }, { "home",      0x1A }, { "menu",      0x45 },
  { "back",      0x42 }, { "source",    0x4F }, { "up",        0x0B },
  { "down",      0x0E }, { "left",      0x10 }, { "right",     0x11 },
  { "ok",        0x0D }, { "setting",   0x48 }, { "setting2",  0x12 },
  { "osd",       0x19 }, { "volup",     0x18 }, { "voldn",     0x17 },
  { "focusplus", 0x1C }, { "focusminus",0x1D }, { "menurot",   0x1E },
  { "keystone",  0x1F }, { "lensshift", 0x20 }, { "hdmi1",     0x24 },
  { "hdmi2",     0x25 }, { "usb",       0x26 }, { "picstd",    0x41 },
  { "picmovie",  0x44 }, { "picvivid",  0x4A }, { "pictv",     0x4B },
};
static const size_t NIRKEYS = sizeof(IRKEYS) / sizeof(IRKEYS[0]);

// ============================ serial link ==================================

static uint32_t txCount = 0, rxCount = 0;
static uint32_t lastRxMs = 0;

static void sendCmd(uint8_t instr, const uint8_t *params, uint8_t nparams) {
  uint8_t buf[16];
  uint8_t len = 1 + nparams;
  uint8_t i   = 0;
  buf[i++] = 0x2A;
  buf[i++] = 0x2A;
  buf[i++] = len;
  buf[i++] = instr;
  for (uint8_t k = 0; k < nparams; k++) buf[i++] = params[k];

  uint16_t sum = len + instr;
  for (uint8_t k = 0; k < nparams; k++) sum += params[k];
  buf[i++] = (uint8_t)(sum & 0xFF);

  LINK.write(buf, i);
  LINK.flush();
  txCount++;

  stamp();
  Serial.print("TX -> ");
  for (uint8_t k = 0; k < i; k++) {
    if (buf[k] < 0x10) Serial.print('0');
    Serial.print(buf[k], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

static bool sendNamed(const char *name) {
  for (size_t i = 0; i < NCMDS; i++) {
    if (!strcasecmp(name, CMDS[i].name)) {
      sendCmd(CMDS[i].instr, CMDS[i].p, CMDS[i].plen);
      return true;
    }
  }
  return false;
}

// Decode anything the projector pushes back at us.
static uint8_t  rxBuf[64];
static uint8_t  rxLen = 0;
static uint32_t rxLast = 0;

static void describeFrame(const uint8_t *f, uint8_t n) {
  if (n >= 6 && f[0] == 0x2A && f[1] == 0x2A && f[3] == 0x13) {
    Serial.print("           temperature status: ");
    switch (f[5]) {
      case 0x00: Serial.println("normal"); break;
      case 0x01: Serial.println("HIGH TEMPERATURE WARNING"); break;
      case 0x02: Serial.println("HIGH TEMPERATURE SHUTDOWN WARNING"); break;
      case 0x03: Serial.println("LOW TEMPERATURE SHUTDOWN WARNING"); break;
      case 0x04: Serial.println("SENSOR ABNORMAL (open/short circuit)"); break;
      default:   Serial.println("unknown code"); break;
    }
  }
}

static void pumpLink() {
  while (LINK.available()) {
    uint8_t b = LINK.read();
    if (rxLen < sizeof(rxBuf)) rxBuf[rxLen++] = b;
    rxLast = millis();
    rxCount++;
    lastRxMs = rxLast;
  }
  // 30 ms of silence = end of frame
  if (rxLen && (millis() - rxLast > 30)) {
    stamp();
    Serial.print("RX <- ");
    for (uint8_t i = 0; i < rxLen; i++) {
      if (rxBuf[i] < 0x10) Serial.print('0');
      Serial.print(rxBuf[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
    describeFrame(rxBuf, rxLen);
    rxLen = 0;
  }
}

// ============================== HID keyboard ===============================
// Raw HID usage IDs — avoids depending on which KEY_* constants your core has.

struct HidKey { const char *name; uint8_t usage; };
static const HidKey HIDKEYS[] = {
  { "up",      0x52 }, { "down",    0x51 }, { "left",    0x50 },
  { "right",   0x4F }, { "ok",      0x28 },  // Enter
  { "back",    0x29 },                        // Esc
  { "home",    0x4A },                        // Home -> opens settings
  { "volup",   0x4B },                        // PageUp
  { "voldn",   0x4E },                        // PageDown
  { "focus+",  0x57 },                        // Keypad +
  { "focus-",  0x56 },                        // Keypad -
  { "menu",    0x65 },                        // Application/Menu key
};
static const size_t NHIDKEYS = sizeof(HIDKEYS) / sizeof(HIDKEYS[0]);

static bool sendHid(const char *name) {
  for (size_t i = 0; i < NHIDKEYS; i++) {
    if (!strcasecmp(name, HIDKEYS[i].name)) {
      KB.pressRaw(HIDKEYS[i].usage);
      delay(30);
      KB.releaseRaw(HIDKEYS[i].usage);
      stamp();
      Serial.printf("HID -> %s (usage 0x%02X)\n", HIDKEYS[i].name,
                    HIDKEYS[i].usage);
      return true;
    }
  }
  return false;
}

// ================================== IR =====================================
// Absolute-deadline timing.  Interrupts stay enabled: instead of trying to
// hold off the scheduler for a whole 68 ms NEC frame (which upsets the
// interrupt watchdog and, in phase 2, Wi-Fi), each edge is scheduled against
// a running total so jitter never accumulates.

static uint32_t irT0, irAcc;

static inline void irEdge(bool on, uint32_t us) {
  if (on) IR_ON(); else IR_OFF();
  irAcc += us;
  while ((int32_t)(micros() - (irT0 + irAcc)) < 0) { /* spin */ }
}

static void irByte(uint8_t b) {                 // NEC is LSB-first
  for (uint8_t i = 0; i < 8; i++) {
    irEdge(true, 560);
    irEdge(false, (b & 0x01) ? 1690 : 560);
    b >>= 1;
  }
}

static void irSendNec(uint16_t addr, uint8_t cmd) {
  irT0 = micros(); irAcc = 0;
  irEdge(true, 9000);
  irEdge(false, 4500);
  irByte(addr & 0xFF);          // extended NEC: low address byte first
  irByte(addr >> 8);
  irByte(cmd);
  irByte((uint8_t)~cmd);
  irEdge(true, 560);
  IR_OFF();

  // two repeat frames, as a real remote would send
  for (uint8_t r = 0; r < 2; r++) {
    delay(40);
    irT0 = micros(); irAcc = 0;
    irEdge(true, 9000);
    irEdge(false, 2250);
    irEdge(true, 560);
    IR_OFF();
  }
}

// Send both plausible byte orders of the published 0x21DE header, because
// vendor docs are inconsistent about which byte goes on the wire first.
static void irSendBoth(uint8_t cmd) {
  irSendNec(0x21DE, cmd);
  delay(120);
  irSendNec(0xDE21, cmd);
}

static void irSweep() {
  Serial.println("IR sweep — aim the LED at the REAR panel, ~30 cm.");
  Serial.println("Watch the SCREEN, not this console. Any OSD reaction counts.");
  for (size_t i = 0; i < NIRKEYS; i++) {
    stamp();
    Serial.printf("  %-11s cmd 0x%02X\n", IRKEYS[i].name, IRKEYS[i].code);
    irSendBoth(IRKEYS[i].code);
    delay(700);       // long enough to notice an OSD reaction
  }
  Serial.println("Sweep done. Any reaction at all on screen?");
  Serial.println("If nothing: repeat aiming at the FRONT, then the BOTTOM-FRONT,");
  Serial.println("before recording Test 6 as a negative.");
}

static void irRepeat(const char *name, uint16_t times) {
  for (size_t i = 0; i < NIRKEYS; i++) {
    if (!strcasecmp(name, IRKEYS[i].name)) {
      Serial.printf("Repeating '%s' (0x%02X) %u times, 2 s apart.\n",
                    IRKEYS[i].name, IRKEYS[i].code, times);
      for (uint16_t n = 0; n < times; n++) {
        irSendNec(0x21DE, IRKEYS[i].code);   // single order for clean learning
        Serial.printf("  %u/%u\n", n + 1, times);
        delay(2000);
      }
      return;
    }
  }
  Serial.println("Unknown IR key name.");
}

// ============================== USB events =================================
// This is the Test 5 instrumentation.  Put the projector into standby and
// read the timestamps: a DETACH or SUSPEND means the port went dead and
// nothing you send can ever wake it.

#if ARDUINO_USB_MODE == 0
static void usbEventCb(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == ARDUINO_USB_EVENTS) {
    stamp();
    switch (id) {
      case ARDUINO_USB_STARTED_EVENT:   Serial.println("USB: started");   break;
      case ARDUINO_USB_STOPPED_EVENT:   Serial.println("USB: stopped");   break;
      case ARDUINO_USB_SUSPEND_EVENT:   Serial.println("USB: SUSPEND  <- host stopped driving the bus"); break;
      case ARDUINO_USB_RESUME_EVENT:    Serial.println("USB: RESUME");    break;
      default:                          Serial.printf("USB: event %ld\n", (long)id); break;
    }
  } else if (base == ARDUINO_USB_CDC_EVENTS) {
    stamp();
    switch (id) {
      case ARDUINO_USB_CDC_CONNECTED_EVENT:
        Serial.println("CDC: host OPENED the port  <- serial daemon bound!"); break;
      case ARDUINO_USB_CDC_DISCONNECTED_EVENT:
        Serial.println("CDC: host CLOSED the port"); break;
      case ARDUINO_USB_CDC_LINE_STATE_EVENT: {
        arduino_usb_cdc_event_data_t *d = (arduino_usb_cdc_event_data_t *)data;
        Serial.printf("CDC: line state dtr=%d rts=%d\n",
                      d->line_state.dtr, d->line_state.rts);
        break;
      }
      case ARDUINO_USB_CDC_LINE_CODING_EVENT: {
        arduino_usb_cdc_event_data_t *d = (arduino_usb_cdc_event_data_t *)data;
        Serial.printf("CDC: host set %lu baud, %u data, %u stop, %u parity\n",
                      (unsigned long)d->line_coding.bit_rate,
                      d->line_coding.data_bits, d->line_coding.stop_bits,
                      d->line_coding.parity);
        break;
      }
      case ARDUINO_USB_CDC_RX_EVENT:  /* handled in pumpLink */ break;
      default: break;
    }
  }
}
#endif

// ================================ console ==================================

static bool  autoPoll     = false;
static uint32_t pollEvery = 5000;
static uint32_t pollLast  = 0;

static void help() {
  Serial.println();
  Serial.println("=== TITAN Noir bridge, phase 1 ===");
  Serial.println("Serial commands (sent over the projector link):");
  for (size_t i = 0; i < NCMDS; i++) {
    Serial.print("  ");
    Serial.print(CMDS[i].name);
    if ((i % 5) == 4) Serial.println(); else Serial.print('\t');
  }
  Serial.println();
  Serial.println("  raw <hex>        e.g. raw 2A2A020101 04  (spaces ok)");
  Serial.println("  sweep <instr> <from> <to>");
  Serial.println("                   probe undocumented parameters, e.g.");
  Serial.println("                   'sweep 01 00 08' walks every source value");
  Serial.println("HID keyboard:");
  Serial.print("  k <key>          keys:");
  for (size_t i = 0; i < NHIDKEYS; i++) { Serial.print(' '); Serial.print(HIDKEYS[i].name); }
  Serial.println();
  Serial.println("  k <key> <n>      repeat n times, 200 ms apart");
  Serial.println("IR:");
  Serial.println("  ir all           sweep the whole TITAN codeset");
  Serial.println("  ir <key>         send one, both byte orders");
  Serial.println("  irlearn <key>    repeat 20x for remote-learning");
  Serial.println("Misc:");
  Serial.println("  poll [sec]       auto 'temp' every n seconds (0 = off)");
  Serial.println("                   leave this running for Test 5 standby watch");
  Serial.println("  status           USB / link state and counters");
  Serial.println("  ?                this help");
  Serial.println();
}

static void status() {
  stamp();
  Serial.println("--- status ---");
#if USE_PHASE2_UART
  Serial.printf("  Link      : UART1 -> external USB-TTL adapter (phase 2)\n");
  Serial.printf("              TX=GPIO%d  RX=GPIO%d  115200 8N1\n", P2_TX_PIN, P2_RX_PIN);
#else
  Serial.printf("  Link      : native USB CDC\n");
  Serial.printf("  Port open : %s\n", PJ ? "YES (host asserted DTR)" : "no");
  Serial.println("              'no' just means nothing opened the port — it could");
  Serial.println("              still be enumerated. Try 'k down' to test that.");
#endif
  Serial.printf("  Frames TX : %lu\n", (unsigned long)txCount);
  Serial.printf("  Bytes  RX : %lu\n", (unsigned long)rxCount);
  if (lastRxMs) Serial.printf("  Last RX   : %lu ms ago\n",
                              (unsigned long)(millis() - lastRxMs));
  else          Serial.println("  Last RX   : never");
  Serial.printf("  Auto-poll : %s\n", autoPoll ? "on" : "off");
  Serial.printf("  Uptime    : %lu s\n", (unsigned long)(millis() / 1000));
}

static uint8_t parseHexByte(const char *s, bool *ok) {
  uint8_t v = 0; int n = 0;
  for (; *s; s++) {
    uint8_t d;
    if      (*s >= '0' && *s <= '9') d = *s - '0';
    else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
    else break;
    v = (v << 4) | d; n++;
  }
  if (ok) *ok = (n > 0);
  return v;
}

static void handleRaw(char *arg) {
  uint8_t buf[64];
  uint8_t n = 0;
  uint8_t nib = 0, have = 0;
  for (char *c = arg; *c && n < sizeof(buf); c++) {
    if (*c == ' ' || *c == ',') continue;
    uint8_t v;
    if      (*c >= '0' && *c <= '9') v = *c - '0';
    else if (*c >= 'a' && *c <= 'f') v = *c - 'a' + 10;
    else if (*c >= 'A' && *c <= 'F') v = *c - 'A' + 10;
    else continue;
    if (!have) { nib = v; have = 1; }
    else       { buf[n++] = (nib << 4) | v; have = 0; }
  }
  if (!n) { Serial.println("no valid hex"); return; }
  LINK.write(buf, n);
  LINK.flush();
  stamp();
  Serial.printf("TX -> %u raw bytes\n", n);
}

// "sweep 01 00 08" — walk instruction 0x01 through parameters 0x00..0x08,
// 1.2 s apart, so you can watch the screen and see which values do anything.
static void handleSweep(char *arg) {
  char *a = arg, *b = NULL, *c = NULL;
  b = strchr(a, ' '); if (b) { *b++ = 0; while (*b == ' ') b++; }
  if (b) { c = strchr(b, ' '); if (c) { *c++ = 0; while (*c == ' ') c++; } }
  if (!b || !c) { Serial.println("usage: sweep <instr> <from> <to>  (hex)"); return; }
  bool o1, o2, o3;
  uint8_t instr = parseHexByte(a, &o1);
  uint8_t from  = parseHexByte(b, &o2);
  uint8_t to    = parseHexByte(c, &o3);
  if (!o1 || !o2 || !o3 || to < from) { Serial.println("bad arguments"); return; }
  if (instr == 0x06) { Serial.println("refusing: 0x06 is factory reset"); return; }
  Serial.printf("Sweeping instr 0x%02X, params 0x%02X..0x%02X. Watch the screen.\n",
                instr, from, to);
  for (uint16_t p = from; p <= to; p++) {
    uint8_t pb = (uint8_t)p;
    sendCmd(instr, &pb, 1);
    delay(1200);
    pumpLink();
  }
  Serial.println("Sweep done. Which values did something?");
}

static void handleLine(char *line) {
  while (*line == ' ') line++;
  if (!*line) return;

  char *arg = strchr(line, ' ');
  if (arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }

  if (!strcmp(line, "?") || !strcasecmp(line, "help")) { help(); return; }
  if (!strcasecmp(line, "status")) { status(); return; }
  if (!strcasecmp(line, "raw"))    { if (arg) handleRaw(arg); else Serial.println("raw <hex>"); return; }
  if (!strcasecmp(line, "sweep"))  { if (arg) handleSweep(arg); else Serial.println("sweep <instr> <from> <to>"); return; }
  if (!strcasecmp(line, "poll")) {
    uint32_t s = arg ? (uint32_t)atoi(arg) : 5;
    if (s == 0) { autoPoll = false; Serial.println("auto-poll off"); }
    else { autoPoll = true; pollEvery = s * 1000UL; pollLast = 0;
           Serial.printf("auto-poll every %lu s — leave this running through standby\n",
                         (unsigned long)s); }
    return;
  }
  if (!strcasecmp(line, "k")) {
    if (!arg) { Serial.println("k <key> [repeat]"); return; }
    char *rep = strchr(arg, ' ');
    int   n   = 1;
    if (rep) { *rep++ = 0; n = atoi(rep); if (n < 1) n = 1; if (n > 50) n = 50; }
    for (int i = 0; i < n; i++) {
      if (!sendHid(arg)) { Serial.println("unknown HID key"); return; }
      if (i + 1 < n) delay(200);
    }
    return;
  }
  if (!strcasecmp(line, "ir")) {
    if (!arg) { Serial.println("ir all | ir <key>"); return; }
    if (!strcasecmp(arg, "all")) { irSweep(); return; }
    for (size_t i = 0; i < NIRKEYS; i++)
      if (!strcasecmp(arg, IRKEYS[i].name)) {
        stamp();
        Serial.printf("IR -> %s (0x%02X), both byte orders\n",
                      IRKEYS[i].name, IRKEYS[i].code);
        irSendBoth(IRKEYS[i].code);
        return;
      }
    Serial.println("unknown IR key");
    return;
  }
  if (!strcasecmp(line, "irlearn")) {
    if (arg) irRepeat(arg, 20); else Serial.println("irlearn <key>");
    return;
  }
  if (!sendNamed(line)) Serial.println("unknown command — type ? for help");
}

// ================================== main ===================================

void setup() {
  Serial.begin(115200);          // UART0 console on the DevKitC "UART" port
  delay(400);

  IR_ATTACH();
  IR_OFF();

#if USE_PHASE2_UART
  Serial1.begin(115200, SERIAL_8N1, P2_RX_PIN, P2_TX_PIN);
#else
  PJ.begin(115200);
  PJ.setDebugOutput(false);
#endif

#if ARDUINO_USB_MODE == 0
  USB.onEvent(usbEventCb);
  PJ.onEvent(usbEventCb);
#endif

  KB.begin();
  USB.productName("Titan Bridge");
  USB.manufacturerName("DIY");
  USB.begin();

  delay(300);
  help();
  status();
  Serial.println("Ready. Suggested first test: 'k down', then 'temp'.");
  Serial.println("Before standby testing, run 'poll 5' and leave it running.");
}

void loop() {
  static char buf[96];
  static uint8_t n = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; handleLine(buf); n = 0; }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
  }

  if (autoPoll && (millis() - pollLast >= pollEvery)) {
    pollLast = millis();
    sendNamed("temp");
  }

  pumpLink();
}
