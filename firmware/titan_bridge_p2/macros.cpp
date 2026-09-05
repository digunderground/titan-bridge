#include <Arduino.h>
#include <strings.h>
#include <Preferences.h>
#include "macros.h"
#include "titan.h"
#include "config.h"

// --------------------------------------------------------------------------
// Built-ins. These are starting points, not gospel: the ones that walk the
// OSD are positional and depend on your firmware version. Verify each against
// docs/05-menu-mapping-worksheet.md before trusting it, then override it with
// "macdef <name> <script>" — user definitions win over built-ins.
// --------------------------------------------------------------------------
struct BuiltIn { const char *name; const char *desc; const char *script; };

static const BuiltIn BUILTINS[] = {
  { "movie",   "power on, HDMI1, Filmmaker, brightness 7",
    "p:on; d1500; s:hdmi1; d600; s:filmmaker; d400; s:b7; s:unblank" },

  { "bright",  "power on, HDMI1, Vivid, brightness 10 (daytime)",
    "p:on; d1500; s:hdmi1; d600; s:vivid; d400; s:b10; s:unblank" },

  { "game",    "HDMI2, Performance, high refresh extreme",
    "s:hdmi2; d600; s:perf; d400; s:hrrmax" },

  { "off",     "verified power down",
    "p:off" },

  { "anchortest", "prove the anchor works: unwind to root, open settings, leave",
    "s:back*3; d400; s:setting; d800; s:back*3" },

  // ---- templates for the deep features the serial set cannot reach ----
  // Fill in the counted paths from your §2 menu photographs, then save them
  // with macdef so they persist. Left deliberately short and obvious.
  { "tpl3d",   "TEMPLATE - 3D mode. Replace the counted path before use.",
    "anchor; d500; s:down*0; s:ok; d400; s:back*3" },

  { "tpllens", "TEMPLATE - lens memory. Replace the counted path before use.",
    "anchor; d500; s:down*0; s:ok; d400; s:back*3" },
};
static const size_t NBUILTINS = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

// --------------------------------------------------------------------------
// User macro store: one NVS string, "name\tscript\n" per line. Avoids the
// 15-character NVS key limit and keeps the whole set editable as one blob.
// --------------------------------------------------------------------------
static Preferences prefs;
static String userStore;

static void storeLoad() {
  prefs.begin("titan", true);
  userStore = prefs.getString("macros", "");
  prefs.end();
}

static void storeSave() {
  prefs.begin("titan", false);
  prefs.putString("macros", userStore);
  prefs.end();
}

static int storeFind(const char *name, int *lineEnd = NULL) {
  String key = String(name) + "\t";
  int i = 0;
  while (i < (int)userStore.length()) {
    int e = userStore.indexOf('\n', i);
    if (e < 0) e = userStore.length();
    if (userStore.substring(i, e).startsWith(key)) {
      if (lineEnd) *lineEnd = e;
      return i;
    }
    i = e + 1;
  }
  return -1;
}

String macroScript(const char *name) {
  int e, i = storeFind(name, &e);
  if (i >= 0) {
    int tab = userStore.indexOf('\t', i);
    return userStore.substring(tab + 1, e);
  }
  for (size_t k = 0; k < NBUILTINS; k++)
    if (!strcasecmp(name, BUILTINS[k].name)) return String(BUILTINS[k].script);
  return String();
}

bool macroDefine(const char *name, const char *script) {
  if (!name || !*name || !script || !*script) return false;
  if (strchr(name, '\t') || strchr(name, '\n')) return false;
  macroDelete(name);
  userStore += String(name) + "\t" + script + "\n";
  if (userStore.length() > 3500) { tlog("macro store full"); return false; }
  storeSave();
  tlog("macro '%s' saved", name);
  return true;
}

bool macroDelete(const char *name) {
  int e, i = storeFind(name, &e);
  if (i < 0) return false;
  userStore.remove(i, (e < (int)userStore.length() ? e + 1 : e) - i);
  storeSave();
  return true;
}

static void jsonEscapeInto(String &out, const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += ' ';
    else out += c;
  }
}

String macroListJson() {
  String out = "[";
  bool first = true;
  int i = 0;
  while (i < (int)userStore.length()) {
    int e = userStore.indexOf('\n', i); if (e < 0) e = userStore.length();
    int t = userStore.indexOf('\t', i);
    if (t > i && t < e) {
      if (!first) out += ',';
      first = false;
      out += "{\"name\":\""; jsonEscapeInto(out, userStore.substring(i, t));
      out += "\",\"user\":true,\"desc\":\"saved\",\"script\":\"";
      jsonEscapeInto(out, userStore.substring(t + 1, e));
      out += "\"}";
    }
    i = e + 1;
  }
  for (size_t k = 0; k < NBUILTINS; k++) {
    if (storeFind(BUILTINS[k].name) >= 0) continue;   // shadowed by a user macro
    if (!first) out += ',';
    first = false;
    out += "{\"name\":\""; out += BUILTINS[k].name;
    out += "\",\"user\":false,\"desc\":\""; jsonEscapeInto(out, String(BUILTINS[k].desc));
    out += "\",\"script\":\""; jsonEscapeInto(out, String(BUILTINS[k].script));
    out += "\"}";
  }
  out += ']';
  return out;
}

// --------------------------------------------------------------------------
// Compiled step list
// --------------------------------------------------------------------------
enum StepType { ST_SERIAL, ST_HID, ST_KEY, ST_RAW, ST_DELAY, ST_POWER };

struct Step {
  uint8_t type;
  char    arg[20];
  uint16_t ms;        // ST_DELAY only
};

static Step    steps[MACRO_MAX_STEPS];
static uint16_t nSteps = 0, curStep = 0;
static uint32_t nextAt = 0;
static bool     running = false;
static char     curName[24] = "";

bool  macroBusy()    { return running; }
const char *macroCurrent() { return running ? curName : ""; }

void macroAbort() {
  if (running) tlog("macro '%s' aborted at step %u", curName, curStep);
  running = false; nSteps = curStep = 0;
}

static bool pushStep(uint8_t type, const char *arg, uint16_t ms) {
  if (nSteps >= MACRO_MAX_STEPS) return false;
  steps[nSteps].type = type;
  steps[nSteps].ms   = ms;
  if (arg) { strncpy(steps[nSteps].arg, arg, sizeof(steps[0].arg) - 1);
             steps[nSteps].arg[sizeof(steps[0].arg) - 1] = 0; }
  else steps[nSteps].arg[0] = 0;
  nSteps++;
  return true;
}

static bool pushToken(const char *tok) {
  if (!*tok) return true;

  if (!strcasecmp(tok, "anchor")) {
    // Unwind whatever is on screen, then enter at a known root.
    // Navigation goes out on whichever channel this projector honours, so an
    // anchored macro keeps working if the answer turns out to be HID rather
    // than serial. See titanKey() and `keychan`.
    for (int i = 0; i < 3; i++) {
      pushStep(ST_KEY, "back", 0);
      pushStep(ST_DELAY, NULL, MACRO_ANCHOR_MS);
    }
    pushStep(ST_KEY, "setting", 0);
    pushStep(ST_DELAY, NULL, MACRO_ANCHOR_MS * 2);
    return true;
  }
  if (tok[0] == 'd' && isdigit((unsigned char)tok[1]))
    return pushStep(ST_DELAY, NULL, (uint16_t)atoi(tok + 1));
  if (!strncasecmp(tok, "s:", 2)) return pushStep(ST_SERIAL, tok + 2, 0);
  if (!strncasecmp(tok, "h:", 2)) return pushStep(ST_HID,    tok + 2, 0);
  if (!strncasecmp(tok, "k:", 2)) return pushStep(ST_KEY,    tok + 2, 0);
  if (!strncasecmp(tok, "r:", 2)) return pushStep(ST_RAW,    tok + 2, 0);
  if (!strncasecmp(tok, "p:", 2)) return pushStep(ST_POWER,  tok + 2, 0);

  // bare word = serial command, so "hdmi1" works as shorthand for "s:hdmi1"
  return pushStep(ST_SERIAL, tok, 0);
}

static bool compile(const char *script) {
  nSteps = curStep = 0;
  char tok[40];
  const char *p = script;
  while (*p) {
    while (*p == ' ' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (!*p) break;
    size_t n = 0;
    while (*p && *p != ';' && *p != '\n' && n < sizeof(tok) - 1) tok[n++] = *p++;
    while (n && tok[n - 1] == ' ') n--;
    tok[n] = 0;

    // trailing *<count>
    uint16_t rep = 1;
    char *star = strrchr(tok, '*');
    if (star && isdigit((unsigned char)star[1])) { rep = (uint16_t)atoi(star + 1); *star = 0; }
    if (rep > 60) rep = 60;

    for (uint16_t r = 0; r < rep; r++) {
      if (!pushToken(tok)) { tlog("macro too long (>%d steps)", MACRO_MAX_STEPS); return false; }
      if (r + 1 < rep) pushStep(ST_DELAY, NULL, MACRO_STEP_MS);
    }
  }
  return nSteps > 0;
}

bool macroRunScript(const char *script) {
  if (running) { tlog("macro busy: '%s'", curName); return false; }
  if (!compile(script)) return false;
  running = true; curStep = 0; nextAt = millis();
  tlog("macro '%s': %u steps", curName[0] ? curName : "(inline)", nSteps);
  return true;
}

bool macroRun(const char *name) {
  String s = macroScript(name);
  if (!s.length()) return false;
  strncpy(curName, name, sizeof(curName) - 1);
  curName[sizeof(curName) - 1] = 0;
  if (!macroRunScript(s.c_str())) { curName[0] = 0; return false; }
  return true;
}

void macrosLoop() {
  if (!running) return;
  if ((int32_t)(millis() - nextAt) < 0) return;

  if (curStep >= nSteps) {
    tlog("macro '%s' done", curName);
    running = false; curName[0] = 0;
    return;
  }

  Step &s = steps[curStep++];
  uint32_t gap = MACRO_STEP_MS;

  switch (s.type) {
    case ST_DELAY:  gap = s.ms; break;
    case ST_SERIAL: if (!titanSendNamed(s.arg)) tlog("macro: unknown command '%s'", s.arg); break;
    case ST_HID:    if (!titanHid(s.arg))       tlog("macro: unknown HID key '%s'", s.arg); break;
    case ST_KEY:    if (!titanKey(s.arg))       tlog("macro: key '%s' failed on the %s channel",
                                                     s.arg, titanKeyChannelStr()); break;
    case ST_RAW:    titanSendRawHex(s.arg); break;
    case ST_POWER:
      if (!strcasecmp(s.arg, "on"))       titanPowerOn();
      else if (!strcasecmp(s.arg, "off")) titanPowerOff();
      else                                titanPowerToggle();
      gap = 500;
      break;
  }
  nextAt = millis() + gap;
}

void macrosBegin() {
  storeLoad();
  int n = 0;
  for (int i = 0; i < (int)userStore.length(); i++) if (userStore[i] == '\n') n++;
  tlog("macros: %u built-in, %d saved", (unsigned)NBUILTINS, n);
}
