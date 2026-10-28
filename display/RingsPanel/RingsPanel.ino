// DaisyRings touchscreen: ESP32 "CYD" 2.8" 240x320 resistive display.
// Talks to the Pod over UART (GPIO35 RX, GPIO27 TX), same wiring as PodPlaits.
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "Shared.h"

HardwareSerial podLink(2);
TFT_eSPI tft; SPIClass touchSpi(VSPI); XPT2046_Touchscreen touch(33, 36); Preferences preferences;
float ax = 0, bx = 0, cx = 0, ay = 0, by = 0, cy = 0;

const uint16_t bg = 0x0000, panel = 0x18E3, mint = 0x7DB7, ink = 0xBDF7, dim = 0x7BEF;
enum { P_PLAY, P_SOUND, P_GEN, P_COMP, P_MIX, P_MOODS, NPAGES };
const uint16_t accents[NPAGES] = {0x4DF6, 0xFCAA, 0xB37F, 0x8E51, 0xFBAE, 0xFC8D};
const char* const tabNames[NPAGES] = {"PLAY", "SOUND", "GEN", "COMP", "MIX", "MOODS"};
const char* const labels[NVAL] = {"STRUCTURE", "BRIGHTNESS", "DAMPING", "POSITION", "ROOT NOTE", "RATE", "DENSITY", "RANGE", "DRIFT", "VOLUME"};
const char* const compLabels[NCOMP] = {"RHYTHM", "SECTIONS", "STEPS", "PULSES", "ROTATE", "TEMPO", "SECTION BARS", "CHORD BARS", "SEED"};
const char* const knobPairNames[4] = {"STRUCT/BRIGHT", "DAMP/POS", "RATE/DENSITY", "DRIFT/VOL"};

int values[NVAL] = {520, 780, 820, 400, 400, 280, 950, 400, 250, 600};
int comp[NCOMP] = {0, 0, 16, 5, 0, 100, 4, 2, 1234};
int engine = 0, polyIndex = 2, genMode = 5, scaleIndex = 2, external = 0, knobPair = 0, section = 0, chordNow = 0;
bool genRun = false, linked = false;
int lastNote = 48, cpu = 0; unsigned strumCount = 0, overruns = 0;
int page = P_PLAY;
uint32_t lastState = 0;
int seedHistory[16], seedDepth = 0; bool goingBack = false;

// ---- sliders: index into values[] (comp=false) or comp[] (comp=true) ----
struct Slider { int v, x, y, w; bool comp; };
const Slider soundSliders[4] = {{V_STRUCT, 10, 60, 300, false}, {V_BRIGHT, 10, 96, 300, false}, {V_DAMP, 10, 132, 300, false}, {V_POS, 10, 168, 300, false}};
const Slider genSliders[4] = {{V_RATE, 10, 100, 145, false}, {V_CHANCE, 165, 100, 145, false}, {V_RANGE, 10, 152, 145, false}, {V_DRIFT, 165, 152, 145, false}};
const Slider compSliders[6] = {{C_TEMPO, 10, 92, 145, true}, {C_SECBARS, 165, 92, 145, true}, {C_STEPS, 10, 126, 145, true}, {C_PULSES, 165, 126, 145, true},
                               {C_ROT, 10, 160, 145, true}, {C_CHORDBARS, 165, 160, 145, true}};
const Slider mixSliders[2] = {{V_ROOT, 10, 60, 300, false}, {V_VOL, 10, 96, 300, false}};
const Slider* pageSliders(int p, int& n) {
  if (p == P_SOUND) { n = 4; return soundSliders; }
  if (p == P_GEN) { n = 4; return genSliders; }
  if (p == P_COMP) { n = 6; return compSliders; }
  if (p == P_MIX) { n = 2; return mixSliders; }
  n = 0; return nullptr;
}

uint16_t blend(uint16_t a, uint16_t b, int t) {  // t 0..255: a -> b
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bgc = (b >> 5) & 63, bb = b & 31;
  return ((ar + (br - ar) * t / 255) << 11) | ((ag + (bgc - ag) * t / 255) << 5) | (ab + (bb - ab) * t / 255);
}
void label(const char* s, int x, int y, int font = 2, uint16_t color = ink) { tft.setTextColor(color, bg); tft.drawString(s, x, y, font); }
void button(int x, int y, int w, int h, const char* text, uint16_t color, bool filled = false) {
  tft.fillRoundRect(x, y, w, h, 5, filled ? color : panel);
  if (!filled) tft.drawRoundRect(x, y, w, h, 5, color);
  tft.setTextColor(filled ? bg : color, filled ? color : panel);
  tft.drawCentreString(text, x + w / 2, y + h / 2 - 7, 2);
}

float sliderFraction(const Slider& s) {
  if (!s.comp) return values[s.v] / 1000.f;
  return float(comp[s.v] - kCompMin[s.v]) / (kCompMax[s.v] - kCompMin[s.v]);
}
void valueText(const Slider& s, char* t, size_t n) {
  static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (s.comp) { if (s.v == C_TEMPO) snprintf(t, n, "%d bpm", comp[s.v]); else snprintf(t, n, "%d", comp[s.v]); return; }
  int i = s.v;
  if (i == V_ROOT) { int m = rootNote(values[i]); snprintf(t, n, "%s%d", notes[m % 12], m / 12 - 1); }
  else if (i == V_RATE) snprintf(t, n, "%.1f/s", .1f * powf(100.f, values[i] / 1000.f));
  else if (i == V_RANGE) snprintf(t, n, "%d oct", rangeOctaves(values[i]));
  else snprintf(t, n, "%d%%", values[i] / 10);
}
void drawSlider(const Slider& s) {
  tft.fillRect(s.x, s.y, s.w, 34, bg);
  uint16_t c = accents[page];
  label(s.comp ? compLabels[s.v] : labels[s.v], s.x + 4, s.y, 2, c);
  char t[16]; valueText(s, t, sizeof(t)); tft.setTextColor(c, bg); tft.drawRightString(t, s.x + s.w - 4, s.y, 2);
  int bw = s.w - 12;
  tft.fillRoundRect(s.x + 6, s.y + 22, bw, 9, 4, panel);
  tft.fillRoundRect(s.x + 6, s.y + 22, max(9, int(sliderFraction(s) * bw)), 9, 4, c);
}

// ---- play pad ----
const int padY = 116, padH = 89;
uint8_t flash[28];
int heldColumn = -1;
int padColumns() { return kScaleLen[scaleIndex] * padOctaves(scaleIndex); }
int padColumnW() { return 300 / padColumns(); }
void drawKey(int c) {
  int w = padColumnW(), x = 10 + c * w;
  bool isRoot = c % kScaleLen[scaleIndex] == 0;
  uint16_t base = isRoot ? 0x2945 : panel;
  tft.fillRoundRect(x + 1, padY, w - 2, padH, 4, blend(base, accents[0], flash[c]));
  if (isRoot) tft.drawRoundRect(x + 1, padY, w - 2, padH, 4, accents[0]);
}
void drawPad() { tft.fillRect(0, padY - 2, 320, padH + 4, bg); for (int c = 0; c < padColumns(); c++) drawKey(c); }
void drawEngine() {
  tft.fillRect(0, 58, 320, 28, bg);
  button(10, 58, 40, 26, "<", accents[0]); button(270, 58, 40, 26, ">", accents[0]);
  tft.setTextColor(accents[0], bg); tft.drawCentreString(kEngineNames[engine], 160, 63, 2);
}
void drawIdea() {
  tft.fillRect(0, 87, 320, 27, bg);
  button(10, 88, 60, 24, "BACK", accents[0]);
  char t[32]; snprintf(t, sizeof(t), "NEW IDEA   #%04d", comp[C_SEED]);
  button(80, 88, 230, 24, t, accents[0], true);
}
int noteColumn(int note) {
  int root = rootNote(values[V_ROOT]), n = padColumns();
  int lo = padNote(root, scaleIndex, 0), hi = padNote(root, scaleIndex, n - 1);
  while (note < lo) note += 12;
  while (note > hi) note -= 12;
  for (int c = 0; c < n; c++) if (padNote(root, scaleIndex, c) == note) return c;
  return -1;
}

// ---- other widgets ----
void drawHeader() {
  tft.fillRect(0, 0, 320, 23, bg);
  label("DAISY RINGS", 14, 5, 1, accents[page]);
  if (linked) {
    char t[48]; snprintf(t, sizeof(t), "KNOBS: %s", knobPairNames[knobPair]); label(t, 150, 5, 1, mint);
    if (cpu > 85 || overruns > 50) { snprintf(t, sizeof(t), "CPU %d%%", cpu); label(t, 100, 5, 1, accents[1]); }
  } else label("WAITING FOR POD", 200, 5, 1, dim);
}
void drawRun() {
  button(14, 208, 292, 30, genRun ? "STOP GENERATIVE" : "START GENERATIVE", genRun ? panel : mint, !genRun);
  if (genRun) { tft.drawRoundRect(14, 208, 292, 30, 6, mint); tft.setTextColor(mint, panel); tft.drawCentreString("STOP GENERATIVE", 160, 216, 2); }
}
void drawGenButtons() {
  tft.fillRect(0, 58, 320, 38, bg);
  button(10, 60, 145, 30, kGenNames[genMode], accents[2]); button(165, 60, 145, 30, kScaleNames[scaleIndex], accents[2]);
}
const char* const romans[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
void drawCompStatus() {
  tft.fillRect(0, 196, 320, 11, bg);
  char t[64];
  if (genMode == 5) snprintf(t, sizeof(t), "Composer: chord %s   section %s", chordNow < 7 ? romans[chordNow] : "-", comp[C_SECT] ? kSectionNames[section] : "off");
  else snprintf(t, sizeof(t), "Pick the COMPOSER pattern on GEN for melodies");
  label(t, 14, 197, 1, dim);
}
void drawCompButtons() {
  tft.fillRect(0, 56, 320, 34, bg);
  button(10, 58, 145, 28, comp[C_RHYTHM] ? "RHYTHM: EUCLID" : "RHYTHM: FREE", accents[3], comp[C_RHYTHM]);
  button(165, 58, 145, 28, comp[C_SECT] ? "SECTIONS: ON" : "SECTIONS: OFF", accents[3], comp[C_SECT]);
}
void drawMixButtons() {
  tft.fillRect(0, 134, 320, 72, bg);
  const char* p[3] = {"1 VOICE", "2 VOICES", "4 VOICES"};
  for (int i = 0; i < 3; i++) button(10 + i * 102, 136, 96, 30, p[i], accents[4], polyIndex == i);
  button(10, 172, 145, 30, external ? "EXCITER: LINE IN" : "EXCITER: INTERNAL", accents[4]);
  button(165, 172, 145, 30, "RANDOMIZE", accents[1]);
}
bool sameComp(const int* c) { for (int i = 0; i < NCOMP; i++) if (i != C_SEED && c[i] != comp[i]) return false; return true; }
int matchingPreset() {
  for (int m = 0; m < kPresets; m++) {
    const Preset& p = presets[m];
    bool match = engine == p.engine && polyIndex == p.poly && genMode == p.gen && scaleIndex == p.scale && external == p.external && sameComp(p.comp);
    for (int i = 0; i < NVAL; i++) if (values[i] != p.values[i]) match = false;
    if (match) return m;
  }
  return -1;
}
void drawPresets() {
  tft.fillRect(0, 56, 320, 149, bg); int active = matchingPreset();
  for (int i = 0; i < kPresets; i++) {
    int x = 10 + (i % 2) * 155, y = 60 + (i / 2) * 48;
    tft.fillRoundRect(x, y, 145, 43, 5, panel);
    if (i == active) tft.drawRoundRect(x, y, 145, 43, 5, accents[5]);
    tft.setTextColor(accents[5], panel); tft.drawString(presets[i].name, x + 7, y + 5, 2);
    tft.setTextColor(ink, panel); tft.drawString(presets[i].description, x + 7, y + 28, 1);
  }
}
const int tabW = 53;
void draw() {
  tft.fillScreen(bg); drawHeader();
  for (int i = 0; i < NPAGES; i++) {
    int x = 2 + i * tabW;
    tft.fillRoundRect(x, 25, tabW - 2, 28, 5, i == page ? panel : bg);
    if (i == page) tft.drawRoundRect(x, 25, tabW - 2, 28, 5, accents[i]); else tft.drawFastHLine(x + 4, 51, tabW - 10, accents[i]);
    tft.setTextColor(accents[i], i == page ? panel : bg); tft.drawCentreString(tabNames[i], x + tabW / 2 - 1, 31, 2);
  }
  if (page == P_PLAY) { drawEngine(); drawIdea(); drawPad(); }
  else if (page == P_MOODS) drawPresets();
  else {
    if (page == P_GEN) drawGenButtons();
    if (page == P_COMP) { drawCompButtons(); drawCompStatus(); }
    if (page == P_MIX) drawMixButtons();
    int n; const Slider* s = pageSliders(page, n); for (int i = 0; i < n; i++) drawSlider(s[i]);
  }
  drawRun();
}

void calibrate() {
  float rx[3], ry[3]; const float sx[3] = {28, 292, 160}, sy[3] = {28, 28, 212};
  for (int i = 0; i < 3; i++) {
    tft.fillScreen(bg); label("TOUCH AND HOLD EACH CROSS", 40, 100, 2);
    tft.drawLine(sx[i] - 10, sy[i], sx[i] + 10, sy[i], mint); tft.drawLine(sx[i], sy[i] - 10, sx[i], sy[i] + 10, mint);
    while (touch.touched()) delay(10);
    unsigned n = 0; float x = 0, y = 0;
    while (n < 24) { if (touch.touched()) { auto p = touch.getPoint(); if (p.z > 200) { x += p.x; y += p.y; n++; } } delay(12); }
    rx[i] = x / n; ry[i] = y / n; while (touch.touched()) delay(10);
  }
  float det = rx[0] * (ry[1] - ry[2]) + rx[1] * (ry[2] - ry[0]) + rx[2] * (ry[0] - ry[1]);
  if (fabsf(det) < 1000) { calibrate(); return; }
  auto solve = [&](const float* v, float& a, float& b, float& c) {
    a = (v[0] * (ry[1] - ry[2]) + v[1] * (ry[2] - ry[0]) + v[2] * (ry[0] - ry[1])) / det;
    b = (v[0] * (rx[2] - rx[1]) + v[1] * (rx[0] - rx[2]) + v[2] * (rx[1] - rx[0])) / det;
    c = v[0] - a * rx[0] - b * ry[0];
  };
  solve(sx, ax, bx, cx); solve(sy, ay, by, cy);
  float calibration[] = {ax, bx, cx, ay, by, cy}; preferences.putBytes("cal", calibration, sizeof(calibration));
}

void setup() {
  Serial.begin(115200);
  podLink.setRxBufferSize(1024); podLink.begin(115200, SERIAL_8N1, 35, 27);
  pinMode(21, OUTPUT); digitalWrite(21, HIGH);
  for (uint8_t pin : {22, 16, 17}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }  // rear LED off
  tft.init(); tft.setRotation(1); tft.invertDisplay(true);
  touchSpi.begin(25, 39, 32, 33); touch.begin(touchSpi); touch.setRotation(1);
  preferences.begin("rings", false);
  float c[6]; bool valid = preferences.getBytesLength("cal") == sizeof(c);
  if (valid) { preferences.getBytes("cal", c, sizeof(c)); ax = c[0]; bx = c[1]; cx = c[2]; ay = c[3]; by = c[4]; cy = c[5]; }
  delay(500); if (!valid || touch.touched()) calibrate();
  draw();
}

// ---- state from the Pod ----
void redrawSlidersFor(bool isComp, int index) {
  int n; const Slider* s = pageSliders(page, n);
  for (int k = 0; k < n; k++) if (s[k].comp == isComp && s[k].v == index) drawSlider(s[k]);
}
void applyState(int e, int pi, int g, int sc, int ex, int run, int note, const int* v, unsigned strums, int kp, int cpuPeak, unsigned over) {
  bool full = false;
  if (e != engine) { engine = e; if (page == P_PLAY) drawEngine(); }
  if (sc != scaleIndex) { scaleIndex = sc; if (page == P_PLAY) drawPad(); if (page == P_GEN) drawGenButtons(); }
  if (g != genMode) { genMode = g; if (page == P_GEN) drawGenButtons(); if (page == P_COMP) drawCompStatus(); }
  if (pi != polyIndex || ex != external) { polyIndex = pi; external = ex; if (page == P_MIX) drawMixButtons(); }
  if (bool(run) != genRun) { genRun = run; drawRun(); }
  if (kp != knobPair) { knobPair = kp; drawHeader(); }
  for (int i = 0; i < 8; i++) if (values[i] != v[i]) { values[i] = v[i]; redrawSlidersFor(false, i); if (i == V_ROOT && page == P_PLAY) full = true; }
  if (full) drawPad();
  if (strums != strumCount) { strumCount = strums; int c = page == P_PLAY ? noteColumn(note) : -1; if (c >= 0) { flash[c] = 255; drawKey(c); } }
  lastNote = note;
  static uint32_t lastHdr = 0; bool hdr = (cpuPeak > 85) != (cpu > 85); cpu = cpuPeak; overruns = over;
  if (hdr && millis() - lastHdr > 500) { lastHdr = millis(); drawHeader(); }
  if (page == P_MOODS) { static int lastPreset = -2; int a = matchingPreset(); if (a != lastPreset) { lastPreset = a; drawPresets(); } }
}
void parseLine(char* line) {
  int e, pi, g, sc, ex, run, note, lvl, v[8], kp, cpuPeak; unsigned strums, over;
  if (sscanf(line, "RSTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %u %d %d %u", &e, &pi, &g, &sc, &ex, &run, &note, &lvl,
             &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &strums, &kp, &cpuPeak, &over) == 20) {
    bool ok = e >= 0 && e < kEngines && pi >= 0 && pi < 3 && g >= 0 && g < kGenModes && sc >= 0 && sc < kScales && kp >= 0 && kp < 4;
    for (int i = 0; i < 8; i++) if (v[i] < 0 || v[i] > 1000) ok = false;
    if (!ok) return;
    if (!linked) { linked = true; drawHeader(); }
    lastState = millis();
    applyState(e, pi, g, sc, ex, run, note, v, strums, kp, cpuPeak, over);
  } else if (!strncmp(line, "RSTATE2 ", 8)) {
    int a, b;
    if (sscanf(line, "RSTATE2 %d %d", &a, &b) == 2 && a >= 0 && a <= 1000 && b >= 0 && b <= 1000) {
      int nv[2] = {a, b};
      for (int i = 0; i < 2; i++) if (values[8 + i] != nv[i]) { values[8 + i] = nv[i]; redrawSlidersFor(false, 8 + i); }
    }
  } else if (!strncmp(line, "RSTATE3 ", 8)) {
    int c[NCOMP], sec, ch;
    if (sscanf(line, "RSTATE3 %d %d %d %d %d %d %d %d %d %d %d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8], &sec, &ch) == 11) {
      bool ok = sec >= 0 && sec < 4 && ch >= 0 && ch < 12;
      for (int i = 0; i < NCOMP; i++) if (c[i] < kCompMin[i] || c[i] > kCompMax[i]) ok = false;
      if (!ok) return;
      bool statusChanged = sec != section || ch != chordNow; section = sec; chordNow = ch;
      for (int i = 0; i < NCOMP; i++) if (comp[i] != c[i]) {
        if (i == C_SEED) {
          if (!goingBack && seedDepth < 16) seedHistory[seedDepth++] = comp[i];
          else if (!goingBack) { memmove(seedHistory, seedHistory + 1, 15 * sizeof(int)); seedHistory[15] = comp[i]; }
          goingBack = false;
        }
        comp[i] = c[i]; redrawSlidersFor(true, i);
        if (i == C_SEED && page == P_PLAY) drawIdea();
        if ((i == C_RHYTHM || i == C_SECT) && page == P_COMP) drawCompButtons();
      }
      if (statusChanged && page == P_COMP) drawCompStatus();
    }
  }
}

void send(const char* fmt, int a) { podLink.printf(fmt, a); }
void sendSet(int i, int v) { values[i] = v; podLink.printf("SET %d %d\n", i, v); }
void sendComp(int i, int v) { comp[i] = v; podLink.printf("C %d %d\n", i, v); }

void handleTouch(int x, int y, bool fresh) {
  if (fresh && y >= 25 && y < 54 && x >= 2 && x < 320) { int next = constrain((x - 2) / tabW, 0, NPAGES - 1); if (next != page) { page = next; draw(); } return; }
  if (!linked) return;
  if (fresh && y >= 208) { send("RUN %d\n", genRun ? 0 : 1); return; }
  static uint32_t lastBright = 0;
  if (page == P_PLAY) {
    if (fresh && y >= 58 && y < 86) { if (x < 54) send("ENGINE %d\n", (engine + kEngines - 1) % kEngines); else if (x > 266) send("ENGINE %d\n", (engine + 1) % kEngines); return; }
    if (fresh && y >= 88 && y < 114) {
      if (x < 74) { if (seedDepth > 0) { goingBack = true; sendComp(C_SEED, seedHistory[--seedDepth]); } }
      else podLink.print("NEWSEED\n");
      return;
    }
    if (y >= padY && y < padY + padH && x >= 10 && x < 310) {
      int c = constrain((x - 10) / padColumnW(), 0, padColumns() - 1);
      if (c != heldColumn) { heldColumn = c; send("NOTE %d\n", padNote(rootNote(values[V_ROOT]), scaleIndex, c)); flash[c] = 255; drawKey(c); }
      if (millis() - lastBright > 60) { lastBright = millis(); sendSet(V_BRIGHT, 350 + constrain(padH - (y - padY), 0, padH) * 650 / padH); }
    }
    return;
  }
  if (page == P_GEN && fresh && y >= 60 && y < 90) { if (x < 160) send("GEN %d\n", (genMode + 1) % kGenModes); else send("SCALE %d\n", (scaleIndex + 1) % kScales); return; }
  if (page == P_COMP && fresh && y >= 58 && y < 88) { if (x < 160) sendComp(C_RHYTHM, comp[C_RHYTHM] ? 0 : 1); else sendComp(C_SECT, comp[C_SECT] ? 0 : 1); return; }
  if (page == P_MIX && fresh) {
    if (y >= 136 && y < 166) { send("POLY %d\n", constrain((x - 10) / 102, 0, 2)); return; }
    if (y >= 172 && y < 202) { if (x < 160) send("EXT %d\n", external ? 0 : 1); else podLink.print("RANDOM\n"); return; }
  }
  if (page == P_MOODS && fresh && y >= 60 && y < 199) {
    int col = x >= 165 ? 1 : 0, row = (y - 60) / 48, i = row * 2 + col;
    if (x >= 10 && x < 310 && (x - 10) % 155 < 145 && (y - 60) % 48 < 43 && i < kPresets) send("PRESET %d\n", i);
    return;
  }
  int n; const Slider* s = pageSliders(page, n);
  static uint32_t lastSet = 0;
  for (int i = 0; i < n; i++) if (x >= s[i].x && x < s[i].x + s[i].w && y >= s[i].y && y < s[i].y + 34) {
    if (millis() - lastSet < 40) return; lastSet = millis();
    int bw = s[i].w - 12, px = constrain(x - s[i].x - 6, 0, bw);
    if (s[i].comp) { int lo = kCompMin[s[i].v], hi = kCompMax[s[i].v]; sendComp(s[i].v, lo + (px * (hi - lo) + bw / 2) / bw); }
    else sendSet(s[i].v, px * 1000 / bw);
    drawSlider(s[i]); return;
  }
}

void loop() {
  static char line[160]; static unsigned used = 0; static bool overflow = false, wasDown = false;
  static uint32_t lastTouch = 0;
  while (podLink.available()) {
    char c = podLink.read();
    if (c == '\n') { line[used] = 0; if (!overflow) parseLine(line); used = 0; overflow = false; }
    else if (c != '\r') { if (used < sizeof(line) - 1) line[used++] = c; else overflow = true; }
  }
  if (linked && millis() - lastState > 2000) { linked = false; drawHeader(); }
  bool down = touch.touched();
  if (down && millis() - lastTouch > 25) {
    auto p = touch.getPoint();
    if (p.z > 200) {
      int x = lroundf(ax * p.x + bx * p.y + cx), y = lroundf(ay * p.x + by * p.y + cy);
      handleTouch(x, y, !wasDown); wasDown = true; lastTouch = millis();
    }
  }
  if (!down && wasDown) { wasDown = false; heldColumn = -1; }
  static uint32_t lastAnim = 0;
  if (page == P_PLAY && millis() - lastAnim > 40) {
    lastAnim = millis();
    for (int c = 0; c < padColumns(); c++) if (flash[c]) { flash[c] = flash[c] > 40 ? flash[c] - 40 : 0; drawKey(c); }
  }
  static uint32_t lastPing = 0; if (millis() - lastPing >= 1000) { lastPing = millis(); podLink.print("PING\n"); }
  delay(2);
}
