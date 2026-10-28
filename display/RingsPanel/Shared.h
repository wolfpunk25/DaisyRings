// Tables shared by the Pod firmware and the touchscreen sketch.
#pragma once

enum { V_STRUCT, V_BRIGHT, V_DAMP, V_POS, V_ROOT, V_RATE, V_CHANCE, V_RANGE, V_DRIFT, V_VOL, NVAL };

const int kEngines = 12;
const char* const kEngineNames[kEngines] = {
  "Modal", "Sympathetic", "Inharmonic", "FM voice", "Quantized", "Strings + verb",
  "Formant strings", "Chorus strings", "Reverb strings", "Bi-formant", "Ensemble", "Cathedral"};

const int kGenModes = 5;
const char* const kGenNames[kGenModes] = {"Walk", "Random", "Arp up", "Arp up/down", "Pedal"};

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
};
// structure, bright, damp, position, root, rate, chance, range, drift, volume
const int kPresets = 6;
const Preset presets[kPresets] = {
  {"Glass Harp", "Bright, ringing", 0, 2, 0, 0, 0, {520, 780, 820, 400, 400, 280, 700, 400, 250, 600}},
  {"Slow Bells", "Sparse, long decay", 3, 1, 1, 1, 0, {300, 700, 900, 500, 500, 140, 450, 600, 200, 600}},
  {"Dark Strings", "Low, bowed-ish", 2, 2, 0, 2, 0, {450, 350, 880, 300, 150, 200, 800, 300, 350, 650}},
  {"Rain Kalimba", "Fast, short plucks", 1, 1, 3, 4, 0, {600, 650, 450, 600, 600, 620, 650, 500, 150, 600}},
  {"Drone Choir", "Slow washed chords", 8, 2, 4, 5, 0, {550, 600, 800, 700, 250, 90, 500, 350, 500, 550}},
  {"Wind Chimes", "Airy random notes", 5, 2, 1, 6, 0, {700, 850, 700, 450, 700, 180, 350, 700, 300, 600}},
};
