#!/usr/bin/env python3
"""Turn drum-machine WAVs into the blobs trommelsynthesizer embeds with #embed.

Reads a folder of recordings, mixes each to mono, trims trailing silence and
writes src/samples/<name>.bin (8-bit mu-law, ITU G.711) and generates
src/samples.c, which #embeds them and holds the lookup table.

The plugin's load time grows with its size (about 5 ms per MB, paid on every
module load), so samples are stored as mu-law (half the size of 16-bit, and no
coarser than the 8-12 bit machines they come from) and recordings with no
energy above a quarter of the sample rate are stored at half rate.

Usage:
  prep_samples.py WAV_ROOT
  prep_samples.py --folders     list the machine folders it reads, one per line

WAV_ROOT is a checkout of tidal-drum-machines' machines/ folder (or anything
with the same RolandTR707/rolandtr707-bd/Bassdrum-01.wav layout). See
README.md#samples for where these come from and the licensing caveat.
"""

import array
import cmath
import glob
import math
import os
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src')

# Every blob starts with this so no sample can begin with a byte-order mark,
# which clang's #embed refuses (it sniffs UTF-16 BOMs).
MAGIC = b'SMP1'

# A recording is halved in rate when less than this fraction of its energy
# (measured around its loudest point) is above a quarter of the sample rate.
HALVE_BELOW = 1e-4
FFT_SIZE = 4096

MAX_SECONDS = 2.5  # cymbal tails past this are inaudible; keeps the plugin small
FADE_SECONDS = 0.02

# machine id -> (folder, {voice: 'kind/File name'}). Each machine uses the
# recording that best stands for each voice it had; kind is the folder suffix
# ("bd" for rolandtr707-bd), so the files below need no machine prefix.
MACHINES = {
  'M_707': ('RolandTR707', {
    'V_BD': 'bd/Bassdrum-01', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum-01',
    'V_SD2': 'sd/Snaredrum-02', 'V_RS': 'rim/Rimshot', 'V_CP': 'cp/Clap',
    'V_LT': 'lt/Tom L', 'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Crash', 'V_CB': 'cb/Cowbell', 'V_TB': 'tb/Tambourine',
  }),
  'M_505': ('RolandTR505', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_RS': 'rim/Rimshot', 'V_CP': 'cp/Clap',
    'V_LT': 'lt/Tom L', 'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Crash', 'V_RD': 'rd/Ride', 'V_CB': 'cb/Cowbell H',
    'V_CB2': 'cb/Cowbell L', 'V_LC': 'perc/Conga L', 'V_HC': 'perc/Conga H',
    'V_TI': 'perc/Timbale',
  }),
  'M_LINNDRUM': ('LinnDrum', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/0Snarderum-01', 'V_SD2': 'sd/0Snarderum-02',
    'V_RS': 'rim/Sidestick-01', 'V_CP': 'cp/Clap', 'V_LT': 'lt/Tom L-01', 'V_MT': 'mt/Tom M-01',
    'V_HT': 'ht/Tom H-01', 'V_CH': 'hh/Hat Closed-01', 'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Crash',
    'V_RD': 'rd/Ride', 'V_CB': 'cb/Cowbell', 'V_TB': 'tb/Tambourine', 'V_LC': 'perc/Conga L-01',
    'V_MC': 'perc/Conga M-01', 'V_HC': 'perc/Conga H-01', 'V_MA': 'sh/Cabasa',
  }),
  'M_LM1': ('LinnLM1', {
    'V_BD': 'bd/LM-1_BD_1_TL', 'V_BD2': 'bd/LM-1_BD_2_TL', 'V_SD': 'sd/LM-1_SD_1_TL',
    'V_RS': 'rim/LM-1_RIMSHOT_1_TL', 'V_CP': 'cp/LM-1_CLAP_1_TL', 'V_LT': 'lt/LM-1_Tom_1_TL',
    'V_HT': 'ht/LM-1_Tom_2_TL', 'V_CH': 'hh/LM-1_HH_1_TL', 'V_OH': 'oh/LM-1_HH_2_TL',
    'V_CB': 'cb/LM-1_COWBELL_TL', 'V_TB': 'tb/LM-1_TAMB_TL', 'V_MA': 'sh/LM-1_SHAKER_1_TL',
    'V_MC': 'perc/LM-1_BONGO_1_TL', 'V_HC': 'perc/LM-1_BONGO_2_TL',
    'V_CL': 'perc/LM-1_WOODBLOCK_TL',
  }),
  'M_DMX': ('OberheimDMX', {
    'V_BD': 'bd/Bassdrum-01', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum-01',
    'V_SD2': 'sd/Snaredrum-02', 'V_RS': 'rim/Rim Shot', 'V_CP': 'cp/Clap', 'V_LT': 'lt/Tom L',
    'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed', 'V_OH': 'oh/Hat Open',
    'V_CY': 'cr/Crash', 'V_RD': 'rd/Ride', 'V_TB': 'tb/Tamborine', 'V_MA': 'sh/Cabasa',
    'V_TI': 'perc/Timbale H',
  }),
  'M_DRUMULATOR': ('EmuDrumulator', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/0Snaredrum', 'V_RS': 'rim/Rim Shot', 'V_CP': 'cp/Clap',
    'V_LT': 'lt/Tom L', 'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Cymbal', 'V_CB': 'cb/Cowbell', 'V_CL': 'perc/Claves',
  }),
  'M_DRUMTRAKS': ('SequentialCircuitsDrumtracks', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_RS': 'rim/Rim Shot', 'V_CP': 'cp/Clap',
    'V_HT': 'ht/Tom', 'V_CH': 'hh/Hat Closed', 'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Crash',
    'V_RD': 'rd/Ride', 'V_CB': 'cb/Cowbell', 'V_TB': 'tb/Tambourine', 'V_MA': 'sh/Cabasa',
  }),
  'M_MPC60': ('AkaiMPC60', {
    'V_BD': 'bd/0 Bassdrum', 'V_BD2': 'bd/Bassdrum Gated', 'V_SD': 'sd/Snare 1',
    'V_SD2': 'sd/Snare 2', 'V_RS': 'rim/Rim Gated', 'V_CP': 'cp/Clap', 'V_LT': 'lt/Tom L',
    'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Closed Hat', 'V_OH': 'oh/Open Hat',
    'V_CY': 'cr/Crash', 'V_RD': 'rd/Ride', 'V_LC': 'perc/Conga L', 'V_HC': 'perc/Conga H',
    'V_MC': 'perc/Bongo', 'V_TI': 'perc/Timbale', 'V_CL': 'perc/Click',
  }),
  'M_RX5': ('YamahaRX5', {
    'V_BD': 'bd/Bassdrum', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum',
    'V_SD2': 'sd/Snaredrum-02', 'V_RS': 'rim/Rimshot', 'V_LT': 'lt/Tom',
    'V_CH': 'hh/Hat Closed', 'V_OH': 'oh/Hat Open', 'V_CB': 'cb/Cowbell',
    'V_TB': 'tb/Tambourine', 'V_MA': 'sh/Shaker',
  }),
  'M_RZ1': ('CasioRZ1', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/0Snaredrum', 'V_RS': 'rim/Rim Shot', 'V_CP': 'cp/Clap',
    'V_LT': 'lt/Tom L', 'V_MT': 'mt/Tom M', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'rd/Hat Open', 'V_CY': 'cr/Crash', 'V_RD': 'rd/Ride', 'V_CB': 'cb/Cowbell',
  }),
  'M_SK1': ('CasioSK1', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_MT': 'mt/Tom L', 'V_HT': 'ht/Tom H',
    'V_CH': 'hh/Hat Closed', 'V_OH': 'oh/Hat Open',
  }),
  'M_KPR77': ('KorgKPR77', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_CP': 'cp/Clap', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open',
  }),
  'M_DR55': ('BossDR55', {
    'V_BD': 'bd/Bassdrum-01', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum-01',
    'V_SD2': 'sd/Snaredrum-02', 'V_RS': 'rim/Rimshot', 'V_CH': 'hh/Hihat1',
  }),
  'M_DR110': ('BossDR110', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_CP': 'cp/Clap', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Crash', 'V_RD': 'rd/Ride',
  }),
  'M_TR606': ('RolandTR606', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_LT': 'lt/Tom L', 'V_HT': 'ht/Tom H',
    'V_CH': 'hh/Hat Closed', 'V_OH': 'oh/Hat Open', 'V_CY': 'cr/Cymbal',
  }),
  'M_RHYTHMACE': ('RhythmAce', {
    'V_BD': 'bd/Bassdrum-01', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum-01',
    'V_SD2': 'sd/Snaredrum-02', 'V_LT': 'lt/Tom L', 'V_HT': 'ht/Tom H', 'V_CH': 'hh/Hat Closed',
    'V_OH': 'oh/Hat Open', 'V_CL': 'perc/Clave',
  }),
  'M_SPACEDRUM': ('ViscoSpaceDrum', {
    'V_BD': 'bd/Bassdrum-01', 'V_BD2': 'bd/Bassdrum-02', 'V_SD': 'sd/Snaredrum-01',
    'V_SD2': 'sd/Snaredrum-02', 'V_RS': 'rim/Rimshot', 'V_CB': 'cb/Cowbell',
    'V_LT': 'lt/Synth Tom L', 'V_MT': 'mt/Synth Tom M-01', 'V_HT': 'ht/Synth Tom H',
    'V_CH': 'hh/Hat Closed-01', 'V_OH': 'oh/Hat Open-01', 'V_CL': 'perc/Woodblock1',
  }),
  'M_HR16': ('AlesisHR16', {
    'V_BD': 'bd/Bassdrum', 'V_SD': 'sd/Snaredrum', 'V_RS': 'rim/Rim', 'V_CP': 'cp/Clap',
    'V_LT': 'lt/Tom-1', 'V_HT': 'ht/Tom-2', 'V_CH': 'hh/Closed Hat', 'V_OH': 'oh/Open Hat',
    'V_LC': 'perc/Conga L', 'V_HC': 'perc/Conga H', 'V_TI': 'perc/Timbale',
    'V_CL': 'perc/Claves', 'V_CB2': 'perc/Agogo Bell', 'V_MA': 'sh/Maracas',
  }),
  'M_SDS5': ('SimmonsSDS5', {
    'V_BD': 'bd/Bassdrum-01', 'V_SD': 'sd/Snaredrum-01', 'V_SD2': 'sd/Snaredrum-02',
    'V_RS': 'rim/Rimshot-01', 'V_LT': 'lt/Tom-07', 'V_MT': 'mt/Tom-02', 'V_HT': 'ht/Tom-01',
    'V_CH': 'hh/Hat Closed-01', 'V_OH': 'oh/Hat Open-01',
  }),
}


def find_wav(root, folder, rel):
  kind, name = rel.split('/')
  for d in glob.glob(os.path.join(root, folder, '*-' + kind)):
    path = os.path.join(d, name + '.wav')
    if os.path.exists(path):
      return path
  sys.exit('missing sample: %s/%s' % (folder, rel))


def load_mono(path):
  """Any PCM width and channel count -> 16-bit mono."""
  w = wave.open(path)
  width, ch = w.getsampwidth(), w.getnchannels()
  raw = w.readframes(w.getnframes())
  if width == 1:
    vals = [(b - 128) << 8 for b in raw]
  elif width == 2:
    vals = list(array.array('h', raw))
  elif width == 3:
    vals = [int.from_bytes(raw[i:i + 3], 'little', signed=True) >> 8 for i in range(0, len(raw), 3)]
  elif width == 4:
    vals = [v >> 16 for v in array.array('i', raw)]
  else:
    sys.exit('unsupported sample width %d: %s' % (width, path))
  if ch > 1:
    vals = [sum(vals[i:i + ch]) // ch for i in range(0, len(vals) - ch + 1, ch)]
  a = array.array('h', [max(-32768, min(32767, v)) for v in vals])
  rate = w.getframerate()
  while len(a) > 1 and abs(a[-1]) < 3:
    a.pop()
  limit = int(MAX_SECONDS * rate)
  if len(a) > limit:
    a = a[:limit]
    fade = int(FADE_SECONDS * rate)
    for i in range(fade):
      a[limit - fade + i] = int(a[limit - fade + i] * (1 - (i + 1) / fade))
  return a, rate


def fft(x):
  """Radix-2 FFT of a power-of-two-length list of floats."""
  n = len(x)
  if n == 1:
    return x
  even, odd = fft(x[0::2]), fft(x[1::2])
  out = [0] * n
  for k in range(n // 2):
    t = cmath.exp(-2j * math.pi * k / n) * odd[k]
    out[k] = even[k] + t
    out[k + n // 2] = even[k] - t
  return out


def high_energy_fraction(a):
  """Fraction of energy above a quarter of the sample rate, around the peak."""
  if len(a) < FFT_SIZE:
    seg = list(a) + [0] * (FFT_SIZE - len(a))
  else:
    peak = max(range(len(a)), key=lambda i: abs(a[i]))
    start = max(0, min(len(a) - FFT_SIZE, peak - FFT_SIZE // 4))
    seg = list(a[start:start + FFT_SIZE])
  seg = [v * (0.5 - 0.5 * math.cos(2 * math.pi * i / FFT_SIZE)) for i, v in enumerate(seg)]
  spec = fft(seg)[:FFT_SIZE // 2]
  power = [abs(c) ** 2 for c in spec]
  total = sum(power) or 1.0
  return sum(power[FFT_SIZE // 4:]) / total


def halve_rate(a):
  """Low-pass at 0.45 of the new Nyquist (windowed sinc), then keep every other sample."""
  taps = 63
  mid = taps // 2
  h = []
  for i in range(taps):
    n = i - mid
    sinc = 0.45 if n == 0 else math.sin(math.pi * n * 0.45) / (math.pi * n)
    h.append(sinc * (0.54 + 0.46 * math.cos(math.pi * n / (mid + 1))))
  gain = sum(h)
  h = [v / gain for v in h]
  out = array.array('h')
  for i in range(0, len(a), 2):
    acc = 0.0
    for j in range(taps):
      k = i + j - mid
      if 0 <= k < len(a):
        acc += h[j] * a[k]
    out.append(max(-32768, min(32767, int(round(acc)))))
  return out


def ulaw_encode(sample):
  """16-bit linear PCM -> one G.711 mu-law byte."""
  bias, clip = 0x84, 32635
  sign = 0x80 if sample < 0 else 0
  mag = min(-sample if sample < 0 else sample, clip) + bias
  exp = 7
  mask = 0x4000
  while exp > 0 and not (mag & mask):
    exp -= 1
    mask >>= 1
  mant = (mag >> (exp + 3)) & 0x0F
  return (~(sign | (exp << 4) | mant)) & 0xFF


def main():
  if sys.argv[1:] == ['--folders']:
    print('\n'.join(folder for folder, _ in MACHINES.values()))
    return
  if len(sys.argv) != 2:
    sys.exit(__doc__)
  root = sys.argv[1]
  os.makedirs(os.path.join(OUT, 'samples'), exist_ok=True)
  decls, table = [], []
  total = 0
  halved = 0
  for mid, (folder, voices) in MACHINES.items():
    for voice, rel in voices.items():
      data, rate = load_mono(find_wav(root, folder, rel))
      if rate >= 32000 and high_energy_fraction(data) < HALVE_BELOW:
        data, rate = halve_rate(data), rate // 2
        halved += 1
      ulaw = bytes(ulaw_encode(v) for v in data)
      base = '%s_%s' % (mid[2:].lower(), voice[2:].lower())
      with open(os.path.join(OUT, 'samples', base + '.bin'), 'wb') as f:
        f.write(MAGIC + ulaw)
      decls.append('static const uint8_t smp_%s[] = {\n#embed "samples/%s.bin"\n};' % (base, base))
      table.append('    {%s, %s, {smp_%s + %d, (sizeof smp_%s - %d), %d}},' % (mid, voice, base, len(MAGIC), base, len(MAGIC), rate))
      total += len(ulaw)
  print('%d of %d recordings stored at half rate' % (halved, len(table)))
  with open(os.path.join(OUT, 'samples.c'), 'w') as f:
    f.write('// Generated by scripts/prep_samples.py. Do not edit.\n')
    f.write('#include "samples.h"\n\n#include <stddef.h>\n\n#include "trommelsynthesizer.h"\n\n')
    f.write('\n'.join(decls))
    f.write('\n\nstatic const struct {\n  int m, v;\n  Sample s;\n} TABLE[] = {\n')
    f.write('\n'.join(table))
    f.write('\n};\n\nconst Sample* sample_get(int machine, int voice) {\n')
    f.write('  for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {\n')
    f.write('    if (TABLE[i].m == machine && TABLE[i].v == voice) return &TABLE[i].s;\n  }\n')
    f.write('  return NULL;\n}\n')
  # Stale blobs from a machine that was dropped from the table would still
  # sit in the folder; remove them so the checkout matches the table.
  keep = {'%s_%s.bin' % (m[2:].lower(), v[2:].lower()) for m, (_, vs) in MACHINES.items() for v in vs}
  for f in os.listdir(os.path.join(OUT, 'samples')):
    if f not in keep:
      os.remove(os.path.join(OUT, 'samples', f))
  print('wrote %d samples for %d machines, %.2f MB' % (len(table), len(MACHINES), total / 1e6))


if __name__ == '__main__':
  main()
