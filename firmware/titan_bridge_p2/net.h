/* ===========================================================================
   net.h — Wi-Fi, mDNS and OTA.

   Credentials live in NVS, not in the sketch, so the firmware you flash is the
   firmware you can share. First boot with no credentials brings up a setup
   access point; join it and the config page is at http://192.168.4.1/.
   =========================================================================== */
#pragma once
#include <Arduino.h>

void   netBegin();
void   netLoop();
bool   netConnected();
bool   netApMode();
String netIp();
String netSsid();
bool   netSetCreds(const char *ssid, const char *pass);   // saves, then reboots
void   netForget();
