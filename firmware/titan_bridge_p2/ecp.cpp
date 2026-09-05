#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include "ecp.h"
#include "net.h"
#include "titan.h"
#include "macros.h"
#include "irrx.h"
#include "webui.h"
#include "config.h"

static WebServer ui(UI_PORT);
static WebServer ecp(ECP_PORT);
static WiFiUDP   ssdp;
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

static const EcpKey ECPMAP[] = {
  { "PowerOn",     "p:on"    },
  { "PowerOff",    "p:off"   },
  { "Power",       "p:toggle"},

  { "Up",          "s:up"    },
  { "Down",        "s:down"  },
  { "Left",        "s:left"  },
  { "Right",       "s:right" },
  { "Select",      "s:ok"    },
  { "Back",        "s:back"  },
  { "Home",        "s:home"  },
  { "Options",     "s:setting" },

  { "VolumeUp",    "s:volup" },
  { "VolumeDown",  "s:voldn" },
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

static bool ecpKey(const char *key) {
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
  j += "\"ssid\":\"";   jesc(j, netSsid());   j += "\",";
  j += "\"ip\":\"";     j += netIp();         j += "\",";
  j += "\"ap\":";       j += netApMode() ? "true" : "false"; j += ",";
  j += "\"fw\":\"";     j += FW_VERSION;      j += "\",";
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

  ui.on("/api/status", HTTP_ANY, []() { okJson(ui, statusJson()); });
  ui.on("/api/keychan", HTTP_ANY, []() {
    String m = ui.arg("mode");
    if (m == "hid" || m == "serial") titanSetKeyChannel(m == "hid");
    okJson(ui, statusJson());
  });
  ui.on("/api/log",    HTTP_ANY, []() { cors(ui); ui.send(200, "text/plain", logDump()); });
  ui.on("/api/macros", HTTP_ANY, []() { okJson(ui, macroListJson()); });
  ui.on("/api/irmaps", HTTP_ANY, []() { okJson(ui, irMapJson()); });

  ui.on("/api/cmd", HTTP_ANY, []() {
    String n = argOr(ui, "name");
    okText(ui, titanSendNamed(n.c_str()) ? "ok" : "unknown command");
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
  ui.on("/api/macdef", HTTP_ANY, []() {
    okText(ui, macroDefine(argOr(ui, "name").c_str(), argOr(ui, "script").c_str())
               ? "saved" : "bad name or script");
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
    ecp.send(200, "text/xml; charset=\"utf-8\"", rootDescXml());
  });
  ecp.on("/query/device-info", HTTP_GET, []() {
    ecp.send(200, "text/xml; charset=\"utf-8\"", deviceInfoXml());
  });
  ecp.on("/query/apps", HTTP_GET, []() {
    ecp.send(200, "text/xml; charset=\"utf-8\"",
      F("<apps>"
        "<app id=\"1\" type=\"appl\" version=\"1.0.0\">HDMI1</app>"
        "<app id=\"2\" type=\"appl\" version=\"1.0.0\">HDMI2</app>"
        "<app id=\"3\" type=\"appl\" version=\"1.0.0\">HDMI3</app>"
        "<app id=\"4\" type=\"appl\" version=\"1.0.0\">USB</app>"
        "</apps>"));
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
  ssdp.beginPacket(to, port);
  ssdp.write((const uint8_t *)buf, n);
  ssdp.endPacket();
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
  ssdp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
  ssdp.write((const uint8_t *)buf, n);
  ssdp.endPacket();
}

static void ssdpLoop() {
  int len = ssdp.parsePacket();
  if (len > 0) {
    char buf[512];
    int n = ssdp.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      if (strncasecmp(buf, "M-SEARCH", 8) == 0) {
        // Answer roku:ecp searches, and the broad ones a hub might use.
        if (containsCI(buf, "roku:ecp") || containsCI(buf, "ssdp:all") ||
            containsCI(buf, "upnp:rootdevice")) {
          ssdpRespond(ssdp.remoteIP(), ssdp.remotePort());
          tlog("SSDP: answered %s", ssdp.remoteIP().toString().c_str());
        }
      }
    }
  }
  if ((int32_t)(millis() - notifyAt) >= 0) {
    notifyAt = millis() + SSDP_NOTIFY_INTERVAL_MS;
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

  uiRoutes();
  ui.begin();
  tlog("UI + REST on http://%s/", netIp().c_str());

#if ECP_ENABLE
  ecpRoutes();
  ecp.begin();
  if (!netApMode()) {
    ssdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
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
