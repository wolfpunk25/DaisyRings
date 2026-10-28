// Tables shared by the Pod firmware and the touchscreen sketch.
#pragma once

enum { V_STRUCT, V_BRIGHT, V_DAMP, V_POS, V_ROOT, V_RATE, V_CHANCE, V_RANGE, V_DRIFT, V_VOL, NVAL };

const int kEngines = 12;
const char* const kEngineNames[kEngines] = {
  "Modal", "Sympathetic", "Inharmonic", "FM voice", "Quantized", "Strings + verb",
  "Formant strings", "Chorus strings", "Reverb strings", "Bi-formant", "Ensemble", "Cathedral"};

const int kGenModes = 6;
const char* const kGenNames[kGenModes] = {"Walk", "Random", "Arp up", "Arp up/down", "Pedal", "Composer"};

// Composer parameters (real units, not 0..1000)
enum { C_RHYTHM, C_SECT, C_STEPS, C_PULSES, C_ROT, C_TEMPO, C_SECBARS, C_CHORDBARS, C_SEED, NCOMP };
const int kCompMin[NCOMP] = {0, 0, 4, 1, 0, 40, 1, 1, 0};
const int kCompMax[NCOMP] = {1, 1, 16, 16, 15, 200, 8, 4, 9999};
const int kCompDefault[NCOMP] = {0, 0, 16, 5, 0, 100, 4, 2, 1234};
const char* const kSectionNames[4] = {"Pulse", "Emerge", "Peak", "Dissolve"};

const int kScales = 8;
const char* const kScaleNames[kScales] = {"Minor pent", "Major pent", "Minor", "Major", "Dorian", "Lydian", "Whole tone", "Chromatic"};
const int kScaleLen[kScales] = {5, 5, 7, 7, 7, 7, 6, 12};
const int kScaleSteps[kScales][12] = {
  {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11},
  {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 6, 8, 10},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};

// root note value 0..1000 -> MIDI note
inline int rootNote(int v) { return 36 + (v * 36 + 500) / 1000; }
// number of octaves the generator spans for the RANGE value
inline int rangeOctaves(int v) { return 1 + v * 3 / 1000; }
// playing-pad layout: two octaves for 7-note-or-fewer scales, else one
inline int padOctaves(int scale) { return kScaleLen[scale] <= 7 ? 2 : 1; }
inline int padNote(int root, int scale, int column) {
  int n = kScaleLen[scale];
  return root + 12 * (column / n) + kScaleSteps[scale][column % n];
}

struct Preset {
  const char* name;
  const char* description;
  int engine, poly, gen, scale, external;   // poly index: 0=1 voice, 1=2, 2=4
  int values[NVAL];
  int comp[NCOMP];
};
// structure, bright, damp, position, root, rate, chance, range, drift, volume
const int kPresets = 6;
const Preset presets[kPresets] = {
  {"Glass Harp", "Bright melodic ideas", 0, 2, 5, 2, 0, {520, 780, 820, 400, 400, 280, 950, 400, 250, 600}, {0, 0, 16, 5, 0, 96, 4, 2, 1234}},
  {"Slow Bells", "Sparse, long decay", 3, 1, 5, 1, 0, {300, 700, 900, 500, 500, 140, 800, 600, 200, 600}, {0, 0, 16, 5, 0, 60, 4, 2, 2468}},
  {"Dark Chorale", "Low, hymn-like", 2, 2, 5, 2, 0, {450, 350, 880, 300, 150, 200, 1000, 300, 350, 650}, {0, 1, 16, 4, 0, 66, 2, 2, 4242}},
  {"Rain Kalimba", "Euclid 9/16 plucks", 1, 1, 3, 4, 0, {600, 650, 450, 600, 600, 620, 900, 500, 150, 600}, {1, 0, 16, 9, 2, 112, 4, 2, 77}},
  {"Drone Choir", "Washed, evolving", 8, 2, 5, 5, 0, {550, 600, 800, 700, 250, 90, 900, 350, 500, 550}, {0, 1, 8, 3, 0, 54, 2, 1, 909}},
  {"Wind Chimes", "Airy, quick phrases", 5, 2, 5, 6, 0, {700, 850, 700, 450, 700, 180, 950, 700, 300, 600}, {0, 1, 12, 5, 1, 84, 4, 2, 31}},
};
