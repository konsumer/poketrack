// Dexed's output stage (Source/PluginFx.{h,cpp} upstream, ported to plain
// C++): a DC blocker, the Output gain, and the 4-pole Obxd-style multimode
// lowpass on Cutoff/Resonance. Ported rather than vendored because the
// original uses juce::MathConstants (and nothing else from JUCE) and links
// against the processor for its params.
//
// Kept bit-faithful: same state, same coefficient math, same order of
// operations. The one behavioural difference is the sample rate: upstream is
// handed an int, this takes a double and derives the same float values.
#pragma once

class DexedFx {
 public:
  // Set directly by params (upstream's `uiCutoff`/`uiReso`/`uiGain`).
  float uiCutoff = 1.0f;
  float uiReso = 0.0f;
  float uiGain = 1.0f;

  void init(double sampleRateHz);
  void process(float* work, int sampleSize);

 private:
  float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
  float sampleRate = 44100.0f;
  float sampleRateInv = 1.0f / 44100.0f;
  float d = 0, c = 0;
  float R24 = 0;
  float rcor24 = 0, rcor24Inv = 0;
  float bright = 0;

  // 24 dB multimode
  float mm = 0, mmt = 0;
  int mmch = 0;

  // preprocess values taken from the UI
  float rCutoff = 0, rReso = 0;

  // thread values; if these differ from the UI ones, recalculate
  float pReso = -1, pCutoff = -1;

  float rcor = 0, rcorInv = 0;
  int R = 1;

  float dc_id = 0, dc_od = 0, dc_r = 0;

  float NR24(float sample, float g, float lpc);
};
