#include <Arduino.h>
#include <strings.h>
#include <Preferences.h>
#include "irrx.h"
#include "titan.h"
#include "macros.h"
#include "config.h"

#if IR_RX_ENABLE

#define IR_CAP 72

static volatile uint32_t edgeDur[IR_CAP];
static volatile uint16_t edgeIdx  = 0;
static volatile uint32_t lastEdge = 0;

static char     lastCode[16] = "";
static uint32_t lastCodeAt   = 0;

static Preferences prefs;
static String      mapStore;      // "21DE:4D\taction\n"

// --------------------------------------------------------------------------

static void IRAM_ATTR irIsr() {
  uint32_t now = micros();
  uint32_t d   = now - lastEdge;
  lastEdge = now;
  if (d > 20000) { edgeIdx = 0; return; }        // long idle = start of a frame
  uint16_t i = edgeIdx;                          // no ++ on a volatile
  if (i < IR_CAP) { edgeDur[i] = d; edgeIdx = i + 1; }
}

static inline bool near_(uint32_t v, uint32_t target, uint32_t tol) {
  return v > (target - tol) && v < (target + tol);
}

// --------------------------------------------------------------------------

static void storeLoad() {
  prefs.begin("titan", true);
  mapStore = prefs.getString("irmap", "");
  prefs.end();
}
static void storeSave() {
  prefs.begin("titan", false);
  prefs.putString("irmap", mapStore);
  prefs.end();
}

static int storeFind(const char *code, int *lineEnd = NULL) {
  String key = String(code) + "\t";
  key.toUpperCase();
  int i = 0;
  while (i < (int)mapStore.length()) {
    int e = mapStore.indexOf('\n', i); if (e < 0) e = mapStore.length();
    String line = mapStore.substring(i, e); line.toUpperCase();
    if (line.startsWith(key)) { if (lineEnd) *lineEnd = e; return i; }
    i = e + 1;
  }
  return -1;
}

bool irMap(const char *code, const char *action) {
  if (!code || !*code || !action || !*action) return false;
  irUnmap(code);
  mapStore += String(code) + "\t" + action + "\n";
  if (mapStore.length() > 2500) { tlog("IR map full"); return false; }
  storeSave();
  tlog("IR %s -> %s", code, action);
  return true;
}

bool irUnmap(const char *code) {
  int e, i = storeFind(code, &e);
  if (i < 0) return false;
  mapStore.remove(i, (e < (int)mapStore.length() ? e + 1 : e) - i);
  storeSave();
  return true;
}

String irMapJson() {
  String out = "[";
  bool first = true;
  int i = 0;
  while (i < (int)mapStore.length()) {
    int e = mapStore.indexOf('\n', i); if (e < 0) e = mapStore.length();
    int t = mapStore.indexOf('\t', i);
    if (t > i && t < e) {
      if (!first) out += ',';
      first = false;
      out += "{\"code\":\"" + mapStore.substring(i, t) +
             "\",\"action\":\"" + mapStore.substring(t + 1, e) + "\"}";
    }
    i = e + 1;
  }
  out += ']';
  return out;
}

const char *irLastCode() { return lastCode; }

// --------------------------------------------------------------------------

static void dispatch(const char *code) {
  int e, i = storeFind(code, &e);
  if (i < 0) {
    tlog("IR %s (unmapped) — bind it with: irmap %s <action>", code, code);
    return;
  }
  int t = mapStore.indexOf('\t', i);
  String action = mapStore.substring(t + 1, e);
  tlog("IR %s -> %s", code, action.c_str());

  if (action.startsWith("m:") || action.startsWith("M:")) {
    if (!macroRun(action.c_str() + 2)) tlog("IR: no macro '%s'", action.c_str() + 2);
  } else {
    macroRunScript(action.c_str());
  }
}

static void decode() {
  uint16_t n = edgeIdx;

  // NEC repeat: 9 ms mark, 2.25 ms space, 560 us mark
  if (n >= 2 && near_(edgeDur[0], 9000, 1800) && near_(edgeDur[1], 2250, 700)) {
    if (lastCode[0] && (millis() - lastCodeAt) < 400) {
      lastCodeAt = millis();          // held down; ignore, we don't auto-repeat
    }
    edgeIdx = 0;
    return;
  }

  if (n < 66) { edgeIdx = 0; return; }
  if (!near_(edgeDur[0], 9000, 1800) || !near_(edgeDur[1], 4500, 1200)) { edgeIdx = 0; return; }

  uint32_t bits = 0;
  for (uint8_t b = 0; b < 32; b++) {
    uint32_t space = edgeDur[3 + 2 * b];
    if (space > 3000) { edgeIdx = 0; return; }
    if (space > 1000) bits |= (1UL << b);          // NEC is LSB-first
  }
  edgeIdx = 0;

  uint8_t a0 = bits & 0xFF, a1 = (bits >> 8) & 0xFF;
  uint8_t c  = (bits >> 16) & 0xFF, ci = (bits >> 24) & 0xFF;
  if ((uint8_t)~c != ci) { tlog("IR frame failed its check byte"); return; }

  uint16_t addr = ((uint16_t)a1 << 8) | a0;        // as printed by most remotes
  char code[16];
  snprintf(code, sizeof(code), "%04X:%02X", addr, c);

  if (!strcmp(code, lastCode) && (millis() - lastCodeAt) < IR_REPEAT_GAP_MS) {
    lastCodeAt = millis();
    return;
  }
  strncpy(lastCode, code, sizeof(lastCode) - 1);
  lastCode[sizeof(lastCode) - 1] = 0;
  lastCodeAt = millis();

  dispatch(code);
}

void irrxBegin() {
  storeLoad();
  pinMode(IR_RX_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(IR_RX_PIN), irIsr, CHANGE);
  int n = 0;
  for (int i = 0; i < (int)mapStore.length(); i++) if (mapStore[i] == '\n') n++;
  tlog("IR receiver on GPIO%d, %d binding(s)", IR_RX_PIN, n);
}

void irrxLoop() {
  if (edgeIdx && (micros() - lastEdge) > 15000) decode();
}

#else   // IR_RX_ENABLE == 0

void   irrxBegin() {}
void   irrxLoop()  {}
bool   irMap(const char *, const char *) { return false; }
bool   irUnmap(const char *)             { return false; }
String irMapJson()   { return String("[]"); }
const char *irLastCode() { return ""; }

#endif
