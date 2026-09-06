#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include "ecp.h"
#include "net.h"
#include "titan.h"
#include "macros.h"
#include "irrx.h"
#include "webui.h"
#include <Preferences.h>
#include "icon.h"
#include "config.h"

static WebServer ui(UI_PORT);
static WebServer ecp(ECP_PORT);
// Two sockets, deliberately. Sending with beginPacket()/endPacket() on this
// stack reconfigures the socket and drops its multicast group membership, so a
// single object that both announces and listens stops receiving within seconds
// of every announcement. Measured before the split: inbound SSDP arrived only
// in a brief window after each beginMulticast() and then never again.
static WiFiUDP   ssdpRx;     // joined to 239.255.255.250:1900, receive only
static WiFiUDP   ssdpTx;     // sends replies and NOTIFY, never joined
static WiFiUDP   ssdpUni;    // unicast M-SEARCH straight at our IP

static void ssdpNotify();    // defined with the SSDP section, below
static uint32_t  notifyAt = 0;
static char      serialNo[16];
static char      deviceId[16];

// Open-loop brightness index. The projector has no brightness query, so the
// stepping keys track their own idea of where they are; it resyncs whenever a
// b1..b10 command is sent explicitly.
static int brightness = 7;

// ===========================================================================
// Roku External Control Protocol key map (plan §5)
//
// Actions are macro scripts, so anything you can write in a macro you can bind
// to a remote button — including "m:<macro>" to fire a saved macro.
// ===========================================================================
struct EcpKey { const char *key; const char *action; };

// Navigation uses k: so it follows the active key channel. On a projector
// whose serial daemon never binds, s: here meant every SofaBaton arrow key
// was silently dead. Input and picture entries stay on s: — they have no HID
// equivalent at all and need menu macros instead.
static const EcpKey ECPMAP[] = {
  { "PowerOn",     "p:on"    },
  { "PowerOff",    "p:off"   },
  { "Power",       "p:toggle"},

  { "Up",          "k:up"    },
  { "Down",        "k:down"  },
  { "Left",        "k:left"  },
  { "Right",       "k:right" },
  { "Select",      "k:ok"    },
  { "Back",        "k:back"  },
  { "Home",        "k:home"  },
  { "Options",     "k:setting" },

  { "VolumeUp",    "k:volup" },
  { "VolumeDown",  "k:voldn" },
  { "VolumeMute",  "s:mute"  },

  { "InputHDMI1",  "s:hdmi1" },
  { "InputHDMI2",  "s:hdmi2" },
  { "InputHDMI3",  "s:hdmi3" },     // undocumented; harmless if unsupported
  { "InputHDMI4",  "s:usbsrc"},
  { "InputAV1",    "s:usbsrc"},
  { "InputTuner",  "s:source"},

  // Transport keys become picture presets — the projector has no transport.
  { "Play",        "s:filmmaker" },
  { "Rev",         "s:movie"     },
  { "Fwd",         "s:vivid"     },

  // Brightness stepping (handled specially below, listed here for /api/keys)
  { "Info",            "@bright+" },
  { "InstantReplay",   "@bright-" },

  // Free slots for the deep-feature macros from §6
  { "Search",      "m:tpl3d"   },
  { "Enter",       "m:tpllens" },
  { "FindRemote",  "s:autofocus" },
};
static const size_t NECPMAP = sizeof(ECPMAP) / sizeof(ECPMAP[0]);

static bool runAction(const char *action) {
  if (!action || !*action) return false;
  if (action[0] == '@') {
    if (!strcmp(action, "@bright+") || !strcmp(action, "@bright-")) {
      brightness += (action[7] == '+') ? 1 : -1;
      if (brightness < 1)  brightness = 1;
      if (brightness > 10) brightness = 10;
      char n[6]; snprintf(n, sizeof(n), "b%d", brightness);
      tlog("brightness step -> %s (open loop)", n);
      return titanSendNamed(n);
    }
    return false;
  }
  if (!strncasecmp(action, "m:", 2)) return macroRun(action + 2);
  return macroRunScript(action);
}

// ===========================================================================
// User overrides for the key map.
//
// The compiled ECPMAP is a default, not a decision. A hub button that does the
// wrong thing — or a macro you would rather have on it — should be fixable from
// the app rather than by reflashing. Overrides live in NVS as "key\taction"
// lines and shadow the table; clearing one restores the default.
// ===========================================================================
static void jesc(String &o, const String &s);   // defined with statusJson, below

static Preferences ecpPrefs;
static String      ecpOverrides;

static void ecpMapLoad() {
  ecpPrefs.begin("titan", true);
  ecpOverrides = ecpPrefs.getString("ecpmap", "");
  ecpPrefs.end();
}
static void ecpMapSave() {
  ecpPrefs.begin("titan", false);
  ecpPrefs.putString("ecpmap", ecpOverrides);
  ecpPrefs.end();
}

static int ecpFind(const char *key, int *lineEnd = NULL) {
  String want = String(key) + "\t";
  int i = 0;
  while (i < (int)ecpOverrides.length()) {
    int e = ecpOverrides.indexOf('\n', i); if (e < 0) e = ecpOverrides.length();
    if (ecpOverrides.substring(i, e).startsWith(want)) { if (lineEnd) *lineEnd = e; return i; }
    i = e + 1;
  }
  return -1;
}

static String ecpOverrideFor(const char *key) {
  int e, i = ecpFind(key, &e);
  if (i < 0) return String();
  int t = ecpOverrides.indexOf('\t', i);
  return ecpOverrides.substring(t + 1, e);
}

static bool ecpClear(const char *key) {
  int e, i = ecpFind(key, &e);
  if (i < 0) return false;
  ecpOverrides.remove(i, (e < (int)ecpOverrides.length() ? e + 1 : e) - i);
  ecpMapSave();
  return true;
}

static bool ecpSet(const char *key, const char *action) {
  if (!key || !*key) return false;
  if (strchr(key, '\t') || strchr(key, '\n')) return false;
  ecpClear(key);
  if (!action || !*action) return true;         // cleared back to the default
  String keep = ecpOverrides;
  ecpOverrides += String(key) + "\t" + action + "\n";
  if (ecpOverrides.length() > ECP_MAP_MAX) {
    ecpOverrides = keep;
    tlog("ECP map full — '%s' NOT saved", key);
    return false;
  }
  ecpMapSave();
  tlog("ECP '%s' -> %s", key, action);
  return true;
}

static void ecpResetAll() {
  ecpOverrides = "";
  ecpMapSave();
  tlog("ECP map reset to defaults");
}

// Every key the hub can send, its default, and any override.
static String ecpMapJson() {
  String out = "[";
  for (size_t i = 0; i < NECPMAP; i++) {
    if (i) out += ',';
    String ov = ecpOverrideFor(ECPMAP[i].key);
    out += "{\"key\":\""; out += ECPMAP[i].key;
    out += "\",\"def\":\"";  jesc(out, String(ECPMAP[i].action));
    out += "\",\"action\":\"";
    jesc(out, ov.length() ? ov : String(ECPMAP[i].action));
    out += "\",\"custom\":"; out += ov.length() ? "true" : "false";
    out += '}';
  }
  out += ']';
  return out;
}

static bool ecpKey(const char *key) {
  String ov = ecpOverrideFor(key);
  if (ov.length()) return runAction(ov.c_str());
  for (size_t i = 0; i < NECPMAP; i++)
    if (!strcasecmp(key, ECPMAP[i].key)) return runAction(ECPMAP[i].action);
  tlog("ECP key '%s' is not mapped", key);
  return false;
}

// ===========================================================================
// status
// ===========================================================================
static void jesc(String &o, const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
}

String statusJson() {
  uint32_t since = titanMsSinceRx();
  String j = "{";
  j += "\"power\":\"";  j += titanPowerStr();  j += "\",";
  j += "\"temp\":\"";   j += titanTempStr();   j += "\",";
  j += "\"busy\":\"";   j += titanBusyStr();   j += "\",";
  j += "\"macro\":\"";  j += macroCurrent();   j += "\",";
  j += "\"tx\":";       j += titanTxFrames();  j += ",";
  j += "\"rx\":";       j += titanRxBytes();   j += ",";
  j += "\"since\":";    j += (since == UINT32_MAX ? -1 : (long)since); j += ",";
  j += "\"cdc\":";      j += titanCdcOpen() ? "true" : "false";        j += ",";
  j += "\"brightness\":"; j += brightness; j += ",";
  j += "\"lastrx\":\""; jesc(j, String(titanLastRxHex())); j += "\",";
  j += "\"irlast\":\""; j += irLastCode();  j += "\",";
  j += "\"link\":\"";
  j += (SERIAL_CHANNELS & CH_NATIVE_CDC) ? "native-CDC " : "";
  j += (SERIAL_CHANNELS & CH_UART1) ? "UART1 " : "";
  j += (SERIAL_CHANNELS & CH_UART0) ? "UART0/onboard-bridge" : "";
  j += "\",";
  // Has the far end ever said anything at all? Without this, "power":"asleep"
  // and "there is no serial link" are indistinguishable to any consumer of
  // this API — which is exactly the confusion that cost an evening.
  j += "\"linkalive\":"; j += titanLinkEverRx() ? "true" : "false"; j += ",";
  j += "\"keychan\":\""; j += titanKeyChannelStr(); j += "\",";
  j += "\"powermode\":\""; j += titanPowerModeStr(); j += "\",";
  // "Observed" means genuinely measured. A live serial link does NOT qualify:
  // this projector answers the temperature probe in standby, so a reply proves
  // the link works and says nothing about power. Only a USB bus transition is
  // real evidence, and this projector does not produce one either — so on this
  // model power is always assumed, and the UI says so.
  j += "\"powerobserved\":";
  j += titanUsbPowerKnown() ? "true" : "false"; j += ",";
  j += "\"ssid\":\"";   jesc(j, netSsid());   j += "\",";
  j += "\"ip\":\"";     j += netIp();         j += "\",";
  j += "\"ap\":";       j += netApMode() ? "true" : "false"; j += ",";
  j += "\"fw\":\"";     j += FW_VERSION;      j += "\",";
  j += "\"repo\":\"";   j += FW_REPO;         j += "\",";
  j += "\"heap\":";     j += (uint32_t)ESP.getFreeHeap(); j += ",";
  j += "\"uptime\":";   j += (uint32_t)(millis() / 1000);
  j += "}";
  return j;
}

// ===========================================================================
// REST API + UI  (port 80)
// ===========================================================================
static void cors(WebServer &s) { s.sendHeader("Access-Control-Allow-Origin", "*"); }

static void okJson(WebServer &s, const String &body) {
  cors(s);
  s.send(200, "application/json", body);
}
static void okText(WebServer &s, const char *msg) {
  cors(s);
  s.send(200, "text/plain", msg);
}

static String argOr(WebServer &s, const char *k, const char *d = "") {
  return s.hasArg(k) ? s.arg(k) : String(d);
}

static void uiRoutes() {
  ui.on("/", HTTP_GET, []() {
    ui.sendHeader("Cache-Control", "no-store");
    ui.send_P(200, "text/html", UI_HTML);
  });

  // Served as a real file, not a data: URI — iOS ignores data URIs for
  // apple-touch-icon, so "Add to Home Screen" would fall back to a screenshot.
  ui.on("/icon.png", HTTP_GET, []() {
    ui.sendHeader("Cache-Control", "public, max-age=86400");
    ui.send_P(200, "image/png", (const char *)APP_ICON_PNG, APP_ICON_PNG_LEN);
  });
  ui.on("/api/status", HTTP_ANY, []() { okJson(ui, statusJson()); });
  ui.on("/api/keychan", HTTP_ANY, []() {
    String m = ui.arg("mode");
    if (m == "hid" || m == "serial") {
      if (!titanSetKeyChannel(m == "hid")) {
        okText(ui, "refused: the serial link has never answered");
        return;
      }
    }
    okJson(ui, statusJson());
  });
  ui.on("/api/log",    HTTP_ANY, []() { cors(ui); ui.send(200, "text/plain", logDump()); });
  ui.on("/api/macros", HTTP_ANY, []() { okJson(ui, macroListJson()); });
  ui.on("/api/irmaps", HTTP_ANY, []() { okJson(ui, irMapJson()); });

  ui.on("/api/cmd", HTTP_ANY, []() {
    String n = argOr(ui, "name");
    okText(ui, titanSendNamed(n.c_str()) ? "ok" : "unknown command");
  });
  ui.on("/api/nav", HTTP_ANY, []() {      // navigation on the active channel
    String n = argOr(ui, "name");
    okText(ui, titanKey(n.c_str()) ? "ok" : "key failed on this channel");
  });
  ui.on("/api/hidraw", HTTP_ANY, []() {   // arbitrary usage code, for probing
    String u = argOr(ui, "usage");
    long v = strtol(u.c_str(), NULL, 16);
    if (v <= 0 || v > 0xFF) { okText(ui, "usage must be 01..FF hex"); return; }
    okText(ui, titanHidRaw((uint8_t)v) ? "ok" : "no HID on this board");
  });
  ui.on("/api/hid", HTTP_ANY, []() {
    String k = argOr(ui, "key");
    okText(ui, titanHid(k.c_str()) ? "ok" : "unknown key");
  });
  ui.on("/api/raw", HTTP_ANY, []() {
    String h = argOr(ui, "hex");
    okText(ui, titanSendRawHex(h.c_str()) ? "ok" : "no valid hex");
  });
  ui.on("/api/key", HTTP_ANY, []() {      // ECP key by name, for HA
    String k = argOr(ui, "name");
    okText(ui, ecpKey(k.c_str()) ? "ok" : "unmapped");
  });
  ui.on("/api/power", HTTP_ANY, []() {
    String st = argOr(ui, "state", "toggle");
    if (st == "mode") {                    // obey | assume
      String m = argOr(ui, "m", "obey");
      titanSetPowerObey(m != "assume");
      okJson(ui, statusJson());
      return;
    }
    if (st == "sync") {                    // correct the assumed state, send nothing
      String is = argOr(ui, "is", "on");
      titanAssumeState(is == "on");
      okJson(ui, statusJson());
      return;
    }
    if      (st == "on")  titanPowerOn();
    else if (st == "off") titanPowerOff();
    else                  titanPowerToggle();
    okText(ui, "ok");
  });
  ui.on("/api/macro", HTTP_ANY, []() {
    if (ui.hasArg("script")) { okText(ui, macroRunScript(ui.arg("script").c_str()) ? "ok" : "busy or empty"); return; }
    String n = argOr(ui, "name");
    okText(ui, macroRun(n.c_str()) ? "ok" : "no such macro (or busy)");
  });
  ui.on("/api/macabort", HTTP_ANY, []() { macroAbort(); okText(ui, "ok"); });

  // ------------------------------ recorder --------------------------------
  ui.on("/api/rec", HTTP_ANY, []() {
    String st = argOr(ui, "state");
    if      (st == "start") recStart();
    else if (st == "stop")  recStop();
    else if (st == "clear") recClear();
    else if (st == "save") {
      String n = argOr(ui, "name"), g = argOr(ui, "group");
      if (!n.length())               { okText(ui, "name required"); return; }
      uint16_t mx = (uint16_t)argOr(ui, "maxgap", "0").toInt();
      if (!recSaveAs(n.c_str(), g.c_str(), mx)) { okText(ui, "nothing recorded, or store full"); return; }
    }
    okJson(ui, recJson());
  });
  ui.on("/api/recstep", HTTP_ANY, []() {
    String op = argOr(ui, "op");
    long idx  = argOr(ui, "idx", "0").toInt();
    if (op == "del") recDeleteStep((uint16_t)idx);
    else if (op == "ins") {
      String tok = argOr(ui, "tok");
      long gap   = argOr(ui, "gap", "0").toInt();
      if (tok.length()) recInsertStep((uint16_t)idx, tok.c_str(), (uint16_t)gap);
    }
    okJson(ui, recJson());
  });

  // -------------------------- button assignments ---------------------------
  ui.on("/api/ecpmap", HTTP_ANY, []() { okJson(ui, ecpMapJson()); });
  ui.on("/api/ecpset", HTTP_ANY, []() {
    String k = argOr(ui, "key");
    if (!k.length()) { okText(ui, "key required"); return; }
    ecpSet(k.c_str(), argOr(ui, "action").c_str());
    okJson(ui, ecpMapJson());
  });
  ui.on("/api/ecpreset", HTTP_ANY, []() { ecpResetAll(); okJson(ui, ecpMapJson()); });
  ui.on("/api/buttons", HTTP_ANY, []() { okJson(ui, buttonsJson()); });
  ui.on("/api/button", HTTP_ANY, []() {
    String id = argOr(ui, "id");
    if (!id.length()) { okText(ui, "id required"); return; }
    buttonSet(id.c_str(), argOr(ui, "label").c_str(), argOr(ui, "action").c_str(),
              argOr(ui, "icon").c_str(), argOr(ui, "style").c_str());
    okJson(ui, buttonsJson());
  });
  ui.on("/api/buttonmove", HTTP_ANY, []() {
    String id = argOr(ui, "id"), dir = argOr(ui, "dir", "up");
    buttonMove(id.c_str(), dir == "up");
    okJson(ui, buttonsJson());
  });
  ui.on("/api/press", HTTP_ANY, []() {
    String id = argOr(ui, "id");
    String a  = buttonAction(id.c_str());
    if (!a.length()) { okText(ui, "unassigned"); return; }
    okText(ui, macroRunScript(a.c_str()) ? "ok" : "script failed");
  });
  ui.on("/api/macgroup", HTTP_ANY, []() {   // move a macro between groups
    String n = argOr(ui, "name"), g = argOr(ui, "group");
    String sc = macroScript(n.c_str());
    if (!sc.length()) { okText(ui, "no such macro"); return; }
    okText(ui, macroDefineIn(n.c_str(), g.c_str(), sc.c_str()) ? "ok" : "failed");
  });
  ui.on("/api/macdef", HTTP_ANY, []() {
    // An absent group means "keep the one it already has", so editing a script
    // from a client that does not send the field cannot silently ungroup it.
    String n = argOr(ui, "name");
    String g = ui.hasArg("group") ? ui.arg("group") : macroGroup(n.c_str());
    okText(ui, macroDefineIn(n.c_str(), g.c_str(), argOr(ui, "script").c_str())
               ? "saved" : "bad name or script, or store full");
  });
  ui.on("/api/macdel", HTTP_ANY, []() {
    okText(ui, macroDelete(argOr(ui, "name").c_str()) ? "deleted" : "not found");
  });
  ui.on("/api/irmap", HTTP_ANY, []() {
    okText(ui, irMap(argOr(ui, "code").c_str(), argOr(ui, "action").c_str()) ? "bound" : "bad arguments");
  });
  ui.on("/api/irdel", HTTP_ANY, []() {
    okText(ui, irUnmap(argOr(ui, "code").c_str()) ? "deleted" : "not found");
  });
  ui.on("/api/wifi", HTTP_ANY, []() {
    String s = argOr(ui, "ssid"), p = argOr(ui, "pass");
    if (!s.length()) { okText(ui, "ssid required"); return; }
    okText(ui, "saving, rebooting");
    delay(150);
    netSetCreds(s.c_str(), p.c_str());
  });
  ui.on("/api/forget", HTTP_ANY, []() { okText(ui, "clearing"); delay(150); netForget(); });
  ui.on("/api/announce", HTTP_ANY, []() {
    // Fire a burst before starting a hub scan, for clients that listen for
    // ssdp:alive rather than sending their own search.
    for (int i = 0; i < 6; i++) { ssdpNotify(); delay(120); }
    tlog("SSDP: announced 6x on request");
    okText(ui, "announced");
  });
  ui.on("/api/reboot", HTTP_ANY, []() { okText(ui, "rebooting"); delay(200); ESP.restart(); });

  ui.onNotFound([]() { cors(ui); ui.send(404, "text/plain", "not found"); });
}

// ===========================================================================
// Roku ECP  (port 8060)
// ===========================================================================

static String deviceInfoXml() {
  String x;
  x.reserve(1400);
  x += F("<device-info>");
  x += F("<udn>29380007-0800-1025-8035-");   x += serialNo; x += F("</udn>");
  x += F("<serial-number>");                 x += serialNo; x += F("</serial-number>");
  x += F("<device-id>");                     x += deviceId; x += F("</device-id>");
  x += F("<vendor-name>XGIMI</vendor-name>");
  x += F("<model-name>TITAN Noir</model-name>");
  x += F("<model-number>TitanBridge</model-number>");
  x += F("<model-region>US</model-region>");
  x += F("<is-tv>true</is-tv>");
  x += F("<is-stick>false</is-stick>");
  x += F("<screen-size>120</screen-size>");
  x += F("<panel-id>1</panel-id>");
  x += F("<wifi-mac>");   x += WiFi.macAddress(); x += F("</wifi-mac>");
  x += F("<network-type>wifi</network-type>");
  x += F("<friendly-device-name>"); x += DEVICE_NAME; x += F("</friendly-device-name>");
  x += F("<friendly-model-name>XGIMI TITAN Noir</friendly-model-name>");
  x += F("<default-device-name>");  x += DEVICE_NAME; x += F("</default-device-name>");
  x += F("<user-device-name>");     x += DEVICE_NAME; x += F("</user-device-name>");
  x += F("<software-version>12.0.0</software-version>");
  x += F("<software-build>4444</software-build>");
  x += F("<power-mode>");
  x += (titanPower() == PWR_AWAKE) ? F("PowerOn") : F("DisplayOff");
  x += F("</power-mode>");
  x += F("<supports-suspend>true</supports-suspend>");
  x += F("<supports-find-remote>false</supports-find-remote>");
  x += F("<supports-audio-guide>false</supports-audio-guide>");
  x += F("<developer-enabled>false</developer-enabled>");
  x += F("<search-enabled>false</search-enabled>");
  x += F("<voice-search-enabled>false</voice-search-enabled>");
  x += F("<notifications-enabled>false</notifications-enabled>");
  x += F("<headphones-connected>false</headphones-connected>");
  x += F("<supports-ecs-textedit>false</supports-ecs-textedit>");
  x += F("<supports-ethernet>false</supports-ethernet>");
  x += F("</device-info>");
  return x;
}

static String rootDescXml() {
  String ip = netIp();
  String x;
  x.reserve(1000);
  x += F("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
         "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
         "<specVersion><major>1</major><minor>0</minor></specVersion>"
         "<device>"
         "<deviceType>urn:roku-com:device:player:1-0</deviceType>"
         "<friendlyName>");
  x += DEVICE_NAME;
  x += F("</friendlyName><manufacturer>Roku</manufacturer>"
         "<manufacturerURL>http://www.roku.com/</manufacturerURL>"
         "<modelDescription>XGIMI TITAN Noir control bridge</modelDescription>"
         "<modelName>Roku Streaming Player</modelName>"
         "<modelNumber>TitanBridge</modelNumber>"
         "<serialNumber>");
  x += serialNo;
  x += F("</serialNumber><UDN>uuid:roku:ecp:");
  x += serialNo;
  x += F("</UDN><serviceList><service>"
         "<serviceType>urn:roku-com:service:ecp:1</serviceType>"
         "<serviceId>urn:roku-com:serviceId:ecp1-0</serviceId>"
         "<controlURL/><eventSubURL/><SCPDURL>ecp_SCPD.xml</SCPDURL>"
         "</service></serviceList></device></root>");
  return x;
}

static void ecpRoutes() {
  ecp.on("/", HTTP_GET, []() {
    tlog("ECP root <- %s", ecp.client().remoteIP().toString().c_str());
    ecp.send(200, "text/xml; charset=\"utf-8\"", rootDescXml());
  });
  // Log who asks. A hub that probes and then declines is a different problem
  // from a hub that never finds us, and the two are indistinguishable from
  // outside the device.
  ecp.on("/query/device-info", HTTP_GET, []() {
    tlog("ECP device-info <- %s", ecp.client().remoteIP().toString().c_str());
    ecp.send(200, "text/xml; charset=\"utf-8\"", deviceInfoXml());
  });
  ecp.on("/query/apps", HTTP_GET, []() {
    // Inputs 1-4 are serial commands. On a projector whose serial daemon never
    // binds they do nothing, so publishing them would put four dead buttons on
    // the hub's remote. Macros are published either way, which is how anything
    // beyond the fixed key map reaches the SofaBaton at all.
    String x = "<apps>";
    if (titanLinkEverRx()) {
      x += F("<app id=\"1\" type=\"appl\" version=\"1.0.0\">HDMI1</app>"
             "<app id=\"2\" type=\"appl\" version=\"1.0.0\">HDMI2</app>"
             "<app id=\"3\" type=\"appl\" version=\"1.0.0\">HDMI3</app>"
             "<app id=\"4\" type=\"appl\" version=\"1.0.0\">USB</app>");
    }
    x += macroAppsXml();
    x += "</apps>";
    ecp.send(200, "text/xml; charset=\"utf-8\"", x);
  });
  ecp.on("/query/active-app", HTTP_GET, []() {
    ecp.send(200, "text/xml; charset=\"utf-8\"",
             F("<active-app><app>Roku</app></active-app>"));
  });
  ecp.on("/query/media-player", HTTP_GET, []() {
    ecp.send(200, "text/xml; charset=\"utf-8\"",
             F("<player state=\"close\" error=\"false\"/>"));
  });
  ecp.on("/query/tv-active-channel", HTTP_GET, []() {
    ecp.send(200, "text/xml; charset=\"utf-8\"", F("<tv-channel/>"));
  });

  // /keypress/<Key>, /keydown/<Key>, /keyup/<Key>, /launch/<id>, /input
  ecp.onNotFound([]() {
    String u = ecp.uri();
    tlog("ECP %s <- %s", u.c_str(), ecp.client().remoteIP().toString().c_str());

    auto tail = [&](const char *pfx) -> String {
      if (!u.startsWith(pfx)) return String();
      return u.substring(strlen(pfx));
    };

    String k = tail("/keypress/");
    if (k.length()) { ecpKey(k.c_str()); ecp.send(200, "text/plain", ""); return; }

    k = tail("/keydown/");
    if (k.length()) { ecpKey(k.c_str()); ecp.send(200, "text/plain", ""); return; }

    if (u.startsWith("/keyup/")) { ecp.send(200, "text/plain", ""); return; }

    String app = tail("/launch/");
    if (app.length()) {
      long id = app.toInt();
      if (macroRunAppId((int)id)) { ecp.send(200, "text/plain", ""); return; }
      if      (app.startsWith("1")) titanSendNamed("hdmi1");
      else if (app.startsWith("2")) titanSendNamed("hdmi2");
      else if (app.startsWith("3")) titanSendNamed("hdmi3");
      else if (app.startsWith("4")) titanSendNamed("usbsrc");
      ecp.send(200, "text/plain", "");
      return;
    }

    if (u == "/input" || u.startsWith("/input")) { ecp.send(200, "text/plain", ""); return; }

    ecp.send(404, "text/plain", "not found");
  });
}

// ===========================================================================
// SSDP — answer M-SEARCH for ST: roku:ecp, and announce periodically
// ===========================================================================

// strcasestr is a GNU extension and not reliably present in this toolchain.
static bool containsCI(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  for (const char *p = hay; *p; p++)
    if (!strncasecmp(p, needle, nl)) return true;
  return false;
}

static void ssdpRespond(const IPAddress &to, uint16_t port) {
  char buf[420];
  int n = snprintf(buf, sizeof(buf),
    "HTTP/1.1 200 OK\r\n"
    "Cache-Control: max-age=3600\r\n"
    "ST: roku:ecp\r\n"
    "USN: uuid:roku:ecp:%s\r\n"
    "Ext: \r\n"
    "Server: Roku UPnP/1.0 MiniUPnPd/1.4\r\n"
    "LOCATION: http://%s:%d/\r\n"
    "device-group.roku.com: %s\r\n"
    "\r\n",
    serialNo, netIp().c_str(), ECP_PORT, deviceId);
  ssdpTx.beginPacket(to, port);
  ssdpTx.write((const uint8_t *)buf, n);
  ssdpTx.endPacket();
}

static void ssdpNotify() {
  char buf[460];
  int n = snprintf(buf, sizeof(buf),
    "NOTIFY * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "Cache-Control: max-age=3600\r\n"
    "NT: roku:ecp\r\n"
    "NTS: ssdp:alive\r\n"
    "USN: uuid:roku:ecp:%s\r\n"
    "Server: Roku UPnP/1.0 MiniUPnPd/1.4\r\n"
    "LOCATION: http://%s:%d/\r\n"
    "device-group.roku.com: %s\r\n"
    "\r\n",
    serialNo, netIp().c_str(), ECP_PORT, deviceId);
  ssdpTx.beginPacket(IPAddress(239, 255, 255, 250), 1900);
  ssdpTx.write((const uint8_t *)buf, n);
  ssdpTx.endPacket();
}

// Both sockets carry the same kind of request, so handle them the same way.
// The unicast socket is bound to 0.0.0.0:1900, so on this stack it also
// receives the multicast the joined socket gets — every search arrived twice,
// was answered twice, and logged twice. Drop the duplicate.
static bool ssdpDuplicate(const IPAddress &from, uint16_t port) {
  static uint32_t lastAt = 0;
  static uint32_t lastKey = 0;
  uint32_t key = (uint32_t)from[0] << 24 | (uint32_t)from[1] << 16 |
                 (uint32_t)from[2] << 8  | (uint32_t)from[3];
  key ^= (uint32_t)port << 3;
  uint32_t now = millis();
  if (key == lastKey && (now - lastAt) < 250) { lastAt = now; return true; }
  lastKey = key; lastAt = now;
  return false;
}

static void ssdpHandle(WiFiUDP &sock, int len) {
  if (len > 0) {
    char buf[512];
    int n = sock.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      if (strncasecmp(buf, "M-SEARCH", 8) == 0) {
        // Log every search, matched or not. "We never saw the hub ask" and
        // "we saw it ask for something we don't answer" look identical from
        // the outside and need completely different fixes.
        char st[64] = "?";
        const char *p = buf;
        while (*p) {
          if (!strncasecmp(p, "ST:", 3)) {
            p += 3;
            while (*p == ' ') p++;
            size_t k = 0;
            while (*p && *p != '\r' && *p != '\n' && k < sizeof(st) - 1) st[k++] = *p++;
            st[k] = 0;
            break;
          }
          while (*p && *p != '\n') p++;
          if (*p) p++;
        }
        bool match = containsCI(buf, "roku:ecp") || containsCI(buf, "ssdp:all") ||
                     containsCI(buf, "upnp:rootdevice");
        if (ssdpDuplicate(sock.remoteIP(), sock.remotePort())) return;
        if (match) ssdpRespond(sock.remoteIP(), sock.remotePort());
        // Only narrate searches we answer. Other devices' SSDP is constant on a
        // normal LAN and buried the lines that matter.
        if (match) tlog("SSDP answered %s ST=%s",
                        sock.remoteIP().toString().c_str(), st);
      }
    }
  }
}

static void ssdpLoop() {
  ssdpHandle(ssdpRx,  ssdpRx.parsePacket());
  ssdpHandle(ssdpUni, ssdpUni.parsePacket());

  if ((int32_t)(millis() - notifyAt) >= 0) {
    notifyAt = millis() + SSDP_NOTIFY_INTERVAL_MS;
    // Re-join the group before announcing. Multicast membership on this stack
    // has been observed to lapse a few seconds after boot — measured: the
    // bridge received SSDP from two LAN devices between t=3s and t=10s and
    // then never again, while searches from a laptop on the same subnet never
    // arrived at all. Re-arming is cheap and makes the failure self-healing
    // rather than requiring a reboot.
    ssdpNotify();
  }
}

// ===========================================================================

void ecpBegin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(serialNo, sizeof(serialNo), "TB%02X%02X%02X%02X",
           mac[2], mac[3], mac[4], mac[5]);
  snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  ecpMapLoad();
  uiRoutes();
  ui.begin();
  tlog("UI + REST on http://%s/", netIp().c_str());

#if ECP_ENABLE
  ecpRoutes();
  ecp.begin();
  if (!netApMode()) {
    ssdpRx.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
    ssdpTx.begin(0);                 // ephemeral port, send only
    // Some clients probe a known IP with a unicast M-SEARCH rather than
    // multicasting. beginMulticast() alone does not deliver those.
    ssdpUni.begin(1900);
    notifyAt = millis() + 3000;
  }
  tlog("Roku ECP on http://%s:%d/ (serial %s)", netIp().c_str(), ECP_PORT, serialNo);
#endif
}

void ecpLoop() {
  ui.handleClient();
#if ECP_ENABLE
  ecp.handleClient();
  if (!netApMode()) ssdpLoop();
#endif
}
