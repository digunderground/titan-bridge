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
    "k:back*3; d400; k:setting; d800; k:back*3" },

  // ---- templates for the deep features the serial set cannot reach ----
  // Fill in the counted paths from your §2 menu photographs, then save them
  // with macdef so they persist. Left deliberately short and obvious.
  { "tpl3d",   "TEMPLATE - 3D mode. Replace the counted path before use.",
    "anchor; d500; k:down*0; k:ok; d400; k:back*3" },

  { "tpllens", "TEMPLATE - lens memory. Replace the counted path before use.",
    "anchor; d500; k:down*0; k:ok; d400; k:back*3" },
};
static const size_t NBUILTINS = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

// --------------------------------------------------------------------------
// User macro store: one NVS string, "name\tscript\n" per line. Avoids the
// 15-character NVS key limit and keeps the whole set editable as one blob.
// --------------------------------------------------------------------------
static Preferences prefs;
static String userStore;

static void buttonsLoad();   // defined with the button store, below
static bool compileAppend(const char *script);   // defined below pushToken

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

// Lines are "name\tgroup\tscript". Records written before groups existed are
// "name\tscript" and are still read correctly — an old store is not a
// migration, just a group of "".
static void splitLine(const String &line, String &name, String &group, String &script) {
  int t1 = line.indexOf('\t');
  if (t1 < 0) { name = line; group = ""; script = ""; return; }
  name = line.substring(0, t1);
  int t2 = line.indexOf('\t', t1 + 1);
  if (t2 < 0) { group = ""; script = line.substring(t1 + 1); return; }
  group  = line.substring(t1 + 1, t2);
  script = line.substring(t2 + 1);
}

String macroScript(const char *name) {
  int e, i = storeFind(name, &e);
  if (i >= 0) {
    String n, g, sc; splitLine(userStore.substring(i, e), n, g, sc);
    return sc;
  }
  for (size_t k = 0; k < NBUILTINS; k++)
    if (!strcasecmp(name, BUILTINS[k].name)) return String(BUILTINS[k].script);
  return String();
}

bool macroDefine(const char *name, const char *script) {
  return macroDefineIn(name, "", script);
}

String macroGroup(const char *name) {
  int e, i = storeFind(name, &e);
  if (i < 0) return String();
  String n, g, sc; splitLine(userStore.substring(i, e), n, g, sc);
  return g;
}

bool macroDefineIn(const char *name, const char *group, const char *script) {
  if (!name || !*name || !script || !*script) return false;
  if (strchr(name, '\t') || strchr(name, '\n')) return false;
  if (group && (strchr(group, '\t') || strchr(group, '\n'))) return false;
  String keep = userStore;
  macroDelete(name);
  userStore += String(name) + "\t" + (group ? group : "") + "\t" + script + "\n";
  if (userStore.length() > MACRO_STORE_MAX) {
    userStore = keep;                      // put it back rather than truncate
    tlog("macro store full (%u bytes) — '%s' NOT saved",
         (unsigned)userStore.length(), name);
    return false;
  }
  storeSave();
  tlog("macro '%s' saved%s%s", name, (group && *group) ? " in group " : "",
       (group && *group) ? group : "");
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
      String n, g, sc; splitLine(userStore.substring(i, e), n, g, sc);
      out += "{\"name\":\""; jsonEscapeInto(out, n);
      out += "\",\"group\":\""; jsonEscapeInto(out, g);
      out += "\",\"user\":true,\"desc\":\"saved\",\"script\":\"";
      jsonEscapeInto(out, sc);
      out += "\"}";
    }
    i = e + 1;
  }
  for (size_t k = 0; k < NBUILTINS; k++) {
    if (storeFind(BUILTINS[k].name) >= 0) continue;   // shadowed by a user macro
    if (!first) out += ',';
    first = false;
    out += "{\"name\":\""; out += BUILTINS[k].name;
    out += "\",\"group\":\"built-in\",\"user\":false,\"desc\":\""; jsonEscapeInto(out, String(BUILTINS[k].desc));
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
  // m:<name> inlines another macro, so a button can be bound to one by name
  // and grouped macros can share a common opening. One level only — deeper
  // nesting is how you get a script that never terminates.
  if (!strncasecmp(tok, "m:", 2)) {
    static bool inlining = false;
    if (inlining) { tlog("macro: refusing nested m:%s", tok + 2); return false; }
    String sub = macroScript(tok + 2);
    if (!sub.length()) { tlog("macro: no such macro '%s'", tok + 2); return false; }
    inlining = true;
    bool ok = compileAppend(sub.c_str());
    inlining = false;
    return ok;
  }
  if (!strncasecmp(tok, "r:", 2)) return pushStep(ST_RAW,    tok + 2, 0);
  if (!strncasecmp(tok, "p:", 2)) return pushStep(ST_POWER,  tok + 2, 0);

  // bare word = serial command, so "hdmi1" works as shorthand for "s:hdmi1"
  return pushStep(ST_SERIAL, tok, 0);
}

// Appends to the step list already under construction. compile() resets first;
// m: calls this directly so an inlined macro extends the caller's script
// instead of wiping it.
static bool compileAppend(const char *script) {
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
  return true;
}

static bool compile(const char *script) {
  nSteps = curStep = 0;
  if (!compileAppend(script)) return false;
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
  buttonsLoad();
  int n = 0;
  for (int i = 0; i < (int)userStore.length(); i++) if (userStore[i] == '\n') n++;
  tlog("macros: %u built-in, %d saved", (unsigned)NBUILTINS, n);
}


// ===========================================================================
// Recorder
//
// Counting keypresses off photographs works, but doing it by hand is the
// tedious part of this project. Recording captures what you actually pressed
// *and how long you waited* — which also settles MACRO_STEP_MS empirically
// instead of by guessing, because the gaps are the ones the OSD kept up with.
// ===========================================================================

struct RecStep { String tok; uint16_t gap; };
static RecStep  recBuf[MACRO_MAX_STEPS];
static uint16_t recN = 0;
static bool     recOn = false;
static uint32_t recLast = 0;

// titanKey() records the channel-neutral "k:" form itself, then dispatches to
// titanHid()/titanSendNamed(), which would each record their own token. One
// press should be one step, so the inner capture is suppressed for that call.
static bool recSuppress = false;
void recSuppressNext()  { recSuppress = true; }
void recSuppressClear() { recSuppress = false; }

bool recActive() { return recOn; }
uint16_t recCount() { return recN; }

void recStart() { recOn = true;  recLast = millis(); tlog("recording started"); }
void recStop()  { recOn = false; tlog("recording stopped, %u step(s)", recN); }
void recClear() { recN = 0; recLast = millis(); tlog("recording cleared"); }

void recCapture(const char *tok) {
  if (recSuppress) { recSuppress = false; return; }
  // A running macro drives the same dispatch points; capturing those would
  // record the macro playing itself back.
  if (!recOn || macroBusy() || recN >= MACRO_MAX_STEPS) return;
  uint32_t now = millis();
  uint32_t gap = now - recLast;
  recLast = now;
  if (gap > 10000) gap = 10000;            // a coffee break is not a delay
  recBuf[recN].tok = tok;
  recBuf[recN].gap = (uint16_t)((recN == 0) ? 0 : ((gap + 5) / 10) * 10);
  recN++;
}

bool recDeleteStep(uint16_t idx) {
  if (idx >= recN) return false;
  for (uint16_t i = idx; i + 1 < recN; i++) recBuf[i] = recBuf[i + 1];
  recN--;
  return true;
}

bool recInsertStep(uint16_t idx, const char *tok, uint16_t gap) {
  if (recN >= MACRO_MAX_STEPS || idx > recN) return false;
  for (uint16_t i = recN; i > idx; i--) recBuf[i] = recBuf[i - 1];
  recBuf[idx].tok = tok;
  recBuf[idx].gap = gap;
  recN++;
  return true;
}

String recJson() {
  String out = "{\"recording\":"; out += recOn ? "true" : "false";
  out += ",\"steps\":[";
  for (uint16_t i = 0; i < recN; i++) {
    if (i) out += ',';
    out += "{\"tok\":\""; jsonEscapeInto(out, recBuf[i].tok);
    out += "\",\"gap\":"; out += recBuf[i].gap; out += '}';
  }
  out += "]}";
  return out;
}

String recScript() {
  String sc;
  for (uint16_t i = 0; i < recN; i++) {
    if (i) {
      sc += "; ";
      if (recBuf[i].gap >= 20) { sc += 'd'; sc += recBuf[i].gap; sc += "; "; }
    }
    sc += recBuf[i].tok;
  }
  return sc;
}

bool recSaveAs(const char *name, const char *group) {
  if (!recN) return false;
  String sc = recScript();
  return macroDefineIn(name, group ? group : "", sc.c_str());
}

// ===========================================================================
// Button assignments — the virtual remote's buttons are data, not code.
// ===========================================================================

static String buttonStore;

static void buttonsLoad() {
  prefs.begin("titan", true);
  buttonStore = prefs.getString("buttons", "");
  prefs.end();
}
static void buttonsSave() {
  prefs.begin("titan", false);
  prefs.putString("buttons", buttonStore);
  prefs.end();
}

static int buttonFind(const char *id, int *lineEnd = NULL) {
  String key = String(id) + "\t";
  int i = 0;
  while (i < (int)buttonStore.length()) {
    int e = buttonStore.indexOf('\n', i);
    if (e < 0) e = buttonStore.length();
    if (buttonStore.substring(i, e).startsWith(key)) {
      if (lineEnd) *lineEnd = e;
      return i;
    }
    i = e + 1;
  }
  return -1;
}

bool buttonClear(const char *id) {
  int e, i = buttonFind(id, &e);
  if (i < 0) return false;
  buttonStore.remove(i, (e < (int)buttonStore.length() ? e + 1 : e) - i);
  buttonsSave();
  return true;
}

bool buttonSet(const char *id, const char *label, const char *action) {
  if (!id || !*id) return false;
  if (strchr(id, '\t') || strchr(id, '\n')) return false;
  buttonClear(id);
  if (!action || !*action) { buttonsSave(); return true; }   // cleared
  String keep = buttonStore;
  buttonStore += String(id) + "\t" + (label ? label : "") + "\t" + action + "\n";
  if (buttonStore.length() > BUTTON_STORE_MAX) {
    buttonStore = keep;
    tlog("button store full — '%s' NOT saved", id);
    return false;
  }
  buttonsSave();
  return true;
}

String buttonAction(const char *id) {
  int e, i = buttonFind(id, &e);
  if (i < 0) return String();
  String bid, label, action;
  splitLine(buttonStore.substring(i, e), bid, label, action);
  return action;
}

String buttonsJson() {
  String out = "[";
  bool first = true;
  int i = 0;
  while (i < (int)buttonStore.length()) {
    int e = buttonStore.indexOf('\n', i); if (e < 0) e = buttonStore.length();
    String id, label, action;
    splitLine(buttonStore.substring(i, e), id, label, action);
    if (id.length()) {
      if (!first) out += ',';
      first = false;
      out += "{\"id\":\""; jsonEscapeInto(out, id);
      out += "\",\"label\":\""; jsonEscapeInto(out, label);
      out += "\",\"action\":\""; jsonEscapeInto(out, action);
      out += "\"}";
    }
    i = e + 1;
  }
  out += ']';
  return out;
}


// ===========================================================================
// Roku "apps" — how anything beyond the fixed ECP key map reaches the hub.
//
// The SofaBaton's Roku profile gives a fixed button layout, so a macro has no
// key to live on. But Roku also has apps, and a hub can launch one by id. Each
// user macro is therefore published as an app, which turns "run my 3D macro"
// into something the hub can actually send.
// ===========================================================================

#define MACRO_APP_BASE 100

String macroNameByIndex(int idx) {
  int i = 0, n = 0;
  while (i < (int)userStore.length()) {
    int e = userStore.indexOf('\n', i); if (e < 0) e = userStore.length();
    String nm, g, sc; splitLine(userStore.substring(i, e), nm, g, sc);
    if (nm.length()) {
      if (n == idx) return nm;
      n++;
    }
    i = e + 1;
  }
  return String();
}

String macroAppsXml() {
  String x;
  int i = 0, n = 0;
  while (i < (int)userStore.length()) {
    int e = userStore.indexOf('\n', i); if (e < 0) e = userStore.length();
    String nm, g, sc; splitLine(userStore.substring(i, e), nm, g, sc);
    if (nm.length()) {
      x += "<app id=\""; x += (MACRO_APP_BASE + n);
      x += "\" type=\"appl\" version=\"1.0.0\">";
      // XML text, so the few characters that would break the document.
      for (size_t c = 0; c < nm.length(); c++) {
        char ch = nm[c];
        if      (ch == '&') x += "&amp;";
        else if (ch == '<') x += "&lt;";
        else if (ch == '>') x += "&gt;";
        else x += ch;
      }
      x += "</app>";
      n++;
    }
    i = e + 1;
  }
  return x;
}

bool macroRunAppId(int id) {
  if (id < MACRO_APP_BASE) return false;
  String nm = macroNameByIndex(id - MACRO_APP_BASE);
  if (!nm.length()) return false;
  return macroRun(nm.c_str());
}
