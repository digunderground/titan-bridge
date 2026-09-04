/* ===========================================================================
   macros.h — the anchored-navigation macro engine (plan §6).

   A macro is a semicolon-separated script. It runs one step per pass of
   macrosLoop(), never blocking, so the web server and Roku emulation stay
   responsive while a 30-step menu walk is in progress.

   Tokens
     s:<name>     serial command from the XGIMI table, e.g.  s:hdmi1
     h:<name>     HID keyboard key, e.g.  h:menu     (no serial equivalent)
     r:<hex>      raw frame, e.g.  r:2A2A020101 04
     p:on p:off   power state machine, idempotent
     d<ms>        delay, e.g.  d750
     anchor       back;back;back;setting — forces a known menu root
     <tok>*<n>    repeat, e.g.  s:down*3

   Example
     anchor; d500; s:down*4; s:ok; d400; s:right; s:ok; s:back*3

   Every step is followed by MACRO_STEP_MS unless an explicit d<ms> says
   otherwise. Tune per macro; find the fastest reliable value rather than
   using one global number.

   User macros are stored in NVS and survive a reflash, so the menu mapping
   you do on Sunday is not lost the next time you rebuild the firmware.
   =========================================================================== */
#pragma once
#include <Arduino.h>

void   macrosBegin();
void   macrosLoop();

bool   macroRun(const char *name);        // false = no such macro
bool   macroRunScript(const char *script);
void   macroAbort();
bool   macroBusy();
const char *macroCurrent();

bool   macroDefine(const char *name, const char *script);  // persists
bool   macroDelete(const char *name);
String macroListJson();
String macroScript(const char *name);     // "" if unknown
