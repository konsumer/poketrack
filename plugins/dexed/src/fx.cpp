// See fx.h. Direct port of dexed's Source/PluginFx.cpp — algorithm and
// constants unchanged; only JUCE's MathConstants::pi and the unused 2-pole
// filter variant are dropped.
#include "fx.h"

#include <math.h>

namespace {
constexpr float kPi = 3.14159265358979323846f;

inline float tptpc(float& state, float inp, float cutoff) {
  double v = (inp - state) * cutoff / (1 + cutoff);
  double res = v + state;
  state = res + v;
  return (float)res;
}

inline float tptlpupw(float& state, float inp, float cutoff, float srInv) {
  cutoff = (cutoff * srInv) * kPi;
  double v = (inp - state) * cutoff / (1 + cutoff);
  double res = v + state;
  state = res + v;
  return (float)res;
}

float logsc(float param, const float min, const float max, const float rolloff = 19.0f) {
  return ((expf(param * logf(rolloff + 1)) - 1.0f) / (rolloff)) * (max - min) + min;
}
}  // namespace

void DexedFx::init(double sampleRateHz) {
  mm = 0;
  s1 = s2 = s3 = s4 = c = d = 0;
  R24 = 0;

  mmch = (int)(mm * 3);
  mmt = mm * 3 - mmch;

  sampleRate = (float)sampleRateHz;
  sampleRateInv = 1 / sampleRate;
  float rcrate = sqrtf((44000 / sampleRate));
  rcor24 = (970.0f / 44000) * rcrate;
  rcor24Inv = 1 / rcor24;

  bright = tanf((sampleRate * 0.5f - 10) * kPi * sampleRateInv);

  R = 1;
  rcor = (480.0f / 44000) * rcrate;
  rcorInv = 1 / rcor;

  pCutoff = -1;
  pReso = -1;

  dc_r = 1.0f - (126.0f / sampleRate);
  dc_id = 0;
  dc_od = 0;
}

inline float DexedFx::NR24(float sample, float g, float lpc) {
  float ml = 1 / (1 + g);
  float S = (lpc * (lpc * (lpc * s1 + s2) + s3) + s4) * ml;
  float G = lpc * lpc * lpc * lpc;
  float y = (sample - R24 * S) / (1 + R24 * G);
  return y + 1e-8f;
}

void DexedFx::process(float* work, int sampleSize) {
  if (sampleSize <= 0)
    return;

  // very basic DC filter
  float t_fd = work[0];
  work[0] = work[0] - dc_id + dc_r * dc_od;
  dc_id = t_fd;
  for (int i = 1; i < sampleSize; i++) {
    t_fd = work[i];
    work[i] = work[i] - dc_id + dc_r * work[i - 1];
    dc_id = t_fd;
  }
  dc_od = work[sampleSize - 1];

  if (uiGain != 1) {
    for (int i = 0; i < sampleSize; i++)
      work[i] *= uiGain;
  }

  // don't apply the LPF if the cutoff is at maximum
  if (uiCutoff == 1)
    return;

  if (uiCutoff != pCutoff || uiReso != pReso) {
    rReso = (0.991f - logsc(1 - uiReso, 0, 0.991f));
    R24 = 3.5f * rReso;

    float cutoffNorm = logsc(uiCutoff, 60, 19000);
    rCutoff = tanf(cutoffNorm * sampleRateInv * kPi);

    pCutoff = uiCutoff;
    pReso = uiReso;

    R = 1 - rReso;
  }

  float g = rCutoff;
  float lpc = g / (1 + g);

  for (int i = 0; i < sampleSize; i++) {
    float s = work[i];
    s = s - 0.45f * tptlpupw(c, s, 15, sampleRateInv);
    s = tptpc(d, s, bright);

    float y0 = NR24(s, g, lpc);

    // first lowpass in cascade
    double v = (y0 - s1) * lpc;
    double res = v + s1;
    s1 = (float)(res + v);

    // damping
    s1 = atanf(s1 * rcor24) * rcor24Inv;
    float y1 = (float)res;
    float y2 = tptpc(s2, y1, g);
    float y3 = tptpc(s3, y2, g);
    float y4 = tptpc(s4, y3, g);
    float mc = 0;

    switch (mmch) {
      case 0:
        mc = ((1 - mmt) * y4 + (mmt)*y3);
        break;
      case 1:
        mc = ((1 - mmt) * y3 + (mmt)*y2);
        break;
      case 2:
        mc = ((1 - mmt) * y2 + (mmt)*y1);
        break;
      default:
        mc = y1;
        break;
    }

    // half volume comp
    work[i] = mc * (1 + R24 * 0.45f);
  }
}
