// Small generative composer: Euclidean rhythm, sections, weighted chord
// progressions in the current scale, and melody rules (big leaps tend to
// reverse, small steps tend to continue). Deterministic for a given seed.
#pragma once
#include "Shared.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>

class Composer {
 public:
  struct Params {
    int scale, mode, root, octaves, chance;  // chance 0..1000
    int rhythm, sections, steps, pulses, rot, secBars, chordBars;
  };

  void Reseed(uint32_t seed) {
    rng_ = seed * 2654435761u + 0x9E3779B9u; if (!rng_) rng_ = 1;
    tick_ = 0; bar_ = 0; section_ = 0; chord_ = 0; sinceTonic_ = 0;
    degree_ = 0; dir_ = 1; lastDeg_ = 0; lastInt_ = 0; phrase_ = -1; shift_ = 1;
    for (int i = 0; i < 4; i++) plan_[i] = 0;
  }
  int section() const { return section_; }
  int chord() const { return chord_; }
  float brightOffset(bool sections) const { static const float b[4] = {-.15f, 0.f, .12f, -.2f}; return sections ? b[section_] : 0.f; }

  // Advance one clock tick. Writes up to 2 MIDI notes (bass first) and returns the count.
  int Tick(const Params& p, int* out) {
    if (p.mode == 5) return TickMelody(p, out);
    const int len = kScaleLen[p.scale], barLen = p.rhythm ? 16 : 8;
    const int steps = p.steps < 1 ? 1 : p.steps, t = tick_;
    int count = 0; bool strong = (t % 4) == 0;
    if (t > 0 && t % barLen == 0) {
      bar_++;
      bool chordChange = p.mode == 5 && bar_ % (p.chordBars < 1 ? 1 : p.chordBars) == 0;
      if (p.sections && bar_ % (p.secBars < 1 ? 1 : p.secBars) == 0) {
        section_ = (section_ + 1) % 4;
        if (section_ == 0) { chord_ = 0; sinceTonic_ = 0; chordChange = false; count = Bass(p, out, count); }
      }
      if (chordChange) { NextChord(len); count = Bass(p, out, count); }
    }
    tick_++;
    bool hit = true;
    if (p.rhythm) {
      int pulses = p.pulses > steps ? steps : p.pulses, s = (t % steps + steps - p.rot % steps) % steps;
      hit = (s * pulses) % steps < pulses;
    }
    if (!hit) return count;
    float density = p.sections ? kDensity[section_] : 1.f;
    if (Rand01() >= p.chance / 1000.f * density) return count;
    int oct = p.octaves;
    if (p.sections) { if (section_ == 0 || section_ == 3) oct = 1; else if (section_ == 2) oct = oct + 1 > 4 ? 4 : oct + 1; }
    int span = len * oct;
    int g = p.mode == 5 ? PickComposer(p, len, span, strong) : PickLegacy(p.mode, span);
    out[count++] = p.root + 12 * (g / len) + kScaleSteps[p.scale][g % len];
    return count;
  }

 private:
  struct Motif { uint8_t hit[8]; int8_t off[8]; };
  Motif a_{}, b_{}; int phrase_ = -1, shift_ = 1, planIdx_ = 0, plan_[4] = {0, 0, 0, 0};

  static int ChordDeg(int f, int len) { return (f * len + 3) / 7 % len; }  // 7-note degree -> this scale
  // nearest degree to g (within +-3) that is a tone of the triad on chord root c
  static int SnapToChord(int g, int c, int len, int span) {
    int t0 = c, t1 = (c + 2) % len, t2 = (c + 4) % len;
    for (int d = 0; d <= 3; d++)
      for (int sgn = -1; sgn <= 1; sgn += 2) {
        int h = g + sgn * d; if (h < 0 || h >= span) continue;
        int ci = h % len; if (ci == t0 || ci == t1 || ci == t2) return h;
      }
    return g;
  }
  // A hook: simple contour shape over a regular rhythm. Off-beat hits are thinned once, here,
  // so the motif stays identical every time it repeats.
  void GenMotif(Motif& m, const uint8_t* rhythm, int keepWeak) {
    static const int8_t shapes[5][8] = {{0, 1, 2, 1, 0, 1, 0, -1}, {0, 1, 2, 3, 2, 1, 0, 0}, {2, 1, 0, 1, 2, 1, 0, 0}, {0, 0, 1, 2, 1, 0, -1, 0}, {0, 2, 1, 2, 3, 2, 1, 0}};
    const int8_t* sh = shapes[Rand() % 5]; int k = 0;
    for (int i = 0; i < 8; i++) {
      m.hit[i] = rhythm[i]; m.off[i] = 0;
      if (i == 0) m.hit[i] = 1; else if (i % 2 == 1 && (int)(Rand() % 1000) >= keepWeak) m.hit[i] = 0;
      if (!m.hit[i]) continue;
      int o = sh[k % 8] - (k == 0 ? sh[0] : 0); if (k > 0 && Rand01() < .15f) o += (Rand() & 1) ? 1 : -1;
      if (o > 3) o = 3; if (o < -3) o = -3;
      m.off[i] = o; k++;
    }
  }
  void NewPhrase(int len, int chance) {
    static const uint8_t cells[8][8] = {
      {1,0,1,0,1,0,1,0}, {1,0,0,1,0,0,1,0}, {1,0,1,1,0,1,0,0}, {1,1,0,1,1,0,1,0},
      {1,0,1,0,1,1,0,0}, {1,0,0,1,0,1,0,0}, {1,1,1,0,1,0,1,0}, {1,0,1,0,0,1,1,0}};
    static const int plans[5][2] = {{5, 3}, {3, 4}, {4, 5}, {3, 1}, {5, 4}};
    phrase_++;
    bool fresh = phrase_ == 0 || ((phrase_ & 1) == 0 && Rand01() < .4f);      // a new tune only every other phrase
    if (fresh) {
      const uint8_t* cell = cells[Rand() % 8]; GenMotif(a_, cell, chance);
      GenMotif(b_, cells[Rand() % 8], chance);
      planIdx_ = Rand() % 5;
    }
    shift_ = ((phrase_ / 2) & 1) ? -1 : 1;
    plan_[0] = 0; plan_[1] = ChordDeg(plans[planIdx_][0], len); plan_[2] = ChordDeg(plans[planIdx_][1], len);
    plan_[3] = (phrase_ & 1) ? ChordDeg(4, len) : 0;                          // odd phrases ask (half cadence), even answer
  }
  int TickMelody(const Params& p, int* out) {
    const int len = kScaleLen[p.scale], t = tick_, slot = t % 8, bar = t / 8, barIn = bar % 4;
    int count = 0;
    if (t % 32 == 0) NewPhrase(len, p.chance);
    if (slot == 0) {
      if (bar > 0 && p.sections && bar % (p.secBars < 1 ? 1 : p.secBars) == 0) section_ = (section_ + 1) % 4;
      chord_ = plan_[barIn];
      if (barIn == 0 || barIn == 2) count = Bass(p, out, count);
    }
    tick_++;
    int oct = p.octaves;
    if (p.sections) { if (section_ == 0 || section_ == 3) oct = oct > 2 ? 2 : oct; else if (section_ == 2) oct = oct + 1 > 4 ? 4 : oct + 1; }
    if (oct < 2 && len <= 7) oct = 2;
    int span = len * oct;
    const Motif& m = (barIn == 2 && (phrase_ & 1)) ? b_ : a_;               // bar 3 repeats the hook, or answers with the contrast
    if (!m.hit[slot]) return count;
    int lastSlot = 0; for (int i = 0; i <= 4; i++) if (m.hit[i]) lastSlot = i;
    bool cadence = barIn == 3, finalNote = cadence && slot == lastSlot;
    if (cadence && slot > lastSlot) return count;
    bool strong = slot == 0 || slot == 4;
    int g = span / 2 + m.off[slot] + (barIn == 1 ? shift_ : 0);
    if (g < 0) g = 0; if (g >= span) g = span - 1;
    if (finalNote) {                                                         // land on the chord root nearest the middle
      int best = g, bd = 99;
      for (int h = 0; h < span; h++) if (h % len == chord_ && abs(h - span / 2) < bd) { bd = abs(h - span / 2); best = h; }
      g = best;
    } else if (strong) g = SnapToChord(g, chord_, len, span);
    out[count++] = p.root + 12 * (g / len) + kScaleSteps[p.scale][g % len];
    return count;
  }
  static constexpr float kDensity[4] = {.5f, .8f, 1.f, .55f};
  uint32_t rng_ = 1;
  int tick_ = 0, bar_ = 0, section_ = 0, chord_ = 0, sinceTonic_ = 0, degree_ = 0, dir_ = 1, lastDeg_ = 0, lastInt_ = 0;

  uint32_t Rand() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  float Rand01() { return (Rand() >> 8) * (1.0f / 16777216.0f); }

  int Bass(const Params& p, int* out, int count) {
    int n = p.root + kScaleSteps[p.scale][chord_] - 12; if (n < 24) n += 12;
    out[count++] = n; return count;
  }
  void NextChord(int len) {
    float w[12], sum = 0;
    for (int e = 0; e < len; e++) {
      int rel = (e - chord_ + len) % len; float x = 1.f;
      if (rel == 0) x = .5f;
      else if (rel == 1 || rel == len - 1) x = 2.f;
      else if (rel == 2 || rel == len - 2) x = 2.f;
      else if (rel == 3 || rel == len - 3) x = 3.5f;
      if (e == 0) x *= 1.f + sinceTonic_ * .6f;
      w[e] = x; sum += x;
    }
    float r = Rand01() * sum; int pick = len - 1;
    for (int e = 0; e < len; e++) { r -= w[e]; if (r <= 0) { pick = e; break; } }
    sinceTonic_ = pick == 0 ? 0 : sinceTonic_ + 1;
    chord_ = pick;
  }
  int PickLegacy(int mode, int span) {
    switch (mode) {
      case 0: { static const int steps[8] = {-2, -1, -1, 0, 1, 1, 2, 1}; degree_ += steps[Rand() % 8];
        if (degree_ < 0) degree_ = -degree_; if (degree_ >= span) degree_ = 2 * (span - 1) - degree_; break; }
      case 1: degree_ = Rand() % span; break;
      case 2: degree_ = (degree_ + 1) % span; break;
      case 3: degree_ += dir_; if (degree_ >= span - 1) { degree_ = span - 1; dir_ = -1; } if (degree_ <= 0) { degree_ = 0; dir_ = 1; } break;
      default: degree_ = (Rand() % 10 < 6) ? 0 : int(Rand() % span); break;
    }
    if (degree_ < 0) degree_ = 0; if (degree_ >= span) degree_ = span - 1;
    return degree_;
  }
  int PickComposer(const Params&, int len, int span, bool strong) {
    float w[48], sum = 0; int c0 = chord_, c1 = (chord_ + 2) % len, c2 = (chord_ + 4) % len;
    if (lastDeg_ >= span) lastDeg_ = span - 1;
    for (int g = 0; g < span; g++) {
      int move = g - lastDeg_; float x = 1.f / (1 + abs(move));
      if (abs(lastInt_) >= 4) { if (move * lastInt_ < 0) x *= 3.f; }
      else if (abs(lastInt_) > 0 && abs(lastInt_) <= 2) { if (move * lastInt_ > 0) x *= 2.f; }
      int ci = g % len; bool tone = ci == c0 || ci == c1 || ci == c2;
      x *= tone ? (strong ? 4.f : 2.2f) : .7f;
      if (move == 0) x *= .3f;
      w[g] = x; sum += x;
    }
    float r = Rand01() * sum; int pick = span - 1;
    for (int g = 0; g < span; g++) { r -= w[g]; if (r <= 0) { pick = g; break; } }
    lastInt_ = pick - lastDeg_; lastDeg_ = pick;
    return pick;
  }
};
