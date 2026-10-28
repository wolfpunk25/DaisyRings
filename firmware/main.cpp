// DaisyRings: Mutable Instruments Rings (via Resonate) on a Daisy Pod,
// played from a touchscreen, MIDI, or a built-in generative sequencer.
#include "daisy_pod.h"
#include "dsp/part.h"
#include "dsp/string_synth_part.h"
#include "command_line.h"
#include "Shared.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace daisy;
using namespace resonate;

DaisyPod hw;
uint16_t reverbBuffer[32768];
Part part;
StringSynthPart stringSynth;

volatile int values[NVAL] = {520, 780, 820, 400, 400, 280, 700, 400, 250, 600};
volatile int engine = 0, polyIndex = 2, genMode = 0, scaleIndex = 0, external = 0;
volatile bool genRun = false;
volatile int lastNote = 48;
volatile unsigned strumCount = 0, cpuPeak = 0, overruns = 0;
volatile float outLevel = 0;
volatile float bend = 0;
volatile bool muteAll = false;

// Notes from touch/MIDI/generator are queued, then played one at a time.
// A note has to be set one block before its strum so the voice latches it.
struct NoteQueue {
  volatile uint8_t buf[32]; volatile unsigned w = 0, r = 0;
  void Push(int n) { unsigned nx = (w + 1) % 32; if (nx != r) { buf[w] = n; w = nx; } }
  bool Pop(int& n) { if (r == w) return false; n = buf[r]; r = (r + 1) % 32; return true; }
};
NoteQueue userQueue;
volatile unsigned genWalkStep = 0;

uint32_t rng = 0x2545F491;
uint32_t Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
float Rand01() { return (Rand() >> 8) * (1.0f / 16777216.0f); }

UartHandler screenUart;
alignas(32) uint8_t DMA_BUFFER_MEM_SECTION screenDma[256];
volatile unsigned screenWrite = 0, screenRead = 0, screenErrors = 0;
char screenRing[1024];
bool screenReady = false;
void ScreenReceive(uint8_t* data, size_t size, void*, UartHandler::Result result) {
  if (result != UartHandler::Result::OK) { screenErrors++; return; }
  for (size_t i = 0; i < size; i++) {
    unsigned next = (screenWrite + 1) % sizeof(screenRing);
    if (next == screenRead) { screenErrors++; break; }
    screenRing[screenWrite] = data[i]; __DMB(); screenWrite = next;
  }
}
void InitScreen() {
  UartHandler::Config config; config.periph = UartHandler::Config::Peripheral::UART_4;
  config.pin_config.tx = seed::D12; config.pin_config.rx = seed::D11;
  config.mode = UartHandler::Config::Mode::TX_RX; config.baudrate = 115200;
  screenReady = screenUart.Init(config) == UartHandler::Result::OK;
  if (screenReady) screenReady = screenUart.DmaListenStart(screenDma, sizeof(screenDma), ScreenReceive, nullptr) == UartHandler::Result::OK;
}

// ---- generative note choice -------------------------------------------------
int NextGenNote() {
  static int degree = 0, dir = 1;
  int len = kScaleLen[scaleIndex], span = len * rangeOctaves(values[V_RANGE]);
  switch (genMode) {
    case 0: { static const int steps[8] = {-2, -1, -1, 0, 1, 1, 2, 1}; degree += steps[Rand() % 8];
      if (degree < 0) degree = -degree; if (degree >= span) degree = 2 * (span - 1) - degree; break; }
    case 1: degree = Rand() % span; break;
    case 2: degree = (degree + 1) % span; break;
    case 3: degree += dir; if (degree >= span - 1) { degree = span - 1; dir = -1; } if (degree <= 0) { degree = 0; dir = 1; } break;
    default: degree = (Rand() % 10 < 6) ? 0 : int(Rand() % span); break;
  }
  if (degree < 0) degree = 0; if (degree >= span) degree = span - 1;
  return rootNote(values[V_ROOT]) + 12 * (degree / len) + kScaleSteps[scaleIndex][degree % len];
}

void Audio(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
  uint32_t begin = System::GetTick();
  static int appliedEngine = -1, appliedPoly = -1, stage = 0, pendingNote = 48;
  static float until = 0, phase = 0, note = 48, level = 0;
  static float input[kMaxBlockSize], left[kMaxBlockSize], right[kMaxBlockSize];
  if (engine != appliedEngine) {
    memset(reverbBuffer, 0, sizeof(reverbBuffer));
    if (engine < 6) part.set_model((ResonatorModel)engine); else stringSynth.set_fx((FxType)(engine - 6));
    appliedEngine = engine;
  }
  if (polyIndex != appliedPoly) { part.set_polyphony(1 << polyIndex); stringSynth.set_polyphony(1 << polyIndex); appliedPoly = polyIndex; }

  // generator clock
  if (genRun) {
    until -= size;
    if (until <= 0) {
      float hz = 0.1f * powf(100.f, values[V_RATE] / 1000.f);
      until += 48000.f / hz;
      if (Rand01() < values[V_CHANCE] / 1000.f) userQueue.Push(NextGenNote());
    }
  }
  bool strum = false;
  if (stage == 1) { strum = true; stage = 0; strumCount++; }
  else { int n; if (userQueue.Pop(n)) { pendingNote = n; note = n; lastNote = n; stage = 1; } }

  // slow parameter drift
  phase += size / 48000.f;
  float drift = values[V_DRIFT] / 1000.f * .18f;
  auto clamp = [](float x, float hi) { return fmaxf(0.f, fminf(hi, x)); };
  Patch patch;
  patch.structure = clamp(values[V_STRUCT] / 1000.f + drift * sinf(phase * 6.2832f / 31.f), .9995f);
  patch.brightness = clamp(values[V_BRIGHT] / 1000.f + drift * sinf(phase * 6.2832f / 47.f + 1.3f), 1.f);
  patch.damping = clamp(values[V_DAMP] / 1000.f + drift * .6f * sinf(phase * 6.2832f / 59.f + 2.1f), 1.f);
  patch.position = clamp(values[V_POS] / 1000.f + drift * sinf(phase * 6.2832f / 23.f + .4f), 1.f);
  if (phase > 4000.f) phase -= 4000.f * 1.f;

  PerformanceState state;
  state.strum = strum; state.internal_exciter = !external; state.internal_strum = false; state.internal_note = false;
  state.tonic = 0.f; state.note = note; state.fm = bend; state.chord = int(patch.structure * (kNumChords - 1) + .5f);
  if (state.chord >= kNumChords) state.chord = kNumChords - 1;

  for (size_t i = 0; i < size; i++) input[i] = in[0][i];
  if (engine < 6) part.Process(state, patch, input, left, right, size);
  else stringSynth.Process(state, patch, input, left, right, size);

  float gain = values[V_VOL] / 1000.f * 1.2f;
  for (size_t i = 0; i < size; i++) {
    level += .002f * ((muteAll ? 0.f : 1.f) - level);
    float l = left[i] * gain * level, r = right[i] * gain * level;
    out[0][i] = tanhf(l); out[1][i] = tanhf(r);
  }
  outLevel = fmaxf(outLevel * .98f, fabsf(left[0]));
  unsigned usec = (unsigned)((System::GetTick() - begin) * (1000000.f / System::GetTickFreq()));
  if (usec / 10 > cpuPeak) cpuPeak = usec / 10;
  if (usec >= 1000) overruns++;
}

void LoadPreset(int i) {
  if (i < 0 || i >= kPresets) return;
  const Preset& p = presets[i];
  for (int k = 0; k < NVAL; k++) values[k] = p.values[k];
  engine = p.engine; polyIndex = p.poly; genMode = p.gen; scaleIndex = p.scale; external = p.external; genRun = true;
}

void Command(char* line) {
  int a, b;
  if (!strcmp(line, "UPDATE")) { muteAll = true; System::Delay(350); System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT); return; }
  if (sscanf(line, "SET %d %d", &a, &b) == 2 && a >= 0 && a < NVAL && b >= 0 && b <= 1000) values[a] = b;
  else if (sscanf(line, "ENGINE %d", &a) == 1 && a >= 0 && a < kEngines) engine = a;
  else if (sscanf(line, "POLY %d", &a) == 1 && a >= 0 && a < 3) polyIndex = a;
  else if (sscanf(line, "GEN %d", &a) == 1 && a >= 0 && a < kGenModes) genMode = a;
  else if (sscanf(line, "SCALE %d", &a) == 1 && a >= 0 && a < kScales) scaleIndex = a;
  else if (sscanf(line, "EXT %d", &a) == 1) external = a ? 1 : 0;
  else if (sscanf(line, "RUN %d", &a) == 1) genRun = a != 0;
  else if (sscanf(line, "NOTE %d", &a) == 1 && a >= 12 && a <= 108) userQueue.Push(a);
  else if (sscanf(line, "PRESET %d", &a) == 1) LoadPreset(a);
  else if (!strcmp(line, "RANDOM")) {
    static const int pick[4] = {V_STRUCT, V_BRIGHT, V_DAMP, V_POS};
    for (int k = 0; k < 4; k++) values[pick[k]] = 150 + Rand() % 750;
    scaleIndex = Rand() % kScales; genMode = Rand() % kGenModes; engine = Rand() % kEngines;
  }
}

void HandleMidi(MidiEvent e) {
  switch (e.type) {
    case NoteOn: { NoteOnEvent n = e.AsNoteOn(); if (n.velocity) userQueue.Push(n.note); break; }
    case PitchBend: bend = e.AsPitchBend().value / 8192.f * 2.f; break;
    case ControlChange: {
      ControlChangeEvent c = e.AsControlChange(); int v = c.value * 1000 / 127;
      switch (c.control_number) {
        case 71: values[V_STRUCT] = v; break; case 74: values[V_BRIGHT] = v; break;
        case 75: values[V_DAMP] = v; break; case 76: values[V_POS] = v; break;
        case 7: values[V_VOL] = v; break; case 1: values[V_POS] = v; break;
      }
      break;
    }
    default: break;
  }
}

int main() {
  hw.Init();
  hw.led1.Set(0, 0, 0); hw.led2.Set(0, 0, 0); hw.UpdateLeds(); hw.seed.SetLed(false);
  hw.SetAudioBlockSize(48); hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  InitResources();
  part.Init(reverbBuffer); stringSynth.Init(reverbBuffer);
  hw.seed.usb_handle.Init(UsbHandle::FS_INTERNAL);
  InitScreen();
  hw.midi.StartReceive();
  hw.StartAdc(); hw.StartAudio(Audio);

  CommandLine screenLine;
  uint32_t lastReport = 0, lastControls = 0;
  int knobPair = 0;
  static const int pairs[4][2] = {{V_STRUCT, V_BRIGHT}, {V_DAMP, V_POS}, {V_RATE, V_CHANCE}, {V_DRIFT, V_VOL}};
  float previous[2] = {0, 0}, reference[2] = {0, 0}; bool caught[2] = {false, false}; int seen[2] = {-1, -1};
  bool resetPickup = true;
  static char message[200];
  for (;;) {
    hw.midi.Listen();
    while (hw.midi.HasEvents()) HandleMidi(hw.midi.PopEvent());
    while (screenRead != screenWrite) {
      __DMB(); char c = screenRing[screenRead]; screenRead = (screenRead + 1) % sizeof(screenRing);
      screenLine.Feed(c, [](char* line) { if (strcmp(line, "PING")) Command(line); });
    }
    if (screenReady && !screenUart.IsListening()) screenUart.DmaListenStart(screenDma, sizeof(screenDma), ScreenReceive, nullptr);
    uint32_t now = System::GetNow();
    if (now - lastControls >= 5) {
      lastControls = now; hw.ProcessAllControls();
      for (int i = 0; i < 2; i++) {
        int p = pairs[knobPair][i]; float v = i ? hw.knob2.Process() : hw.knob1.Process(), target = values[p] / 1000.f;
        if (resetPickup || values[p] != seen[i]) { caught[i] = false; reference[i] = v; previous[i] = v; }
        if (fabsf(v - reference[i]) > .008f && (fabsf(v - target) < .025f || (v - target) * (previous[i] - target) < 0)) caught[i] = true;
        if (caught[i] && fabsf(v - target) > .004f) values[p] = int(v * 1000);
        seen[i] = values[p]; previous[i] = v;
      }
      resetPickup = false;
      int inc = hw.encoder.Increment(); if (inc) engine = (engine + inc + kEngines) % kEngines;
      if (hw.encoder.RisingEdge()) genMode = (genMode + 1) % kGenModes;
      if (hw.button1.RisingEdge()) genRun = !genRun;
      if (hw.button2.RisingEdge()) { knobPair = (knobPair + 1) % 4; resetPickup = true; }
    }
    if (now - lastReport >= 100) {
      lastReport = now;
      int n = snprintf(message, sizeof(message), "RSTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %u %d %u %u\n",
                       engine, polyIndex, genMode, scaleIndex, external, genRun ? 1 : 0, lastNote, int(outLevel * 1000),
                       values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], strumCount, knobPair, cpuPeak, overruns);
      // values[8], values[9] follow on a second line to keep each line short
      n += snprintf(message + n, sizeof(message) - n, "RSTATE2 %d %d\n", values[8], values[9]);
      if (screenReady) screenUart.BlockingTransmit((uint8_t*)message, n, 30);
    }
    System::Delay(1);
  }
}
