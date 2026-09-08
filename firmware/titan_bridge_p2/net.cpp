#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "net.h"
#include "titan.h"
#include "config.h"

static Preferences prefs;
static bool   apMode = false;
static String ssid, pass;
static uint32_t retryAt = 0;

bool   netApMode()    { return apMode; }
bool   netConnected() { return WiFi.status() == WL_CONNECTED; }
String netSsid()      { return apMode ? String(SETUP_AP_PREFIX) : ssid; }
String netIp() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

static String apName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s%02X%02X", SETUP_AP_PREFIX, mac[4], mac[5]);
  return String(buf);
}

bool netSetCreds(const char *s, const char *p) {
  if (!s || !*s) return false;
  prefs.begin("titan", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p ? p : "");
  prefs.end();
  tlog("Wi-Fi credentials saved for '%s' — rebooting", s);
  delay(400);
  ESP.restart();
  return true;
}

void netForget() {
  prefs.begin("titan", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  tlog("Wi-Fi credentials cleared — rebooting into setup mode");
  delay(400);
  ESP.restart();
}

static void startAp() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName().c_str(), SETUP_AP_PASS);
  tlog("setup AP '%s' up, password '%s'", apName().c_str(), SETUP_AP_PASS);
  tlog("join it and open http://%s/", WiFi.softAPIP().toString().c_str());
}

static void startOta() {
  ArduinoOTA.setHostname(MDNS_HOST);
  ArduinoOTA.onStart([]() { tlog("OTA start"); });
  ArduinoOTA.onEnd([]()   { tlog("OTA done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { tlog("OTA error %u", (unsigned)e); });
  ArduinoOTA.begin();
}

void netBegin() {
  prefs.begin("titan", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();

  if (!ssid.length()) { startAp(); return; }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // SSDP is multicast; sleeping drops packets
  WiFi.setHostname(MDNS_HOST);
  WiFi.begin(ssid.c_str(), pass.c_str());
  tlog("Wi-Fi: joining '%s'", ssid.c_str());

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);

  if (WiFi.status() != WL_CONNECTED) {
    tlog("Wi-Fi: no join in 20 s — starting setup AP instead");
  // Recorded persistently: a board that boots but never reaches the LAN looks
  // identical from outside to one that never booted at all. On 2026-09-07 that
  // ambiguity cost a USB recovery flash that may not have been needed.
  evlogAdd("Wi-Fi FAILED to join - setup AP started");
    startAp();
    return;
  }

  // Re-assert after the association completes. Setting it before WiFi.begin()
  // does not reliably survive the connect, and once power save is on the AP
  // buffers multicast into DTIM windows the station then misses — which looks
  // exactly like "SSDP works for a few seconds after boot, then stops".
  WiFi.setSleep(false);

  tlog("Wi-Fi: %s  rssi %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  evlogAdd("Wi-Fi up %s rssi %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", UI_PORT);
    tlog("mDNS: http://%s.local/", MDNS_HOST);
  }
  startOta();
}

void netLoop() {
  if (apMode) return;
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - retryAt) >= 0) {
    retryAt = millis() + 15000;
    tlog("Wi-Fi: link down, reconnecting");
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}
