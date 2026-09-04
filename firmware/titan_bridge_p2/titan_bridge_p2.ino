/* ============================================================================
   titan_bridge_p2.ino — XGIMI TITAN Noir control bridge, production firmware
   ============================================================================

   The Day 1 diagnostic sketch (firmware/titan_bridge_p1) answers *whether* the
   projector can be driven. This is what you run afterwards.

   WHAT IT IS
     A single ESP32-S3 that presents the projector to the rest of the house
     four different ways, all of which funnel into one command path:

       Wi-Fi   Roku ECP emulation on :8060 + SSDP, so a SofaBaton X2 discovers
               it as a Roku TV and gives you discrete PowerOn / PowerOff /
               InputHDMI keys — the things IR can never do.
       Wi-Fi   a web console and a REST API on :80 for Home Assistant.
       IR      a TSOP receiver, so a dead router doesn't cost you the remote.
       USB     HID keyboard on the native port for the Menu key and manual
               focus, which have no serial equivalent.

     Plus a macro engine with anchored menu navigation for the deep features
     (3D, lens memory, iris) that XGIMI's serial set does not expose, and a
     power state machine that makes "on" and "off" idempotent by using the
     temperature query as a liveness probe.

   BOARD / IDE SETTINGS  (all of these matter)
     Board:            "ESP32S3 Dev Module"
     USB Mode:         "USB-OTG (TinyUSB)"
     USB CDC On Boot:  "Disabled"
     Upload Mode:      "UART0 / Hardware CDC"
     Partition Scheme: "Default 4MB with spiffs" (needs OTA space)
     No external libraries. Everything used ships with arduino-esp32.

   WIRING (target architecture, plan §5)
     ESP32-S3 "UART" USB-C  -> laptop, or a 5 V supply once you stop watching
     ESP32-S3 "USB"  USB-C  -> projector USB 2.0   (HID keyboard channel)
     GPIO17 -> CH340 RXD,  GPIO18 <- CH340 TXD,  GND common
       CH340 USB-A         -> projector USB 3.0   (serial channel)
       CH340 VCC deliberately NOT connected — see docs/03-wiring.md
     GPIO15 <- TSOP38238 OUT (IR receiver, optional)

   FIRST BOOT
     No Wi-Fi credentials are compiled in. The bridge starts an access point
     called TitanBridge-XXXX (password "titanbridge"); join it, open
     http://192.168.4.1/ and enter your network. After that it lives at
     http://titan-bridge.local/ and accepts OTA updates.

   CONSOLE
     UART port, 115200, line ending = Newline. Type "?" for commands.
   ========================================================================= */

#include <Arduino.h>
#include <strings.h>

#include "config.h"
#include "titan.h"
#include "macros.h"
#include "net.h"
#include "ecp.h"
#include "irrx.h"

// -------------------------------- console ----------------------------------
// Where the interactive console writes. On a board whose only USB port is
// committed to the projector (CH_UART0) that is nowhere: anything printed to
// UART0 would land in the middle of the 2A2A stream and confuse the daemon at
// the other end. The web UI and the /api/log ring carry the same information,
// so the console degrades to a sink rather than disappearing from the source.
#if CONSOLE_ON_SERIAL
  #define CON Serial
#else
  class NullPrint : public Print {
   public:
    size_t write(uint8_t) override            { return 1; }
    size_t write(const uint8_t *, size_t n) override { return n; }
  };
  static NullPrint CON;
#endif

// ------------------------------- status LED --------------------------------
// Colour is the power state at a glance:
//   blue   booting / setup AP      amber  unknown      green  awake
//   dim red  asleep                white flash         a frame went out
// On boards with a plain single-colour LED the colour collapses to brightness,
// which still distinguishes the states well enough to debug by.
#if STATUS_LED_PIN >= 0
static void led(uint8_t r, uint8_t g, uint8_t b) {
  #if STATUS_LED_IS_RGB
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      rgbLedWrite(STATUS_LED_PIN, r, g, b);
    #else
      neopixelWrite(STATUS_LED_PIN, r, g, b);
    #endif
  #else
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, (r || g || b) ? HIGH : LOW);
  #endif
}
#else
static void led(uint8_t, uint8_t, uint8_t) {}
#endif

static void ledUpdate() {
  static uint32_t at = 0;
  if ((int32_t)(millis() - at) < 0) return;
  at = millis() + 500;
  if (netApMode())                    led(0, 0, 24);
  else if (macroBusy())               led(24, 0, 24);
  else switch (titanPower()) {
    case PWR_AWAKE:  led(0, 20, 4);  break;
    case PWR_ASLEEP: led(10, 0, 0);  break;
    default:         led(20, 12, 0); break;
  }
}

// ================================= console =================================

static void help() {
  CON.println();
  CON.println("=== TITAN Noir bridge (p2) ===");
  CON.println("Projector commands — type the name on its own:");
  for (size_t i = 0; i < NCMDS; i++) {
    CON.print("  ");
    CON.print(CMDS[i].name);
    if ((i % 5) == 4) CON.println(); else CON.print('\t');
  }
  CON.println();
  CON.println("Power (idempotent, verified with a temperature probe):");
  CON.println("  on | off | toggle");
  CON.println("Keyboard channel (native USB HID):");
  CON.print("  k <key>          ");
  for (size_t i = 0; i < NHIDKEYS; i++) { CON.print(HIDKEYS[i].name); CON.print(' '); }
  CON.println();
  CON.println("Macros:");
  CON.println("  macs             list");
  CON.println("  mac <name>       run");
  CON.println("  run <script>     run an inline script");
  CON.println("  macdef <name> <script>");
  CON.println("  macdel <name>    macabort");
  CON.println("  script grammar:  s:<cmd>  h:<key>  r:<hex>  p:on|off  d<ms>  anchor  tok*<n>");
  CON.println("Infrared:");
  CON.println("  irmaps           list bindings");
  CON.println("  irmap <code> <action>     e.g. irmap 21DE:4D m:movie");
  CON.println("  irdel <code>");
  CON.println("Network:");
  CON.println("  wifi <ssid> <pass>        forget        reboot");
  CON.println("Diagnostics:");
  CON.println("  raw <hex>                 status        ?");
  CON.println("  sweep <instr> <from> <to> probe undocumented parameters");
  CON.println();
}

static void status() {
  CON.println();
  CON.println(statusJson());
  CON.println();
}

static uint8_t hexByte(const char *s, bool *ok) {
  uint8_t v = 0; int n = 0;
  for (; *s; s++) {
    uint8_t d;
    if      (*s >= '0' && *s <= '9') d = *s - '0';
    else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
    else break;
    v = (v << 4) | d; n++;
  }
  if (ok) *ok = n > 0;
  return v;
}

static void doSweep(char *arg) {
  char *b = strchr(arg, ' '); if (!b) { CON.println("sweep <instr> <from> <to>"); return; }
  *b++ = 0; while (*b == ' ') b++;
  char *c = strchr(b, ' ');   if (!c) { CON.println("sweep <instr> <from> <to>"); return; }
  *c++ = 0; while (*c == ' ') c++;
  bool o1, o2, o3;
  uint8_t instr = hexByte(arg, &o1), from = hexByte(b, &o2), to = hexByte(c, &o3);
  if (!o1 || !o2 || !o3 || to < from) { CON.println("bad arguments"); return; }
  if (instr == 0x06) { CON.println("refusing: 0x06 is factory reset"); return; }
  String s;
  for (uint16_t p = from; p <= to; p++) {
    char t[24];
    snprintf(t, sizeof(t), "r:2A2A02%02X%02X%02X; d1200", instr, (uint8_t)p,
             (uint8_t)((2 + instr + p) & 0xFF));
    s += t; s += "; ";
  }
  CON.printf("Sweeping instr 0x%02X params 0x%02X..0x%02X — watch the screen.\n",
                instr, from, to);
  macroRunScript(s.c_str());
}

static void handleLine(char *line) {
  while (*line == ' ') line++;
  if (!*line) return;

  char *arg = strchr(line, ' ');
  if (arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }

  if (!strcmp(line, "?") || !strcasecmp(line, "help"))  { help(); return; }
  if (!strcasecmp(line, "status"))   { status(); return; }
  if (!strcasecmp(line, "reboot"))   { CON.println("rebooting"); delay(200); ESP.restart(); }
  if (!strcasecmp(line, "on"))       { titanPowerOn();     return; }
  if (!strcasecmp(line, "off"))      { titanPowerOff();    return; }
  if (!strcasecmp(line, "toggle"))   { titanPowerToggle(); return; }
  if (!strcasecmp(line, "raw"))      { if (arg && titanSendRawHex(arg)) return;
                                       CON.println("raw <hex>"); return; }
  if (!strcasecmp(line, "sweep"))    { if (arg) doSweep(arg); else CON.println("sweep <instr> <from> <to>"); return; }
  if (!strcasecmp(line, "k")) {
    if (!arg || !titanHid(arg)) CON.println("k <key> — unknown key");
    return;
  }
  if (!strcasecmp(line, "macs"))     { CON.println(macroListJson()); return; }
  if (!strcasecmp(line, "macabort")) { macroAbort(); return; }
  if (!strcasecmp(line, "mac")) {
    if (!arg || !macroRun(arg)) CON.println("no such macro (or one is already running)");
    return;
  }
  if (!strcasecmp(line, "run")) {
    if (!arg || !macroRunScript(arg)) CON.println("run <script>");
    return;
  }
  if (!strcasecmp(line, "macdef")) {
    if (!arg) { CON.println("macdef <name> <script>"); return; }
    char *sp = strchr(arg, ' ');
    if (!sp) { CON.println("macdef <name> <script>"); return; }
    *sp++ = 0; while (*sp == ' ') sp++;
    if (!macroDefine(arg, sp)) CON.println("could not save");
    return;
  }
  if (!strcasecmp(line, "macdel")) {
    if (!arg || !macroDelete(arg)) CON.println("not found");
    return;
  }
  if (!strcasecmp(line, "irmaps"))   { CON.println(irMapJson()); return; }
  if (!strcasecmp(line, "irmap")) {
    if (!arg) { CON.println("irmap <code> <action>"); return; }
    char *sp = strchr(arg, ' ');
    if (!sp) { CON.println("irmap <code> <action>"); return; }
    *sp++ = 0; while (*sp == ' ') sp++;
    if (!irMap(arg, sp)) CON.println("could not bind");
    return;
  }
  if (!strcasecmp(line, "irdel")) {
    if (!arg || !irUnmap(arg)) CON.println("not found");
    return;
  }
  if (!strcasecmp(line, "wifi")) {
    if (!arg) { CON.println("wifi <ssid> <pass>"); return; }
    char *sp = strchr(arg, ' ');
    if (sp) { *sp++ = 0; while (*sp == ' ') sp++; }
    netSetCreds(arg, sp ? sp : "");
    return;
  }
  if (!strcasecmp(line, "forget"))   { netForget(); return; }

  if (!titanSendNamed(line)) CON.println("unknown — type ? for help");
}

// ================================== main ===================================

void setup() {
  // titanBegin() re-opens this at LINK_BAUD when UART0 is the projector link.
  Serial.begin(115200);
  delay(400);
  led(0, 0, 24);

  CON.println();
  CON.println("=== " FW_VERSION " ===");

  titanBegin();
  macrosBegin();
  netBegin();
  ecpBegin();
  irrxBegin();

  help();
  status();
  CON.println("Ready.");
}

void loop() {
  static char buf[192];
  static uint16_t n = 0;

  // Compiled out, not merely silenced, when UART0 belongs to the projector:
  // this loop and titanLoop() would otherwise race for the same bytes and each
  // would see a corrupted half of every reply.
#if CONSOLE_ON_SERIAL
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; handleLine(buf); n = 0; }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
  }
#else
  (void)buf; (void)n;
#endif

  titanLoop();
  macrosLoop();
  netLoop();
  ecpLoop();
  irrxLoop();
  ledUpdate();
}
