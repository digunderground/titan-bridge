/* ===========================================================================
   titan.h — XGIMI serial protocol, the dual-channel link, the HID keyboard,
             and the power state machine.
   =========================================================================== */
#pragma once
#include <Arduino.h>
#include "config.h"

// ------------------------------- logging -----------------------------------
// Everything goes to the UART console AND a ring buffer the web UI can read,
// so you can watch the bridge from a phone while standing at the projector.
void   tlog(const char *fmt, ...);
String logDump();

// ------------------------------- protocol ----------------------------------
struct Cmd {
  const char *name;
  uint8_t     instr;
  uint8_t     plen;
  uint8_t     p[6];
};
extern const Cmd   CMDS[];
extern const size_t NCMDS;

struct HidKey { const char *name; uint8_t usage; };
extern const HidKey HIDKEYS[];
extern const size_t  NHIDKEYS;

// --------------------------------- link ------------------------------------
void titanBegin();
void titanLoop();

void titanSendCmd(uint8_t instr, const uint8_t *params, uint8_t nparams);
bool titanSendNamed(const char *name);      // false = unknown name
bool titanSendRawHex(const char *hex);      // false = no valid hex
bool titanHid(const char *name);            // false = unknown key
const Cmd *titanFindCmd(const char *name);

// ------------------------------ key channel --------------------------------
// Menu navigation can travel over serial (instruction 0x07) or over USB HID.
// Which one a given projector honours is discovered, not compiled in — so
// macros route their navigation keys through titanKey() and the channel is a
// runtime setting, persisted across reboots. Explicit s: and h: macro steps
// still bypass this and go where they say.
bool        titanKey(const char *name);     // send a key on the current channel
bool        titanKeyChannelHid();
void        titanSetKeyChannel(bool useHid);
const char *titanKeyChannelStr();           // "serial" | "hid"

// Has this serial link ever delivered a **checksum-valid frame**? Distinguishes
// "the projector is asleep" from "there is no serial link at all" — which look
// identical from the transmit side, and cost an evening to tell apart. Counting
// raw bytes is not enough: a floating UART pin produces them on its own.
bool        titanLinkEverRx();

// -------------------------------- status -----------------------------------
enum PowerState { PWR_UNKNOWN = 0, PWR_AWAKE, PWR_ASLEEP };

PowerState  titanPower();
const char *titanPowerStr();
const char *titanTempStr();
uint32_t    titanTxFrames();
uint32_t    titanRxBytes();
uint32_t    titanMsSinceRx();               // UINT32_MAX if never
bool        titanCdcOpen();                 // host asserted DTR on native USB
const char *titanBusyStr();                 // "" when idle

// Idempotent, non-blocking. They queue a sequence that titanLoop() drives and
// then verifies with a temperature probe, so "on" and "off" mean what they say
// regardless of the state you started in.
void titanPowerOn();
void titanPowerOff();
void titanPowerToggle();

// Last thing the projector told us, for the UI.
const char *titanLastRxHex();
