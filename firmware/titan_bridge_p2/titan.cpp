#include <Arduino.h>
#include <strings.h>
#include <stdarg.h>
#include <Preferences.h>
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
static bool     everFrame= false;   // a checksum-valid frame — the real signal
static uint32_t lastFrameAt = 0;    // millis of the last valid frame
static uint8_t  lastAckInstr = 0;   // last acknowledged instruction
static uint32_t lastAckAt = 0;      // millis of that ACK
static int8_t   lastTemp = -1;
static char     lastRxHex[52] = "-";

static PowerState pwr = PWR_UNKNOWN;
static bool       assumedOn = false, assumedKnown = false;
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

  char hex[48]; int p = 0;
  for (uint8_t k = 0; k < i && p < (int)sizeof(hex) - 3; k++)
    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", buf[k]);
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
  if (c->instr == 0x09) assumeAfterToggle(true);          // wake
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
  tlog("TX raw %u bytes", n);
  return true;
}

// ------------------------------ key channel --------------------------------

static bool keyHid = (DEFAULT_KEY_CHANNEL == KEY_CHANNEL_HID);

bool        titanKeyChannelHid() { return keyHid; }
const char *titanKeyChannelStr() { return keyHid ? "hid" : "serial"; }
bool        titanLinkEverRx()    { return everFrame; }

// Was the given instruction acknowledged within the last `withinMs`? The
// absence of an ACK is how an unsupported command announces itself.
bool titanAcked(uint8_t instr, uint32_t withinMs) {
  return lastAckAt && (lastAckInstr == (instr & 0x7F)) &&
         (millis() - lastAckAt) <= withinMs;
}

bool titanSetKeyChannel(bool useHid) {
  // Refuse to route navigation down a serial link that has never answered.
  // The UI gates this too, but the API is reachable from Home Assistant and a
  // hub, and the failure mode — every key silently doing nothing — is exactly
  // the one that cost an evening to diagnose.
  if (!useHid && !everFrame) {
    tlog("key channel: refusing serial, no valid frame has ever arrived");
    return false;
  }
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
  p.end();
  // A saved preference for serial is honoured, but the link has to prove
  // itself before anything routes down it — titanKey() falls back until then.
  if (!keyHid) tlog("key channel: serial saved; will use it once a frame arrives");
}

bool titanKey(const char *name) {
  // Record the channel-neutral form: a macro recorded while driving one channel
  // stays correct on the other, because k: resolves at run time.
  recCapture((String("k:") + name).c_str());
  recSuppressNext();

  // Saved preference says serial, but the link has not spoken yet — use HID
  // rather than dropping the key on the floor.
  bool useHid = keyHid || !everFrame;

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
  everFrame = true;
  lastFrameAt = millis();

  // The projector repeats a status six times, ~50 ms apart, and the log ring
  // is 60 lines — unchecked, a single poll flushes a minute of history. Collapse
  // consecutive identical frames instead of printing each one.
  static char     prevHex[52] = "";
  static uint8_t  prevRepeat  = 0;
  if (!strcmp(hex, prevHex)) {
    if (prevRepeat < 250) prevRepeat++;
    return;                       // counted, not printed
  }
  if (prevRepeat > 1) tlog("   (previous frame x%u)", prevRepeat);
  strncpy(prevHex, hex, sizeof(prevHex) - 1);
  prevHex[sizeof(prevHex) - 1] = 0;
  prevRepeat = 1;

  // Undocumented, discovered 2026-09-05: every command is acknowledged with
  // the instruction ORed with 0x80 and the parameter echoed back —
  //   TX 2A 2A 02 03 1B 20  ->  RX 2A 2A 02 83 1B A0
  // which makes an unsupported command detectable by the *absence* of an ACK.
  if (n >= 6 && (f[3] & 0x80)) {
    uint8_t instr = f[3] & 0x7F;
    const char *nm = "?";
    for (size_t i = 0; i < NCMDS; i++)
      if (CMDS[i].instr == instr && CMDS[i].p[0] == f[4]) { nm = CMDS[i].name; break; }
    lastAckInstr = instr;
    lastAckAt    = millis();
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
  titanSendNamed("temp");
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
//   ASSUME (default) — suppress when our own assumption already matches.
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
static bool     powerObey = false;
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
  powerObey = p.getBool("pwrobey", false);
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
  return titanKeyChannelHid() || !everFrame;
#else
  return false;
#endif
}

void titanPowerOn() {
  // These guards are about intent, not transport, so they apply whichever
  // channel ends up carrying the command.
  if (titanUsbPowerKnown() && titanUsbAwake()) { tlog("power on: already on"); return; }
  if (!powerObey && assumedKnown && assumedOn) {
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
#if USB_DEAD_IN_STANDBY
  if (pwr == PWR_ASLEEP) {
    tlog("power on refused: USB_DEAD_IN_STANDBY is set — use the smart plug "
         "or HDMI-CEC path (plan §7)");
    return;
  }
#endif
  if (act != ACT_NONE) { tlog("busy: %s", titanBusyStr()); return; }
  if (pwr == PWR_AWAKE) { tlog("power on: already awake"); return; }
  act = ACT_ON; actStep = 0; actTries = 0; actAt = millis();
  tlog("power on: starting");
}

void titanPowerOff() {
  if (titanUsbPowerKnown() && !titanUsbAwake()) { tlog("power off: already off"); return; }
  if (!powerObey && assumedKnown && !assumedOn) {
    tlog("power off: skipped, believed already off — resync in Settings if wrong");
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
  act = ACT_OFF; actStep = 0; actTries = 0; actAt = millis();
  tlog("power off: starting");
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

  if (act == ACT_ON) {
    switch (actStep) {
      case 0:
        if (pwr == PWR_AWAKE) { tlog("power on: confirmed awake"); act = ACT_NONE; return; }
        actTries++;
        titanSendNamed("wake");
        actAt = millis() + WAKE_SETTLE_MS; actStep = 1;
        break;
      case 1:
        sendProbe();
        actAt = millis() + POLL_REPLY_TIMEOUT_MS; actStep = 2;
        break;
      case 2:
#if TEMP_PROBE_INDICATES_POWER
        if (probeAnswered()) { setPower(PWR_AWAKE); pollMisses = 0;
                               tlog("power on: OK after %u attempt(s)", actTries);
                               act = ACT_NONE; }
        else if (actTries < WAKE_ATTEMPTS) { actStep = 0; actAt = millis(); }
        else { tlog("power on: FAILED after %u attempts — projector is not "
                    "answering. If its USB ports die in standby this is "
                    "expected; see plan §7.", actTries);
               setPower(PWR_ASLEEP); act = ACT_NONE; }
#else
        // Nothing can confirm this. Retrying would send wake again, which is
        // harmless for "on" but pointless — record the intent and stop.
        tlog("power on: wake sent (cannot be verified on this projector)");
        assumeAfterToggle(true);
        act = ACT_NONE;
#endif
        break;
    }
    return;
  }

  // ACT_OFF — the power key is not discrete; it raises a confirmation dialog.
  switch (actStep) {
    case 0:
      if (pwr == PWR_ASLEEP) { tlog("power off: confirmed asleep"); act = ACT_NONE; return; }
      actTries++;
      titanSendNamed("power");
      actAt = millis() + POWEROFF_CONFIRM_MS; actStep = 1;
      break;
    case 1:
      titanSendNamed("ok");                 // accept the confirmation dialog
      actAt = millis() + POWEROFF_SETTLE_MS; actStep = 2;
      break;
    case 2:
      sendProbe();
      actAt = millis() + POLL_REPLY_TIMEOUT_MS; actStep = 3;
      break;
    case 3:
#if TEMP_PROBE_INDICATES_POWER
      if (!probeAnswered()) { setPower(PWR_ASLEEP); pollMisses = POLL_MISSES_TO_SLEEP;
                              tlog("power off: OK"); act = ACT_NONE; }
      else if (actTries < 2) { tlog("power off: still awake, retrying");
                               actStep = 0; actAt = millis(); }
      else { tlog("power off: FAILED — the confirmation dialog may need a "
                  "different key or delay; tune POWEROFF_CONFIRM_MS");
             setPower(PWR_AWAKE); act = ACT_NONE; }
#else
      // This retry was actively harmful. probeAnswered() is always true here —
      // the projector answers the temperature probe in standby — so a
      // successful power-off looked like "still awake", the sequence pressed
      // power a second time, and the projector came straight back on.
      tlog("power off: sent (cannot be verified on this projector)");
      assumeAfterToggle(false);
      act = ACT_NONE;
#endif
      break;
  }
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
    sendProbe();
  }
}

// ================================ lifecycle ================================

void titanBegin() {
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
  // a partial frame that stops arriving is abandoned, not left to poison
  // the next one
  if (frLen && (millis() - frLast > 120)) frLen = 0;

  runAction();
  runPoll();
}
