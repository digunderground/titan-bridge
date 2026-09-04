#include <Arduino.h>
#include <strings.h>
#include <stdarg.h>
#include "titan.h"

#include "USB.h"
#include "USBCDC.h"
#include "USBHIDKeyboard.h"

static USBCDC         PJ;
static USBHIDKeyboard KB;

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

  Serial.println(line);
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

const HidKey HIDKEYS[] = {
  { "up",     0x52 }, { "down",   0x51 }, { "left",   0x50 },
  { "right",  0x4F }, { "ok",     0x28 }, { "back",   0x29 },
  { "home",   0x4A }, { "volup",  0x4B }, { "voldn",  0x4E },
  { "focus+", 0x57 }, { "focus-", 0x56 }, { "menu",   0x65 },
};
const size_t NHIDKEYS = sizeof(HIDKEYS) / sizeof(HIDKEYS[0]);

// ============================== link state =================================

static uint32_t txFrames = 0, rxBytes = 0;
static uint32_t lastRxAt = 0;           // millis of last byte in
static bool     everRx   = false;
static int8_t   lastTemp = -1;
static char     lastRxHex[52] = "-";

static PowerState pwr = PWR_UNKNOWN;
static uint8_t    pollMisses = 0;

uint32_t    titanTxFrames()  { return txFrames; }
uint32_t    titanRxBytes()   { return rxBytes; }
uint32_t    titanMsSinceRx() { return everRx ? (millis() - lastRxAt) : UINT32_MAX; }
bool        titanCdcOpen()   { return (bool)PJ; }
PowerState  titanPower()     { return pwr; }
const char *titanLastRxHex() { return lastRxHex; }

const char *titanPowerStr() {
  switch (pwr) {
    case PWR_AWAKE:  return "awake";
    case PWR_ASLEEP: return "asleep";
    default:         return "unknown";
  }
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

bool titanHid(const char *name) {
  for (size_t i = 0; i < NHIDKEYS; i++) {
    if (!strcasecmp(name, HIDKEYS[i].name)) {
      KB.pressRaw(HIDKEYS[i].usage);
      delay(30);
      KB.releaseRaw(HIDKEYS[i].usage);
      tlog("HID %s (0x%02X)", HIDKEYS[i].name, HIDKEYS[i].usage);
      return true;
    }
  }
  return false;
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

// Did anything arrive since the probe went out?
static bool probeAnswered() {
  return everRx && (int32_t)(lastRxAt - probeSentAt) >= 0;
}

void titanPowerOn() {
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
  if (act != ACT_NONE) { tlog("busy: %s", titanBusyStr()); return; }
  if (pwr == PWR_ASLEEP) { tlog("power off: already asleep"); return; }
  act = ACT_OFF; actStep = 0; actTries = 0; actAt = millis();
  tlog("power off: starting");
}

void titanPowerToggle() {
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
        if (probeAnswered()) { setPower(PWR_AWAKE); pollMisses = 0;
                               tlog("power on: OK after %u attempt(s)", actTries);
                               act = ACT_NONE; }
        else if (actTries < WAKE_ATTEMPTS) { actStep = 0; actAt = millis(); }
        else { tlog("power on: FAILED after %u attempts — projector is not "
                    "answering. If its USB ports die in standby this is "
                    "expected; see plan §7.", actTries);
               setPower(PWR_ASLEEP); act = ACT_NONE; }
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
      if (!probeAnswered()) { setPower(PWR_ASLEEP); pollMisses = POLL_MISSES_TO_SLEEP;
                              tlog("power off: OK"); act = ACT_NONE; }
      else if (actTries < 2) { tlog("power off: still awake, retrying");
                               actStep = 0; actAt = millis(); }
      else { tlog("power off: FAILED — the confirmation dialog may need a "
                  "different key or delay; tune POWEROFF_CONFIRM_MS");
             setPower(PWR_AWAKE); act = ACT_NONE; }
      break;
  }
}

// ============================ liveness polling =============================

static uint32_t pollAt = 0;

static void runPoll() {
  if (act != ACT_NONE) return;               // don't fight a power sequence

  if (probePending && (millis() - probeSentAt) > POLL_REPLY_TIMEOUT_MS) {
    probePending = false;
    if (probeAnswered()) { pollMisses = 0; setPower(PWR_AWAKE); }
    else if (pollMisses < 255) {
      pollMisses++;
      if (pollMisses >= POLL_MISSES_TO_SLEEP) setPower(PWR_ASLEEP);
    }
  }

  if ((int32_t)(millis() - pollAt) >= 0) {
    pollAt = millis() + POLL_INTERVAL_MS;
    sendProbe();
  }
}

// ================================ lifecycle ================================

void titanBegin() {
#if (SERIAL_CHANNELS & CH_UART1)
  Serial1.begin(LINK_BAUD, SERIAL_8N1, P2_RX_PIN, P2_TX_PIN);
#endif
#if (SERIAL_CHANNELS & CH_NATIVE_CDC)
  PJ.begin(LINK_BAUD);
  PJ.setDebugOutput(false);
  PJ.setTxTimeoutMs(20);          // never block the loop on an unopened port
#endif
  KB.begin();
  USB.productName(DEVICE_NAME);
  USB.manufacturerName("DIY");
  USB.begin();

  pollAt = millis() + 2000;
  tlog("link up: %s%s @ %d 8N1",
       (SERIAL_CHANNELS & CH_NATIVE_CDC) ? "native-CDC " : "",
       (SERIAL_CHANNELS & CH_UART1) ? "UART1" : "", LINK_BAUD);
}

void titanLoop() {
#if (SERIAL_CHANNELS & CH_NATIVE_CDC)
  while (PJ.available()) rxByte((uint8_t)PJ.read());
#endif
#if (SERIAL_CHANNELS & CH_UART1)
  while (Serial1.available()) rxByte((uint8_t)Serial1.read());
#endif
  // a partial frame that stops arriving is abandoned, not left to poison
  // the next one
  if (frLen && (millis() - frLast > 120)) frLen = 0;

  runAction();
  runPoll();
}
