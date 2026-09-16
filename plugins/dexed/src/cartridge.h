// DX7 cartridge handling — a plain-C++ port of dexed's Cartridge
// (Source/PluginData.h/cpp) with the JUCE file/clipboard plumbing removed.
//
// Only the two halves the plugin needs survive: parsing a 4104-byte bulk-
// voice-dump SysEx blob, and unpacking one of its 32 packed 128-byte voices
// into the 156-byte "unpacked" form the msfa engine reads. Both are ported
// verbatim, including dexed's `normparm()` clamp for out-of-range bytes from
// corrupt dumps — that function is what keeps a malformed cartridge from
// handing the engine parameters it would misbehave on.
#pragma once

#include <stdint.h>

#include "presets-data.h"

#define DEXED_SYSEX_HEADER {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00}

class DexedCartridge {
 public:
  DexedCartridge();

  // Parses a SysEx stream. Returns 0 when a checksum-valid DX7 cartridge was
  // found, 1 when one was found but the checksum didn't match, 2 when the
  // stream held no DX7 data (dexed's own return codes).
  int load(const uint8_t* stream, int size);

  // Unpacks voice `idx` (0-31) into unpackPgm, which must have room for 156
  // bytes.
  void unpackProgram(uint8_t* unpackPgm, int idx) const;

  // Voice name, normalized the way dexed's Cartridge::normalizePgmName does
  // (don't-care high bit stripped, Yamaha's yen/>/<< glyphs translated,
  // unprintables blanked, trailing spaces trimmed). Writes 11 bytes.
  void getProgramName(int idx, char* out /*[11]*/) const;

  const uint8_t* rawVoice(int idx) const { return voiceData + 6 + idx * 128; }

 private:
  uint8_t voiceData[DEXED_SYSEX_SIZE];
};
