/* ===========================================================================
   irrx.h — NEC infrared receiver, the offline fallback channel (plan §5).

   Deliberately library-free. IRremoteESP8266 v2.8.6 does not build against
   arduino-esp32 3.x (see the plan's risk table), and a NEC decoder is 60 lines
   of edge timing. One less thing to break on a core upgrade.

   Wiring: TSOP38238 OUT -> IR_RX_PIN, VS -> 3V3 through 100R, GND -> GND,
   4.7uF from VS to GND. Output is active low, idle high.

   Any remote works — this decodes whatever you point at it. Press a button,
   read the code out of the log, and bind it with "irmap".
   =========================================================================== */
#pragma once
#include <Arduino.h>

void   irrxBegin();
void   irrxLoop();

bool   irMap(const char *code, const char *action);   // code = "21DE:4D"
bool   irUnmap(const char *code);
String irMapJson();
const char *irLastCode();                              // "" until something arrives
