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
const uint16_t accents[5] = {0x4DF6, 0xFCAA, 0xB37F, 0x8E51, 0xFC8D};  // cyan, amber, purple, green, pink
const char* const tabNames[5] = {"PLAY", "SOUND", "GEN", "MIX", "PRESET"};
const char* const labels[NVAL] = {"STRUCTURE", "BRIGHTNESS", "DAMPING", "POSITION", "ROOT NOTE", "RATE", "CHANCE", "RANGE", "DRIFT", "VOLUME"};
const char* const knobPairNames[4] = {"STRUCT/BRIGHT", "DAMP/POS", "RATE/CHANCE", "DRIFT/VOL"};

int values[NVAL] = {520, 780, 820, 400, 400, 280, 700, 400, 250, 600};
int engine = 0, polyIndex = 2, genMode = 0, scaleIndex = 0, external = 0, knobPair = 0;
bool genRun = false, linked = false;
int lastNote = 48, level = 0, cpu = 0; unsigned strumCount = 0, overruns = 0;
int page = 0;
unsigned sentCmds = 0, podCmds = 0, podNotes = 0; int touchX = 0, touchY = 0;
uint32_t lastState = 0, lastTouch = 0;

// ---- slider layout per page: value index, x, y, width ----
struct Slider { int v, x, y, w; };
const Slider soundSliders[4] = {{V_STRUCT, 10, 60, 300}, {V_BRIGHT, 10, 96, 300}, {V_DAMP, 10, 132, 300}, {V_POS, 10, 168, 300}};
const Slider genSliders[4] = {{V_RATE, 10, 100, 145}, {V_CHANCE, 165, 100, 145}, {V_RANGE, 10, 152, 145}, {V_DRIFT, 165, 152, 145}};
const Slider mixSliders[2] = {{V_ROOT, 10, 60, 300}, {V_VOL, 10, 96, 300}};
const Slider* pageSliders(int p, int& n) {
  if (p == 1) { n = 4; return soundSliders; }
  if (p == 2) { n = 4; return genSliders; }
  if (p == 3) { n = 2; return mixSliders; }
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

void valueText(int i, char* t, size_t n) {
  static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (i == V_ROOT) { int m = rootNote(values[i]); snprintf(t, n, "%s%d", notes[m % 12], m / 12 - 1); }
  else if (i == V_RATE) snprintf(t, n, "%.1f/s", .1f * powf(100.f, values[i] / 1000.f));
  else if (i == V_RANGE) snprintf(t, n, "%d oct", rangeOctaves(values[i]));
  else snprintf(t, n, "%d%%", values[i] / 10);
}
void drawSlider(const Slider& s) {
  tft.fillRect(s.x, s.y, s.w, 34, bg);
  uint16_t c = accents[page % 5];
  label(labels[s.v], s.x + 4, s.y, 2, c);
  char t[16]; valueText(s.v, t, sizeof(t)); tft.setTextColor(c, bg); tft.drawRightString(t, s.x + s.w - 4, s.y, 2);
  int bw = s.w - 12;
  tft.fillRoundRect(s.x + 6, s.y + 22, bw, 9, 4, panel);
  tft.fillRoundRect(s.x + 6, s.y + 22, max(9, values[s.v] * bw / 1000), 9, 4, c);
}

// ---- play pad ----
const int padY = 88, padH = 117;
uint8_t flash[28];
int heldColumn = -1;
int padColumns() { return kScaleLen[scaleIndex] * padOctaves(scaleIndex); }
int padColumnW() { return 300 / padColumns(); }
void drawKey(int c) {
  int w = padColumnW(), x = 10 + c * w;
  bool isRoot = c % kScaleLen[scaleIndex] == 0;
  uint16_t base = isRoot ? 0x2945 : panel;
  uint16_t fill = blend(base, accents[0], flash[c]);
  tft.fillRoundRect(x + 1, padY, w - 2, padH, 4, fill);
  if (isRoot) tft.drawRoundRect(x + 1, padY, w - 2, padH, 4, accents[0]);
}
void drawPad() { tft.fillRect(0, padY - 2, 320, padH + 4, bg); for (int c = 0; c < padColumns(); c++) drawKey(c); }
void drawEngine() {
  tft.fillRect(0, 58, 320, 28, bg);
  button(10, 58, 40, 26, "<", accents[0]); button(270, 58, 40, 26, ">", accents[0]);
  tft.setTextColor(accents[0], bg); tft.drawCentreString(kEngineNames[engine], 160, 63, 2);
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
  label("DAISY RINGS", 14, 5, 1, accents[page % 5]);
  if (linked) {
    char t[48]; snprintf(t, sizeof(t), "KNOBS: %s", knobPairNames[knobPair]); label(t, 150, 5, 1, mint);
    if (cpu > 85 || overruns > 50) { snprintf(t, sizeof(t), "CPU %d%%", cpu); label(t, 100, 5, 1, accents[1]); }
  }
  else label("WAITING FOR POD", 200, 5, 1, dim);
}
void drawRun() {
  button(14, 208, 292, 30, genRun ? "STOP GENERATIVE" : "START GENERATIVE", genRun ? panel : mint, !genRun);
  if (genRun) { tft.drawRoundRect(14, 208, 292, 30, 6, mint); tft.setTextColor(mint, panel); tft.drawCentreString("STOP GENERATIVE", 160, 216, 2); }
}
void drawGenButtons() {
  tft.fillRect(0, 58, 320, 38, bg);
  button(10, 60, 145, 30, kGenNames[genMode], accents[2]); button(165, 60, 145, 30, kScaleNames[scaleIndex], accents[2]);
}
void drawMixButtons() {
  tft.fillRect(0, 134, 320, 72, bg);
  const char* p[3] = {"1 VOICE", "2 VOICES", "4 VOICES"};
  for (int i = 0; i < 3; i++) button(10 + i * 102, 136, 96, 30, p[i], accents[3], polyIndex == i);
  button(10, 172, 145, 30, external ? "EXCITER: LINE IN" : "EXCITER: INTERNAL", accents[3]);
  button(165, 172, 145, 30, "RANDOMIZE", accents[1]);
}
int matchingPreset() {
  for (int m = 0; m < kPresets; m++) {
    const Preset& p = presets[m];
    bool match = engine == p.engine && polyIndex == p.poly && genMode == p.gen && scaleIndex == p.scale && external == p.external;
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
    if (i == active) tft.drawRoundRect(x, y, 145, 43, 5, accents[4]);
    tft.setTextColor(accents[4], panel); tft.drawString(presets[i].name, x + 7, y + 5, 2);
    tft.setTextColor(ink, panel); tft.drawString(presets[i].description, x + 7, y + 28, 1);
  }
}
void draw() {
  tft.fillScreen(bg); drawHeader();
  for (int i = 0; i < 5; i++) {
    tft.fillRoundRect(3 + i * 63, 25, 60, 28, 5, i == page ? panel : bg);
    if (i == page) tft.drawRoundRect(3 + i * 63, 25, 60, 28, 5, accents[i]); else tft.drawFastHLine(8 + i * 63, 51, 50, accents[i]);
    tft.setTextColor(accents[i], i == page ? panel : bg); tft.drawCentreString(tabNames[i], 33 + i * 63, 31, 2);
  }
  if (page == 0) { drawEngine(); drawPad(); }
  else if (page == 4) drawPresets();
  else {
    if (page == 2) drawGenButtons();
    if (page == 3) drawMixButtons();
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
void applyState(int e, int pi, int g, int sc, int ex, int run, int note, int lvl, const int* v, unsigned strums, int kp, int cpuPeak, unsigned over) {
  bool full = false;
  if (e != engine) { engine = e; if (page == 0) drawEngine(); }
  if (sc != scaleIndex) { scaleIndex = sc; if (page == 0) drawPad(); if (page == 2) drawGenButtons(); }
  if (g != genMode) { genMode = g; if (page == 2) drawGenButtons(); }
  if (pi != polyIndex || ex != external) { polyIndex = pi; external = ex; if (page == 3) drawMixButtons(); }
  if (bool(run) != genRun) { genRun = run; drawRun(); }
  if (kp != knobPair) { knobPair = kp; drawHeader(); }
  int n; const Slider* s = pageSliders(page, n);
  for (int i = 0; i < 8; i++) if (values[i] != v[i]) { values[i] = v[i]; for (int k = 0; k < n; k++) if (s[k].v == i) drawSlider(s[k]); if (i == V_ROOT && page == 0) full = true; }
  if (full) drawPad();
  if (strums != strumCount) { strumCount = strums; lastNote = note; int c = page == 0 ? noteColumn(note) : -1; if (c >= 0) { flash[c] = 255; drawKey(c); } }
  lastNote = note; level = lvl; static uint32_t lastHdr = 0; bool hdr = (cpuPeak > 85) != (cpu > 85); cpu = cpuPeak; overruns = over; if (hdr && millis() - lastHdr > 500) { lastHdr = millis(); drawHeader(); }
  if (page == 4) { static int lastPreset = -2; int a = matchingPreset(); if (a != lastPreset) { lastPreset = a; drawPresets(); } }
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
    applyState(e, pi, g, sc, ex, run, note, lvl, v, strums, kp, cpuPeak, over);
  } else if (!strncmp(line, "RSTATE2 ", 8)) {
    int a, b; unsigned pc = 0, pn = 0;
    if (sscanf(line, "RSTATE2 %d %d %u %u", &a, &b, &pc, &pn) >= 2 && a >= 0 && a <= 1000 && b >= 0 && b <= 1000) {
      podCmds = pc; podNotes = pn;
      int n; const Slider* s = pageSliders(page, n);
      int nv[2] = {a, b};
      for (int i = 0; i < 2; i++) if (values[8 + i] != nv[i]) { values[8 + i] = nv[i]; for (int k = 0; k < n; k++) if (s[k].v == 8 + i) drawSlider(s[k]); }
    }
  }
}

void send(const char* fmt, int a) { sentCmds++; podLink.printf(fmt, a); }
void sendSet(int i, int v) { sentCmds++; values[i] = v; podLink.printf("SET %d %d\n", i, v); }

void handleTouch(int x, int y, bool fresh) {
  if (fresh && y >= 25 && y < 54 && x >= 3 && x < 318) { int next = constrain((x - 3) / 63, 0, 4); if (next != page) { page = next; draw(); } return; }
  if (!linked) return;
  if (fresh && y >= 208) { send("RUN %d\n", genRun ? 0 : 1); return; }
  static uint32_t lastBright = 0;
  if (page == 0) {
    if (fresh && y >= 58 && y < 86) { if (x < 54) send("ENGINE %d\n", (engine + kEngines - 1) % kEngines); else if (x > 266) send("ENGINE %d\n", (engine + 1) % kEngines); return; }
    if (y >= padY && y < padY + padH && x >= 10 && x < 310) {
      int c = constrain((x - 10) / padColumnW(), 0, padColumns() - 1);
      if (c != heldColumn) { heldColumn = c; send("NOTE %d\n", padNote(rootNote(values[V_ROOT]), scaleIndex, c)); flash[c] = 255; drawKey(c); }
      if (millis() - lastBright > 60) { lastBright = millis(); sendSet(V_BRIGHT, 350 + constrain(padH - (y - padY), 0, padH) * 650 / padH); }
    }
    return;
  }
  if (page == 2 && fresh && y >= 60 && y < 90) { if (x < 160) send("GEN %d\n", (genMode + 1) % kGenModes); else send("SCALE %d\n", (scaleIndex + 1) % kScales); return; }
  if (page == 3 && fresh) {
    if (y >= 136 && y < 166) { int i = constrain((x - 10) / 102, 0, 2); send("POLY %d\n", i); return; }
    if (y >= 172 && y < 202) { if (x < 160) send("EXT %d\n", external ? 0 : 1); else podLink.print("RANDOM\n"); return; }
  }
  if (page == 4 && fresh && y >= 60 && y < 199) {
    int col = x >= 165 ? 1 : 0, row = (y - 60) / 48, i = row * 2 + col;
    if (x >= 10 && x < 310 && (x - 10) % 155 < 145 && (y - 60) % 48 < 43 && i < kPresets) send("PRESET %d\n", i);
    return;
  }
  int n; const Slider* s = pageSliders(page, n);
  static uint32_t lastSet = 0;
  for (int i = 0; i < n; i++) if (x >= s[i].x && x < s[i].x + s[i].w && y >= s[i].y && y < s[i].y + 34) {
    if (millis() - lastSet < 40) return; lastSet = millis();
    int v = constrain((x - s[i].x - 6) * 1000 / (s[i].w - 12), 0, 1000);
    sendSet(s[i].v, v); drawSlider(s[i]); return;
  }
}

void loop() {
  static char line[160]; static unsigned used = 0; static bool overflow = false, wasDown = false;
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
      touchX = x; touchY = y; handleTouch(x, y, !wasDown); wasDown = true; lastTouch = millis();
    }
  }
  if (!down && wasDown) { wasDown = false; heldColumn = -1; }
  static uint32_t lastAnim = 0;
  if (page == 0 && millis() - lastAnim > 40) {
    lastAnim = millis();
    for (int c = 0; c < padColumns(); c++) if (flash[c]) { flash[c] = flash[c] > 40 ? flash[c] - 40 : 0; drawKey(c); }
  }
  static uint32_t lastPing = 0; if (millis() - lastPing >= 1000) { lastPing = millis(); podLink.print("PING\n"); }
  delay(2);
}
