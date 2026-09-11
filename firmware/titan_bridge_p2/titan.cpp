#include <Arduino.h>
#include <strings.h>
#include <stdarg.h>
#include <Preferences.h>
#include <esp_system.h>
#include "driver/uart.h"
#include "titan.h"
#include "macros.h"

// Native USB exists only on the S3 family, and even there only when the
// connector is wired to the S3's own D+/D- rather than to an onboard bridge
// chip. Both the headers and the objects have to go behind the guard: a
// classic ESP32 has no USB peripheral for these to compile against at all.
#if HAS_NATIVE_USB
  #include "USB.h"
  #include "USBCDC.h"
  #include "USBHIDKeyboard.h"

  static USBCDC         PJ;
  static USBHIDKeyboard KB;
#endif

// ============================== log ring ===================================

static char     logBuf[LOG_LINES][LOG_LINE_MAX];
static uint16_t logHead = 0;
static bool     logWrapped = false;

void tlog(const char *fmt, ...) {
  char line[LOG_LINE_MAX];
  uint32_t ms = millis();
  int n = snprintf(line, sizeof(line), "[%lu.%03lu] ",
                   (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);

  // When UART0 is carrying the projector, printing here would inject log text
  // straight into the 2A2A stream. The ring buffer and the web UI are the only
  // console in that configuration.
#if CONSOLE_ON_SERIAL
  Serial.println(line);
#endif
  strncpy(logBuf[logHead], line, LOG_LINE_MAX - 1);
  logBuf[logHead][LOG_LINE_MAX - 1] = 0;
  logHead = (logHead + 1) % LOG_LINES;
  if (logHead == 0) logWrapped = true;
}

String logDump() {
  String s;
  s.reserve(LOG_LINES * 40);
  uint16_t start = logWrapped ? logHead : 0;
  uint16_t count = logWrapped ? LOG_LINES : logHead;
  for (uint16_t i = 0; i < count; i++) {
    s += logBuf[(start + i) % LOG_LINES];
    s += '\n';
  }
  return s;
}

// ============================ command table ================================
// Verified byte-for-byte against XGIMI's published command-set image
// (helpcenter article 53406154382361, attachment 53406327353497).
//
// Frame: 2A 2A <len> <instr> <params...> <checksum>
//   len      = 1 (instruction) + number of parameter bytes
//   checksum = (len + instr + all params) & 0xFF

const Cmd CMDS[] = {
  { "hdmi1",     0x01, 1, {0x01} },
  { "hdmi2",     0x01, 1, {0x02} },
  { "hdmi3",     0x01, 1, {0x03} },   // undocumented; XGIMI lists only 2 HDMI
  { "usbsrc",    0x01, 1, {0x14} },

  { "vivid",     0x03, 1, {0x00} },
  { "movie",     0x03, 1, {0x01} },
  { "imax",      0x03, 1, {0x02} },
  { "perf",      0x03, 1, {0x05} },
  { "tvmode",    0x03, 1, {0x07} },
  { "sport",     0x03, 1, {0x09} },
  { "filmmaker", 0x03, 1, {0x1B} },

  { "b1",  0x05, 1, {0x01} }, { "b2",  0x05, 1, {0x02} },
  { "b3",  0x05, 1, {0x03} }, { "b4",  0x05, 1, {0x04} },
  { "b5",  0x05, 1, {0x05} }, { "b6",  0x05, 1, {0x06} },
  { "b7",  0x05, 1, {0x07} }, { "b8",  0x05, 1, {0x08} },
  { "b9",  0x05, 1, {0x09} }, { "b10", 0x05, 1, {0x0A} },

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

  { "wake",      0x09, 6, {0x77, 0x61, 0x6B, 0x65, 0x75, 0x70} },  // "wakeup"

  // Kept reachable by name (macros, /api/cmd, raw) but deliberately NOT
  // surfaced in the app or the hub key map: tested 2026-09-07 and judged not
  // worth a button on this projector. Documented in docs/06-command-reference.md.
  { "blank",     0x0D, 1, {0x00} },
  { "unblank",   0x0D, 1, {0x01} },

  { "hrroff",    0x0E, 1, {0x00} },
  { "hrrbasic",  0x0E, 1, {0x01} },
  { "hrrmax",    0x0E, 1, {0x02} },   // doc prints 01, its checksum says 02

  { "temp",      0x13, 1, {0x00} },
};
const size_t NCMDS = sizeof(CMDS) / sizeof(CMDS[0]);

// Never in the table by name: instruction 0x06 (factory reset).
//   2A2A 02 06 00 08  wipes apps      2A2A 02 06 01 09  keeps apps
// Reachable only through an explicit raw frame.

// Measured against a TITAN Noir Max on 2026-09-05, not assumed. Every entry
// below was fired individually and watched; see docs/10-hid-key-probe.md.
const HidKey HIDKEYS[] = {
  { "up",     0x52 }, { "down",   0x51 }, { "left",   0x50 },
  { "right",  0x4F }, { "ok",     0x28 }, { "back",   0x29 },

  // 0x4A is Keyboard Home, and on this projector it opens the OSD. Both names
  // point at it: "menu" is what it does, "home" is what ECP calls the key.
  { "menu",   0x4A }, { "home",   0x4A },

  // 0x66 is Keyboard Power, and it is a *toggle* that works in both
  // directions — it switches the projector off, and it wakes it from standby.
  // This is the discrete power control the whole project was chasing, and it
  // needs no serial channel, no smart plug and no HDMI-CEC.
  { "power",  0x66 },

  { "volup",  0x80 }, { "voldn",  0x81 },   // keyboard-page volume usages
  { "pgup",   0x4B }, { "pgdn",   0x4E },   // also honoured; see the doc
  { "focus+", 0x57 }, { "focus-", 0x56 },
  { "mute",   0x7F },                       // sent, effect not yet confirmed
};

// 0x65 (Keyboard Application / the context-menu key) was the previous "menu"
// binding. It does nothing on this projector — which mattered, because the
// macro anchor routed k:setting to it and every anchored macro would have
// failed silently. Left here as a warning, not a binding.
const size_t NHIDKEYS = sizeof(HIDKEYS) / sizeof(HIDKEYS[0]);

// =========================== USB bus liveness ==============================
// With no serial channel there is no temperature probe, so the power state
// machine has nothing to poll. But the projector's USB host stops driving the
// bus when it sleeps, and TinyUSB reports that — which is a better liveness
// signal than the temperature ever was: it is pushed, not polled, and it
// cannot be confused with a link that was never there.

#if HAS_NATIVE_USB
static volatile bool usbSuspended = false;
static volatile bool usbStateKnown = false;

// Only a real SUSPEND/RESUME transition proves anything about the *host*.
// STARTED fires when our own device stack initialises, which says nothing
// about whether the projector is awake — trusting it made the bridge believe
// a projector was on when it had just switched it off, so the next "off"
// fired the toggle again and turned it back on. Measured on a TITAN Noir Max:
// this projector does NOT suspend the bus in standby, so usbStateKnown stays
// false here and the power state honestly reports that it cannot tell.
static void usbEventCb(void *, esp_event_base_t base, int32_t id, void *) {
  if (base != ARDUINO_USB_EVENTS) return;
  switch (id) {
    case ARDUINO_USB_SUSPEND_EVENT: usbSuspended = true;  usbStateKnown = true; break;
    case ARDUINO_USB_RESUME_EVENT:  usbSuspended = false; usbStateKnown = true; break;
    default: break;   // STARTED/STOPPED describe us, not the host
  }
}
bool titanUsbPowerKnown() { return usbStateKnown; }
bool titanUsbAwake()      { return usbStateKnown && !usbSuspended; }
#else
bool titanUsbPowerKnown() { return false; }
bool titanUsbAwake()      { return false; }
#endif

// ============================== link state =================================

static uint32_t txFrames = 0, rxBytes = 0;
static uint32_t lastRxAt = 0;           // millis of last byte in
static bool     everRx   = false;   // any byte at all, including line noise
static bool     everFrame= false;   // a checksum-valid frame THIS boot
// Has serial ever worked on this hardware, across reboots? everFrame resets on
// every boot, and the ordinary reason it is false is that the projector is OFF
// and its USB rail — which powers the adapter — is dead. Treating that as "the
// serial link is broken" is what silently failed the bridge over to an unwired
// HID channel and then refused to let it back (2026-09-10).
static bool     serialEverWorked = false;
// True only while the loopback self-test runs. A looped-back frame is our own
// transmission, not a reply, and must not mark the link proven — it did on
// 2026-09-10 and wrote a false positive into NVS.
static bool     inSelfTest = false;
static uint32_t lastFrameAt = 0;    // millis of the last valid frame
static bool     inPoll = false;     // routine liveness poll, not a user action
static uint8_t  lastAckInstr = 0;   // last acknowledged instruction
static uint8_t  lastAckP0    = 0;   // ...and its first parameter
static uint32_t lastAckAt = 0;      // millis of that ACK
static int8_t   lastTemp = -1;
static char     lastRxHex[52] = "-";

static PowerState pwr = PWR_UNKNOWN;
static bool       assumedOn = false, assumedKnown = false;
// When the last wake went out, so a failed power-off can be correlated with
// how long the projector had been booting. 0 = no wake seen this session.
static uint32_t   lastWakeAt = 0;

// Outbound frames are occasionally lost (see ACK_TIMEOUT_MS in config.h). The
// ACK is worthless as proof of effect but it IS proof of receipt, so it is
// exactly the right signal to retransmit on. One frame is held here until its
// ACK comes back or the retries run out.
static uint8_t    pendBuf[16];
static uint8_t    pendLen   = 0;      // 0 = nothing outstanding
static uint8_t    pendInstr = 0, pendP0 = 0;
static uint32_t   pendAt    = 0;
static uint8_t    pendTries = 0;
static void       assumeAfterToggle(bool nowOn);   // defined with the power section
static uint8_t    pollMisses = 0;

uint32_t    titanTxFrames()  { return txFrames; }
uint32_t    titanRxBytes()   { return rxBytes; }
uint32_t    titanMsSinceRx() { return everRx ? (millis() - lastRxAt) : UINT32_MAX; }
#if HAS_NATIVE_USB
bool        titanCdcOpen()   { return (bool)PJ; }
#else
bool        titanCdcOpen()   { return false; }   // no native port to open
#endif
PowerState  titanPower()     { return pwr; }
const char *titanLastRxHex() { return lastRxHex; }

const char *titanPowerStr() {
  switch (pwr) {
    case PWR_AWAKE:  return "awake";
    case PWR_ASLEEP: return "asleep";
    default: break;
  }
  // Nothing observable — say so, rather than dressing a guess up as a reading.
  if (assumedKnown) return assumedOn ? "on (assumed)" : "off (assumed)";
  return "unknown";
}

const char *titanTempStr() {
  switch (lastTemp) {
    case 0x00: return "normal";
    case 0x01: return "high temperature warning";
    case 0x02: return "high temperature shutdown warning";
    case 0x03: return "low temperature shutdown warning";
    case 0x04: return "sensor abnormal";
    default:   return "unknown";
  }
}

// ============================== transmit ===================================

static void linkWrite(const uint8_t *b, size_t n) {
#if (SERIAL_CHANNELS & CH_NATIVE_CDC)
  PJ.write(b, n);
  PJ.flush();
#endif
#if (SERIAL_CHANNELS & CH_UART1)
  Serial1.write(b, n);
  Serial1.flush();
#endif
#if (SERIAL_CHANNELS & CH_UART0)
  Serial.write(b, n);
  Serial.flush();
#endif
}

void titanSendCmd(uint8_t instr, const uint8_t *params, uint8_t nparams) {
  uint8_t buf[16];
  uint8_t len = 1 + nparams;
  uint8_t i = 0;
  buf[i++] = 0x2A;
  buf[i++] = 0x2A;
  buf[i++] = len;
  buf[i++] = instr;
  for (uint8_t k = 0; k < nparams; k++) buf[i++] = params[k];
  uint16_t sum = len + instr;
  for (uint8_t k = 0; k < nparams; k++) sum += params[k];
  buf[i++] = (uint8_t)(sum & 0xFF);

  linkWrite(buf, i);
  txFrames++;

  // Arm retransmission. The 10 s liveness poll is excluded: it repeats on its
  // own, and retrying it would only double the quietest traffic on the link.
  // Retransmit only ABSOLUTE-STATE commands. 0x07 is key simulation, so a
  // repeat is a second physical key press, not a duplicate of the same
  // request -- and for the power key that cancels the shutdown countdown the
  // first press raised. The earlier reasoning here ("no ACK means the command
  // was lost, so a retry is free") only held for the one loss we happened to
  // observe; a lost ACK on a received command is just as possible, and turns
  // the retry into a double press. 0x13 is the poll, which repeats anyway.
  if (instr != 0x13 && instr != 0x07) {
    memcpy(pendBuf, buf, i);
    pendLen   = i;
    pendInstr = instr;
    pendP0    = nparams ? params[0] : 0;
    pendAt    = millis();
    pendTries = 0;
  }

  char hex[48]; int p = 0;
  for (uint8_t k = 0; k < i && p < (int)sizeof(hex) - 3; k++)
    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", buf[k]);
#if !LOG_POLL_TRAFFIC
  if (inPoll) return;                 // counted in tx, just not narrated
#endif
  tlog("TX %s", hex);
}

const Cmd *titanFindCmd(const char *name) {
  for (size_t i = 0; i < NCMDS; i++)
    if (!strcasecmp(name, CMDS[i].name)) return &CMDS[i];
  return NULL;
}

bool titanSendNamed(const char *name) {
  const Cmd *c = titanFindCmd(name);
  if (!c) return false;
  titanSendCmd(c->instr, c->p, c->plen);
  recCapture((String("s:") + name).c_str());

  // Keep the assumed power state honest no matter which door the command came
  // through. Sending "wake" via /api/cmd bypasses the power state machine, so
  // the projector woke while the bridge went on believing it was off — the
  // same hole /api/hidraw opened when it fired the HID power key directly.
  if (c->instr == 0x09) { assumeAfterToggle(true); lastWakeAt = millis(); }  // wake — discrete on
  // The power key is a toggle wherever it comes from, including a raw macro
  // step or a remapped hub button. Tracking it here keeps the assumed state
  // closer to reality than only watching titanPowerOn/Off did.
  if (c->instr == 0x07 && c->p[0] == 0x00) assumeAfterToggle(!assumedOn);
  return true;
}

bool titanSendRawHex(const char *hex) {
  uint8_t buf[64], n = 0, nib = 0, have = 0;
  for (const char *c = hex; *c && n < sizeof(buf); c++) {
    uint8_t v;
    if      (*c >= '0' && *c <= '9') v = *c - '0';
    else if (*c >= 'a' && *c <= 'f') v = *c - 'a' + 10;
    else if (*c >= 'A' && *c <= 'F') v = *c - 'A' + 10;
    else continue;
    if (!have) { nib = v; have = 1; } else { buf[n++] = (nib << 4) | v; have = 0; }
  }
  if (!n) return false;
  linkWrite(buf, n);

  // Log the bytes, not just the count. A sweep used to leave 64 identical
  // "TX raw 6 bytes" lines with no record of which parameter went out.
  char hx[52]; int q = 0;
  for (uint8_t k = 0; k < n && q < (int)sizeof(hx) - 3; k++)
    q += snprintf(hx + q, sizeof(hx) - q, "%02X ", buf[k]);
  tlog("TX raw %s", hx);

  // A power key sent by this route is still a power key. Without this, a raw
  // send silently desynced the assumed state — which is exactly the failure
  // the off-guard then turns into "the remote does nothing".
  if (n >= 6 && buf[0] == 0x2A && buf[1] == 0x2A) {
    if      (buf[3] == 0x09)                    assumeAfterToggle(true);
    else if (buf[3] == 0x07 && buf[4] == 0x00)  assumeAfterToggle(!assumedOn);
  }
  return true;
}

// ------------------------------ key channel --------------------------------

static bool keyHid = (DEFAULT_KEY_CHANNEL == KEY_CHANNEL_HID);

bool        titanKeyChannelHid() { return keyHid; }
const char *titanKeyChannelStr() { return keyHid ? "hid" : "serial"; }
bool        titanLinkEverRx()    { return everFrame; }
bool        titanSerialProven()  { return serialEverWorked; }

// Was the given instruction acknowledged within the last `withinMs`? The
// absence of an ACK is how an unsupported command announces itself.
// Was this exact instruction+parameter acknowledged recently? 0x07 covers every
// simulated key, so matching the instruction alone would let the OK's ACK stand
// in for the power key's.
static bool ackedParam(uint8_t instr, uint8_t p0, uint32_t withinMs) {
  return lastAckAt && lastAckInstr == (instr & 0x7F) && lastAckP0 == p0 &&
         (millis() - lastAckAt) <= withinMs;
}

bool titanAcked(uint8_t instr, uint32_t withinMs) {
  return lastAckAt && (lastAckInstr == (instr & 0x7F)) &&
         (millis() - lastAckAt) <= withinMs;
}

bool titanSetKeyChannel(bool useHid) {
  // The operator's choice is honoured, always. Refusing serial when no frame had
  // arrived created a deadlock: with the projector off the adapter is unpowered
  // and cannot answer, so serial could not be selected — but serial is what
  // turns the projector on. Warn, do not refuse.
  if (!useHid && !everFrame && !serialEverWorked)
    tlog("key channel: serial selected, but no frame has ever arrived — "
         "check the adapter if keys do nothing");
  keyHid = useHid;
  Preferences p;
  p.begin("titan", false);
  p.putBool("keyhid", useHid);
  p.end();
  tlog("key channel -> %s", titanKeyChannelStr());
  return true;
}

static void keyChannelBegin() {
  Preferences p;
  p.begin("titan", true);
  keyHid = p.getBool("keyhid", DEFAULT_KEY_CHANNEL == KEY_CHANNEL_HID);
  serialEverWorked = p.getBool("serok", false);
  p.end();
  // A saved preference for serial is honoured, but the link has to prove
  // itself before anything routes down it — titanKey() falls back until then.
  if (!keyHid) tlog("key channel: serial%s", serialEverWorked ? "" : " (never proven yet)");
}

bool titanKey(const char *name) {
  // Record the channel-neutral form: a macro recorded while driving one channel
  // stays correct on the other, because k: resolves at run time.
  recCapture((String("k:") + name).c_str());
  recSuppressNext();

  // Saved preference says serial, but the link has not spoken yet — use HID
  // rather than dropping the key on the floor.
  // The CONFIGURED channel is used. No inference, no silent fallback.
  // This used to read `keyHid || !everFrame`, so a quiet link — i.e. a projector
  // that is simply switched off — routed every key down HID. On a serial-only
  // build that means nothing works at all, silently. Failing visibly on the
  // channel the operator chose beats succeeding invisibly on one that is not
  // wired.
  bool useHid = keyHid;

  // The two vocabularies diverge, and translation has to work BOTH ways or a
  // key silently dies on one channel. Serial calls the OSD key "setting"
  // (0x07 0x0F); on HID the key that opens it is Home (0x4A), bound as "menu".
  const char *n = name;
  if (useHid) {
    if (!strcasecmp(name, "setting")) n = "menu";
  } else {
    if (!strcasecmp(name, "menu")) n = "setting";
  }

  bool ok = useHid ? titanHid(n) : titanSendNamed(n);
  recSuppressClear();

  if (!ok) {
    // Say which channel refused it. "Unknown key" alone sent me looking at the
    // key when the key was fine and the channel had no equivalent for it.
    tlog("key '%s' has no equivalent on the %s channel", name,
         useHid ? "HID" : "serial");
  }
  return ok;
}

bool titanHidRaw(uint8_t usage) {
#if HAS_HID
  KB.pressRaw(usage);
  delay(30);
  KB.releaseRaw(usage);
  tlog("HID raw 0x%02X", usage);
  // 0x66 is the power toggle. Fired straight through this probe endpoint it
  // used to change the projector without the bridge noticing, which is exactly
  // how the assumed state drifted far enough to make the buttons look reversed.
  if (usage == 0x66) assumeAfterToggle(!assumedOn);
  return true;
#else
  (void)usage;
  tlog("HID unavailable: this board has no native USB device port");
  return false;
#endif
}

bool titanHid(const char *name) {
#if HAS_HID
  for (size_t i = 0; i < NHIDKEYS; i++) {
    if (!strcasecmp(name, HIDKEYS[i].name)) {
      KB.pressRaw(HIDKEYS[i].usage);
      delay(30);
      KB.releaseRaw(HIDKEYS[i].usage);
      tlog("HID %s (0x%02X)", HIDKEYS[i].name, HIDKEYS[i].usage);
      recCapture((String("h:") + HIDKEYS[i].name).c_str());
      return true;
    }
  }
  return false;
#else
  // Report the reason rather than returning a bare false, which the console
  // and /api/hid would otherwise render as "unknown key" — a misleading answer
  // when the key is fine and it is the board that cannot type it.
  (void)name;
  tlog("HID unavailable: this board has no native USB device port");
  return false;
#endif
}

// ============================== receive ====================================
// Proper framing rather than a silence timeout: 2A 2A <len> then len+1 more
// bytes. Anything that doesn't start with the header is dropped one byte at a
// time so a garbled burst resynchronises on the next real frame.

static uint8_t  fr[72];
static uint8_t  frLen = 0;
static uint32_t frLast = 0;

static void frameComplete(const uint8_t *f, uint8_t n) {
  char hex[52]; int p = 0;
  for (uint8_t k = 0; k < n && p < (int)sizeof(hex) - 3; k++)
    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", f[k]);
  strncpy(lastRxHex, hex, sizeof(lastRxHex) - 1);
  lastRxHex[sizeof(lastRxHex) - 1] = 0;

  // Verify before believing. An unconnected UART1 pin floats and invents
  // bytes: one of them was enough to make the bridge claim the link was alive
  // and then report "asleep" for a projector nothing was even attached to.
  // Liveness means a frame that checksums, not an electrical event.
  uint16_t sum = 0;
  for (uint8_t k = 2; k < n - 1; k++) sum += f[k];
  if ((uint8_t)(sum & 0xFF) != f[n - 1]) {
    tlog("RX %s BAD CHECKSUM (want %02X)", hex, (uint8_t)(sum & 0xFF));
    return;
  }
  if (!everFrame) {
    everFrame = true;
    if (!serialEverWorked && !inSelfTest) {   // first ever — remember permanently
      serialEverWorked = true;
      Preferences p;
      if (p.begin("titan", false)) { p.putBool("serok", true); p.end(); }
      tlog("serial link proven — remembered across reboots");
    }
  }
  lastFrameAt = millis();

#if !LOG_POLL_TRAFFIC
  // Handle the liveness poll before anything else touches the log. Its ACK, its
  // status postback and the "repeated" notice are three separate lines every
  // ten seconds; leaving any of them in defeats the point.
  if (n >= 6 && (f[3] & 0x7F) == 0x13) {
    if (f[3] & 0x80) { lastAckInstr = 0x13; lastAckAt = millis(); return; }
    if (n >= 7 && f[4] == 0x01) {
      int8_t was = lastTemp;
      lastTemp = f[5];
      if (was == lastTemp) return;                 // unchanged: nothing to say
      tlog("temperature -> %s", titanTempStr());   // a change is worth a line
      return;
    }
  }
#endif

  // The projector repeats a *status* six times, ~50 ms apart. Collapse
  // consecutive identical frames instead of printing each one — but never an
  // ACK. Probing a parameter means sending the same frame twice and watching
  // whether it is still answered; collapsing that made the second send look
  // like it had failed, and cost an hour of wrong conclusions on 2026-09-06.
  bool isAck = (n >= 6 && (f[3] & 0x80));
  static char     prevHex[52] = "";
  static uint8_t  prevRepeat  = 0;
  if (!isAck) {
    if (!strcmp(hex, prevHex)) {
      if (prevRepeat < 250) prevRepeat++;
      return;                     // counted, not printed
    }
    if (prevRepeat > 1) tlog("   (previous frame x%u)", prevRepeat);
    strncpy(prevHex, hex, sizeof(prevHex) - 1);
    prevHex[sizeof(prevHex) - 1] = 0;
    prevRepeat = 1;
  } else if (prevRepeat > 1) {
    tlog("   (previous frame x%u)", prevRepeat);
    prevHex[0] = 0;
    prevRepeat = 0;
  }

  // Undocumented, discovered 2026-09-05: every command is acknowledged with
  // the instruction ORed with 0x80 and the parameter echoed back —
  //   TX 2A 2A 02 03 1B 20  ->  RX 2A 2A 02 83 1B A0
  // The ACK confirms RECEIPT ONLY. It does not mean the parameter is
  // supported, and it does not mean anything changed: 0x00 (Vivid) ACKs
  // cleanly while leaving the picture mode untouched. Absence of an ACK is
  // evidence; presence of one is not.
  if (n >= 6 && (f[3] & 0x80)) {
    uint8_t instr = f[3] & 0x7F;
    const char *nm = "?";
    for (size_t i = 0; i < NCMDS; i++)
      if (CMDS[i].instr == instr && CMDS[i].p[0] == f[4]) { nm = CMDS[i].name; break; }
    lastAckInstr = instr;
    lastAckP0    = f[4];
    lastAckAt    = millis();
    if (pendLen && instr == pendInstr && f[4] == pendP0) pendLen = 0;  // delivered
    tlog("ACK %s instr=0x%02X %s", hex, instr, nm);
    return;
  }

  // 2A 2A 03 13 01 XX CS — temperature status postback
  if (n >= 7 && f[3] == 0x13 && f[4] == 0x01) {
    lastTemp = f[5];
    tlog("RX %s temp=%s", hex, titanTempStr());
  } else {
    tlog("RX %s", hex);
  }
}

static void rxByte(uint8_t b) {
  rxBytes++;
  lastRxAt = millis();
  everRx = true;
  frLast = lastRxAt;

  if (frLen == 0) { if (b == 0x2A) fr[frLen++] = b; return; }
  if (frLen == 1) { if (b == 0x2A) fr[frLen++] = b; else frLen = (b == 0x2A) ? 1 : 0; return; }
  fr[frLen++] = b;
  if (frLen >= 3) {
    uint8_t want = fr[2] + 4;            // 2A 2A LEN + (LEN bytes) + checksum
    if (want > sizeof(fr)) { frLen = 0; return; }
    if (frLen >= want) { frameComplete(fr, want); frLen = 0; }
  }
}

// =========================== power state machine ===========================

enum Act { ACT_NONE = 0, ACT_ON, ACT_OFF };
static Act      act      = ACT_NONE;
static uint8_t  actStep  = 0;
static uint8_t  actTries = 0;
static uint32_t actAt    = 0;
static uint32_t probeSentAt = 0;
static bool     probePending = false;

const char *titanBusyStr() {
  if (act == ACT_ON)  return "powering on";
  if (act == ACT_OFF) return "powering off";
  return "";
}

static void setPower(PowerState s) {
  if (pwr != s) { pwr = s; tlog("power state -> %s", titanPowerStr()); }
}

static void sendProbe() {
  probeSentAt = millis();
  probePending = true;
  inPoll = true;
  titanSendNamed("temp");
  inPoll = false;
}

// Did the projector *answer* since the probe went out?
//
// "Anything arrived" is not the same question. A single byte of noise on a
// floating or newly connected line was enough to report a projector awake that
// had never said a word — so this asks for a checksum-valid frame, the same
// standard the asleep transition already uses.
static bool probeAnswered() {
  return everFrame && (int32_t)(lastFrameAt - probeSentAt) >= 0;
}

// The HID power key is a toggle, so "on" and "off" are only idempotent if we
// can see the current state. USB bus suspend gives us exactly that, which is
// what makes this safe to expose as PowerOn/PowerOff to Home Assistant and the
// Roku emulation rather than a single ambiguous Toggle.
// No observable liveness on this projector: serial never binds, and the USB
// bus stays fully awake through standby. So the bridge tracks what it *did* —
// the same thing an IR remote macro does — and says plainly that the state is
// assumed. /api/power?state=sync corrects it when a human uses the real remote.

bool titanAssumedKnown() { return assumedKnown; }
bool titanAssumedOn()    { return assumedOn; }

void titanAssumeState(bool on) {
  assumedOn = on; assumedKnown = true;
  Preferences p; p.begin("titan", false);
  p.putBool("pwron", on); p.putBool("pwrk", true); p.end();
  tlog("power state assumed %s (told, not sent)", on ? "on" : "off");
}

static void assumeAfterToggle(bool nowOn) {
  assumedOn = nowOn; assumedKnown = true;
  Preferences p; p.begin("titan", false);
  p.putBool("pwron", nowOn); p.putBool("pwrk", true); p.end();
}

static void assumeBegin() {
  Preferences p; p.begin("titan", true);
  assumedOn    = p.getBool("pwron", false);
  assumedKnown = p.getBool("pwrk",  false);
  p.end();
}

// Who decides whether a power command is needed.
//
//   OBEY (default) — do what you are told, with a short debounce so one press
//     is one toggle. Chosen because the guard's belief cannot be verified and
//     drifts in practice — raw scripts, the physical remote and macros all
//     change the projector without telling it — and a drifted guard makes the
//     button silently do nothing, which needs manual repair. A hub that tracks
//     its own state will not send "off" twice, so the double-toggle this
//     exposes is largely theoretical.
//   ASSUME — suppress when our own assumption already matches.
//     The power key is a *toggle*, so obeying blindly means PowerOn on an
//     already-on projector turns it off: the buttons appear reversed. An
//     assumption that is right gives genuinely discrete behaviour, and the
//     bridge sees every toggle it sends, so it only drifts when a human uses
//     the physical remote. One tap in Settings resyncs it.
//   OBEY — always send. Every press does something, which is honest but not
//     discrete. Reasonable only if you accept the buttons as a toggle pair.
//
// With no feedback from the projector neither is perfect. ASSUME fails by
// doing nothing; OBEY fails by doing the opposite of what the label says —
// and the second is worse, because it looks like a wiring bug.
static bool     powerObey = true;
static uint32_t lastPowerAt = 0;

bool        titanPowerObey()  { return powerObey; }
const char *titanPowerModeStr() { return powerObey ? "obey" : "assume"; }

void titanSetPowerObey(bool obey) {
  powerObey = obey;
  Preferences p; p.begin("titan", false);
  p.putBool("pwrobey", obey); p.end();
  tlog("power mode -> %s", titanPowerModeStr());
}

static void powerModeBegin() {
  Preferences p; p.begin("titan", true);
  powerObey = p.getBool("pwrobey", true);
  p.end();
}

// True when a power key went out too recently to be a separate intent.
static bool powerDebounced() {
  if (lastPowerAt && (millis() - lastPowerAt) < POWER_DEBOUNCE_MS) {
    tlog("power: ignoring repeat within %lu ms", (unsigned long)POWER_DEBOUNCE_MS);
    return true;
  }
  return false;
}

static bool hidPowerPath() {
#if HAS_HID
  // The configured channel decides, full stop. This used to fall back to HID
  // whenever the serial link "had never spoken", which coupled power routing to
  // whether the liveness poll happened to be running — so silencing the poll
  // silently sent power over an unwired HID channel and recorded success.
  // Two unrelated things must not share a variable.
  return titanKeyChannelHid();
#else
  return false;
#endif
}

// ===================== editable power sequences =============================
//
// Power on and power off each run a short script of literal frames, editable
// from Settings so sequences can be tested without a reflash. What is written
// here is what goes on the wire: no checksum correction, no added confirm key,
// no retry. See config.h for the format and the defaults.

struct PwrStep { uint8_t frame[12]; uint8_t len; uint16_t waitMs; };
static PwrStep  pwrSteps[PWRSEQ_MAX_STEPS];
static uint8_t  pwrNSteps = 0, pwrIdx = 0;

static String pwrSeqOn, pwrSeqOff;

const char *titanPowerSeq(bool on)        { return (on ? pwrSeqOn : pwrSeqOff).c_str(); }
const char *titanPowerSeqDefault(bool on) { return on ? PWRSEQ_ON_DEFAULT : PWRSEQ_OFF_DEFAULT; }

void titanPowerSeqSet(bool on, const char *seq) {
  String v = seq ? String(seq) : String();
  if (v.length() > PWRSEQ_MAX_LEN) v = v.substring(0, PWRSEQ_MAX_LEN);
  if (on) pwrSeqOn = v; else pwrSeqOff = v;
  Preferences p;
  if (p.begin("titan", false)) {
    p.putString(on ? "pwrseqon" : "pwrseqoff", v);
    p.end();
  }
  tlog("power %s sequence saved (%u chars)", on ? "on" : "off", v.length());
}

static void powerSeqBegin() {
  Preferences p;
  if (p.begin("titan", true)) {
    pwrSeqOn  = p.getString("pwrseqon",  PWRSEQ_ON_DEFAULT);
    pwrSeqOff = p.getString("pwrseqoff", PWRSEQ_OFF_DEFAULT);
    p.end();
  } else {
    pwrSeqOn  = PWRSEQ_ON_DEFAULT;
    pwrSeqOff = PWRSEQ_OFF_DEFAULT;
  }
}

// Parse one sequence into steps. Returns the count; 0 means nothing to send,
// which is reported rather than silently doing nothing.
static uint8_t parsePowerSeq(const String &src) {
  pwrNSteps = 0;
  int i = 0;
  while (i < (int)src.length() && pwrNSteps < PWRSEQ_MAX_STEPS) {
    int nl = src.indexOf('\n', i);
    String line = (nl < 0) ? src.substring(i) : src.substring(i, nl);
    i = (nl < 0) ? src.length() : nl + 1;
    line.trim();
    if (!line.length() || line.startsWith("#")) continue;

    if (line.startsWith("wait") || line.startsWith("WAIT")) {
      long ms = line.substring(4).toInt();
      if (ms < 0) ms = 0;
      if (ms > 20000) ms = 20000;
      if (pwrNSteps) pwrSteps[pwrNSteps - 1].waitMs = (uint16_t)ms;  // wait AFTER the previous frame
      continue;
    }

    PwrStep st; st.len = 0; st.waitMs = 0;
    uint8_t nib = 0; bool have = false;
    for (int k = 0; k < (int)line.length() && st.len < sizeof(st.frame); k++) {
      char c = line[k]; uint8_t v;
      if      (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else continue;
      if (!have) { nib = v; have = true; }
      else { st.frame[st.len++] = (nib << 4) | v; have = false; }
    }
    if (st.len) pwrSteps[pwrNSteps++] = st;
  }
  return pwrNSteps;
}

void titanPowerOn(bool force) {
  // Turn the projector on the SAME WAY ITS OWN REMOTE DOES — with the power
  // key — not with wake.
  //
  // Measured 2026-09-07, and this was the whole two-day power saga: a projector
  // woken by `wake` (0x09 "wakeup") will not power off afterwards. Not from
  // serial, and not from the OEM remote either. Five consecutive power-offs
  // failed after a wake; every power-off following a power-key or OEM-remote
  // power-on succeeded, with byte-identical frames. Power-off was never broken.
  // wake was poisoning it.
  //
  // The cost is that ON is now a toggle, exactly like OFF, so it needs the same
  // guard. That is a fair trade for a projector that can actually be switched
  // off, and it makes power symmetric: one key, one guard, both directions.
  // wake is idempotent, so there is never a reason to suppress it in obey mode.
  if (!force && !powerObey && assumedKnown && assumedOn) {
    tlog("power on: skipped, believed already on — resync in Settings if wrong");
    return;
  }
  if (powerDebounced()) return;
  lastPowerAt = millis();

  if (hidPowerPath()) {
    tlog("power on: HID power key");
    titanHid("power");
    assumeAfterToggle(true);
    return;
  }
  if (act != ACT_NONE) { tlog("busy: %s", titanBusyStr()); return; }
  if (!parsePowerSeq(pwrSeqOn)) {
    tlog("power on: sequence is empty — nothing sent. Fix it in Settings.");
    return;
  }
  pwrIdx = 0;
  act = ACT_ON; actStep = 0; actTries = 0; actAt = millis();
  tlog("power on: starting (%u step(s))", pwrNSteps);
  evlogAdd("power ON requested");
}

void titanPowerOff(bool force) {
  if (titanUsbPowerKnown() && !titanUsbAwake()) { tlog("power off: already off"); return; }

  // There is NO discrete off. Off is a toggle on both channels, so an "off"
  // sent to an already-off projector switches it ON. That is exactly what made
  // the hub's "Power off" and "Turn off" buttons appear to wake the unit —
  // both send the same ECP key, and both landed here while it was in standby.
  //
  // Probed 2026-09-06 looking for a discrete off to pair with wake: instruction
  // 0x09 is string-keyed ("wakeup"), so "sleep", "standby", "poweroff" and
  // "shutdown" were each sent to an awake projector. All four ACKed with the
  // payload echoed back verbatim — identically to the working "wakeup" — and
  // none of them did anything. No discrete off exists.
  //
  // Hence the asymmetry with titanPowerOn(): on is idempotent so it is never
  // guarded; off is a toggle so it always is, in BOTH power modes. Obey mode
  // used to skip this check, which is what let a stale belief turn the
  // projector on. The worst case for a guarded off is "nothing happened"; for
  // an unguarded one it is "the projector switched on and stayed on".
  // Restored to the original condition. The unconditional guard was added to
  // stop "off" toggling an already-off projector ON — a real bug — but it reads
  // a belief that is mutated by the very command it guards (titanSendNamed
  // flips the assumed state on every power key) and that comes back stale from
  // NVS after every reboot. In practice it suppressed far more legitimate
  // power-offs than it prevented bad ones, silently and before any frame went
  // out.
  //
  // The two-cycle sequence is largely self-correcting anyway: sent to an
  // already-off projector it presses power twice, on then off.
  //
  // In obey mode (the default) this never fires, which is the original
  // behaviour. Choose "Skip if already there" in Settings to re-enable it.
  if (!force && !powerObey && assumedKnown && !assumedOn) {
    tlog("power off: skipped, believed already off — press On then Off "
         "(on is a safe no-op), or resync in Settings");
    evlogAdd("power OFF suppressed (believed off)");
    return;
  }
  if (powerDebounced()) return;
  lastPowerAt = millis();

  if (hidPowerPath()) {
    tlog("power off: HID power key");
    titanHid("power");
    assumeAfterToggle(false);
    return;
  }
  if (act != ACT_NONE) { tlog("busy: %s", titanBusyStr()); return; }
  if (pwr == PWR_ASLEEP) { tlog("power off: already asleep"); return; }
  if (!parsePowerSeq(pwrSeqOff)) {
    tlog("power off: sequence is empty — nothing sent. Fix it in Settings.");
    return;
  }
  pwrIdx = 0;
  act = ACT_OFF; actStep = 0; actTries = 0; actAt = millis();
  tlog("power off: starting (%u step(s))", pwrNSteps);
  evlogAdd("power OFF requested");
}

void titanPowerToggle() {
  if (hidPowerPath()) {
    if (powerDebounced()) return;
    lastPowerAt = millis();
    tlog("power toggle: HID power key");
    titanHid("power");
    assumeAfterToggle(!assumedOn);
    return;
  }
  if (pwr == PWR_AWAKE) titanPowerOff();
  else                  titanPowerOn();
}

static void runAction() {
  if (act == ACT_NONE || (int32_t)(millis() - actAt) < 0) return;

  // Both directions run the editable sequence. The steps were parsed when the
  // action started, so editing a sequence mid-flight cannot corrupt a run.
  if (pwrIdx >= pwrNSteps) {
    tlog("power %s: sequence complete (%u step(s)) — cannot be verified on this "
         "projector", act == ACT_ON ? "on" : "off", pwrNSteps);
    assumeAfterToggle(act == ACT_ON);
    act = ACT_NONE;
    return;
  }

  PwrStep &st = pwrSteps[pwrIdx];
  char hex[40]; int q = 0;
  for (uint8_t k = 0; k < st.len && q < (int)sizeof(hex) - 3; k++)
    q += snprintf(hex + q, sizeof(hex) - q, "%02X ", st.frame[k]);
  linkWrite(st.frame, st.len);
  txFrames++;
  tlog("power %s: step %u/%u  TX %s", act == ACT_ON ? "on" : "off",
       pwrIdx + 1, pwrNSteps, hex);

  actAt = millis() + st.waitMs;
  pwrIdx++;
}

// ======================== serial self-test =================================
//
// Puts UART1 into the ESP32's internal loopback mode, sends one frame, and sees
// whether it comes back. TX is fed straight to RX inside the chip, so nothing
// leaves the board.
//
// This answers the one question the counters cannot: when tx climbs and rx stays
// at zero, is the ESP32 actually driving the line, or is the fault off-board?
// A PASS means the UART, the pins' peripheral routing and the frame parser all
// work, and the problem is the adapter, the wiring, or the projector's Serial
// Port Control setting. Three separate investigations have gone looking in this
// firmware for a fault that was never here.
bool titanSerialSelfTest(String &detail) {
#if (SERIAL_CHANNELS & CH_UART1)
  uint32_t rx0 = rxBytes;
  bool     ever0 = everFrame;

  inSelfTest = true;
  if (uart_set_loop_back(UART_NUM_1, true) != ESP_OK) {
    inSelfTest = false;
    detail = "could not enable loopback";
    return false;
  }
  const uint8_t probe[] = { 0x2A, 0x2A, 0x02, 0x13, 0x00, 0x15 };
  Serial1.write(probe, sizeof(probe));
  Serial1.flush();

  uint32_t t0 = millis();
  while (millis() - t0 < 300) {                 // pump RX ourselves
    while (Serial1.available()) rxByte((uint8_t)Serial1.read());
    delay(5);
  }
  uart_set_loop_back(UART_NUM_1, false);
  inSelfTest = false;

  uint32_t got = rxBytes - rx0;
  everFrame = ever0;                            // a loopback must not count as
                                                // the projector having answered
  if (got >= sizeof(probe)) {
    detail = String("UART1 OK — ") + got + " bytes looped back. The ESP32 is "
             "driving the line; the fault is off-board.";
    return true;
  }
  detail = String("UART1 FAILED — only ") + got + " bytes returned. The problem "
           "is on the board.";
  return false;
#else
  detail = "UART1 is not compiled into this build";
  return false;
#endif
}

// ============================ liveness polling =============================

static uint32_t pollAt = 0;

static void runPoll() {
  if (act != ACT_NONE) return;               // don't fight a power sequence

  if (probePending && (millis() - probeSentAt) > POLL_REPLY_TIMEOUT_MS) {
    probePending = false;
    if (probeAnswered()) {
      pollMisses = 0;
#if TEMP_PROBE_INDICATES_POWER
      setPower(PWR_AWAKE);
#endif
    }
    else if (pollMisses < 255) {
      pollMisses++;
      // Silence only means "asleep" if this link has ever spoken. On a
      // projector that never binds the adapter at all (see logs/TEST-LOG.md,
      // Test 2) the old code reported "asleep" for a projector that was wide
      // awake — and Home Assistant and the Roku emulation would both have
      // believed it. Never having heard anything is PWR_UNKNOWN, not asleep.
#if TEMP_PROBE_INDICATES_POWER
      if (pollMisses >= POLL_MISSES_TO_SLEEP && everFrame && !titanUsbPowerKnown())
        setPower(PWR_ASLEEP);
#endif
    }
  }

  if ((int32_t)(millis() - pollAt) >= 0) {
    pollAt = millis() + POLL_INTERVAL_MS;
#if !POLL_WHEN_BELIEVED_OFF
    // The poll is the only thing the bridge sends unprompted, and it is
    // excluded from the log — which is why "nothing was sent" readings of the
    // log were wrong: the polls were the only traffic there was. Suspect in
    // the spontaneous power-ons, so it stops while we believe the projector
    // is off. It buys us nothing there anyway: the projector answers the probe
    // identically in standby (TEMP_PROBE_INDICATES_POWER 0).
    if (assumedKnown && !assumedOn) return;
#endif
    sendProbe();
  }
}

// ================================ lifecycle ================================

// ===================== persistent event log (NVS) ===========================
//
// The RAM ring buffer above dies with the board. On 2026-09-06 the ESP32 lost
// power overnight and every trace of what happened went with it — including
// whether the projector had rebooted, which was the whole question. This log
// survives power loss, and records WHY the board restarted, which is what
// separates "the USB rail died" from "the firmware crashed".

static uint32_t bootNum = 0;

uint32_t evlogBootNum() { return bootNum; }

static const char *resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";     // the rail was cut
    case ESP_RST_EXT:      return "ext-reset";
    case ESP_RST_SW:       return "sw-reset";     // our own ESP.restart / OTA
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "int-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT";     // marginal supply
    case ESP_RST_DEEPSLEEP:return "deep-sleep";
    default:               return "unknown";
  }
}

void evlogAdd(const char *fmt, ...) {
  char msg[72];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char line[110];
  snprintf(line, sizeof(line), "b%lu+%lus %s\n",
           (unsigned long)bootNum, (unsigned long)(millis() / 1000), msg);

  Preferences p;
  if (!p.begin("titan", false)) return;
  String s = p.getString("evlog", "");
  s += line;
  while (s.length() > EVLOG_MAX) {           // drop whole lines from the front
    int nl = s.indexOf('\n');
    if (nl < 0) { s = ""; break; }
    s.remove(0, nl + 1);
  }
  p.putString("evlog", s);
  p.end();
}

String evlogDump() {
  Preferences p;
  if (!p.begin("titan", true)) return String();
  String s = p.getString("evlog", "");
  p.end();
  return s;
}

static void evlogBegin() {
  Preferences p;
  if (p.begin("titan", false)) {
    bootNum = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", bootNum);
    p.end();
  }
  // A "power-on" reset here with no OTA beforehand means the supply went away,
  // which on this rig means the projector's USB port stopped delivering power.
  evlogAdd("boot #%lu reset=%s", (unsigned long)bootNum, resetReasonStr());
}

void titanBegin() {
  evlogBegin();
  powerSeqBegin();
  keyChannelBegin();
  assumeBegin();
  powerModeBegin();
#if (SERIAL_CHANNELS & CH_UART1)
  // Bias RX to the idle-high state. Left floating with no adapter attached it
  // picks up noise and delivers phantom bytes.
  pinMode(P2_RX_PIN, INPUT_PULLUP);
  Serial1.begin(LINK_BAUD, SERIAL_8N1, P2_RX_PIN, P2_TX_PIN);
#endif
#if (SERIAL_CHANNELS & CH_NATIVE_CDC)
  PJ.begin(LINK_BAUD);
  PJ.setDebugOutput(false);
  PJ.setTxTimeoutMs(20);          // never block the loop on an unopened port
#endif
#if (SERIAL_CHANNELS & CH_UART0)
  // Serial was already opened by setup() at this baud; restated here so the
  // link's baud is not silently inherited from whatever the console wanted.
  Serial.begin(LINK_BAUD, SERIAL_8N1);
#endif
#if HAS_NATIVE_USB
  USB.onEvent(usbEventCb);
  KB.begin();
  USB.productName(DEVICE_NAME);
  USB.manufacturerName("DIY");
  USB.begin();
#endif

  pollAt = millis() + 2000;
  tlog("link up: %s%s%s @ %d 8N1",
       (SERIAL_CHANNELS & CH_NATIVE_CDC) ? "native-CDC " : "",
       (SERIAL_CHANNELS & CH_UART1) ? "UART1 " : "",
       (SERIAL_CHANNELS & CH_UART0) ? "UART0/onboard-bridge" : "", LINK_BAUD);
}

void titanLoop() {
#if (SERIAL_CHANNELS & CH_NATIVE_CDC)
  while (PJ.available()) rxByte((uint8_t)PJ.read());
#endif
#if (SERIAL_CHANNELS & CH_UART1)
  while (Serial1.available()) rxByte((uint8_t)Serial1.read());
#endif
#if (SERIAL_CHANNELS & CH_UART0)
  while (Serial.available()) rxByte((uint8_t)Serial.read());
#endif
#if HAS_NATIVE_USB
  // Pushed, not polled: the host controller suspending the bus is a direct
  // observation of *that host* going to sleep.
  //
  // But it is only evidence about whichever host the native port is plugged
  // into, which on the bench is the laptop rather than the projector. A reply
  // to the temperature probe is an answer from the projector itself, so it
  // wins whenever it is available; USB bus state is the fallback for when
  // there is no serial link at all. Without this the two sources contradicted
  // each other every loop and the power state oscillated awake/asleep twice a
  // second.
  if (!everFrame && titanUsbPowerKnown()) {
    PowerState want = titanUsbAwake() ? PWR_AWAKE : PWR_ASLEEP;
    if (want != pwr) setPower(want);
  }
#endif
  // Retransmit a frame the projector never acknowledged. Measured: outbound
  // frames are occasionally dropped, and the loss is silent — the command
  // simply does not happen. That is what made power-off intermittent for days
  // and sent us chasing boot timing, dialog races and UI state.
  if (pendLen && (millis() - pendAt) > ACK_TIMEOUT_MS) {
    if (pendTries < ACK_RETRIES) {
      pendTries++;
      linkWrite(pendBuf, pendLen);
      txFrames++;
      tlog("no ACK for 0x%02X %02X after %lu ms — resending (try %u/%u)",
           pendInstr, pendP0, (unsigned long)ACK_TIMEOUT_MS,
           pendTries, (unsigned)ACK_RETRIES);
      pendAt = millis();
    } else {
      tlog("LOST: 0x%02X %02X never acknowledged after %u retries",
           pendInstr, pendP0, (unsigned)ACK_RETRIES);
      evlogAdd("LOST 0x%02X %02X after %u retries", pendInstr, pendP0,
               (unsigned)ACK_RETRIES);
      pendLen = 0;
    }
  }

  // Recover a wedged UART. If we are transmitting and NOTHING has come back for
  // a long time — not even a junk byte — the peripheral may have stopped
  // delivering after a break or framing error, which is what happens when the
  // projector's USB rail dies mid-byte and takes the adapter with it. Tearing
  // UART1 down and bringing it back is the only thing that clears that state.
#if (SERIAL_CHANNELS & CH_UART1)
  static uint32_t lastRecoverAt = 0;
  {
    // How long have we heard nothing? If we have never heard anything at all,
    // that is the whole uptime.
    uint32_t quietFor = everRx ? (millis() - lastRxAt) : millis();
    if (txFrames > 4 &&
        quietFor > UART_RECOVER_AFTER_MS &&
        (millis() - lastRecoverAt) > UART_RECOVER_EVERY_MS) {
      lastRecoverAt = millis();
      Serial1.end();
      delay(5);
      pinMode(P2_RX_PIN, INPUT_PULLUP);
      Serial1.begin(LINK_BAUD, SERIAL_8N1, P2_RX_PIN, P2_TX_PIN);
      frLen = 0;
      tlog("UART1 reset — %lu frames sent, nothing received for %lu s",
           (unsigned long)txFrames, (unsigned long)(quietFor / 1000));
      evlogAdd("UART1 reset after %lus quiet", (unsigned long)(quietFor / 1000));
    }
  }
#endif

  // a partial frame that stops arriving is abandoned, not left to poison
  // the next one
  if (frLen && (millis() - frLast > 120)) frLen = 0;

  runAction();
  runPoll();
}
