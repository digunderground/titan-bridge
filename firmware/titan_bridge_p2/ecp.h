/* ===========================================================================
   ecp.h — the network face of the bridge.

   Two HTTP servers and one multicast socket:

     :80    the human UI and the REST API that Home Assistant talks to
     :8060  Roku External Control Protocol, so a SofaBaton X2 (or anything
            else that speaks Roku) discovers the bridge as a Roku TV and
            hands you a native button layout — including the discrete
            PowerOn / PowerOff / InputHDMI keys that IR can never give you.
     :1900  SSDP, answering M-SEARCH for ST: roku:ecp

   Every transport ends up in the same place: a macro script. There is one
   execution path, so a button on the web page, a Roku keypress, an IR code
   and a Home Assistant call all behave identically.
   =========================================================================== */
#pragma once
#include <Arduino.h>

void   ecpBegin();
void   ecpLoop();
String statusJson();
