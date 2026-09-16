/* Stand-in for surgesynthteam's tuning-library `Tunings.h`.
 *
 * msfa/tuning.h (vendored verbatim from dexed) exposes
 * `TuningState::getTuning()` returning a `Tunings::Tuning&`, and that
 * declaration alone is why the header needs to exist. The scale/kbm parsing
 * side of the tuning library (parseSCLData/parseKBMData) is only reachable
 * from dexed's file-loaded tunings, which need a filesystem and a file
 * picker — out of scope for a sandboxed WCLAP plugin, so the vendored
 * tuning.cc is replaced by compat/tuning.cc, which implements
 * createStandardTuning() (the only state this plugin ever runs in) and has
 * nothing that needs the rest of the library.
 *
 * `Tuning` therefore only has to be a complete type here, with the one method
 * TuningState::getTuning() callers could reach for — nothing in the
 * vendored engine calls it, because nothing ever leaves standard tuning.
 */
#ifndef DEXED_COMPAT_TUNINGS_H
#define DEXED_COMPAT_TUNINGS_H

namespace Tunings {

class Tuning {
 public:
  // log2(frequency relative to the scale's 1/1) for a scale degree, which is
  // what dexed's SCL/KBM TuningState multiplies by (1 << 24) and offsets by
  // its 440 Hz base. Only 12-TET is reachable here, so degrees map straight
  // onto the octave.
  double logScaledFrequencyForMidiNote(int midiNote) const {
    return (double)midiNote / 12.0;
  }
};

class Scale {
 public:
  int count = 12;
};

class KeyboardMapping {};

}  // namespace Tunings

#endif /* DEXED_COMPAT_TUNINGS_H */
