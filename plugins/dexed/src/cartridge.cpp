// See cartridge.h. Ported from dexed's Source/PluginData.cpp; the parsing and
// unpacking logic is unchanged, only the JUCE types (File, String, InputStream)
// are gone.
#include "cartridge.h"

#include <string.h>

// dexed's sysexChecksum()
static uint8_t sysexChecksum(const uint8_t* sysex, int size) {
  int sum = 0;
  for (int i = 0; i < size; i++)
    sum -= sysex[i];
  return sum & 0x7F;
}

// dexed's normparm(): data from corrupt sysex gets clamped into the range the
// parameter is supposed to have, instead of crashing the engine on an extreme
// value.
static uint8_t normparm(uint8_t value, char max, int id) {
  (void)id;
  if (value <= (uint8_t)max)
    return value;
  return (uint8_t)(((float)value) / 255 * max);
}

DexedCartridge::DexedCartridge() { memset(voiceData, 0, sizeof(voiceData)); }

int DexedCartridge::load(const uint8_t* stream, int size) {
  const uint8_t* pos = stream;

  if (size < 4096) {
    memcpy(voiceData + 6, pos, size);
    return 2;
  }

  if (pos[0] != 0xF0) {
    // not a sysex stream at all — take the first 4096 bytes as raw voice data
    memcpy(voiceData + 6, pos, 4096);
    return 2;
  }

  // limit the size of the sysex scan
  if (size > 65535)
    size = 65535;

  // loop until something looks like a DX7 cartridge (based on size)
  while (size >= DEXED_SYSEX_SIZE) {
    // it was sysex first, now random data; return random
    if (pos[0] != 0xF0) {
      memcpy(voiceData + 6, stream, 4096);
      return 2;
    }

    for (int i = 0; i < size; i++) {
      if (pos[i] == 0xF7) {
        if (i == DEXED_SYSEX_SIZE - 1) {
          memcpy(voiceData, pos, DEXED_SYSEX_SIZE);
          return sysexChecksum(voiceData + 6, 4096) == pos[4102] ? 0 : 1;
        }
        // end of sysex with the wrong DX size... keep scanning
        size -= i;
        pos += i;
        break;
      }
    }
    break;
  }

  // a sysex, but nothing DX-related in it
  memcpy(voiceData + 6, stream, 4096);
  return 2;
}

// dexed's Cartridge::unpackProgram()
void DexedCartridge::unpackProgram(uint8_t* unpackPgm, int idx) const {
  const uint8_t* bulk = voiceData + 6 + (idx * 128);

  for (int op = 0; op < 6; op++) {
    for (int i = 0; i < 11; i++) {
      uint8_t currparm = bulk[op * 17 + i] & 0x7F;  // mask BIT7 (don't care per sysex spec)
      unpackPgm[op * 21 + i] = normparm(currparm, 99, i);
    }

    memcpy(unpackPgm + op * 21, bulk + op * 17, 11);
    uint8_t leftrightcurves = bulk[op * 17 + 11] & 0xF;  // bits 4-7 don't care per sysex spec
    unpackPgm[op * 21 + 11] = leftrightcurves & 3;
    unpackPgm[op * 21 + 12] = (leftrightcurves >> 2) & 3;
    uint8_t detune_rs = bulk[op * 17 + 12] & 0x7F;
    unpackPgm[op * 21 + 13] = detune_rs & 7;
    uint8_t kvs_ams = bulk[op * 17 + 13] & 0x1F;  // bits 5-7 don't care per sysex spec
    unpackPgm[op * 21 + 14] = kvs_ams & 3;
    unpackPgm[op * 21 + 15] = (kvs_ams >> 2) & 7;
    unpackPgm[op * 21 + 16] = bulk[op * 17 + 14] & 0x7F;  // output level
    uint8_t fcoarse_mode = bulk[op * 17 + 15] & 0x3F;     // bits 6-7 don't care per sysex spec
    unpackPgm[op * 21 + 17] = fcoarse_mode & 1;
    unpackPgm[op * 21 + 18] = (fcoarse_mode >> 1) & 0x1F;
    unpackPgm[op * 21 + 19] = bulk[op * 17 + 16] & 0x7F;  // fine freq
    unpackPgm[op * 21 + 20] = (detune_rs >> 3) & 0x7F;
  }

  for (int i = 0; i < 8; i++) {
    uint8_t currparm = bulk[102 + i] & 0x7F;  // mask BIT7 (don't care per sysex spec)
    unpackPgm[126 + i] = normparm(currparm, 99, 126 + i);
  }
  unpackPgm[134] = normparm(bulk[110] & 0x1F, 31, 134);  // bits 5-7 are don't care per sysex spec

  uint8_t oks_fb = bulk[111] & 0xF;  // bits 4-7 are don't care per spec
  unpackPgm[135] = oks_fb & 7;
  unpackPgm[136] = oks_fb >> 3;
  unpackPgm[137] = bulk[112] & 0x7F;  // lfs
  unpackPgm[138] = bulk[113] & 0x7F;  // lfd
  unpackPgm[139] = bulk[114] & 0x7F;  // lpmd
  unpackPgm[140] = bulk[115] & 0x7F;  // lamd
  uint8_t lpms_lfw_lks = bulk[116] & 0x7F;
  unpackPgm[141] = lpms_lfw_lks & 1;
  unpackPgm[142] = (lpms_lfw_lks >> 1) & 7;
  unpackPgm[143] = lpms_lfw_lks >> 4;
  unpackPgm[144] = bulk[117] & 0x7F;
  for (int name_idx = 0; name_idx < 10; name_idx++)
    unpackPgm[145 + name_idx] = bulk[118 + name_idx] & 0x7F;
}

// dexed's Cartridge::normalizePgmName()
void DexedCartridge::getProgramName(int idx, char* out) const {
  const uint8_t* src = rawVoice(idx);

  for (int j = 0; j < 10; j++) {
    char c = (char)(src[118 + j] & 0x7F);  // strip don't-care most-significant bit from name
    switch (c) {
      case 92:
        c = 'Y';
        break;  // yen
      case 126:
        c = '>';
        break;  // >>
      case 127:
        c = '<';
        break;  // <<
      default:
        if (c < 32 || c > 127)
          c = ' ';
        break;
    }
    out[j] = c;
  }
  out[10] = 0;

  // dexed keeps the trailing padding spaces in the name string; trim them so
  // the poketrack picker/param display doesn't carry dead whitespace.
  for (int j = 9; j >= 0 && out[j] == ' '; j--)
    out[j] = 0;
}
