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

// ------------------------------- status LED --------------------------------
// The DevKitC-1's onboard WS2812. Colour is the power state at a glance:
//   blue   booting / setup AP      amber  unknown      green  awake
//   dim red  asleep                white flash         a frame went out
#if STATUS_LED_PIN >= 0
static void led(uint8_t r, uint8_t g, uint8_t b) {
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    rgbLedWrite(STATUS_LED_PIN, r, g, b);
  #else
    neopixelWrite(STATUS_LED_PIN, r, g, b);
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
  Serial.println();
  Serial.println("=== TITAN Noir bridge (p2) ===");
  Serial.println("Projector commands — type the name on its own:");
  for (size_t i = 0; i < NCMDS; i++) {
    Serial.print("  ");
    Serial.print(CMDS[i].name);
    if ((i % 5) == 4) Serial.println(); else Serial.print('\t');
  }
  Serial.println();
  Serial.println("Power (idempotent, verified with a temperature probe):");
  Serial.println("  on | off | toggle");
  Serial.println("Keyboard channel (native USB HID):");
  Serial.print("  k <key>          ");
  for (size_t i = 0; i < NHIDKEYS; i++) { Serial.print(HIDKEYS[i].name); Serial.print(' '); }
  Serial.println();
  Serial.println("Macros:");
  Serial.println("  macs             list");
  Serial.println("  mac <name>       run");
  Serial.println("  run <script>     run an inline script");
  Serial.println("  macdef <name> <script>");
  Serial.println("  macdel <name>    macabort");
  Serial.println("  script grammar:  s:<cmd>  h:<key>  r:<hex>  p:on|off  d<ms>  anchor  tok*<n>");
  Serial.println("Infrared:");
  Serial.println("  irmaps           list bindings");
  Serial.println("  irmap <code> <action>     e.g. irmap 21DE:4D m:movie");
  Serial.println("  irdel <code>");
  Serial.println("Network:");
  Serial.println("  wifi <ssid> <pass>        forget        reboot");
  Serial.println("Diagnostics:");
  Serial.println("  raw <hex>                 status        ?");
  Serial.println("  sweep <instr> <from> <to> probe undocumented parameters");
  Serial.println();
}

static void status() {
  Serial.println();
  Serial.println(statusJson());
  Serial.println();
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
  char *b = strchr(arg, ' '); if (!b) { Serial.println("sweep <instr> <from> <to>"); return; }
  *b++ = 0; while (*b == ' ') b++;
  char *c = strchr(b, ' ');   if (!c) { Serial.println("sweep <instr> <from> <to>"); return; }
  *c++ = 0; while (*c == ' ') c++;
  bool o1, o2, o3;
  uint8_t instr = hexByte(arg, &o1), from = hexByte(b, &o2), to = hexByte(c, &o3);
  if (!o1 || !o2 || !o3 || to < from) { Serial.println("bad arguments"); return; }
  if (instr == 0x06) { Serial.println("refusing: 0x06 is factory reset"); return; }
  String s;
  for (uint16_t p = from; p <= to; p++) {
    char t[24];
    snprintf(t, sizeof(t), "r:2A2A02%02X%02X%02X; d1200", instr, (uint8_t)p,
             (uint8_t)((2 + instr + p) & 0xFF));
    s += t; s += "; ";
  }
  Serial.printf("Sweeping instr 0x%02X params 0x%02X..0x%02X — watch the screen.\n",
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
  if (!strcasecmp(line, "reboot"))   { Serial.println("rebooting"); delay(200); ESP.restart(); }
  if (!strcasecmp(line, "on"))       { titanPowerOn();     return; }
  if (!strcasecmp(line, "off"))      { titanPowerOff();    return; }
  if (!strcasecmp(line, "toggle"))   { titanPowerToggle(); return; }
  if (!strcasecmp(line, "raw"))      { if (arg && titanSendRawHex(arg)) return;
                                       Serial.println("raw <hex>"); return; }
  if (!strcasecmp(line, "sweep"))    { if (arg) doSweep(arg); else Serial.println("sweep <instr> <from> <to>"); return; }
  if (!strcasecmp(line, "k")) {
    if (!arg || !titanHid(arg)) Serial.println("k <key> — unknown key");
    return;
  }
  if (!strcasecmp(line, "macs"))     { Serial.println(macroListJson()); return; }
  if (!strcasecmp(line, "macabort")) { macroAbort(); return; }
  if (!strcasecmp(line, "mac")) {
    if (!arg || !macroRun(arg)) Serial.println("no such macro (or one is already running)");
    return;
  }
  if (!strcasecmp(line, "run")) {
    if (!arg || !macroRunScript(arg)) Serial.println("run <script>");
    return;
  }
  if (!strcasecmp(line, "macdef")) {
    if (!arg) { Serial.println("macdef <name> <script>"); return; }
    char *sp = strchr(arg, ' ');
    if (!sp) { Serial.println("macdef <name> <script>"); return; }
    *sp++ = 0; while (*sp == ' ') sp++;
    if (!macroDefine(arg, sp)) Serial.println("could not save");
    return;
  }
  if (!strcasecmp(line, "macdel")) {
    if (!arg || !macroDelete(arg)) Serial.println("not found");
    return;
  }
  if (!strcasecmp(line, "irmaps"))   { Serial.println(irMapJson()); return; }
  if (!strcasecmp(line, "irmap")) {
    if (!arg) { Serial.println("irmap <code> <action>"); return; }
    char *sp = strchr(arg, ' ');
    if (!sp) { Serial.println("irmap <code> <action>"); return; }
    *sp++ = 0; while (*sp == ' ') sp++;
    if (!irMap(arg, sp)) Serial.println("could not bind");
    return;
  }
  if (!strcasecmp(line, "irdel")) {
    if (!arg || !irUnmap(arg)) Serial.println("not found");
    return;
  }
  if (!strcasecmp(line, "wifi")) {
    if (!arg) { Serial.println("wifi <ssid> <pass>"); return; }
    char *sp = strchr(arg, ' ');
    if (sp) { *sp++ = 0; while (*sp == ' ') sp++; }
    netSetCreds(arg, sp ? sp : "");
    return;
  }
  if (!strcasecmp(line, "forget"))   { netForget(); return; }

  if (!titanSendNamed(line)) Serial.println("unknown — type ? for help");
}

// ================================== main ===================================

void setup() {
  Serial.begin(115200);
  delay(400);
  led(0, 0, 24);

  Serial.println();
  Serial.println("=== " FW_VERSION " ===");

  titanBegin();
  macrosBegin();
  netBegin();
  ecpBegin();
  irrxBegin();

  help();
  status();
  Serial.println("Ready.");
}

void loop() {
  static char buf[192];
  static uint16_t n = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; handleLine(buf); n = 0; }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
  }

  titanLoop();
  macrosLoop();
  netLoop();
  ecpLoop();
  irrxLoop();
  ledUpdate();
}
